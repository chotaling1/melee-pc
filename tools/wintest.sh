#!/usr/bin/env bash
# Prepare a scripted Windows test run of the current build-win/melee.exe.
#
#   tools/wintest.sh NAME [VAR=VALUE...] [cfg:KEY=VALUE...]
#
# Stages the exe and its DLLs into test-runs/bin (leaving dist/ alone, which
# may be running), writes a private profile under test-runs/NAME/user with a
# launcher.cfg (mute, no update check, plus any cfg:KEY=VALUE overrides such
# as cfg:soccer=1), and a run.bat that sets VAR=VALUE (MELEE_SEED,
# MELEE_CAPTURE_FRAMES, MELEE_EXIT_FRAME, ...) and runs the game with stdout
# and stderr in test-runs/NAME/stdout.log. Prints run.bat's Windows path,
# which is what gets run on the Windows machine:
#
#   cmd /c \\wsl.localhost\...\test-runs\NAME\run.bat
#
# Captures land in test-runs/NAME/captures, the game log in
# test-runs/NAME/melee-pc.log, the exit code in test-runs/NAME/exit_code.
# MELEE_EXIT_FRAME defaults to 3600 so a run always ends on its own; runs are
# unthrottled (MELEE_TURBO=1, vsync off) unless overridden with MELEE_TURBO=0.
set -euo pipefail

name=${1:?usage: tools/wintest.sh NAME [VAR=VALUE...] [cfg:KEY=VALUE...]}
shift
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-${root}/build-win}"
runs="${root}/test-runs"
run="${runs}/${name}"
distro="${WSL_DISTRO_NAME:-OpenClawGateway}"
disc="${MELEE_TEST_DISC:-${root}/../orig/GALE01/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso}"

winpath() { # /home/x/y -> \\wsl.localhost\Distro\home\x\y
    local abs
    abs="$(realpath -m "$1")"
    printf '\\\\wsl.localhost\\%s%s' "$distro" "${abs//\//\\}"
}

[ -f "$disc" ] || { echo "disc not found: $disc" >&2; exit 1; }

# Stage only what changed, so a run doesn't recopy 100 MB of DLLs. The
# toolchain/VC runtime DLLs (libwinpthread, VCRUNTIME140, nod.dll) are only
# gathered by tools/package_windows.sh, so take those from its last staging.
mkdir -p "${runs}/bin"
pkg="${root}/dist/melee-windows-x86_64"
[ -d "$pkg" ] || { echo "run tools/package_windows.sh once first (for runtime DLLs)" >&2; exit 1; }
for f in "$pkg"/*.dll; do
    cmp -s "$f" "${runs}/bin/${f##*/}" 2>/dev/null || cp "$f" "${runs}/bin/"
done
for f in melee.exe dxcompiler.dll dxil.dll webgpu_dawn.dll SDL3.dll libpng16.dll libzlib1.dll libnod.dll; do
    if [ -f "${build}/${f}" ]; then
        cmp -s "${build}/${f}" "${runs}/bin/${f}" 2>/dev/null || cp "${build}/${f}" "${runs}/bin/${f}"
    fi
done
rsync -a --delete "${root}/resources/" "${runs}/bin/resources/" 2>/dev/null ||
    { rm -rf "${runs}/bin/resources"; cp -r "${root}/resources" "${runs}/bin/resources"; }
if [ ! -f "${runs}/bin/initial_pipeline_cache.db" ]; then
    gzip -dc "${root}/tools/initial_pipeline_cache.db.gz" >"${runs}/bin/initial_pipeline_cache.db"
fi

rm -rf "${run}"
mkdir -p "${run}/user" "${run}/captures"

declare -A cfg=([mute]=1 [check_updates]=0 [fullscreen]=0 [vsync]=0)
declare -A env=([MELEE_EXIT_FRAME]=3600 [MELEE_TURBO]=1 [MELEE_VSYNC]=0 [MELEE_HANG_WATCHDOG]=15)
for arg in "$@"; do
    case "$arg" in
    cfg:*=*) kv=${arg#cfg:}; cfg[${kv%%=*}]=${kv#*=} ;;
    *=*) env[${arg%%=*}]=${arg#*=} ;;
    *) echo "bad argument: $arg" >&2; exit 1 ;;
    esac
done
{
    printf 'disc "%s"\n' "$(winpath "$disc" | sed 's/\\/\\\\/g')"
    for k in "${!cfg[@]}"; do printf '%s %s\n' "$k" "${cfg[$k]}"; done
} >"${run}/user/launcher.cfg"

{
    printf '@echo off\r\nsetlocal\r\n'
    # The profile lives on local disk: the memory card can't create its
    # directories on the WSL share and SQLite can't lock its caches there.
    # It starts from a copy of the player's card, so unlocks match theirs and
    # their real save is never written.
    printf 'set "MELEE_USER_DIR=%%LOCALAPPDATA%%\\melee-pc-test\\%s\\"\r\n' "$name"
    printf 'if exist "%%MELEE_USER_DIR%%" rmdir /s /q "%%MELEE_USER_DIR%%"\r\n'
    printf 'mkdir "%%MELEE_USER_DIR%%"\r\n'
    printf 'copy /y "%s" "%%MELEE_USER_DIR%%" > nul\r\n' "$(winpath "${run}/user/launcher.cfg")"
    printf 'if exist "%%APPDATA%%\\melee-pc\\USA" xcopy /e /i /q /y "%%APPDATA%%\\melee-pc\\USA" "%%MELEE_USER_DIR%%USA" > nul\r\n'
    printf 'set "MELEE_CAPTURE_DIR=%s"\r\n' "$(winpath "${run}/captures")"
    printf 'set "MELEE_LOG_FILE=%s"\r\n' "$(winpath "${run}/melee-pc.log")"
    printf 'set "MELEE_WINDOW_TITLE=melee-pc-test"\r\n'
    for k in "${!env[@]}"; do printf 'set "%s=%s"\r\n' "$k" "${env[$k]}"; done
    printf '"%s" "%s" > "%s" 2>&1\r\n' "$(winpath "${runs}/bin/melee.exe")" "$(winpath "$disc")" \
        "$(winpath "${run}/stdout.log")"
    printf 'echo %%ERRORLEVEL%% > "%s"\r\n' "$(winpath "${run}/exit_code")"
    printf 'type "%s"\r\n' "$(winpath "${run}/exit_code")"
} >"${run}/run.bat"

winpath "${run}/run.bat"
echo
