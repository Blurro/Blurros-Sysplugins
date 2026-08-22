#include <3ds.h>
#include "csvc.h"
#include "draw.h"
#include "menu.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

typedef struct PluginMenuRegistration
{
    u32 pluginId;
    const char *title;
    void (*callback)(void);
    u32 color;
    struct PluginMenuRegistration *next;
} PluginMenuRegistration;

typedef struct
{
    u32 version;
    u32 active;
} PowerPreventSettings;

extern u32 powerprevent_marker_key_scan;
extern u32 menuCombo;
extern bool PLUGIN_MENU_AddItem(
    PluginMenuRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void),
    u32 color
);
extern bool PLUGIN_MENU_SaveData(u32 pluginId, const void *data, u32 size);
extern bool PLUGIN_MENU_LoadData(u32 pluginId, void *data, u32 size);

PLUGIN_DATA(powr) void *pluginTable_powr[] = {
    (void *)&powerprevent_marker_key_scan,
    (void *)&menuCombo,
    (void *)mcuHwcInit,
    (void *)MCUHWC_ReadRegister,
    (void *)MCUHWC_WriteRegister,
    (void *)mcuHwcExit,
    (void *)svcMapProcessMemoryEx,
    (void *)svcUnmapProcessMemoryEx,
    (void *)svcQueryMemory,
    (void *)svcFlushEntireDataCache,
    (void *)svcInvalidateEntireInstructionCache,
    (void *)PLUGIN_MENU_AddItem,
    (void *)Draw_Lock,
    (void *)Draw_Unlock,
    (void *)Draw_ClearFramebuffer,
    (void *)Draw_DrawString,
    (void *)Draw_FlushFramebuffer,
    (void *)waitInput,
    (void *)&menuShouldExit,
    (void *)PLUGIN_MENU_SaveData,
    (void *)PLUGIN_MENU_LoadData,
};

#define POWR_HOST__mcuHwcInit              ((Result (*)(void))pluginTable_powr[2])
#define POWR_HOST__MCUHWC_ReadRegister     ((Result (*)(u8, void *, u32))pluginTable_powr[3])
#define POWR_HOST__MCUHWC_WriteRegister    ((Result (*)(u8, const void *, u32))pluginTable_powr[4])
#define POWR_HOST__mcuHwcExit              ((void (*)(void))pluginTable_powr[5])
#define POWR_MENU__AddItem                 ((bool (*)(PluginMenuRegistration *, u32, const char *, void (*)(void), u32))pluginTable_powr[11])
#define POWR_HOST__Draw_Lock               ((void (*)(void))pluginTable_powr[12])
#define POWR_HOST__Draw_Unlock             ((void (*)(void))pluginTable_powr[13])
#define POWR_HOST__Draw_ClearFramebuffer   ((void (*)(void))pluginTable_powr[14])
#define POWR_HOST__Draw_DrawString         ((u32 (*)(u32, u32, u32, const char *))pluginTable_powr[15])
#define POWR_HOST__Draw_FlushFramebuffer   ((void (*)(void))pluginTable_powr[16])
#define POWR_HOST__waitInput               ((u32 (*)(void))pluginTable_powr[17])
#define POWR_HOST__menuShouldExit          (*(volatile bool *)pluginTable_powr[18])
#define POWR_MENU__SaveData                ((bool (*)(u32, const void *, u32))pluginTable_powr[19])
#define POWR_MENU__LoadData                ((bool (*)(u32, void *, u32))pluginTable_powr[20])

#define POWR_PLUGIN_ID 0x72776F70u
#define POWR_SETTINGS_VERSION 1u
#define POWR_FRAME_COLOR COLOR_RED
#define POWR_FRAME_TITLE_COLOR COLOR_ORANGE

PLUGIN_RODATA(powr) static const char g_powrMenuTitle[] = "PowerPrevent";
PLUGIN_RODATA(powr) static const char g_powrPlus[] = "+";
PLUGIN_RODATA(powr) static const char g_powrPipe[] = "|";
PLUGIN_RODATA(powr) static const char g_powrRail[] = "----------------------";
PLUGIN_RODATA(powr) static const char g_powrDescription[] =
    "Prevents the POWER button from turning\n"
    "the system off while active, unless\n"
    "START is also held.";
PLUGIN_RODATA(powr) static const char g_powrToggleText[] = "Press START to toggle";
PLUGIN_RODATA(powr) static const char g_powrActiveOn[] = "Active: ON";
PLUGIN_RODATA(powr) static const char g_powrActiveOff[] = "Active: OFF";
PLUGIN_RODATA(powr) static const char g_powrBackText[] = "press B to go back";
PLUGIN_RODATA(powr) static const char g_powrCredit[] = "Originally created by WerWolv";

PLUGIN_BSS(powr) u32 powerprevent_scan_held_addr;
PLUGIN_BSS(powr) static bool powerprevent_ready;
PLUGIN_BSS(powr) static bool powerprevent_start_held;
PLUGIN_DATA(powr) static bool g_powrActive = true;
PLUGIN_BSS(powr) static PluginMenuRegistration g_powrMenuRegistration;

extern bool PLUGIN_powr_InstallHook(void);

PLUGIN_CODE(powr) static bool PLUGIN_powr_SetPowerMasked(bool masked)
{
    u8 irqMask;
    Result res = POWR_HOST__mcuHwcInit();

    if (R_FAILED(res))
        return false;

    res = POWR_HOST__MCUHWC_ReadRegister(0x18, &irqMask, 1);
    if (R_SUCCEEDED(res))
    {
        if (masked)
            irqMask |= 1;
        else
            irqMask &= (u8)~1u;

        res = POWR_HOST__MCUHWC_WriteRegister(0x18, &irqMask, 1);
    }

    POWR_HOST__mcuHwcExit();
    return R_SUCCEEDED(res);
}

PLUGIN_CODE(powr) u32 PLUGIN_powr_ScanAndUpdate(void)
{
    u32 keys = ((u32 (*)(void))powerprevent_scan_held_addr)();
    bool startHeld = (keys & KEY_START) != 0;

    if (!powerprevent_ready)
    {
        if (!PLUGIN_powr_SetPowerMasked(g_powrActive && !startHeld))
            return keys;

        powerprevent_ready = true;
        powerprevent_start_held = startHeld;
    }

    if (startHeld != powerprevent_start_held)
    {
        if (PLUGIN_powr_SetPowerMasked(g_powrActive && !startHeld))
            powerprevent_start_held = startHeld;
    }

    return keys;
}

PLUGIN_CODE(powr) static void PLUGIN_powr_DrawMenuFrame(void)
{
    POWR_HOST__Draw_DrawString(10, 8, POWR_FRAME_COLOR, g_powrPlus);
    POWR_HOST__Draw_DrawString(16, 8, POWR_FRAME_COLOR, g_powrRail);
    POWR_HOST__Draw_DrawString(148, 8, POWR_FRAME_COLOR, g_powrPlus);
    POWR_HOST__Draw_DrawString(10, 16, POWR_FRAME_COLOR, g_powrPipe);
    POWR_HOST__Draw_DrawString(148, 16, POWR_FRAME_COLOR, g_powrPipe);
    POWR_HOST__Draw_DrawString(10, 24, POWR_FRAME_COLOR, g_powrPlus);
    POWR_HOST__Draw_DrawString(16, 24, POWR_FRAME_COLOR, g_powrRail);
    POWR_HOST__Draw_DrawString(148, 24, POWR_FRAME_COLOR, g_powrPlus);
    POWR_HOST__Draw_DrawString(20, 16, POWR_FRAME_TITLE_COLOR, g_powrMenuTitle);
}

PLUGIN_CODE(powr) static void PLUGIN_powr_SaveSettings(void)
{
    if (!POWR_MENU__SaveData)
        return;

    PowerPreventSettings settings;
    settings.version = POWR_SETTINGS_VERSION;
    settings.active = g_powrActive ? 1u : 0u;
    (void)POWR_MENU__SaveData(POWR_PLUGIN_ID, &settings, sizeof(settings));
}

PLUGIN_CODE(powr) static void PLUGIN_powr_LoadSettings(void)
{
    g_powrActive = true;

    if (!POWR_MENU__LoadData)
        return;

    PowerPreventSettings settings;
    if (POWR_MENU__LoadData(POWR_PLUGIN_ID, &settings, sizeof(settings)) &&
        settings.version == POWR_SETTINGS_VERSION &&
        settings.active <= 1u)
    {
        g_powrActive = settings.active != 0;
    }
}

PLUGIN_CODE(powr) static void PLUGIN_powr_DrawMenu(void)
{
    POWR_HOST__Draw_Lock();
    POWR_HOST__Draw_ClearFramebuffer();
    PLUGIN_powr_DrawMenuFrame();
    POWR_HOST__Draw_DrawString(20, 40, COLOR_WHITE, g_powrDescription);
    POWR_HOST__Draw_DrawString(
        20,
        90,
        g_powrActive ? COLOR_GREEN : COLOR_RED,
        g_powrActive ? g_powrActiveOn : g_powrActiveOff
    );
    POWR_HOST__Draw_DrawString(20, 110, COLOR_ORANGE, g_powrToggleText);
    POWR_HOST__Draw_DrawString(20, 130, COLOR_GRAY, g_powrBackText);
    POWR_HOST__Draw_DrawString(20, 220, COLOR_GRAY, g_powrCredit);
    POWR_HOST__Draw_FlushFramebuffer();
    POWR_HOST__Draw_Unlock();
}

PLUGIN_CODE(powr) static void PLUGIN_powr_OpenMenu(void)
{
    PLUGIN_powr_DrawMenu();

    while (!POWR_HOST__menuShouldExit)
    {
        u32 pressed = POWR_HOST__waitInput();

        if (pressed & KEY_B)
            break;

        if (pressed & KEY_START)
        {
            bool next = !g_powrActive;
            if (PLUGIN_powr_SetPowerMasked(false))
            {
                g_powrActive = next;
                powerprevent_ready = false;
                PLUGIN_powr_SaveSettings();
                PLUGIN_powr_DrawMenu();
            }
        }
    }
}

PLUGIN_MAIN(powr) bool PLUGIN_powr_Main(void)
{
    if (!POWR_MENU__AddItem || !POWR_MENU__SaveData || !POWR_MENU__LoadData)
        return false;

    PLUGIN_powr_LoadSettings();

    if (!PLUGIN_powr_InstallHook())
        return false;

    (void)POWR_MENU__AddItem(
        &g_powrMenuRegistration,
        POWR_PLUGIN_ID,
        g_powrMenuTitle,
        PLUGIN_powr_OpenMenu,
        RGB565(31, 41, 20)
    );

    return true;
}