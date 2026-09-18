#ifndef MELEE_MN_MNCHARSEL_DATA_H
#define MELEE_MN_MNCHARSEL_DATA_H

/* The character select screen's asset table, "MnSelectChrDataTable" in
 * MnSlChr.dat/.usd. Shared by the vanilla screen (mncharsel.c) and the PC
 * 8-slot screen (mn8css.c), which loads the same archive from the disc. */

#include <Runtime/platform.h>

#include <melee/sc/types.h>
#include <sysdolphin/baselib/forward.h>

typedef struct DISC_STRUCT MnSelectChrModels {
    /* 0x0 */ StaticModelDesc background;
    /* 0x10 */ StaticModelDesc hand;
    /* 0x20 */ StaticModelDesc token;
    /* 0x30 */ StaticModelDesc menu;
    /* 0x40 */ StaticModelDesc press_start;
    /* 0x50 */ StaticModelDesc debug_camera;
    /* 0x60 */ StaticModelDesc regend_menu;
    /* 0x70 */ StaticModelDesc regend_options;
    /* 0x80 */ StaticModelDesc door;
} MnSelectChrModels;

typedef struct DISC_STRUCT MnSelectChrDataTable {
    /* 0x00 */ DISC_PTR(HSD_CObjDesc) cam;
    /* 0x04 */ DISC_PTR(HSD_LightDesc) light0;
    /* 0x08 */ DISC_PTR(HSD_LightDesc) light1;
    /* 0x0C */ DISC_PTR(HSD_FogDesc) fog;
    /* 0x10 */ MnSelectChrModels models;
} MnSelectChrDataTable;
DISC_ASSERT_SIZE(MnSelectChrDataTable, 0xA0);

#endif
