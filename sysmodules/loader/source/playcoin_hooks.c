#include <3ds.h>
#include "memory.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))

#define COIN_SCRATCH_LOW  0x10000000u
#define COIN_SCRATCH_HIGH 0x1F000000u
#define NOP 0xE1A00000u

extern void *pluginTable_coin[];
#define COIN_HOST__loaderHomePatch ((u32)pluginTable_coin[6])

extern void PLUGIN_coin_ClearTransientHomePointer(void);
extern bool PLUGIN_coin_InitializeHomeMenuState(u16 *coinDat, u32 *coinData, u32 homePointer);
extern void PLUGIN_coin_PatchHomeMenu(u8 *code, u32 textSize);

extern u16 PLUGIN_coin_dat;
extern u32 PLUGIN_coin_bin[4];
extern u16 PLUGIN_coin_change[3];
extern u32 PLUGIN_coin_homePtr;
extern u32 PLUGIN_coin_homeUIReturn;
extern u32 PLUGIN_coin_loaderReturn;
extern void PLUGIN_coin_LoaderPatchCodeHook(void);
extern void PLUGIN_coin_homeLoaderPatch(void);
extern void PLUGIN_coin_homeLoaderUIHook(void);
extern void PLUGIN_coin_preCoinHook(void);
extern void PLUGIN_coin_historyDiagHook(void);
extern void PLUGIN_coin_costPlus3Hook(void);

extern Result PLUGIN_coin_svcQueryMemory(MemInfo *memInfo, PageInfo *pageInfo, u32 addr);
extern Result PLUGIN_coin_svcMapProcessMemoryEx(
    Handle dstProcess,
    u32 dstAddr,
    Handle srcProcess,
    u32 srcAddr,
    u32 size,
    u32 flags
);
extern Result PLUGIN_coin_svcUnmapProcessMemoryEx(Handle process, u32 addr, u32 size);
extern u32 PLUGIN_coin_svcConvertVAToPA(const void *va, bool writeCheck);
extern void PLUGIN_coin_svcFlushEntireDataCache(void);
extern void PLUGIN_coin_svcInvalidateEntireInstructionCache(void);

__asm__(
    ".section .plugin_coin,\"ax\",%progbits\n"
    ".balign 4\n"
    ".global PLUGIN_coin_loaderReturn\n"
    "PLUGIN_coin_loaderReturn:\n"
    ".word 0\n"

    ".global PLUGIN_coin_LoaderPatchCodeHook\n"
    ".type PLUGIN_coin_LoaderPatchCodeHook, %function\n"
    "PLUGIN_coin_LoaderPatchCodeHook:\n"
    "ldr r2, =0xE3A00000\n"
    "str r2, [r7, r3]\n"
    "add r3, r7, r3\n"
    "ldr r2, =0xE12FFF1E\n"
    "str r2, [r3, #4]\n"
    "push {r0-r12, lr}\n"
    "mov r0, r7\n"
    "mov r1, r4\n"
    "bl PLUGIN_coin_PatchHomeMenu\n"
    "pop {r0-r12, lr}\n"
    "adr r12, PLUGIN_coin_loaderReturn\n"
    "ldr pc, [r12]\n"

    ".global PLUGIN_coin_svcQueryMemory\n"
    ".type PLUGIN_coin_svcQueryMemory, %function\n"
    "PLUGIN_coin_svcQueryMemory:\n"
    "push {r0, r1, r4, r5, r6}\n"
    "svc 0x02\n"
    "ldr r6, [sp]\n"
    "str r1, [r6]\n"
    "str r2, [r6, #4]\n"
    "str r3, [r6, #8]\n"
    "str r4, [r6, #12]\n"
    "ldr r6, [sp, #4]\n"
    "str r5, [r6]\n"
    "add sp, sp, #8\n"
    "pop {r4, r5, r6}\n"
    "bx lr\n"

    ".global PLUGIN_coin_svcConvertVAToPA\n"
    ".type PLUGIN_coin_svcConvertVAToPA, %function\n"
    "PLUGIN_coin_svcConvertVAToPA:\n"
    "svc 0x90\n"
    "bx lr\n"

    ".global PLUGIN_coin_svcFlushEntireDataCache\n"
    ".type PLUGIN_coin_svcFlushEntireDataCache, %function\n"
    "PLUGIN_coin_svcFlushEntireDataCache:\n"
    "svc 0x92\n"
    "bx lr\n"

    ".global PLUGIN_coin_svcInvalidateEntireInstructionCache\n"
    ".type PLUGIN_coin_svcInvalidateEntireInstructionCache, %function\n"
    "PLUGIN_coin_svcInvalidateEntireInstructionCache:\n"
    "svc 0x94\n"
    "bx lr\n"

    ".global PLUGIN_coin_svcMapProcessMemoryEx\n"
    ".type PLUGIN_coin_svcMapProcessMemoryEx, %function\n"
    "PLUGIN_coin_svcMapProcessMemoryEx:\n"
    "push {r4, r5, r6}\n"
    "ldr r4, [sp, #12]\n"
    "ldr r5, [sp, #16]\n"
    "mov r6, r0\n"
    "mov r0, #0xFFFFFFF2\n"
    "svc 0xA0\n"
    "pop {r4, r5, r6}\n"
    "bx lr\n"

    ".global PLUGIN_coin_svcUnmapProcessMemoryEx\n"
    ".type PLUGIN_coin_svcUnmapProcessMemoryEx, %function\n"
    "PLUGIN_coin_svcUnmapProcessMemoryEx:\n"
    "svc 0xA1\n"
    "bx lr\n"

    ".global PLUGIN_coin_svcSendSyncRequest\n"
    ".type PLUGIN_coin_svcSendSyncRequest, %function\n"
    "PLUGIN_coin_svcSendSyncRequest:\n"
    "svc 0x32\n"
    "bx lr\n"
    ".ltorg\n"
);

PLUGIN_RODATA(coin) static const u32 g_costPlus3Expected[] = {
    0xE3E03018u, 0xE0812091u, 0xE1A012A1u, 0xE0020391u,
    0xE0803102u, 0xE6FC0071u, 0xE6FF2071u, 0xE350000Au,
    0x3A000004u, 0xE26C000Au, 0xE3500000u, 0xD3A00000u,
    0xE3A03000u, 0xE6FF2070u, 0xE1D400B4u,
};

PLUGIN_RODATA(coin) static const u32 g_uncappedPatch[] = {
    NOP, NOP, NOP, NOP, NOP, NOP, NOP,
    0xE1D400B4u, 0xEA000006u, 0xE3E00000u, 0xE1B00820u,
    0xE1510000u, 0x81A01000u, 0xE1C410B4u, NOP, 0xEA000054u,
};
PLUGIN_RODATA(coin) static const u32 g_coinPatch330[] = {0xE5C40012u, 0xEAFFFFA3u};
PLUGIN_RODATA(coin) static const u32 g_coinPatch340[] = {NOP, NOP};
PLUGIN_RODATA(coin) static const u32 g_badgeReturnPatch[] = {0xE3A00000u, 0xE8BD8070u};
PLUGIN_RODATA(coin) static const u32 g_badgeBranchPatch[] = {0xEA000006u};

PLUGIN_CODE(coin) static u32 PLUGIN_coin_Phys(const void *ptr)
{
    return PLUGIN_coin_svcConvertVAToPA(ptr, false) | 0x80000000u;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_FindFreePage(u32 *out)
{
    MemInfo memInfo;
    PageInfo pageInfo;
    u32 scan = COIN_SCRATCH_LOW;

    while (scan < COIN_SCRATCH_HIGH)
    {
        if (R_FAILED(PLUGIN_coin_svcQueryMemory(&memInfo, &pageInfo, scan)))
            return false;

        u32 end = memInfo.base_addr + memInfo.size;
        if (end <= scan)
            return false;

        if (memInfo.state == MEMSTATE_FREE)
        {
            u32 base = (memInfo.base_addr + 0xFFFu) & ~0xFFFu;
            if (base < COIN_SCRATCH_LOW)
                base = COIN_SCRATCH_LOW;

            if (base < end && base + 0x1000u <= end && base + 0x1000u <= COIN_SCRATCH_HIGH)
            {
                *out = base;
                return true;
            }
        }

        scan = end;
    }

    return false;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_MapOwnPage(u32 source, u32 *mapBase, u32 *mapped)
{
    u32 destination;
    if (!PLUGIN_coin_FindFreePage(&destination))
        return false;

    if (R_FAILED(PLUGIN_coin_svcMapProcessMemoryEx(
        CUR_PROCESS_HANDLE,
        destination,
        CUR_PROCESS_HANDLE,
        source & ~0xFFFu,
        0x1000,
        0
    )))
    {
        return false;
    }

    *mapBase = destination;
    *mapped = destination + (source & 0xFFFu);
    return true;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_UnmapOwnPage(u32 mapBase)
{
    if (mapBase)
        PLUGIN_coin_svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, mapBase, 0x1000);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_PatchWords(
    u8 *code,
    u32 address,
    const u32 *words,
    u32 count
)
{
    u32 *destination = (u32*)(code + address - 0x100000u);
    for (u32 i = 0; i < count; i++)
        destination[i] = words[i];
}

PLUGIN_CODE(coin) void PLUGIN_coin_PatchHomeMenu(u8 *code, u32 textSize)
{
    if (textSize < 0x100u)
        return;

    PLUGIN_coin_ClearTransientHomePointer();

    // coin patches
    u32 coinCalcOffset = 0;
    u32 coinUIOffset = 0;
    u32 preserveBadgeOffset = 0;
    u32 endOffset = textSize - 0x100u;

    for (u32 offset = 0; offset < endOffset; offset += 4)
    {
        u32 *scan = (u32*)(code + offset);

        // sig 1
        if (!coinCalcOffset &&
            scan[0] == 0xE1D431D3u &&
            scan[1] == 0xE1D421D2u &&
            scan[2] == 0xE1D411F0u &&
            scan[3] == 0xE28D0018u)
        {
            coinCalcOffset = offset + 0x100000u;
        }

        // sig 2
        if (!coinUIOffset &&
            (scan[0] & 0xFFFF00F0u) == 0xE1DD00B0u &&
            (scan[1] & 0xFFFF0000u) == 0xE28F0000u &&
            scan[2] == 0xE3A01010u)
        {
            coinUIOffset = offset + 0x100000u;
        }

        // badge sig
        if (!preserveBadgeOffset &&
            scan[0] == 0xE5951024u &&
            scan[1] == 0xE591101Cu &&
            scan[2] == 0xE1500001u)
        {
            preserveBadgeOffset = offset + 0x100000u;
        }

        if (coinCalcOffset && coinUIOffset && preserveBadgeOffset)
            break;
    }

    if (!coinCalcOffset || !coinUIOffset) // if badge scan fails assume old ver and still apply coin patches
        return;

    u32 coinCalcRaw = coinCalcOffset - 0x100000u;
    u32 coinUIRaw = coinUIOffset - 0x100000u;
    if (coinCalcRaw > textSize || textSize - coinCalcRaw < 0x350u ||
        coinUIRaw > textSize || textSize - coinUIRaw < 0x10u)
    {
        return;
    }

    u32 *coinCalc = (u32*)(code + coinCalcRaw);
    if (coinCalc[0x168u / 4u] != 0xE05EA000u ||
        coinCalc[0x16Cu / 4u] != 0xE3A03000u)
    {
        return;
    }

    for (u32 i = 0; i < sizeof(g_costPlus3Expected) / sizeof(g_costPlus3Expected[0]); i++)
    {
        if (coinCalc[0x188u / 4u + i] != g_costPlus3Expected[i])
            return;
    }

    if (preserveBadgeOffset)
    {
        u32 preserveBadgeRaw = preserveBadgeOffset - 0x100000u;
        if (preserveBadgeRaw < 0x18u || preserveBadgeRaw > textSize ||
            textSize - preserveBadgeRaw < 0x38u)
        {
            preserveBadgeOffset = 0;
        }
    }

    u32 stateMapBase;
    u32 stateMapped;
    if (!PLUGIN_coin_MapOwnPage((u32)&PLUGIN_coin_dat, &stateMapBase, &stateMapped))
        return;

    u32 stateDelta = stateMapped - (u32)&PLUGIN_coin_dat;
    *(u32*)((u32)&PLUGIN_coin_homeUIReturn + stateDelta) = coinUIOffset + 0x10u;
    *(u32*)((u32)&PLUGIN_coin_homePtr + stateDelta) = coinCalcOffset + 0x3Cu; // 0x1642BC

    u16 *mappedDat = (u16*)((u32)&PLUGIN_coin_dat + stateDelta);
    u32 *mappedBin = (u32*)((u32)PLUGIN_coin_bin + stateDelta);
    u32 homePointer = *(u32*)((u32)&PLUGIN_coin_homePtr + stateDelta);
    if (!PLUGIN_coin_InitializeHomeMenuState(mappedDat, mappedBin, homePointer))
    {
        PLUGIN_coin_UnmapOwnPage(stateMapBase);
        return;
    }

    u32 patch[2] = {0xE51FF004u, PLUGIN_coin_Phys(PLUGIN_coin_homeLoaderPatch)};
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x348u, patch, 2); // must pass count of instructions

    patch[1] = PLUGIN_coin_Phys(PLUGIN_coin_homeLoaderUIHook);
    PLUGIN_coin_PatchWords(code, coinUIOffset + 0x8u, patch, 2); // 0x1EEF10

    u32 pointerBlock[13] = {
        0xEA000005u, // jump past stuff
        PLUGIN_coin_Phys(&PLUGIN_coin_dat), // create pointers for rosalina swap
        PLUGIN_coin_Phys(PLUGIN_coin_bin),
        PLUGIN_coin_Phys(PLUGIN_coin_change),
        0x0061004Du, // 'M','a'
        0x00650078u, // 'x','e'
        0x00000064u, // 'd','\0'
        0xE51FF004u, // above jump lands here
        PLUGIN_coin_Phys(PLUGIN_coin_preCoinHook),
        // credit to zeroskill for following patches
        NOP,
        NOP,
        NOP,
        0x1A00001Au, // bne to instr 0x164358 (ldr r0, [sp, #0xc])
    };

    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x38u, pointerBlock, 13); // 0x1642B8

    u32 historyDiagPatch[2] = {0xE51FF004u, PLUGIN_coin_Phys(PLUGIN_coin_historyDiagHook)};
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x168u, historyDiagPatch, 2);

    u32 costPlus3Patch[2] = {0xE51FF004u, PLUGIN_coin_Phys(PLUGIN_coin_costPlus3Hook)};
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x188u, costPlus3Patch, 2);

    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x1A4u, g_uncappedPatch, 16); // 0x164424
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x330u, g_coinPatch330, 2); // 0x1645B0
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x340u, g_coinPatch340, 2); // 0x1645C0

    // badges patch (for pretendo toggling) by zeroskill
    if (preserveBadgeOffset)
    {
        PLUGIN_coin_PatchWords(code, preserveBadgeOffset - 0x18u, g_badgeReturnPatch, 2);
        PLUGIN_coin_PatchWords(code, preserveBadgeOffset + 0x34u, g_badgeBranchPatch, 1);
    }

    PLUGIN_coin_svcFlushEntireDataCache();
    PLUGIN_coin_UnmapOwnPage(stateMapBase);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_InstallLoaderHook(void)
{
    u32 marker = COIN_HOST__loaderHomePatch;
    u32 returnMapBase;
    u32 returnMapped;

    if (!PLUGIN_coin_MapOwnPage((u32)&PLUGIN_coin_loaderReturn, &returnMapBase, &returnMapped))
        return false;

    *(u32*)returnMapped = marker + 0x2Cu;
    PLUGIN_coin_UnmapOwnPage(returnMapBase);

    u32 hostMapBase;
    u32 hostAddress;
    if (!PLUGIN_coin_MapOwnPage(marker, &hostMapBase, &hostAddress))
        return false;

    volatile u32 *host = (volatile u32*)hostAddress;
    if ((host[0] & 0xFFFFF000u) != 0xE59F2000u ||
        host[1] != 0xE7872003u)
    {
        PLUGIN_coin_UnmapOwnPage(hostMapBase);
        return false;
    }

    host[1] = (u32)PLUGIN_coin_LoaderPatchCodeHook;
    host[0] = 0xE51FF004u;

    PLUGIN_coin_svcFlushEntireDataCache();
    PLUGIN_coin_svcInvalidateEntireInstructionCache();
    PLUGIN_coin_UnmapOwnPage(hostMapBase);
    return true;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_InstallHooks(void)
{
    return PLUGIN_coin_InstallLoaderHook();
}