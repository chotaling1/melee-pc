/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <aurora/main.h>
#include <dolphin/ar.h>
#include <dolphin/ax.h>
#include <dolphin/card.h>
#include <dolphin/os.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ANDROID__)
#define MELEE_EXPORT __attribute__((visibility("default")))
#else
#define MELEE_EXPORT
#endif

#include "pc/pc.h"
#include "pc/launcher.h"

int melee_main(void);

#if defined(__ANDROID__)
#include <android/log.h>
#endif

/* A log file sink, so a double-clicked build still leaves a diagnosable
 * trace after its console disappears. MELEE_LOG_FILE picks the path; an
 * empty value disables it. Windows defaults it on because that is where the
 * console is not a reliable place to read errors from. */
static FILE* log_file(void) {
    static FILE* fp;
    static bool tried;
    if (!tried) {
        tried = true;
        const char* path = getenv("MELEE_LOG_FILE");
#if defined(_WIN32)
        if (path == NULL) {
            path = "melee-pc.log";
        }
#endif
        if (path != NULL && path[0] != '\0') {
            fp = fopen(path, "w");
        }
    }
    return fp;
}

/* Milliseconds since the first log record. Without a time base there is no
 * way to line a frame stall up against what the engine was loading. */
static double log_now_ms(void) {
    static Uint64 t0;
    const Uint64 now = SDL_GetTicksNS();
    if (t0 == 0) {
        t0 = now;
    }
    return (double)(now - t0) / 1e6;
}

void pc_log_line(const char* fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    const double t = log_now_ms();
    fprintf(stderr, "[%9.3f] %s\n", t, msg);
    fflush(stderr);
    FILE* lf = log_file();
    if (lf != NULL) {
        fprintf(lf, "[%9.3f] %s\n", t, msg);
        fflush(lf);
    }
}

static void log_callback(
    AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
#if defined(__ANDROID__)
    int prio = ANDROID_LOG_INFO;
    switch (level) {
    case LOG_DEBUG:
        prio = ANDROID_LOG_DEBUG;
        break;
    case LOG_INFO:
        prio = ANDROID_LOG_INFO;
        break;
    case LOG_WARNING:
        prio = ANDROID_LOG_WARN;
        break;
    case LOG_ERROR:
        prio = ANDROID_LOG_ERROR;
        break;
    case LOG_FATAL:
        prio = ANDROID_LOG_FATAL;
        break;
    }
    __android_log_print(prio, "Aurora", "[%s] %.*s", module, (int)len, message);
#else
    static const char* const names[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    FILE* out = level >= LOG_ERROR ? stderr : stdout;
    const double t = log_now_ms();
    fprintf(out, "[%9.3f] [%s] %s: %.*s\n", t, names[level], module, (int)len, message);
    /* stdout is block-buffered when redirected to a file, and the abort()
     * below does not flush it. Without this, `melee.exe > log.txt` drops the
     * lines leading up to a fatal -- exactly the ones worth reading. */
    fflush(out);
    FILE* lf = log_file();
    if (lf != NULL) {
        fprintf(lf, "[%9.3f] [%s] %s: %.*s\n", t, names[level], module, (int)len, message);
        fflush(lf);
    }
#endif
    if (level == LOG_FATAL) {
        fflush(stdout);
        fflush(stderr);
        abort();
    }
}

#if defined(_WIN32)
#include <windows.h>

/* A hard crash never reaches log_callback, so the log would otherwise just
 * stop with no reason. Record the exception, the faulting address, and a
 * backtrace as "module+RVA" per frame -- that names the failing module and
 * feeds straight into addr2line against the matching build. */

/* Resolve an address to "module+RVA", which is what addr2line needs. */
static void describe_addr(void* addr, char* out, size_t out_size) {
    char path[MAX_PATH];
    HMODULE mod = NULL;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)addr, &mod) &&
        GetModuleFileNameA(mod, path, sizeof(path)) != 0)
    {
        const char* base = strrchr(path, '\\');
        base = base != NULL ? base + 1 : path;
        snprintf(out, out_size, "%p %s+0x%llX", addr, base,
            (unsigned long long)((uintptr_t)addr - (uintptr_t)mod));
    } else {
        snprintf(out, out_size, "%p <unknown>", addr);
    }
}

#ifdef _M_X64
/* Walk a thread's stack from a captured context: unwind frames, then any
 * stack words that point into melee.exe (a fallback when unwinding fails). */
static void dump_context_stack(FILE* s, const CONTEXT* start) {
    char where[MAX_PATH + 64];
    CONTEXT ctx = *start;
    for (int f = 0; f < 32; f++) {
        describe_addr((void*)ctx.Rip, where, sizeof(where));
        fprintf(s, "[FATAL]   ctx #%02d %s\n", f, where);
        DWORD64 base;
        PRUNTIME_FUNCTION fn =
            ctx.Rip != 0 ? RtlLookupFunctionEntry(ctx.Rip, &base, NULL) : NULL;
        if (fn == NULL) {
            /* Leaf or invalid rip: the return address is on top of the
             * stack, as it is right after a bad call. */
            if (IsBadReadPtr((void*)ctx.Rsp, sizeof(DWORD64))) {
                break;
            }
            ctx.Rip = *(DWORD64*)ctx.Rsp;
            ctx.Rsp += 8;
        } else {
            PVOID handler_data;
            DWORD64 frame;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handler_data,
                &frame, NULL);
        }
        if (ctx.Rip == 0) {
            break;
        }
    }
    /* If unwinding goes wrong, anything on the stack that points into
     * melee.exe is still a likely return address. */
    HMODULE exe = GetModuleHandleA(NULL);
    const DWORD64* sp = (const DWORD64*)start->Rsp;
    int shown = 0;
    for (int i = 0; i < 512 && shown < 24; i++) {
        if (IsBadReadPtr(&sp[i], sizeof(DWORD64))) {
            break;
        }
        HMODULE mod;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)sp[i], &mod) &&
            mod == exe)
        {
            describe_addr((void*)sp[i], where, sizeof(where));
            fprintf(s, "[FATAL]   stack +0x%03X %s\n", (unsigned)(i * 8), where);
            shown++;
        }
    }
}
#endif

static LONG WINAPI crash_handler(EXCEPTION_POINTERS* info) {
    void* frames[32];
    const USHORT count = CaptureStackBackTrace(0, 32, frames, NULL);
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;

    FILE* streams[] = {stderr, log_file()};
    for (size_t i = 0; i < sizeof(streams) / sizeof(*streams); i++) {
        FILE* s = streams[i];
        if (s == NULL) {
            continue;
        }
        char where[MAX_PATH + 64];
        describe_addr((void*)rec->ExceptionAddress, where, sizeof(where));
        fprintf(
            s, "[FATAL] crash: exception 0x%08X at %s\n", (unsigned int)rec->ExceptionCode, where);
        /* For an access violation the second parameter is the address that
         * was touched; 0 vs garbage distinguishes a null deref from a wild
         * pointer, which is the first thing worth knowing. */
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
            fprintf(s, "[FATAL] crash: %s address 0x%llX\n",
                rec->ExceptionInformation[0] ? "write to" : "read from",
                (unsigned long long)rec->ExceptionInformation[1]);
        }
        for (USHORT f = 0; f < count; f++) {
            describe_addr(frames[f], where, sizeof(where));
            fprintf(s, "[FATAL]   #%02u %s\n", (unsigned)f, where);
        }
#ifdef _M_X64
        /* The trace above starts inside this handler and stops at the
         * dispatcher when the fault is a call through a null or wild
         * function pointer, which loses the only frames that matter. Unwind
         * from the faulting context instead. */
        fprintf(s, "[FATAL] faulting thread %lu, rip %p rsp %p\n", GetCurrentThreadId(),
            (void*)info->ContextRecord->Rip, (void*)info->ContextRecord->Rsp);
        dump_context_stack(s, info->ContextRecord);
#endif
        fflush(s);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

/* For OSPanic (pc/os.c): the game's own assertions. abort() on Windows can
 * sit on a modal "abnormal termination" box and never write the log, so
 * record the panic with a backtrace and terminate instead. */
void pc_panic_exit(const char* file, int line, const char* msg) {
    void* frames[32];
    const USHORT count = CaptureStackBackTrace(1, 32, frames, NULL);
    FILE* streams[] = {stderr, log_file()};
    for (size_t i = 0; i < sizeof(streams) / sizeof(*streams); i++) {
        FILE* s = streams[i];
        if (s == NULL) {
            continue;
        }
        char where[MAX_PATH + 64];
        fprintf(s, "[FATAL] panic: %s:%d: %s\n", file, line, msg);
        for (USHORT f = 0; f < count; f++) {
            describe_addr(frames[f], where, sizeof(where));
            fprintf(s, "[FATAL]   #%02u %s\n", (unsigned)f, where);
        }
        fflush(s);
    }
    TerminateProcess(GetCurrentProcess(), 5);
}

/* MELEE_HANG_WATCHDOG=<seconds>: if the game thread stops reaching the frame
 * boundary for that long, log its stack and exit with code 4. A hang
 * otherwise leaves nothing in the log -- the window just stops -- and a
 * scripted run waits on it until its timeout. */
static HANDLE s_main_thread;
static volatile LONG s_frame_ticks;

void pc_watchdog_tick(void) {
    InterlockedIncrement(&s_frame_ticks);
}

static DWORD WINAPI watchdog_thread(LPVOID arg) {
    const DWORD limit_ms = (DWORD)(uintptr_t)arg;
    LONG last = -1;
    DWORD still_ms = 0;
    for (;;) {
        Sleep(250);
        const LONG now = s_frame_ticks;
        if (now != last || now == 0) {
            last = now;
            still_ms = 0;
            continue;
        }
        still_ms += 250;
        if (still_ms < limit_ms) {
            continue;
        }
        FILE* streams[] = {stderr, log_file()};
        SuspendThread(s_main_thread);
        CONTEXT ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_FULL;
        const BOOL got = GetThreadContext(s_main_thread, &ctx);
        for (size_t i = 0; i < sizeof(streams) / sizeof(*streams); i++) {
            FILE* s = streams[i];
            if (s == NULL) {
                continue;
            }
            fprintf(s, "[FATAL] hang: no frame for %lu ms (frame tick %ld)\n",
                (unsigned long)still_ms, (long)now);
#ifdef _M_X64
            if (got) {
                dump_context_stack(s, &ctx);
            }
#endif
            fflush(s);
        }
        TerminateProcess(GetCurrentProcess(), 4);
    }
    return 0;
}

static void watchdog_start(void) {
    const char* env = getenv("MELEE_HANG_WATCHDOG");
    const long secs = env != NULL ? strtol(env, NULL, 10) : 0;
    if (secs <= 0) {
        return;
    }
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s_main_thread,
        0, FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(NULL, 0, watchdog_thread, (LPVOID)(uintptr_t)(secs * 1000), 0, NULL);
    pc_log_line("hang watchdog: %ld s", secs);
}
#else
void pc_panic_exit(const char* file, int line, const char* msg) {
    (void)file;
    (void)line;
    (void)msg;
    abort();
}
void pc_watchdog_tick(void) {}
static void watchdog_start(void) {}
#endif

static void usage(const char* argv0) {
    fprintf(stderr,
        "usage: %s [--no-card] [--dvd] [disc image (iso/gcm/ciso/rvz/...)]\nNo disc argument opens "
        "the launcher.\n",
        argv0);
    exit(2);
}

#include "pc/input_poll.h"

static void pc_shutdown_once(void) {
    static bool done;
    if (done) {
        return;
    }
    done = true;
    /* Stop producers before joining DMA and destroying platform resources.
     * An unjoined ARQ worker aborts in std::thread's static destructor. */
    pc_input_poll_shutdown();
    AXQuit();
    aurora_dvd_close();
    pc_textures_shutdown();
    ARQReset();
    aurora_shutdown();
}

static const struct {
    const char* name;
    AuroraBackend backend;
} k_backends[] = {
    {"auto", BACKEND_AUTO},
    {"d3d11", BACKEND_D3D11},
    {"d3d12", BACKEND_D3D12},
    {"metal", BACKEND_METAL},
    {"vulkan", BACKEND_VULKAN},
    {"opengl", BACKEND_OPENGL},
    {"gles", BACKEND_OPENGLES},
    {"webgpu", BACKEND_WEBGPU},
    {"null", BACKEND_NULL},
};

/* Local, so this costs no header question: strcasecmp lives in <strings.h> on
 * POSIX and is declared in <string.h> on MinGW only when __STRICT_ANSI__ is
 * off, which depends on the -std the target happens to use. */
static int ieq(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }
        if (ca != cb) {
            return 0;
        }
    }
    return *a == *b;
}

static const char* backend_name(AuroraBackend b) {
    for (size_t i = 0; i < sizeof(k_backends) / sizeof(*k_backends); i++) {
        if (k_backends[i].backend == b) {
            return k_backends[i].name;
        }
    }
    return "?";
}

/* MELEE_BACKEND pins the graphics backend instead of taking aurora's preferred
 * order. That order is per-platform -- on Windows D3D12 comes before Vulkan,
 * and on Linux only Vulkan is built -- so the same code takes a different path
 * on each, and a fault that appears on one and not the other cannot be bisected
 * without being able to pin it. `MELEE_BACKEND=vulkan` on Windows is the direct
 * test for "is this the D3D12 path?". An unknown value lists the valid ones and
 * falls back to BACKEND_AUTO rather than failing the run. */
static AuroraBackend backend_from_env(void) {
    const char* want = getenv("MELEE_BACKEND");
    if (want == NULL || want[0] == '\0') {
        return BACKEND_AUTO;
    }
    for (size_t i = 0; i < sizeof(k_backends) / sizeof(*k_backends); i++) {
        if (ieq(want, k_backends[i].name)) {
            return k_backends[i].backend;
        }
    }
    fprintf(stderr, "MELEE_BACKEND: unknown backend '%s'; valid values are", want);
    for (size_t i = 0; i < sizeof(k_backends) / sizeof(*k_backends); i++) {
        fprintf(stderr, "%s %s", i ? "," : "", k_backends[i].name);
    }
    fprintf(stderr, "\nMELEE_BACKEND: falling back to auto\n");
    return BACKEND_AUTO;
}

MELEE_EXPORT int main(int argc, char* argv[]) {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(crash_handler);
#endif

    /* Pre-initialize GameCube OS memory immediately so that MEM1 (96 MB) is
     * committed strictly below 4GB at process startup before SDL, graphics
     * drivers, and fullscreen swapchains fragment low virtual memory. */
    OSInit();

    /* ARAM and MEM1 pointers are told apart purely by value: anything below
     * PC_ARAM_SIZE is ARAM (PC_IS_ARAM_ADDR, src/pc/disc.h). If MEM1 ever
     * landed that low, loads would be silently routed to the wrong memory, so
     * refuse to start instead. aurora's fixed placements are all >= 512 MB;
     * only its VirtualAlloc2 fallback could land lower. */
    if ((uintptr_t) OSPhysicalToCached(0) < PC_ARAM_SIZE) {
        pc_log_line("[FATAL] MEM1 at %p overlaps the ARAM address range "
                    "(below 0x%X); cannot tell them apart",
                    OSPhysicalToCached(0), (unsigned) PC_ARAM_SIZE);
        return 1;
    }

    const char* disc = NULL;
    bool card = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dvd") == 0 && i + 1 < argc) {
            disc = argv[++i];
        } else if (strcmp(argv[i], "--no-card") == 0) {
            card = false;
        } else if (argv[i][0] != '-') {
            disc = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
        }
    }
    /* MELEE_USER_DIR points the memory card, launcher.cfg and caches at a
     * private directory, so scripted test runs never touch the player's save
     * or preferences. SDL_GetPrefPath ignores the environment on Windows. */
    const char* user_dir = getenv("MELEE_USER_DIR");
    char* pref_path = user_dir && user_dir[0] ? NULL : SDL_GetPrefPath(NULL, "melee-pc");
    const char* user_path = pref_path ? pref_path : user_dir;
    AuroraConfig config = {
        /* appName doubles as the window title; the save/cache dirs stay
         * pinned so a renamed test window still uses the same memory card. */
        .appName = getenv("MELEE_WINDOW_TITLE") ? getenv("MELEE_WINDOW_TITLE") : "melee-pc",
        .userPath = user_path,
        .cachePath = user_path,
        .msaa = 1,
        .maxTextureAnisotropy = 16,
        /* MELEE_VSYNC=0 picks Mailbox/Immediate instead of FifoRelaxed; some
         * compositors stop scanning out a FifoRelaxed surface and the window
         * then sits on a stale frame while the game runs on. */
        .vsync = !(getenv("MELEE_VSYNC") && getenv("MELEE_VSYNC")[0] == '0'),
#if defined(__ANDROID__)
        .logLevel = LOG_DEBUG,
#else
        .logLevel = getenv("MELEE_DEBUG") ? LOG_DEBUG : LOG_INFO,
#endif
        .windowWidth = 1280,
        .windowHeight = 960,
        .logCallback = log_callback,
        .desiredBackend = backend_from_env(),
        .mem1Size = PC_MEM1_SIZE,
        .mem2Size = PC_ARAM_SIZE,
    };
    pc_launcher_configure(&config);

    const AuroraInfo info = aurora_initialize(argc, argv, &config);
    /* Record which backend was actually selected. Without this the log cannot
     * say whether a run went through D3D12 or Vulkan, which is the first thing
     * worth knowing about a fault that only reproduces on one platform. */
    pc_log_line("graphics backend: %s%s", backend_name(info.backend),
        config.desiredBackend == BACKEND_AUTO ? " (auto)" : " (MELEE_BACKEND)");
    /* Closing the window exits from inside the frame loop (pc/vi.c), which
     * would otherwise skip aurora_shutdown() entirely: Dawn's static
     * destructors then tear the device down while aurora still thinks it is
     * live, its device-lost callback reports FATAL and log_callback aborts.
     * Run the shutdown from atexit so every exit path goes through it. */
    atexit(pc_shutdown_once);

    const int launched = pc_launcher_run(disc, info.window);
    if (launched != 1)
        return launched == 0 ? 0 : 1;

    watchdog_start();
    pc_menu_init(info.window);
    pc_platform_init();
    aurora_card_set_present(card);
    int rc = melee_main();
    pc_shutdown_once();
    return rc;
}
