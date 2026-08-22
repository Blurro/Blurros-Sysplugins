#include <3ds.h>
#include "memory.h"
#include "draw.h"

#define PLUGIN_CODE(id) __attribute__((section(".plugin_" #id), used))
#define PLUGIN_BSS(id)  __attribute__((section(".pluginbss_" #id), used))

#define MENU_SCRATCH_LOW  0x10000000u
#define MENU_SCRATCH_HIGH 0x14000000u
#define MENU_ENTRY_COLOR  RGB565(9, 34, 31)

extern void *pluginTable_MENU[];
extern const char g_MENUEntryTitle[];
extern const char g_MENUUnreadText[];
extern bool g_MENUUnread;
extern bool g_MENUInsideMenu;

#define MENU_HOST__Draw_DrawString \
    ((u32(*)(u32,u32,u32,const char*))pluginTable_MENU[15])
#define MENU_HOST__svcMapProcessMemoryEx \
    ((Result(*)(Handle,u32,Handle,u32,u32,MapExFlags))pluginTable_MENU[22])
#define MENU_HOST__svcUnmapProcessMemoryEx \
    ((Result(*)(Handle,u32,u32))pluginTable_MENU[23])
#define MENU_HOST__svcQueryMemory \
    ((Result(*)(MemInfo*,PageInfo*,u32))pluginTable_MENU[24])
#define MENU_HOST__svcFlushEntireDataCache \
    ((void(*)(void))pluginTable_MENU[25])
#define MENU_HOST__svcInvalidateEntireInstructionCache \
    ((void(*)(void))pluginTable_MENU[26])

PLUGIN_BSS(MENU) static u32 g_MENUDrawStringContinue;

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase)
{
    MemInfo memoryInfo;
    PageInfo pageInfo;
    u32 scan = MENU_SCRATCH_LOW;

    size = (size + 0xFFFu) & ~0xFFFu;

    while (scan < MENU_SCRATCH_HIGH)
    {
        if (R_FAILED(MENU_HOST__svcQueryMemory(&memoryInfo, &pageInfo, scan)))
            return false;

        u32 regionEnd = memoryInfo.base_addr + memoryInfo.size;
        if (regionEnd <= scan)
            return false;

        if (memoryInfo.state == MEMSTATE_FREE)
        {
            u32 base = (memoryInfo.base_addr + 0xFFFu) & ~0xFFFu;
            if (base < MENU_SCRATCH_LOW)
                base = MENU_SCRATCH_LOW;

            if (base < MENU_SCRATCH_HIGH &&
                size <= MENU_SCRATCH_HIGH - base &&
                size <= regionEnd - base)
            {
                *outBase = base;
                return true;
            }
        }

        scan = regionEnd;
    }

    return false;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_MapPage(
    u32 sourceAddress,
    u32 *mappedBase,
    u32 *mappedAddress
)
{
    u32 base;
    u32 page = sourceAddress & ~0xFFFu;

    if (!mappedBase || !mappedAddress || !PLUGIN_MENU_FindFreeRange(0x1000, &base))
        return false;

    if (R_FAILED(MENU_HOST__svcMapProcessMemoryEx(
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

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnmapPage(u32 mappedBase)
{
    if (mappedBase)
        MENU_HOST__svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, mappedBase, 0x1000);
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_StringLength(const char *text)
{
    const volatile char *p = text;
    u32 length = 0;

    while (p[length])
        length++;

    return length;
}

PLUGIN_CODE(MENU) __attribute__((naked, noinline)) static u32 PLUGIN_MENU_OriginalDrawString(
    u32 posX __attribute__((unused)),
    u32 posY __attribute__((unused)),
    u32 color __attribute__((unused)),
    const char *text __attribute__((unused))
)
{
    __asm__ volatile(
        "push {r4, r5, r6, r7, r8, r9, r10, r11, lr}\n"
        "mov r9, r0\n"
        "ldr r12, 1f\n"
        "ldr r12, [r12]\n"
        "bx r12\n"
        "1:\n"
        ".word g_MENUDrawStringContinue\n"
    );
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_DrawStringHook(
    u32 posX,
    u32 posY,
    u32 color,
    const char *text
)
{
    bool isEntry = !g_MENUInsideMenu && text == g_MENUEntryTitle;

    if (isEntry && color == COLOR_WHITE)
        color = MENU_ENTRY_COLOR;

    u32 result = PLUGIN_MENU_OriginalDrawString(posX, posY, color, text);

    if (isEntry && g_MENUUnread)
    {
        PLUGIN_MENU_OriginalDrawString(
            posX + (PLUGIN_MENU_StringLength(text) + 1u) * SPACING_X,
            posY,
            COLOR_RED,
            g_MENUUnreadText
        );
    }

    return result;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_InstallDrawStringHook(void)
{
    u32 address = (u32)MENU_HOST__Draw_DrawString;
    u32 mappedBase = 0;
    u32 mappedAddress = 0;

    if ((address & 0xFFFu) > 0xFF8u ||
        !PLUGIN_MENU_MapPage(address, &mappedBase, &mappedAddress))
    {
        return false;
    }

    u32 instruction0 = *(volatile u32*)mappedAddress;
    u32 instruction1 = *(volatile u32*)(mappedAddress + 4u);

    if (instruction0 != 0xE92D4FF0u || instruction1 != 0xE1A09000u)
    {
        PLUGIN_MENU_UnmapPage(mappedBase);
        return false;
    }

    g_MENUDrawStringContinue = address + 8u;
    *(volatile u32*)(mappedAddress + 4u) = (u32)PLUGIN_MENU_DrawStringHook;
    *(volatile u32*)mappedAddress = 0xE51FF004u;
    PLUGIN_MENU_UnmapPage(mappedBase);

    MENU_HOST__svcFlushEntireDataCache();
    MENU_HOST__svcInvalidateEntireInstructionCache();
    return true;
}