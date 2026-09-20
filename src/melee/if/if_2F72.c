#include "if_2F72.h"

#include "if_2F6E.h"
#include "ifall.h"
#include "ifstatus.h"
#include "types.h"
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmvs.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>

/// Orphaned data strings from original ROM
static char lbl_803F9780[] = "ScInfStc_scene_models";

/* Retail sized this 13 = 2 * 6 + 1, for GM_DISC_MAX_PLAYERS slots: entry 0 is
 * the model desc, then each slot owns base[2*slot + 1] and base[2*slot + 2].
 * With GM_MAX_PLAYERS raised to 8, slots 6 and 7 index 13..16, past the 13
 * declared entries. The reason that crashed rather than merely corrupting
 * something is the cleanup: fn_802F75D4/fn_802F7670 only scanned six slots, so
 * an out-of-bounds entry was never cleared when its gobj was freed. The next
 * stock loss for that slot handed the dangling pointer to HSD_GObjFree ->
 * GObj_RemoveUserData -> efLib_remove_user_data, which read effect->gobj off a
 * null user_data (fault on 0x8, the 64-bit offset of that field). That is why
 * it took a few minutes and a second stock loss on the same slot to hit. */
static void* lbl_804A1340[2 * GM_MAX_PLAYERS + 1];

s32 fn_802F7288(HSD_GObj* gobj, Element_803F9628* entry)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    f32 frame = lbGetJObjCurrFrame(jobj);
    s32 unused;

    HSD_JObjAnimAll(jobj);

    if (!entry->x12.x0) {
        if (frame >= (f32) entry->x10) {
            if ((s32) entry->x20 >= 0) {
                lbAudioAx_800237A8(entry->x20, 127, 64);
                entry->x12.x0 = 1;
            } else if ((s32) entry->xC >= 0) {
                lbAudioAx_800237A8(entry->xC, 127, 64);
                entry->x12.x0 = 1;
            }
        }
    }

    if (!entry->x12.x1) {
        if (frame >= (f32) entry->x11) {
            if ((s32) entry->x24 >= 0) {
                lbAudioAx_800237A8(entry->x24, 127, 64);
                entry->x12.x1 = 1;
            }
        }
    }

    return lb_8000B09C(jobj);
}

void if_802F73C4(HSD_GObj* gobj)
{
    Element_803F9628* const entries = ifStatus_803F9628;
    Element_803F9628* entry;
    s32 i;
    Element_803F9628* curr;
    s32 idx;

    curr = entries;
    for (i = 0; i < 8; curr++, i++) {
        if (curr->x0 == gobj) {
            entry = &entries[i];
            goto found;
        }
    }
    entry = NULL;

found:
    idx = ((u8*) entry - (u8*) entries) / sizeof(Element_803F9628);
    if (entry != NULL && !entry->x12.x2) {
        if (entry->x18 != NULL) {
            entry->x18(idx);
        }
        entry->x12.x2 = 1;
    }
    if (entry != NULL) {
        if (fn_802F7288(gobj, entry) == 0) {
            if (entry->x1C != NULL) {
                entry->x1C(idx);
            }
            entry->x0 = NULL;
            HSD_GObjFree(gobj);
        }
    }
}

void if_802F74D0(HSD_GObj* gobj)
{
    Element_803F9628* const entries = ifStatus_803F9628;
    Element_803F9628* entry;
    s32 i;
    Element_803F9628* curr;
    s32 idx;

    curr = entries;
    for (i = 0; i < 8; curr++, i++) {
        if (curr->x0 == gobj) {
            entry = &entries[i];
            goto found;
        }
    }
    entry = NULL;

found:
    idx = ((u8*) entry - (u8*) entries) / sizeof(Element_803F9628);
    if (entry != NULL && !entry->x12.x2) {
        if (entry->x18 != NULL) {
            entry->x18(idx);
        }
        entry->x12.x2 = 1;
    }
    if (entry != NULL) {
        if (fn_802F7288(gobj, entry) == 0) {
            if (entry->x1C != NULL) {
                entry->x1C(idx);
            }
            entry->x1C = NULL;
        }
    }
}

void fn_802F75D4(HSD_GObj* gobj)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    void** base;
    void** data;
    s32 i;

    if (lb_8000B09C(jobj) == 0) {
        base = lbl_804A1340;
        data = base;
        for (i = 0; i < GM_MAX_PLAYERS; data += 2, i++) {
            if (data[1] == gobj) {
                base[i * 2 + 1] = NULL;
                break;
            }
        }
        HSD_GObjFree(gobj);
    } else {
        HSD_JObjAnimAll(jobj);
    }
}

void fn_802F7670(HSD_GObj* gobj)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    void** base;
    void** data;
    s32 i;

    if (lb_8000B09C(jobj) == 0) {
        base = lbl_804A1340;
        data = base;
        for (i = 0; i < GM_MAX_PLAYERS; data += 2, i++) {
            if (data[2] == gobj) {
                base[i * 2 + 2] = NULL;
                break;
            }
        }
        HSD_GObjFree(gobj);
    } else {
        HSD_JObjAnimAll(jobj);
    }
}

void fn_802F770C(HSD_GObj* gobj, int callback)
{
    HudIndex* status = ifStatus_GetHUDInfo();
    void** base = lbl_804A1340;
    s32 slot;
    s32 i;

    /* Retail unrolled this as six hand-written comparisons on base[2*i + 1].
     * Rolled up so it covers GM_MAX_PLAYERS: left unrolled, slots 6 and 7
     * always fell through to -1 and skipped the digit-hiding check. */
    slot = -1;
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        if (base[i * 2 + 1] == gobj) {
            slot = i;
            break;
        }
    }

    /* The search above yields -1 when the gobj is not in the table; retail
     * then read players[-1], i.e. 0x94 bytes before the HUD array. */
    if (slot < 0 || !status->players[slot].flags.hide_all_digits) {
        HSD_GObj_JObjCallback(gobj, callback);
    }
}

HSD_GObj* fn_802F77F8(HSD_GObj* gobj, u8 slot, u16 arg2)
{
    void** base = lbl_804A1340;
    HSD_JObj* jobj;
    Vec3* pos;
    HSD_JObj* j;

    if (base[0] == NULL) {
        return NULL;
    }

    if (gobj != NULL) {
        HSD_GObjFree(gobj);
    }

    gobj = GObj_Create(14, 15, 0);
    if (gobj != NULL) {
        /* base[0] is a 32-bit disc slot holding the DynamicModelDesc, and
         * the desc's own `joint` is another 32-bit slot. Both need relocating
         * -- reading them as host pointers loads 8 bytes of adjacent disc
         * data and hands HSD_JObjLoadJoint a wild address. */
        DynamicModelDesc* desc =
            DP(DynamicModelDesc, ((const DiscU32*) base[0])->v);

        jobj = HSD_JObjLoadJoint(DP(HSD_Joint, desc->joint));
        if (jobj != NULL) {
            HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, jobj);
            GObj_SetupGXLink(gobj, fn_802F770C, 11, 0);
            j = jobj;
            gm_8016895C(j, desc, (u8) arg2);
            HSD_JObjReqAnimAll(jobj, 0.0f);
            HSD_JObjAnimAll(jobj);

            pos = ifAll_GetPlayerHUDPosition(slot);
            HSD_JObjSetTranslate(jobj, pos);
        } else {
            HSD_GObjFree(gobj);
            gobj = NULL;
        }
    }

    return gobj;
}

void fn_802F7994(HSD_GObj* gobj)
{
    s32 slot;
    void** entry;
    HSD_JObj* jobj;
    void** base = lbl_804A1340;
    s32 idx;
    s32 i;
    f32 frame;
    HSD_GObj* result;

    PAD_STACK(8);

    jobj = gobj->hsd_obj;
    frame = lbGetJObjCurrFrame(jobj);

    /* Same rolled-up six-way scan as fn_802F770C; see the note there. */
    slot = -1;
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        if (base[i * 2 + 1] == gobj) {
            slot = i;
            break;
        }
    }

    if (slot >= 0) {
        if (frame > 12.0f && base[slot * 2 + 2] == NULL) {
            idx = (u8) slot << 1;
            entry = base + ((u8) slot << 1);
            result = fn_802F77F8(*(entry += 2), (u8) slot, 1);
            base[idx + 2] = result;
            if (base[idx + 2] != NULL) {
                HSD_GObj_SetupProc(*entry, (HSD_GObjEvent) fn_802F7670, 0x11);
            }
        }
        if (lb_8000B09C(jobj) == 0) {
            base[slot * 2 + 1] = NULL;
            HSD_GObjFree(gobj);
        } else {
            HSD_JObjAnimAll(jobj);
        }
    }
}

void if_802F7AF8(s32 slot)
{
    s32 idx;
    void** base1;
    void** base = lbl_804A1340;
    s32 slot2 = Player_80036428(slot);
    HSD_GObj* result;

    idx = (u8) slot << 1;
    result = fn_802F77F8(base[idx + 1], (u8) slot, 1);
    base1 = &base[1];
    base1[idx] = result;
    if (base1[idx] != NULL) {
        HSD_GObj_SetupProc(base[idx + 1], (HSD_GObjEvent) fn_802F75D4, 0x11);
    }

    idx = (u8) slot2 << 1;
    result = fn_802F77F8(base[idx + 1], (u8) slot2, 2);
    base1[idx] = result;
    if (base1[idx] != NULL) {
        HSD_GObj_SetupProc(base[idx + 1], (HSD_GObjEvent) fn_802F75D4, 0x11);
    }
}

void if_802F7BB4(s32 player_idx)
{
    void** base;
    u8 idx;
    s32 offset;
    void** entry;

    base = lbl_804A1340;
    idx = (u8) player_idx;
    offset = idx << 1;
    entry = base + (((((((offset & 0xFFFFFFFFu) & 0xFFFFFFFFu) & 0xFFFFFFFFu) &
                       0xFFFFFFFFu) &
                      0xFFFFFFFFu) &
                     0xFFFFFFFFu) &
                    0xFFFFFFFFu);
    (base + offset)[1] = fn_802F77F8(*++entry, idx, 1);
    if ((base + offset)[1] != NULL) {
        HSD_GObj_SetupProc(*entry, (HSD_GObjEvent) fn_802F75D4, 0x11);
    }
}

static inline void if_802F7C30_slot(void** base, s32 slot, s32 idx, s32 flag)
{
    void** entry = base + idx;

    base[idx + 1] = fn_802F77F8(*++entry, (u8) slot, flag);
    if (base[idx + 1] != NULL) {
        HSD_GObj_SetupProc(*entry, (HSD_GObjEvent) fn_802F75D4, 0x11);
    }
}

void if_802F7C30(s32 slot)
{
    void** base = lbl_804A1340;
    s32 idx;
    void** entry;
    s32 ret = gm_8016AEC8();
    HSD_GObj* result;

    if (ret == -2) {
        idx = (u8) slot << 1;
        entry = base + idx;
        result = fn_802F77F8(*++entry, (u8) slot, 0);
        base[idx + 1] = result;
        if (base[idx + 1] != NULL) {
            HSD_GObj_SetupProc(*entry, (HSD_GObjEvent) fn_802F75D4, 0x11);
        }
    } else if (ret == -1) {
        idx = (u8) slot << 1;
        if_802F7C30_slot(base, slot, idx, 1);
    }
}

static inline void if_802F7D08_slot(void** base, s32 slot, s32 idx, s32 flag)
{
    void** entry = base + idx;

    base[idx + 1] = fn_802F77F8(*++entry, (u8) slot, flag);
    if (base[idx + 1] != NULL) {
        HSD_GObj_SetupProc(*entry, (HSD_GObjEvent) fn_802F7994, 0x11);
    }
}

void if_802F7D08(s32 slot)
{
    void** base = lbl_804A1340;
    s32 idx;
    void** entry;
    s32 ret = gm_8016AEC8();
    HSD_GObj* result;
    PAD_STACK(8);

    if (ret == -2) {
        idx = (u8) slot << 1;
        entry = base + idx;
        result = fn_802F77F8(*++entry, (u8) slot, 0);
        base[idx + 1] = result;
        if (base[idx + 1] != NULL) {
            HSD_GObj_SetupProc(*entry, (HSD_GObjEvent) fn_802F7994, 0x11);
        }
    } else if (ret == -1) {
        idx = (u8) slot << 1;
        if_802F7D08_slot(base, slot, idx, 1);
    } else {
        idx = (u8) slot << 1;
        if_802F7C30_slot(base, slot, idx, 1);
    }
}

void if_802F7E24(void)
{
    memzero(lbl_804A1340, sizeof(lbl_804A1340));
    lbArchive_LoadSections(*ifAll_GetArchive(), lbl_804A1340, lbl_803F9780, 0);
}

void if_802F7E7C(void)
{
    s32 i;
    HSD_GObj** base = (HSD_GObj**) lbl_804A1340;

    for (i = 0; i < 6; i++) {
        if (base[i * 2 + 1] != NULL) {
            HSD_GObjFree(base[i * 2 + 1]);
        }
        if (base[i * 2 + 2] != NULL) {
            HSD_GObjFree(base[i * 2 + 2]);
        }
    }

    memzero(base, sizeof(lbl_804A1340));
}
