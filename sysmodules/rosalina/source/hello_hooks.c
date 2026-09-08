#include <3ds.h>
#include "csvc.h"

#define PLUGIN_CODE(id) __attribute__((section(".plugin_" #id), used))
#define PLUGIN_BSS(id)  __attribute__((section(".pluginbss_" #id), used))

#define HELLO_PROCESSLIST_WORD0 0xE92D4FF0u
#define HELLO_PROCESSLIST_WORD1 0xE3A05000u
#define HELLO_LITERAL_JUMP      0xE51FF004u

extern void *pluginTable_helo[];
extern void PLUGIN_helo_OpenHijackedPage(void);

#define HELLO_MENU__MapPage ((bool (*)(Handle, u32, u32 *, u32 *))pluginTable_helo[10])
#define HELLO_MENU__UnmapPage ((void (*)(u32))pluginTable_helo[11])
#define HELLO_HOST__svcFlushEntireDataCache ((void (*)(void))pluginTable_helo[12])
#define HELLO_HOST__svcInvalidateEntireInstructionCache ((void (*)(void))pluginTable_helo[13])
#define HELLO_HOST__RosalinaMenu_ProcessList ((u32)pluginTable_helo[14])

PLUGIN_BSS(helo) static u32 g_helloHookOriginal0;
PLUGIN_BSS(helo) static u32 g_helloHookOriginal1;
PLUGIN_BSS(helo) static bool g_helloHookInstalled;

PLUGIN_CODE(helo) static void PLUGIN_helo_SyncCode(void)
{
    HELLO_HOST__svcFlushEntireDataCache();
    HELLO_HOST__svcInvalidateEntireInstructionCache();
}

PLUGIN_CODE(helo) bool PLUGIN_helo_InstallHook(void)
{
    u32 target = HELLO_HOST__RosalinaMenu_ProcessList;
    u32 mappedBase = 0;
    u32 mappedAddress = 0;
    u32 word0;
    u32 word1;

    if (!target || (target & 3u) || (target & 0xFFFu) > 0xFF8u)
        return false;

    if (!HELLO_MENU__MapPage(CUR_PROCESS_HANDLE, target, &mappedBase, &mappedAddress))
        return false;

    word0 = *(volatile u32 *)mappedAddress;
    word1 = *(volatile u32 *)(mappedAddress + 4u);

    // Fail cleanly if Nexus changed the Process list entry code.
    if (word0 != HELLO_PROCESSLIST_WORD0 || word1 != HELLO_PROCESSLIST_WORD1)
    {
        HELLO_MENU__UnmapPage(mappedBase);
        return false;
    }

    g_helloHookOriginal0 = word0;
    g_helloHookOriginal1 = word1;

    // Put the target down first, then publish the jump.
    *(volatile u32 *)(mappedAddress + 4u) = (u32)PLUGIN_helo_OpenHijackedPage;
    *(volatile u32 *)mappedAddress = HELLO_LITERAL_JUMP;

    HELLO_MENU__UnmapPage(mappedBase);
    PLUGIN_helo_SyncCode();
    g_helloHookInstalled = true;
    return true;
}

PLUGIN_CODE(helo) bool PLUGIN_helo_UninstallHook(void)
{
    u32 target = HELLO_HOST__RosalinaMenu_ProcessList;
    u32 mappedBase = 0;
    u32 mappedAddress = 0;
    u32 word0;
    u32 word1;

    if (!g_helloHookInstalled)
        return true;

    if (!HELLO_MENU__MapPage(CUR_PROCESS_HANDLE, target, &mappedBase, &mappedAddress))
        return false;

    word0 = *(volatile u32 *)mappedAddress;
    word1 = *(volatile u32 *)(mappedAddress + 4u);

    if (word0 == g_helloHookOriginal0 && word1 == g_helloHookOriginal1)
    {
        HELLO_MENU__UnmapPage(mappedBase);
        g_helloHookInstalled = false;
        return true;
    }

    if (word0 != HELLO_LITERAL_JUMP || word1 != (u32)PLUGIN_helo_OpenHijackedPage)
    {
        HELLO_MENU__UnmapPage(mappedBase);
        return false;
    }

    *(volatile u32 *)(mappedAddress + 4u) = g_helloHookOriginal1;
    *(volatile u32 *)mappedAddress = g_helloHookOriginal0;

    HELLO_MENU__UnmapPage(mappedBase);
    PLUGIN_helo_SyncCode();
    g_helloHookInstalled = false;
    return true;
}
