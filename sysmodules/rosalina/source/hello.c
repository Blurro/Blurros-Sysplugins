#include <3ds.h>
#include "csvc.h"
#include "draw.h"
#include "menu.h"
#include "menus/process_list.h"
#include "sysplugin_menu.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define HELLO_PLUGIN_ID 0x6F6C6568u
#define HELLO_DIGIT_COUNT 6u

extern bool PLUGIN_helo_InstallHook(void);
extern bool PLUGIN_helo_UninstallHook(void);
extern bool PLUGIN_MENU_MapPage(Handle sourceProcess, u32 sourceAddress, u32 *mappedBase, u32 *mappedAddress);
extern void PLUGIN_MENU_UnmapPage(u32 mappedBase);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_MapPage);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_UnmapPage);

// Host and MENU calls go through repaired pointers.
PLUGIN_DATA(helo) void *pluginTable_helo[] = {
    (void *)PLUGIN_MENU_AddItem,
    (void *)Draw_Lock,
    (void *)Draw_Unlock,
    (void *)Draw_ClearFramebuffer,
    (void *)Draw_DrawString,
    (void *)Draw_FlushFramebuffer,
    (void *)waitInputWithTimeout,
    (void *)&menuShouldExit,
    (void *)PLUGIN_MENU_SaveData,
    (void *)PLUGIN_MENU_LoadData,
    (void *)PLUGIN_MENU_MapPage,
    (void *)PLUGIN_MENU_UnmapPage,
    (void *)svcFlushEntireDataCache,
    (void *)svcInvalidateEntireInstructionCache,
    (void *)RosalinaMenu_ProcessList,
    (void *)Draw_DrawMenuFrame,
};

#define HELLO_MENU__AddItem               ((bool (*)(PluginMenuRegistration *, u32, const char *, void (*)(void), u32))pluginTable_helo[0])
#define HELLO_HOST__Draw_Lock              ((void (*)(void))pluginTable_helo[1])
#define HELLO_HOST__Draw_Unlock            ((void (*)(void))pluginTable_helo[2])
#define HELLO_HOST__Draw_ClearFramebuffer  ((void (*)(void))pluginTable_helo[3])
#define HELLO_HOST__Draw_DrawString        ((u32 (*)(u32, u32, u32, const char *))pluginTable_helo[4])
#define HELLO_HOST__Draw_FlushFramebuffer  ((void (*)(void))pluginTable_helo[5])
#define HELLO_HOST__waitInputWithTimeout   ((u32 (*)(s32))pluginTable_helo[6])
#define HELLO_HOST__menuShouldExit         (*(volatile bool *)pluginTable_helo[7])
#define HELLO_MENU__SaveData               ((bool (*)(u32, const void *, u32))pluginTable_helo[8])
#define HELLO_MENU__LoadData               ((bool (*)(u32, void *, u32))pluginTable_helo[9])
#define HELLO_HOST__Draw_DrawMenuFrame     ((void (*)(const char *))pluginTable_helo[15])

PLUGIN_RODATA(helo) static const char g_helloMenuTitle[] = "Hello World";
PLUGIN_RODATA(helo) static const char g_helloHijackedTitle[] = "Hijacked by Hello World hook";
PLUGIN_RODATA(helo) static const char g_helloPlus[] = "+";
PLUGIN_RODATA(helo) static const char g_helloPipe[] = "|";
PLUGIN_RODATA(helo) static const char g_helloRail[] = "----------------------";
PLUGIN_RODATA(helo) static const char g_helloMessage[] = "Hello world!";
PLUGIN_RODATA(helo) static const char g_helloDescription[] =
    "lil demo for Nexus3DS Sysplugins!\n"
    "registers a page and counts how long its open.\n"
    "Also saves your visits via a MENU api";
PLUGIN_RODATA(helo) static const char g_helloHijackedText[] = "Process list got rerouted here.";
PLUGIN_RODATA(helo) static const char g_helloBackText[] = "press b to go back";
PLUGIN_RODATA(helo) static const char g_helloClearTimer[] = "                              ";

PLUGIN_DATA(helo) static char g_helloTimerText[] = "Seconds open: 000000";
PLUGIN_DATA(helo) static char g_helloVisitText[] = "Visits: 000000";
PLUGIN_BSS(helo) static PluginMenuRegistration g_helloMenuRegistration;

PLUGIN_CODE(helo) static void PLUGIN_helo_ResetDigits(char *digits)
{
    volatile char *out = (volatile char *)digits;

    for (u32 i = 0; i < HELLO_DIGIT_COUNT; i++)
        out[i] = '0';
}

PLUGIN_CODE(helo) static bool PLUGIN_helo_DigitsValid(const char *digits)
{
    for (u32 i = 0; i < HELLO_DIGIT_COUNT; i++)
    {
        if (digits[i] < '0' || digits[i] > '9')
            return false;
    }

    return true;
}

// Keep the counter as text so GCC doesn't pull in division helpers.
PLUGIN_CODE(helo) static void PLUGIN_helo_IncrementDigits(char *digits)
{
    for (s32 i = (s32)HELLO_DIGIT_COUNT - 1; i >= 0; i--)
    {
        if (digits[i] < '9')
        {
            digits[i]++;
            return;
        }

        digits[i] = '0';
    }
}

PLUGIN_CODE(helo) static void PLUGIN_helo_DrawFrame(const char *title)
{
    HELLO_HOST__Draw_DrawString(10, 8, COLOR_CYAN, g_helloPlus);
    HELLO_HOST__Draw_DrawString(16, 8, COLOR_CYAN, g_helloRail);
    HELLO_HOST__Draw_DrawString(148, 8, COLOR_CYAN, g_helloPlus);
    HELLO_HOST__Draw_DrawString(10, 16, COLOR_CYAN, g_helloPipe);
    HELLO_HOST__Draw_DrawString(148, 16, COLOR_CYAN, g_helloPipe);
    HELLO_HOST__Draw_DrawString(10, 24, COLOR_CYAN, g_helloPlus);
    HELLO_HOST__Draw_DrawString(16, 24, COLOR_CYAN, g_helloRail);
    HELLO_HOST__Draw_DrawString(148, 24, COLOR_CYAN, g_helloPlus);
    HELLO_HOST__Draw_DrawString(20, 16, COLOR_WHITE, title);
}

PLUGIN_CODE(helo) static void PLUGIN_helo_DrawPage(void)
{
    HELLO_HOST__Draw_Lock();
    HELLO_HOST__Draw_ClearFramebuffer();
    PLUGIN_helo_DrawFrame(g_helloMenuTitle);
    HELLO_HOST__Draw_DrawString(20, 45, COLOR_GREEN, g_helloMessage);
    HELLO_HOST__Draw_DrawString(20, 65, COLOR_WHITE, g_helloDescription);
    HELLO_HOST__Draw_DrawString(20, 110, COLOR_YELLOW, g_helloTimerText);
    HELLO_HOST__Draw_DrawString(20, 125, COLOR_CYAN, g_helloVisitText);
    HELLO_HOST__Draw_DrawString(20, 150, COLOR_GRAY, g_helloBackText);
    HELLO_HOST__Draw_FlushFramebuffer();
    HELLO_HOST__Draw_Unlock();
}

// Only redraw the row that changed.
PLUGIN_CODE(helo) static void PLUGIN_helo_RedrawTimer(void)
{
    HELLO_HOST__Draw_Lock();
    HELLO_HOST__Draw_DrawString(20, 110, COLOR_BLACK, g_helloClearTimer);
    HELLO_HOST__Draw_DrawString(20, 110, COLOR_YELLOW, g_helloTimerText);
    HELLO_HOST__Draw_FlushFramebuffer();
    HELLO_HOST__Draw_Unlock();
}

PLUGIN_CODE(helo) static void PLUGIN_helo_OpenMenu(void)
{
    u32 tenths = 0;

    PLUGIN_helo_ResetDigits(&g_helloTimerText[14]);
    PLUGIN_helo_IncrementDigits(&g_helloVisitText[8]);
    (void)HELLO_MENU__SaveData(HELLO_PLUGIN_ID, &g_helloVisitText[8], HELLO_DIGIT_COUNT);
    PLUGIN_helo_DrawPage();

    while (!HELLO_HOST__menuShouldExit)
    {
        u32 pressed = HELLO_HOST__waitInputWithTimeout(100);

        if (pressed & KEY_B)
            break;

        if (pressed == 0 && ++tenths >= 10u)
        {
            tenths = 0;
            PLUGIN_helo_IncrementDigits(&g_helloTimerText[14]);
            PLUGIN_helo_RedrawTimer();
        }
    }
}

// Process list lands here after hello_hooks.c patches its entry.
PLUGIN_CODE(helo) void PLUGIN_helo_OpenHijackedPage(void)
{
    HELLO_HOST__Draw_Lock();
    HELLO_HOST__Draw_ClearFramebuffer();
    HELLO_HOST__Draw_DrawMenuFrame(g_helloHijackedTitle);
    HELLO_HOST__Draw_DrawString(20, 50, COLOR_GREEN, g_helloHijackedText);
    HELLO_HOST__Draw_DrawString(20, 75, COLOR_GRAY, g_helloBackText);
    HELLO_HOST__Draw_FlushFramebuffer();
    HELLO_HOST__Draw_Unlock();

    while (!HELLO_HOST__menuShouldExit)
    {
        if (HELLO_HOST__waitInputWithTimeout(100) & KEY_B)
            break;
    }
}

// Load visits, install the demo hook, then add the Sysplugin Menu page.
PLUGIN_MAIN(helo) bool PLUGIN_helo_Main(void)
{
    if (!HELLO_MENU__AddItem || !HELLO_MENU__SaveData || !HELLO_MENU__LoadData)
        return false;

    PLUGIN_helo_ResetDigits(&g_helloVisitText[8]);
    if (HELLO_MENU__LoadData(HELLO_PLUGIN_ID, &g_helloVisitText[8], HELLO_DIGIT_COUNT) &&
        !PLUGIN_helo_DigitsValid(&g_helloVisitText[8]))
    {
        PLUGIN_helo_ResetDigits(&g_helloVisitText[8]);
    }

    if (!PLUGIN_helo_InstallHook())
        return false;

    if (HELLO_MENU__AddItem(
        &g_helloMenuRegistration,
        HELLO_PLUGIN_ID,
        g_helloMenuTitle,
        PLUGIN_helo_OpenMenu,
        RGB565(12, 43, 31)))
    {
        return true;
    }

    // If restore ever fails, stay loaded so the host can't jump into freed code.
    return !PLUGIN_helo_UninstallHook();
}
