/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gmtest.h"

#include <placeholder.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <melee/ft/forward.h>
#include <melee/gm/gm8player.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/types.h>
#include <melee/gr/forward.h>
#include <melee/mn/types.h>
#include <melee/pl/forward.h>

#include <pc/pc.h>

#define TEST_MAX_PLAYERS 8
#define TEST_PORTS 4
#define TEST_CSS_DELAY 30 ///< frames on the CSS before starting, so it settles
#define TEST_MAX_CAPTURES 32

typedef struct TestPlayer {
    s8 ckind;
    u8 level;
} TestPlayer;

typedef struct TestCapture {
    u8 scene; ///< ::GameSceneKind
    u32 frame;
} TestCapture;

static struct {
    bool init;
    bool enabled;
    TestPlayer players[TEST_MAX_PLAYERS];
    int n_players;
    int stage;
    int time_s;
    int stocks;
    bool items;
    u32 results_frames;
    TestCapture captures[TEST_MAX_CAPTURES];
    int n_captures;

    int css_visits;
    bool css_started;
    u8 scene;       ///< current ::GameSceneKind
    u32 scene_frame; ///< frames since it was entered
} gmtest;

static const struct {
    const char* name;
    s8 ckind;
} test_fighters[] = {
    { "falcon", CKind_Captain },    { "captain", CKind_Captain },
    { "dk", CKind_Donkey },         { "donkey", CKind_Donkey },
    { "fox", CKind_Fox },           { "gnw", CKind_GameWatch },
    { "gameandwatch", CKind_GameWatch }, { "kirby", CKind_Kirby },
    { "bowser", CKind_Koopa },      { "link", CKind_Link },
    { "luigi", CKind_Luigi },       { "mario", CKind_Mario },
    { "marth", CKind_Mars },        { "mewtwo", CKind_Mewtwo },
    { "ness", CKind_Ness },         { "peach", CKind_Peach },
    { "pikachu", CKind_Pikachu },   { "ics", CKind_PopoNana },
    { "iceclimbers", CKind_PopoNana }, { "puff", CKind_Purin },
    { "jigglypuff", CKind_Purin },  { "samus", CKind_Samus },
    { "yoshi", CKind_Yoshi },       { "zelda", CKind_Zelda },
    { "sheik", CKind_Seak },        { "falco", CKind_Falco },
    { "ylink", CKind_CLink },       { "younglink", CKind_CLink },
    { "doc", CKind_DrMario },       { "drmario", CKind_DrMario },
    { "roy", CKind_Emblem },        { "pichu", CKind_Pichu },
    { "ganon", CKind_Ganon },       { "ganondorf", CKind_Ganon },
};

static const struct {
    const char* name;
    int stkind;
} test_stages[] = {
    { "battlefield", St_Kind_Battle },   { "bf", St_Kind_Battle },
    { "fd", St_Kind_Last },              { "final", St_Kind_Last },
    { "dreamland", St_Kind_OldPupupu },  { "dl", St_Kind_OldPupupu },
    { "yoshis", St_Kind_Story },         { "ys", St_Kind_Story },
    { "fod", St_Kind_Izumi },            { "fountain", St_Kind_Izumi },
    { "stadium", St_Kind_PStadium },     { "ps", St_Kind_PStadium },
    { "kongo", St_Kind_Kongo },          { "corneria", St_Kind_Corneria },
    { "mutecity", St_Kind_MuteCity },    { "brinstar", St_Kind_Zebes },
    { "onett", St_Kind_Onett },          { "temple", St_Kind_Shrine },
    { "bigblue", St_Kind_BigBlue },      { "flatzone", St_Kind_Flatzone },
};

static int test_lookup_fighter(const char* s, size_t len)
{
    size_t i;
    char* end;
    long n = strtol(s, &end, 10);
    if (end != s && (size_t) (end - s) == len) {
        return n >= 0 && n < CKind_Playable_Count ? (int) n : -1;
    }
    for (i = 0; i < sizeof(test_fighters) / sizeof(*test_fighters); i++) {
        if (strlen(test_fighters[i].name) == len &&
            strncmp(test_fighters[i].name, s, len) == 0)
        {
            return test_fighters[i].ckind;
        }
    }
    return -1;
}

static int test_lookup_stage(const char* s)
{
    size_t i;
    char* end;
    long n = strtol(s, &end, 0);
    if (end != s && *end == '\0') {
        return (int) n;
    }
    for (i = 0; i < sizeof(test_stages) / sizeof(*test_stages); i++) {
        if (strcmp(test_stages[i].name, s) == 0) {
            return test_stages[i].stkind;
        }
    }
    return -1;
}

static u8 test_scene_by_name(const char* s, size_t len)
{
    if (len == 5 && strncmp(s, "match", 5) == 0) {
        return GS_VS;
    }
    if (len == 7 && strncmp(s, "results", 7) == 0) {
        return GS_RESULTS;
    }
    if (len == 3 && strncmp(s, "css", 3) == 0) {
        return GS_CSS;
    }
    if (len == 3 && strncmp(s, "sss", 3) == 0) {
        return GS_SSS;
    }
    return GS_COUNT;
}

static const char* test_scene_name(u8 scene)
{
    switch (scene) {
    case GS_TITLE:
        return "title";
    case GS_MENU:
        return "menu";
    case GS_VS:
        return "match";
    case GS_SUDDEN_DEATH:
        return "sudden-death";
    case GS_RESULTS:
        return "results";
    case GS_CSS:
        return "css";
    case GS_SSS:
        return "sss";
    case GS_MEMCARD:
        return "memcard";
    default:
        return "other";
    }
}

/// Whole run fails: say why and stop, so a script never waits on a run that
/// cannot finish.
static void test_fail(const char* why)
{
    pc_log_line("test: FAIL %s", why);
    fflush(NULL);
    exit(3);
}

static void test_init(void)
{
    const char* env;

    gmtest.init = true;
    env = getenv("MELEE_TEST_MATCH");
    gmtest.enabled = env != NULL && env[0] != '\0' && env[0] != '0';
    if (!gmtest.enabled) {
        return;
    }

    env = getenv("MELEE_TEST_PLAYERS");
    if (env == NULL || env[0] == '\0') {
        env = "fox,marth";
    }
    while (*env != '\0' && gmtest.n_players < TEST_MAX_PLAYERS) {
        size_t len = strcspn(env, ",:");
        TestPlayer* p = &gmtest.players[gmtest.n_players];
        int ckind = test_lookup_fighter(env, len);
        if (ckind < 0) {
            char buf[96];
            snprintf(buf, sizeof(buf), "unknown fighter '%.*s'", (int) len, env);
            test_fail(buf);
        }
        if (ckind == CKind_Seak) {
            /* No Sheik icon on either select screen: she's Zelda's down-B. */
            test_fail("sheik can't be picked on the CSS; use zelda");
        }
        p->ckind = (s8) ckind;
        p->level = 9;
        env += len;
        if (*env == ':') {
            int level = (int) strtol(env + 1, (char**) &env, 10);
            p->level = (u8) (level < 1 ? 1 : level > 9 ? 9 : level);
        }
        gmtest.n_players++;
        if (*env == ',') {
            env++;
        }
    }
    if (gmtest.n_players < 2) {
        test_fail("MELEE_TEST_PLAYERS needs at least two fighters");
    }
    if (gmtest.n_players > TEST_PORTS && !gm8Player_IsEnabled()) {
        test_fail("more than 4 players needs the 8-player mod (cfg:eight_player=1)");
    }

    env = getenv("MELEE_TEST_STAGE");
    gmtest.stage = env != NULL && env[0] != '\0' ? test_lookup_stage(env) : St_Kind_Battle;
    if (gmtest.stage < 0) {
        test_fail("unknown MELEE_TEST_STAGE");
    }
    env = getenv("MELEE_TEST_TIME");
    gmtest.time_s = env != NULL ? atoi(env) : 60;
    env = getenv("MELEE_TEST_STOCKS");
    gmtest.stocks = env != NULL ? atoi(env) : 0;
    env = getenv("MELEE_TEST_ITEMS");
    gmtest.items = env != NULL && env[0] == '1';
    env = getenv("MELEE_TEST_RESULTS");
    gmtest.results_frames = env != NULL ? (u32) atoi(env) : 300;

    env = getenv("MELEE_TEST_CAPTURE");
    while (env != NULL && *env != '\0' && gmtest.n_captures < TEST_MAX_CAPTURES) {
        size_t len = strcspn(env, ":,");
        u8 scene = test_scene_by_name(env, len);
        if (scene == GS_COUNT || env[len] != ':') {
            test_fail("bad MELEE_TEST_CAPTURE entry (want scene:frame)");
        }
        gmtest.captures[gmtest.n_captures].scene = scene;
        gmtest.captures[gmtest.n_captures].frame =
            (u32) strtoul(env + len + 1, (char**) &env, 10);
        gmtest.n_captures++;
        if (*env == ',') {
            env++;
        }
    }

    pc_log_line("test: enabled, %d players, stage %d, %s %d, items %s",
                gmtest.n_players, gmtest.stage,
                gmtest.stocks > 0 ? "stocks" : "time",
                gmtest.stocks > 0 ? gmtest.stocks : gmtest.time_s,
                gmtest.items ? "on" : "off");
}

bool gmTest_IsEnabled(void)
{
    if (!gmtest.init) {
        test_init();
    }
    return gmtest.enabled;
}

u8 gmTest_BootMode(u8 mode)
{
    if (!gmTest_IsEnabled()) {
        return mode;
    }
    pc_log_line("test: boot straight to VS mode");
    return GM_VS;
}

bool gmTest_CssFrame(CSSData* css, u32 frame)
{
    StartMeleeData* start;
    int i;

    if (!gmTest_IsEnabled() || gmtest.css_started) {
        return false;
    }
    if (gmtest.css_visits > 1) {
        /* Back from the results screen: the whole loop ran. */
        pc_log_line("test: PASS back at character select after results");
        pc_exit_requested = true;
        gmtest.css_started = true;
        return false;
    }
    if (frame < TEST_CSS_DELAY) {
        return false;
    }

    start = &css->vs.start;
    for (i = 0; i < TEST_PORTS; i++) {
        PlayerInitData* p = &start->players[i];
        if (i >= gmtest.n_players) {
            p->slot_type = Gm_PKind_NA;
            continue;
        }
        p->slot_type = Gm_PKind_Cpu;
        p->ckind = gmtest.players[i].ckind;
        p->cpu_level = gmtest.players[i].level;
        p->cpu_kind = CpuKind_4; /* VS AI; 0 is the Training dummy */
        p->color = 0;
        p->team = (u8) (i % 2);
    }
    /* Duplicates of a fighter get distinct costumes, like the CSS forces. */
    for (i = 1; i < gmtest.n_players && i < TEST_PORTS; i++) {
        int j;
        for (j = 0; j < i; j++) {
            if (start->players[j].ckind == start->players[i].ckind) {
                start->players[i].color++;
            }
        }
    }
    if (gm8Player_IsEnabled()) {
        for (i = 0; i < GM_MAX_PLAYERS - TEST_PORTS; i++) {
            int slot = TEST_PORTS + i;
            if (slot < gmtest.n_players) {
                gm8Player_PanelSetCkind(i, gmtest.players[slot].ckind);
                gm8Player_PanelSetLevel(i, gmtest.players[slot].level);
            } else {
                gm8Player_PanelSetCkind(i, ChKind_None);
            }
            gm8Player_PanelSetColor(i, GM8P_COLOR_AUTO);
            gm8Player_PanelSetTeam(i, (u8) (slot % 2));
        }
    }
    gmtest.css_started = true;
    pc_log_line("test: character select done at frame %u", frame);
    return true;
}

int gmTest_ForcedStage(void)
{
    return gmTest_IsEnabled() ? gmtest.stage : -1;
}

void gmTest_ConfigureVs(StartMeleeData* start)
{
    int i;
    int fighters = 0;

    if (!gmTest_IsEnabled()) {
        return;
    }
    if (gmtest.stocks > 0) {
        start->rules.match_kind = MatchKind_Stock;
        start->rules.is_stock = true;
        start->rules.timer_enabled = gmtest.time_s > 0;
    } else {
        start->rules.match_kind = MatchKind_Time;
        start->rules.is_stock = false;
        start->rules.timer_enabled = true;
    }
    start->rules.time_limit = (u32) (gmtest.time_s > 0 ? gmtest.time_s : 60);
    if (!gmtest.items) {
        start->rules.item_freq = -1;
    }
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        PlayerInitData* p = &start->players[i];
        if (p->slot_type == Gm_PKind_NA) {
            continue;
        }
        if (gmtest.stocks > 0) {
            p->stocks = (s8) gmtest.stocks;
        }
        p->rumble_enabled = false;
        fighters++;
        pc_log_line("test: slot %d ckind %d level %d color %d", i, p->ckind,
                    p->cpu_level, p->color);
    }
    pc_log_line("test: match configured, %d fighters on stage %d", fighters,
                start->rules.stkind);
    if (fighters != gmtest.n_players) {
        pc_log_line("test: WARN asked for %d fighters, match has %d",
                    gmtest.n_players, fighters);
    }
}

void gmTest_MatchEnd(const struct MatchEnd* end)
{
    int i;

    if (!gmTest_IsEnabled()) {
        return;
    }
    pc_log_line("test: match ended, outcome %d after %u frames, %d winner(s)",
                end->outcome, end->frame_count, end->n_winners);
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        const struct MatchPlayerData* p = &end->player_standings[i];
        if (p->pkind == Gm_PKind_NA) {
            continue;
        }
        pc_log_line("test: slot %d ckind %d stocks %d percent %u sds %u", i,
                    p->ckind, p->stocks, p->percent, p->self_destructs);
    }
}

void gmTest_SceneEnter(u8 scene_kind)
{
    if (!gmTest_IsEnabled()) {
        return;
    }
    gmtest.scene = scene_kind;
    gmtest.scene_frame = 0;
    if (scene_kind == GS_CSS) {
        gmtest.css_visits++;
        gmtest.css_started = false;
    }
    pc_log_line("test: scene %s (%d)", test_scene_name(scene_kind), scene_kind);
}

void gmTest_Frame(void)
{
    int i;

    if (!gmTest_IsEnabled()) {
        return;
    }
    gmtest.scene_frame++;
    for (i = 0; i < gmtest.n_captures; i++) {
        if (gmtest.captures[i].scene == gmtest.scene &&
            gmtest.captures[i].frame == gmtest.scene_frame)
        {
            char tag[32];
            snprintf(tag, sizeof(tag), "%s_%05u", test_scene_name(gmtest.scene),
                     gmtest.scene_frame);
            pc_capture_tagged(tag);
        }
    }
    if (gmtest.scene == GS_RESULTS && gmtest.scene_frame == gmtest.results_frames) {
        pc_log_line("test: leaving results after %u frames", gmtest.scene_frame);
        gm_801A4B60();
    }
}
