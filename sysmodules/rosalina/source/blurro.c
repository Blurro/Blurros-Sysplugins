#include <3ds.h>
#include "memory.h"
#include "menu.h"
#include "sysplugin_menu.h"
#include "service_manager.h"
#include "MyThread.h"
#include "draw.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#define CTR_ALIGN(x)
#endif

#define PLUGIN_CODE(id)     __attribute__((section(".plugin_" #id), used))
#define PLUGIN_RODATA(id)   __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)     __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)      __attribute__((section(".pluginbss_" #id), used)) // zeroed data doesn't need stored in the 3nx

// delta is how many system ticks passed for this callback
typedef void (*BlurTickFunc)(u64 delta);
typedef void (*BlurMenuDrawFunc)(bool debug);

typedef struct
{
    u32 version;
    u32 menuTextEnabled;
} BlurMenuSettings;

typedef struct BlurFeatureRegistration
{
    u32 pluginId;
    const char *title;
    void (*callback)(void);
    struct BlurFeatureRegistration *next;
} BlurFeatureRegistration;


#define BLUR_SCRATCH_LOW  0x10000000u
#define BLUR_SCRATCH_HIGH 0x14000000u
#define BLUR_PLUGIN_ID     0x72756C62u
#define BLUR_SETTINGS_VERSION 4u

extern bool preTerminationRequested;
extern Handle preTerminationEvent;
extern u32 blur_marker_menudraw_start;
extern u32 blur_marker_menudraw_end;
extern u32 blur_marker_menu_entered;
extern u32 blur_marker_menu_leaving;
extern u64 __aeabi_uldivmod(u64 numerator, u64 denominator);

PLUGIN_DATA(blur) void* pluginTable_blur[] = {
    (void*)isServiceUsable,
    (void*)svcSleepThread,
    (void*)svcGetSystemTick,
    (void*)&preTerminationRequested,
    (void*)MyThread_Create,
    (void*)&preTerminationEvent,
    (void*)svcFlushEntireDataCache,
    (void*)svcMapProcessMemoryEx,
    (void*)svcUnmapProcessMemoryEx,
    (void*)&blur_marker_menudraw_start,
    (void*)&blur_marker_menudraw_end,
    (void*)Draw_DrawFormattedString,
    (void*)&blur_marker_menu_entered,
    (void*)&blur_marker_menu_leaving,
    (void*)Draw_SetupFramebuffer,
    (void*)Draw_RestoreFramebuffer,
    (void*)Draw_FreeFramebufferCache,
    (void*)svcInvalidateEntireInstructionCache,
    (void*)__aeabi_uldivmod,
    (void*)svcCreateEvent,
    (void*)svcSignalEvent,
    (void*)svcWaitSynchronizationN,
    (void*)svcCloseHandle,
    (void*)PLUGIN_MENU_AddItem,
    (void*)Draw_Lock,
    (void*)Draw_Unlock,
    (void*)Draw_ClearFramebuffer,
    (void*)Draw_DrawString,
    (void*)Draw_FlushFramebuffer,
    (void*)waitInputWithTimeout,
    (void*)&menuShouldExit,
    (void*)PLUGIN_MENU_SaveData,
    (void*)PLUGIN_MENU_LoadData,
    (void*)PLUGIN_MENU_AddOnlineEntry,
    (void*)PLUGIN_MENU_FindFreeRange
};
#define BLUR_HOST__isServiceUsable                  ((bool(*)(const char*))pluginTable_blur[0])
#define BLUR_HOST__svcSleepThread                   ((void(*)(s64))pluginTable_blur[1])
#define BLUR_HOST__svcGetSystemTick                 ((u64(*)(void))pluginTable_blur[2])
#define BLUR_HOST__preTerminationRequested          (*(bool*)pluginTable_blur[3])
#define BLUR_HOST__MyThread_Create                  ((Result(*)(MyThread*,void(*)(void),void*,u32,int,int))pluginTable_blur[4])
#define BLUR_HOST__preTerminationEvent              (*(volatile Handle*)pluginTable_blur[5])
#define BLUR_HOST__svcMapProcessMemoryEx            ((Result(*)(Handle,u32,Handle,u32,u32,MapExFlags))pluginTable_blur[7])
#define BLUR_HOST__svcUnmapProcessMemoryEx          ((Result(*)(Handle,u32,u32))pluginTable_blur[8])
#define BLUR_HOST__blur_marker_menudraw_start       ((u32)pluginTable_blur[9])
#define BLUR_HOST__blur_marker_menudraw_end         ((u32)pluginTable_blur[10])
#define BLUR_HOST__Draw_DrawFormattedString         ((void(*)(u32,u32,u32,const char*,...))pluginTable_blur[11])
#define BLUR_HOST__blur_marker_menu_entered         ((u32)pluginTable_blur[12])
#define BLUR_HOST__blur_marker_menu_leaving         ((u32)pluginTable_blur[13])
#define BLUR_HOST__Draw_SetupFramebuffer            ((void(*)(void))pluginTable_blur[14])
#define BLUR_HOST__Draw_RestoreFramebuffer          ((void(*)(void))pluginTable_blur[15])
#define BLUR_HOST__Draw_FreeFramebufferCache        ((void(*)(void))pluginTable_blur[16])
#define BLUR_HOST__udiv64                           ((u64(*)(u64,u64))pluginTable_blur[18])
#define BLUR_HOST__svcCreateEvent                   ((Result(*)(Handle*,ResetType))pluginTable_blur[19])
#define BLUR_HOST__svcSignalEvent                   ((Result(*)(Handle))pluginTable_blur[20])
#define BLUR_HOST__svcWaitSynchronizationN          ((Result(*)(s32*,const Handle*,s32,bool,s64))pluginTable_blur[21])
#define BLUR_HOST__svcCloseHandle                   ((Result(*)(Handle))pluginTable_blur[22])
#define BLUR_MENU__AddItem                          ((bool(*)(PluginMenuRegistration*,u32,const char*,void(*)(void),u32))pluginTable_blur[23])
#define BLUR_HOST__Draw_Lock                        ((void(*)(void))pluginTable_blur[24])
#define BLUR_HOST__Draw_Unlock                      ((void(*)(void))pluginTable_blur[25])
#define BLUR_HOST__Draw_ClearFramebuffer            ((void(*)(void))pluginTable_blur[26])
#define BLUR_HOST__Draw_DrawString                  ((u32(*)(u32,u32,u32,const char*))pluginTable_blur[27])
#define BLUR_HOST__Draw_FlushFramebuffer            ((void(*)(void))pluginTable_blur[28])
#define BLUR_HOST__waitInputWithTimeout             ((u32(*)(s32))pluginTable_blur[29])
#define BLUR_HOST__menuShouldExit                   (*(bool*)pluginTable_blur[30])
#define BLUR_MENU__SaveData                         ((bool(*)(u32,const void*,u32))pluginTable_blur[31])
#define BLUR_MENU__LoadData                         ((bool(*)(u32,void*,u32))pluginTable_blur[32])
#define BLUR_MENU__AddOnlineEntry                   ((bool(*)(const char*,const char*))pluginTable_blur[33])
#define BLUR_MENU__FindFreeRange                    ((bool(*)(u32,u32*))pluginTable_blur[34])

PLUGIN_BSS(blur) static MyThread plgThread;
PLUGIN_BSS(blur) static u8 CTR_ALIGN(8) plgThreadStack[0x1000];

#define BLUR_MAX_TICK_FUNCS 30
#define BLUR_NS_PER_SECOND 1000000000ULL
PLUGIN_BSS(blur) static BlurTickFunc blurTickFuncs[BLUR_MAX_TICK_FUNCS];
PLUGIN_BSS(blur) static u64 blurTickIntervalTicks[BLUR_MAX_TICK_FUNCS];
PLUGIN_BSS(blur) static u64 blurTickLastRunTicks[BLUR_MAX_TICK_FUNCS];
PLUGIN_BSS(blur) static volatile s32 blurTickRegistryLock;
PLUGIN_BSS(blur) static volatile Handle blurTickWakeEvent;
// other plugins can read this, but AddTickFunc should be what changes it
PLUGIN_BSS(blur) volatile u32 blurTickFuncCount;

#define BLUR_MAX_MENU_DRAW_FUNCS 3
PLUGIN_BSS(blur) static BlurMenuDrawFunc blurMenuDrawFuncs[BLUR_MAX_MENU_DRAW_FUNCS];
PLUGIN_BSS(blur) static u32 blurMenuDrawFuncCount;
PLUGIN_BSS(blur) static BlurFeatureRegistration *g_blurFeatureHead;
PLUGIN_BSS(blur) static BlurFeatureRegistration *g_blurFeatureTail;
PLUGIN_DATA(blur) static bool g_blurMenuDrawDebug = false;
PLUGIN_DATA(blur) static bool g_blurMenuTextEnabled = false;
PLUGIN_BSS(blur) static bool blurCoolThreadCreated;
PLUGIN_BSS(blur) static volatile bool menuFreeze;

PLUGIN_BSS(blur) u32 g_debugVal1;
PLUGIN_BSS(blur) u32 g_debugVal2;
PLUGIN_BSS(blur) u32 g_debugVal3;
PLUGIN_BSS(blur) u32 g_debugVal4;

PLUGIN_RODATA(blur) static const char g_debugFormat[] = "DEBUG: 1=%lu 2=%lu 3=%lu 4=%lu";
PLUGIN_RODATA(blur) static const char g_blurNexusText[] = "Nexus3DS - Blurro's Custom";
PLUGIN_RODATA(blur) static const char g_blurLumaText[] = "Luma3DS - Blurro's Custom";
PLUGIN_RODATA(blur) static const char service_acu[] = "ac:u";
PLUGIN_RODATA(blur) static const char service_hid[] = "hid:USER";
PLUGIN_RODATA(blur) static const char service_gpu[] = "gsp::Gpu";
PLUGIN_RODATA(blur) static const char service_lcd[] = "gsp::Lcd";
PLUGIN_RODATA(blur) static const char service_cdc[] = "cdc:CHK";
#define BLUR_FRAME_COLOR RGB565(16, 32, 16)
#define BLUR_FRAME_TITLE_COLOR RGB565(6, 25, 31)
#define BLUR_BACK_COLOR RGB565(15, 31, 15)

PLUGIN_RODATA(blur) const char g_blurFeatureTitle[] = "Blurro Features Menu";
PLUGIN_RODATA(blur) const char g_blurOnlineV1Title[] = "Blurro\'s Sysplugins";
PLUGIN_RODATA(blur) const char g_blurOnlineV1Url[] = "https://blurro.github.io/sysplugins/online_v1/onlinetemporary.3on";
PLUGIN_RODATA(blur) static const char g_blurToggleMenuTextOn[] = "Toggle Menu Text: ON";
PLUGIN_RODATA(blur) static const char g_blurToggleMenuTextOff[] = "Toggle Menu Text: OFF";
PLUGIN_RODATA(blur) static const char g_blurSelected[] = ">";
PLUGIN_RODATA(blur) static const char g_blurUnselected[] = " ";
PLUGIN_RODATA(blur) static const char g_blurFrameCorner[] = "+";
PLUGIN_RODATA(blur) static const char g_blurFrameSide[] = "|";
PLUGIN_RODATA(blur) static const char g_blurFrameRail[] = "----------------------";
PLUGIN_RODATA(blur) static const char g_blurBackText[] = "press B to go back";
PLUGIN_RODATA(blur) static const char g_blurClearRow[] =
    "                                                  ";
PLUGIN_BSS(blur) PluginMenuRegistration g_blurMenuRegistration;
PLUGIN_BSS(blur) static bool g_blurHostIsLuma;

PLUGIN_CODE(blur) void PLUGIN_blur_SetHostIsLuma(bool isLuma)
{
    g_blurHostIsLuma = isLuma;
}

extern bool PLUGIN_blur_SetMenuTextHookEnabled(bool enabled);

PLUGIN_CODE(blur) static void PLUGIN_blur_SaveMenuSettings(void)
{
    // tiny MENU-owned settings blob
    if (!BLUR_MENU__SaveData)
        return;

    BlurMenuSettings settings;
    settings.version = BLUR_SETTINGS_VERSION;
    settings.menuTextEnabled = g_blurMenuTextEnabled ? 1u : 0u;
    (void)BLUR_MENU__SaveData(BLUR_PLUGIN_ID, &settings, sizeof(settings));
}

PLUGIN_CODE(blur) void PLUGIN_blur_LoadMenuSettings(void)
{
    g_blurMenuTextEnabled = false;

    if (!BLUR_MENU__LoadData)
        return;

    BlurMenuSettings settings;
    if (BLUR_MENU__LoadData(BLUR_PLUGIN_ID, &settings, sizeof(settings)) &&
        settings.version == BLUR_SETTINGS_VERSION &&
        settings.menuTextEnabled <= 1u)
    {
        g_blurMenuTextEnabled = settings.menuTextEnabled != 0;
        return;
    }

    PLUGIN_blur_SaveMenuSettings();
}

PLUGIN_CODE(blur) bool PLUGIN_blur_IsMenuTextEnabled(void)
{
    return g_blurMenuTextEnabled;
}

PLUGIN_CODE(blur) static void PLUGIN_blur_DrawFeatureItem(
    u32 y,
    bool selected,
    const char *title
)
{
    BLUR_HOST__Draw_DrawString(
        14,
        y,
        selected ? BLUR_FRAME_TITLE_COLOR : COLOR_GRAY,
        selected ? g_blurSelected : g_blurUnselected
    );
    BLUR_HOST__Draw_DrawString(
        24,
        y,
        selected ? COLOR_CYAN : COLOR_WHITE,
        title
    );
}

PLUGIN_CODE(blur) void PLUGIN_blur_DrawFeatureFrame(const char *title)
{
    BLUR_HOST__Draw_DrawString(10, 8, BLUR_FRAME_COLOR, g_blurFrameCorner);
    BLUR_HOST__Draw_DrawString(16, 8, BLUR_FRAME_COLOR, g_blurFrameRail);
    BLUR_HOST__Draw_DrawString(148, 8, BLUR_FRAME_COLOR, g_blurFrameCorner);
    BLUR_HOST__Draw_DrawString(10, 16, BLUR_FRAME_COLOR, g_blurFrameSide);
    BLUR_HOST__Draw_DrawString(148, 16, BLUR_FRAME_COLOR, g_blurFrameSide);
    BLUR_HOST__Draw_DrawString(10, 24, BLUR_FRAME_COLOR, g_blurFrameCorner);
    BLUR_HOST__Draw_DrawString(16, 24, BLUR_FRAME_COLOR, g_blurFrameRail);
    BLUR_HOST__Draw_DrawString(148, 24, BLUR_FRAME_COLOR, g_blurFrameCorner);
    BLUR_HOST__Draw_DrawString(20, 16, BLUR_FRAME_TITLE_COLOR, title);
}

PLUGIN_CODE(blur) bool PLUGIN_blur_AddFeatureItem(
    BlurFeatureRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void)
)
{
    if (!item || !pluginId || !title || !callback)
        return false;

    for (BlurFeatureRegistration *current = g_blurFeatureHead; current; current = current->next)
    {
        if (current == item)
            return false;
    }

    item->pluginId = pluginId;
    item->title = title;
    item->callback = callback;
    item->next = NULL;

    if (g_blurFeatureTail)
        g_blurFeatureTail->next = item;
    else
        g_blurFeatureHead = item;

    g_blurFeatureTail = item;
    return true;
}

PLUGIN_CODE(blur) static u32 PLUGIN_blur_GetFeatureCount(void)
{
    u32 count = 1;
    for (BlurFeatureRegistration *item = g_blurFeatureHead; item; item = item->next)
        count++;
    return count;
}

PLUGIN_CODE(blur) static BlurFeatureRegistration *PLUGIN_blur_GetFeatureItem(u32 index)
{
    if (index < 1)
        return NULL;

    index -= 1;
    BlurFeatureRegistration *item = g_blurFeatureHead;
    while (item && index)
    {
        item = item->next;
        index--;
    }
    return item;
}

PLUGIN_CODE(blur) static const char *PLUGIN_blur_GetFeatureTitle(u32 index)
{
    if (index == 0)
        return g_blurMenuTextEnabled ? g_blurToggleMenuTextOn : g_blurToggleMenuTextOff;

    BlurFeatureRegistration *item = PLUGIN_blur_GetFeatureItem(index);
    return item ? item->title : NULL;
}

PLUGIN_CODE(blur) static void PLUGIN_blur_DrawFeatureMenu(u32 selected)
{
    BLUR_HOST__Draw_Lock();
    BLUR_HOST__Draw_ClearFramebuffer();
    PLUGIN_blur_DrawFeatureFrame(g_blurFeatureTitle);
    PLUGIN_blur_DrawFeatureItem(
        40,
        selected == 0,
        g_blurMenuTextEnabled ? g_blurToggleMenuTextOn : g_blurToggleMenuTextOff
    );

    u32 index = 1;
    for (BlurFeatureRegistration *item = g_blurFeatureHead; item; item = item->next, index++)
        PLUGIN_blur_DrawFeatureItem(40 + index * 15, selected == index, item->title);

    BLUR_HOST__Draw_DrawString(20, 120, BLUR_BACK_COLOR, g_blurBackText);
    BLUR_HOST__Draw_FlushFramebuffer();
    BLUR_HOST__Draw_Unlock();
}

PLUGIN_CODE(blur) static void PLUGIN_blur_RedrawFeatureSelection(
    u32 oldSelected,
    u32 selected
)
{
    const char *oldTitle = PLUGIN_blur_GetFeatureTitle(oldSelected);
    const char *newTitle = PLUGIN_blur_GetFeatureTitle(selected);
    if (!oldTitle || !newTitle)
        return;

    u32 oldY = 40 + oldSelected * 15;
    u32 newY = 40 + selected * 15;

    BLUR_HOST__Draw_Lock();
    BLUR_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_blurClearRow);
    BLUR_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_blurClearRow);
    PLUGIN_blur_DrawFeatureItem(oldY, false, oldTitle);
    PLUGIN_blur_DrawFeatureItem(newY, true, newTitle);
    BLUR_HOST__Draw_FlushFramebuffer();
    BLUR_HOST__Draw_Unlock();
}

PLUGIN_CODE(blur) void PLUGIN_blur_OpenFeatureMenu(void)
{
    u32 selected = 0;
    PLUGIN_blur_DrawFeatureMenu(selected);

    do
    {
        u32 pressed = BLUR_HOST__waitInputWithTimeout(50);
        u32 count = PLUGIN_blur_GetFeatureCount();

        if (pressed & KEY_B)
            return;
        else if (pressed & KEY_DOWN)
        {
            u32 oldSelected = selected;
            selected = selected + 1u >= count ? 0u : selected + 1u;
            if (selected != oldSelected)
                PLUGIN_blur_RedrawFeatureSelection(oldSelected, selected);
        }
        else if (pressed & KEY_UP)
        {
            u32 oldSelected = selected;
            selected = selected ? selected - 1u : count - 1u;
            if (selected != oldSelected)
                PLUGIN_blur_RedrawFeatureSelection(oldSelected, selected);
        }
        else if (pressed & KEY_A)
        {
            if (selected == 0u)
            {
                bool next = !g_blurMenuTextEnabled;
                if (PLUGIN_blur_SetMenuTextHookEnabled(next))
                {
                    g_blurMenuTextEnabled = next;
                    PLUGIN_blur_SaveMenuSettings();
                }
            }
            else
            {
                BlurFeatureRegistration *item = PLUGIN_blur_GetFeatureItem(selected);
                if (item && item->callback)
                    item->callback();
            }

            PLUGIN_blur_DrawFeatureMenu(selected);
        }
    } while (!BLUR_HOST__menuShouldExit);
}


PLUGIN_CODE(blur) bool PLUGIN_blur_MapPage(u32 sourceAddress, u32 *mappedBase, u32 *mappedAddress)
{
    u32 base;
    u32 page = sourceAddress & ~0xFFFu;

    if (!mappedBase || !mappedAddress || !BLUR_MENU__FindFreeRange(0x1000u, &base))
        return false;

    if (R_FAILED(BLUR_HOST__svcMapProcessMemoryEx(
        CUR_PROCESS_HANDLE,
        base,
        CUR_PROCESS_HANDLE,
        page,
        0x1000,
        (MapExFlags)0)))
    {
        return false;
    }

    *mappedBase = base;
    *mappedAddress = base + (sourceAddress & 0xFFFu);
    return true;
}

PLUGIN_CODE(blur) void PLUGIN_blur_UnmapPage(u32 mappedBase)
{
    if (mappedBase)
        BLUR_HOST__svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, mappedBase, 0x1000);
}

PLUGIN_CODE(blur) bool PLUGIN_blur_ReadHostWord(u32 address, u32 *value)
{
    u32 mapBase;
    u32 mappedAddress;

    if (!value || !PLUGIN_blur_MapPage(address, &mapBase, &mappedAddress))
        return false;

    *value = *(volatile u32*)mappedAddress;
    PLUGIN_blur_UnmapPage(mapBase);
    return true;
}

PLUGIN_CODE(blur) static u64 PLUGIN_blur_NsToTicks(u64 ns)
{
    u64 seconds = BLUR_HOST__udiv64(ns, BLUR_NS_PER_SECOND);
    u64 remainderNs = ns - seconds * BLUR_NS_PER_SECOND;
    u64 ticks = seconds * (u64)SYSCLOCK_ARM11;

    ticks += BLUR_HOST__udiv64(
        remainderNs * (u64)SYSCLOCK_ARM11,
        BLUR_NS_PER_SECOND
    );

    // never let an interval round down to 0 ticks
    return ticks ? ticks : 1;
}

PLUGIN_CODE(blur) static u64 PLUGIN_blur_TicksToNsCeil(u64 ticks)
{
    u64 seconds = BLUR_HOST__udiv64(ticks, (u64)SYSCLOCK_ARM11);
    u64 remainderTicks = ticks - seconds * (u64)SYSCLOCK_ARM11;
    u64 scaledRemainder = remainderTicks * BLUR_NS_PER_SECOND;

    return seconds * BLUR_NS_PER_SECOND + BLUR_HOST__udiv64(
        scaledRemainder + (u64)SYSCLOCK_ARM11 - 1,
        (u64)SYSCLOCK_ARM11
    );
}

PLUGIN_CODE(blur) static void PLUGIN_blur_LockTickRegistry(void)
{
    s32 *lock = (s32*)&blurTickRegistryLock;

    for (;;)
    {
        if (__ldrex(lock) == 0)
        {
            if (!__strex(lock, 1))
            {
                __dmb();
                return;
            }
        }
        else
        {
            __clrex();
        }

        // give the worker a moment to release the lock
        BLUR_HOST__svcSleepThread(1000);
    }
}

PLUGIN_CODE(blur) static void PLUGIN_blur_UnlockTickRegistry(void)
{
    __dmb();
    blurTickRegistryLock = 0;
}

PLUGIN_CODE(blur) static void PLUGIN_blur_WakeTickWorker(void)
{
    Handle event = blurTickWakeEvent;

    if (event)
        BLUR_HOST__svcSignalEvent(event);
}

PLUGIN_CODE(blur) static void PLUGIN_blur_WaitForTickWorker(
    bool hasDeadline,
    u64 waitTicks
)
{
    Handle handles[2];
    s32 handleCount = 0;
    s32 signaledIndex = 0;
    s64 waitNs = -1;
    Handle wakeEvent = blurTickWakeEvent;
    Handle terminationEvent = BLUR_HOST__preTerminationEvent;

    if (hasDeadline)
        waitNs = (s64)PLUGIN_blur_TicksToNsCeil(waitTicks);

    if (wakeEvent)
        handles[handleCount++] = wakeEvent;
    if (terminationEvent)
        handles[handleCount++] = terminationEvent;

    if (handleCount)
        BLUR_HOST__svcWaitSynchronizationN(
            &signaledIndex,
            handles,
            handleCount,
            false,
            waitNs
        );
    else if (hasDeadline)
        BLUR_HOST__svcSleepThread(waitNs);
    else
        BLUR_HOST__svcSleepThread(10 * 1000 * 1000LL);
}

// adding the same func again just changes its own interval
PLUGIN_CODE(blur) bool PLUGIN_blur_AddTickFunc(BlurTickFunc func, s64 intervalNs)
{
    if (!func || intervalNs <= 0)
        return false;

    u64 intervalTicks = PLUGIN_blur_NsToTicks((u64)intervalNs);
    u64 tickNow = BLUR_HOST__svcGetSystemTick();
    bool added = false;
    u32 freeIndex = BLUR_MAX_TICK_FUNCS;

    PLUGIN_blur_LockTickRegistry();

    for (u32 i = 0; i < blurTickFuncCount; i++)
    {
        if (blurTickFuncs[i] == func)
        {
            blurTickIntervalTicks[i] = intervalTicks;
            blurTickLastRunTicks[i] = tickNow;
            added = true;
            break;
        }

        if (!blurTickFuncs[i] && freeIndex == BLUR_MAX_TICK_FUNCS)
            freeIndex = i;
    }

    if (!added)
    {
        if (freeIndex == BLUR_MAX_TICK_FUNCS && blurTickFuncCount < BLUR_MAX_TICK_FUNCS)
        {
            freeIndex = blurTickFuncCount;
            blurTickFuncCount++;
        }

        if (freeIndex < BLUR_MAX_TICK_FUNCS)
        {
            blurTickFuncs[freeIndex] = func;
            blurTickIntervalTicks[freeIndex] = intervalTicks;
            blurTickLastRunTicks[freeIndex] = tickNow;
            added = true;
        }
    }

    PLUGIN_blur_UnlockTickRegistry();

    if (added)
        PLUGIN_blur_WakeTickWorker();

    return added;
}

PLUGIN_CODE(blur) bool PLUGIN_blur_RemoveTickFunc(BlurTickFunc func)
{
    if (!func)
        return false;

    bool removed = false;

    PLUGIN_blur_LockTickRegistry();

    for (u32 i = 0; i < blurTickFuncCount; i++)
    {
        if (blurTickFuncs[i] != func)
            continue;

        blurTickFuncs[i] = NULL;
        blurTickIntervalTicks[i] = 0;
        blurTickLastRunTicks[i] = 0;
        removed = true;
        break;
    }

    while (blurTickFuncCount && !blurTickFuncs[blurTickFuncCount - 1u])
        blurTickFuncCount--;

    PLUGIN_blur_UnlockTickRegistry();

    if (removed)
        PLUGIN_blur_WakeTickWorker();

    return removed;
}

PLUGIN_CODE(blur) bool PLUGIN_blur_AddMenuDrawFunc(BlurMenuDrawFunc func)
{
    if (!func)
        return false;

    for (u32 i = 0; i < blurMenuDrawFuncCount; i++)
    {
        if (blurMenuDrawFuncs[i] == func)
            return true;
    }

    if (blurMenuDrawFuncCount >= BLUR_MAX_MENU_DRAW_FUNCS)
        return false;

    blurMenuDrawFuncs[blurMenuDrawFuncCount++] = func;
    return true;
}

PLUGIN_CODE(blur) bool PLUGIN_blur_IsMenuFrozen(void)
{
    return menuFreeze;
}

PLUGIN_CODE(blur) void PLUGIN_blur_RunMenuDrawHookBody(Menu *currentMenu)
{
    (void)currentMenu;

    for (u32 i = 0; i < blurMenuDrawFuncCount; i++)
    {
        if (blurMenuDrawFuncs[i])
            blurMenuDrawFuncs[i](g_blurMenuDrawDebug);
    }

    if (g_blurMenuDrawDebug)
    {
        BLUR_HOST__Draw_DrawFormattedString(
            10,
            SCREEN_BOT_HEIGHT - 35,
            COLOR_PURPLE,
            g_debugFormat,
            (unsigned long)g_debugVal1,
            (unsigned long)g_debugVal2,
            (unsigned long)g_debugVal3,
            (unsigned long)g_debugVal4
        );
    }

    BLUR_HOST__Draw_DrawFormattedString(
        10,
        SCREEN_BOT_HEIGHT - 20,
        RGB565(5, 17, 31),
        g_blurHostIsLuma ? g_blurLumaText : g_blurNexusText
    );
}


PLUGIN_CODE(blur) static void PLUGIN_blur_ResetTickCadences(u64 tickNow)
{
    PLUGIN_blur_LockTickRegistry();

    for (u32 i = 0; i < blurTickFuncCount; i++)
        blurTickLastRunTicks[i] = tickNow;

    PLUGIN_blur_UnlockTickRegistry();
}

PLUGIN_CODE(blur) static void PLUGIN_blur_RunDueTickCallbacks(void)
{
    for (u32 i = 0;; i++)
    {
        BlurTickFunc func = NULL;
        u64 delta = 0;
        u64 tickNow = BLUR_HOST__svcGetSystemTick();

        PLUGIN_blur_LockTickRegistry();

        if (i >= blurTickFuncCount)
        {
            PLUGIN_blur_UnlockTickRegistry();
            break;
        }

        func = blurTickFuncs[i];
        if (func && blurTickIntervalTicks[i])
        {
            delta = tickNow - blurTickLastRunTicks[i];
            if (delta >= blurTickIntervalTicks[i])
                blurTickLastRunTicks[i] = tickNow;
            else
                func = NULL;
        }
        else
        {
            func = NULL;
        }

        PLUGIN_blur_UnlockTickRegistry();

        // pass how long it has actually been since this callback ran
        if (func && !menuFreeze && !BLUR_HOST__preTerminationRequested)
            func(delta);
    }
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_GetNextTickWait(
    u64 tickNow,
    u64 *waitTicksOut
)
{
    bool hasDeadline = false;
    u64 waitTicks = 0;

    PLUGIN_blur_LockTickRegistry();

    for (u32 i = 0; i < blurTickFuncCount; i++)
    {
        if (!blurTickFuncs[i] || !blurTickIntervalTicks[i])
            continue;

        u64 elapsed = tickNow - blurTickLastRunTicks[i];
        u64 remaining = elapsed >= blurTickIntervalTicks[i] ?
            0 : blurTickIntervalTicks[i] - elapsed;

        if (!hasDeadline || remaining < waitTicks)
        {
            waitTicks = remaining;
            hasDeadline = true;
        }
    }

    PLUGIN_blur_UnlockTickRegistry();
    *waitTicksOut = waitTicks;
    return hasDeadline;
}

PLUGIN_CODE(blur) void PLUGIN_blur_SetMenuFreezeInternal(bool freeze)
{
    // don't make callbacks catch up after closing Rosalina
    if (freeze)
        menuFreeze = true;
    else
    {
        PLUGIN_blur_ResetTickCadences(BLUR_HOST__svcGetSystemTick());
        menuFreeze = false;
    }

    PLUGIN_blur_WakeTickWorker();
}

PLUGIN_CODE(blur) void PLUGIN_blur_CoolThreadMain(void)
{
    Handle wakeEvent;

    // Blur loads before Rosalina has finished setting itself up
    while (!BLUR_HOST__preTerminationRequested && !BLUR_HOST__preTerminationEvent)
        BLUR_HOST__svcSleepThread(10 * 1000 * 1000LL);

    if (BLUR_HOST__preTerminationRequested)
        goto cleanup;

    while (!BLUR_HOST__preTerminationRequested &&
        (!BLUR_HOST__isServiceUsable(service_acu) ||
            !BLUR_HOST__isServiceUsable(service_hid) ||
            !BLUR_HOST__isServiceUsable(service_gpu) ||
            !BLUR_HOST__isServiceUsable(service_lcd) ||
            !BLUR_HOST__isServiceUsable(service_cdc)))
    {
        BLUR_HOST__svcSleepThread(250 * 1000 * 1000LL);
    }

    if (BLUR_HOST__preTerminationRequested)
        goto cleanup;

    BLUR_HOST__svcSleepThread(1 * 1000 * 1000 * 1000LL);

    while (!BLUR_HOST__preTerminationRequested)
    {
        u64 tickNow = BLUR_HOST__svcGetSystemTick();

        if (menuFreeze)
        {
            PLUGIN_blur_WaitForTickWorker(false, 0);
            continue;
        }

        PLUGIN_blur_RunDueTickCallbacks();

        tickNow = BLUR_HOST__svcGetSystemTick();
        u64 waitTicks;
        bool hasDeadline = PLUGIN_blur_GetNextTickWait(
            tickNow,
            &waitTicks
        );

        PLUGIN_blur_WaitForTickWorker(hasDeadline, waitTicks);
    }

cleanup:
    wakeEvent = blurTickWakeEvent;
    blurTickWakeEvent = 0;

    if (wakeEvent)
        BLUR_HOST__svcCloseHandle(wakeEvent);
}

PLUGIN_CODE(blur) bool PLUGIN_blur_EnsureCoolThread(void)
{
    if (!blurCoolThreadCreated)
    {
        Handle wakeEvent = 0;

        if (R_FAILED(BLUR_HOST__svcCreateEvent(&wakeEvent, RESET_ONESHOT)))
            return false;

        blurTickWakeEvent = wakeEvent;

        if (R_FAILED(BLUR_HOST__MyThread_Create(&plgThread, PLUGIN_blur_CoolThreadMain, plgThreadStack, sizeof(plgThreadStack), 63, CORE_SYSTEM)))
        {
            blurTickWakeEvent = 0;
            BLUR_HOST__svcCloseHandle(wakeEvent);
            return false;
        }

        blurCoolThreadCreated = true;
    }

    return true;
}
