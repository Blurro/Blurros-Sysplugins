#include <3ds.h>
#include "csvc.h"

#define PLUGIN_CODE(id) __attribute__((section(".plugin_" #id), used))
#define PLUGIN_DATA(id) __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)  __attribute__((section(".pluginbss_" #id), used))

#define POWR_SCRATCH_LOW  0x10000000u
#define POWR_SCRATCH_HIGH 0x14000000u

extern void *pluginTable_powr[];
extern u32 powerprevent_scan_held_addr;
extern u32 PLUGIN_powr_ScanAndUpdate(void);

#define POWR_HOST__marker                    ((u32)pluginTable_powr[0])
#define POWR_HOST__menuCombo                 ((u32)pluginTable_powr[1])
#define POWR_HOST__svcMapProcessMemoryEx     ((Result (*)(Handle, u32, Handle, u32, u32, MapExFlags))pluginTable_powr[6])
#define POWR_HOST__svcUnmapProcessMemoryEx   ((Result (*)(Handle, u32, u32))pluginTable_powr[7])
#define POWR_MENU__FindFreeRange             ((bool (*)(u32, u32 *))pluginTable_powr[8])
#define POWR_HOST__svcFlushEntireDataCache   ((void (*)(void))pluginTable_powr[9])
#define POWR_HOST__svcInvalidateEntireInstructionCache ((void (*)(void))pluginTable_powr[10])

PLUGIN_BSS(powr) u32 powerprevent_menu_combo_addr;
PLUGIN_BSS(powr) u32 powerprevent_hook_return_addr;


PLUGIN_CODE(powr) static bool PLUGIN_powr_MapPage(u32 sourceAddress, u32 *mappedBase, u32 *mappedAddress)
{
    u32 base;
    u32 page = sourceAddress & ~0xFFFu;

    if (!mappedBase || !mappedAddress || !POWR_MENU__FindFreeRange(0x1000u, &base))
        return false;

    if (R_FAILED(POWR_HOST__svcMapProcessMemoryEx(
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

PLUGIN_CODE(powr) static void PLUGIN_powr_UnmapPage(u32 mappedBase)
{
    if (mappedBase)
        POWR_HOST__svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, mappedBase, 0x1000);
}

PLUGIN_CODE(powr) static bool PLUGIN_powr_ReadHostWord(u32 address, u32 *value)
{
    u32 mappedBase;
    u32 mappedAddress;

    if (!value || !PLUGIN_powr_MapPage(address, &mappedBase, &mappedAddress))
        return false;

    *value = *(volatile u32 *)mappedAddress;
    PLUGIN_powr_UnmapPage(mappedBase);
    return true;
}

PLUGIN_CODE(powr) static bool PLUGIN_powr_DecodeArmBl(u32 instr, u32 address, u32 *target)
{
    if (!target || (instr & 0xFF000000u) != 0xEB000000u)
        return false;

    s32 imm24 = (s32)(instr << 8) >> 8;
    *target = (u32)((s32)(address + 8u) + imm24 * 4);
    return true;
}

PLUGIN_CODE(powr) static bool PLUGIN_powr_ReadPcLdrR3(u32 instr, u32 address, u32 *value)
{
    if (!value ||
        (instr >> 28) != 0xEu ||
        (instr & 0x0F7F0000u) != 0x051F0000u ||
        ((instr >> 12) & 0xFu) != 3u)
    {
        return false;
    }

    u32 offset = instr & 0xFFFu;
    u32 literal = (instr & (1u << 23)) ?
        address + 8u + offset : address + 8u - offset;
    return PLUGIN_powr_ReadHostWord(literal, value);
}

PLUGIN_CODE(powr) __attribute__((naked)) static void PLUGIN_powr_KeyScanHook(void)
{
    __asm__ volatile(
        "push {r12, lr}\n"
        "bl PLUGIN_powr_ScanAndUpdate\n"
        "pop {r12, lr}\n"
        "ldr r3, 1f\n"
        "ldr r3, [r3]\n"
        "ldr r12, 2f\n"
        "ldr r12, [r12]\n"
        "bx r12\n"
        "1:\n"
        ".word powerprevent_menu_combo_addr\n"
        "2:\n"
        ".word powerprevent_hook_return_addr\n"
    );
}

PLUGIN_CODE(powr) static void PLUGIN_powr_SyncExecutableChanges(void)
{
    POWR_HOST__svcFlushEntireDataCache();
    POWR_HOST__svcInvalidateEntireInstructionCache();
}

PLUGIN_CODE(powr) bool PLUGIN_powr_InstallHook(void)
{
    u32 marker = POWR_HOST__marker;
    u32 mappedBase = 0;
    u32 mappedAddress = 0;
    u32 instr0;
    u32 instr1;
    u32 instr2;
    u32 instr3;
    u32 scanTarget;
    u32 menuComboTarget;
    u32 resultRegister;
    bool nexusShape;
    bool lumaShape;

    if (!marker || (marker & 3u) != 0 || (marker & 0xFFFu) > 0xFF0u)
        return false;

    if (!PLUGIN_powr_MapPage(marker, &mappedBase, &mappedAddress))
        return false;

    instr0 = *(volatile u32 *)mappedAddress;
    instr1 = *(volatile u32 *)(mappedAddress + 4u);
    instr2 = *(volatile u32 *)(mappedAddress + 8u);
    instr3 = *(volatile u32 *)(mappedAddress + 12u);

    resultRegister = (instr2 >> 12) & 0xFu;
    nexusShape =
        (instr2 & 0xFFFF0FFFu) == 0xE1A00000u &&
        resultRegister >= 4u && resultRegister <= 11u &&
        instr3 == 0xE5933000u;
    lumaShape = instr2 == 0xE5933000u;

    if (!PLUGIN_powr_DecodeArmBl(instr0, marker, &scanTarget) ||
        !PLUGIN_powr_ReadPcLdrR3(instr1, marker + 4u, &menuComboTarget) ||
        menuComboTarget != POWR_HOST__menuCombo ||
        (!nexusShape && !lumaShape))
    {
        PLUGIN_powr_UnmapPage(mappedBase);
        return false;
    }

    powerprevent_scan_held_addr = scanTarget;
    powerprevent_menu_combo_addr = POWR_HOST__menuCombo;
    powerprevent_hook_return_addr = marker + 8u;

    *(volatile u32 *)(mappedAddress + 4u) = (u32)PLUGIN_powr_KeyScanHook;
    *(volatile u32 *)mappedAddress = 0xE51FF004u;
    PLUGIN_powr_UnmapPage(mappedBase);
    PLUGIN_powr_SyncExecutableChanges();
    return true;
}
