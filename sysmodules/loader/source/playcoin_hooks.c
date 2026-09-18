#include <3ds.h>
#include "memory.h"
#include "sysplugin_menu.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))

#define COIN_SCRATCH_LOW  0x10000000u
#define COIN_SCRATCH_HIGH 0x1F000000u
#define NOP 0xE1A00000u

extern void *pluginTable_coin[];
#define COIN_HOST__FSUSER_OpenArchive       ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))pluginTable_coin[0])
#define COIN_HOST__FSUSER_CloseArchive      ((Result(*)(FS_Archive))pluginTable_coin[1])
#define COIN_HOST__FSUSER_OpenFile          ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))pluginTable_coin[2])
#define COIN_HOST__FSFILE_Read              ((Result(*)(Handle,u32*,u64,void*,u32))pluginTable_coin[3])
#define COIN_HOST__FSFILE_Close             ((Result(*)(Handle))pluginTable_coin[4])
#define COIN_HOST__fsMakePath               ((FS_Path(*)(FS_PathType,const void*))pluginTable_coin[5])

#define COIN_NEWSLIST_ICON_PACK_SIZE 0x4800u
#define COIN_ASSET_VERSION_MAGIC 0x56584E33u
#define COIN_NEWSLIST_ICON_ALLOC_SIZE 0x5000u
#define COIN_NEWSLIST_ICON_PAGES 5u

extern void PLUGIN_coin_ClearTransientHomePointer(void);
extern bool PLUGIN_coin_InitializeHomeMenuState(u16 *coinDat, u32 *coinData, u16 *coinChange, u32 homePointer);
extern void PLUGIN_coin_PatchHomeMenu(u8 *code, u32 textSize);
extern bool PLUGIN_coin_AchievementsEnabledForLoader(void);

extern u16 PLUGIN_coin_dat;
extern u32 PLUGIN_coin_bin[4];
extern u16 PLUGIN_coin_change[4];
extern u32 PLUGIN_coin_homePtr;
extern u32 PLUGIN_coin_handoffControl;
extern u32 PLUGIN_coin_homeUIReturn;
extern void PLUGIN_coin_homeLoaderPatch(void);
extern void PLUGIN_coin_homeLoaderUIHook(void);
extern void PLUGIN_coin_preCoinHook(void);
extern void PLUGIN_coin_rtcDayGateHook(void);
extern void PLUGIN_coin_historyDiagHook(void);
extern void PLUGIN_coin_costPlus3Hook(void);
extern void PLUGIN_coin_NewslistIconHook(void);
extern u32 PLUGIN_coin_newslistIconBaseWord;
extern u32 PLUGIN_coin_newslistIconOriginalWord;
extern u32 PLUGIN_coin_newslistIconResumeWord;
extern void PLUGIN_coin_HomeNewsIconHook(void);
extern u32 PLUGIN_coin_homeNewsIconOriginalWord;
extern u32 PLUGIN_coin_homeNewsIconResumeWord;

PLUGIN_DATA(coin) static u32 g_coinNewslistIconBase;

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
extern Result PLUGIN_coin_svcControlMemoryUnsafe(u32 *out, u32 addr, u32 size, MemOp op, MemPerm perm);
extern u32 PLUGIN_coin_svcConvertVAToPA(const void *va, bool writeCheck);
extern void PLUGIN_coin_svcFlushEntireDataCache(void);
extern void PLUGIN_coin_svcInvalidateEntireInstructionCache(void);

__asm__(
    ".section .plugin_coin,\"ax\",%progbits\n"
    ".balign 4\n"
    ".global PLUGIN_coin_homeNewsIconOriginalWord\n"
    "PLUGIN_coin_homeNewsIconOriginalWord:\n"
    ".word 0\n"
    ".global PLUGIN_coin_homeNewsIconResumeWord\n"
    "PLUGIN_coin_homeNewsIconResumeWord:\n"
    ".word 0\n"
    "PLUGIN_coin_homeNewsCoinLowWord:\n"
    ".word 0x6E696F63\n"
    "PLUGIN_coin_homeNewsCoinHighBaseWord:\n"
    ".word 0x00040030\n"

    ".global PLUGIN_coin_HomeNewsIconHook\n"
    ".type PLUGIN_coin_HomeNewsIconHook, %function\n"
    "PLUGIN_coin_HomeNewsIconHook:\n"
    // Home Menu uses the same notification icon ABI as the NewsList applet
    "push {r0, r2}\n"
    "ldr r12, [r1]\n"
    "adr lr, PLUGIN_coin_homeNewsCoinLowWord\n"
    "ldr lr, [lr]\n"
    "cmp r12, lr\n"
    "bne 2f\n"
    "ldr r12, [r1, #4]\n"
    "adr lr, PLUGIN_coin_homeNewsCoinHighBaseWord\n"
    "ldr lr, [lr]\n"
    "subs r12, r12, lr\n"
    "cmp r12, #3\n"
    "bhi 2f\n"
    "ldr r2, [sp, #8]\n"
    "mov r0, #0x1200\n"
    "cmp r2, r0\n"
    "blo 2f\n"
    // Home Menu uses one baked icon for every Coin PID
    "adr r1, PLUGIN_coin_homeNewsIcon\n"
    "mov r2, #0x1200\n"
    "mov lr, r2\n"
    "1:\n"
    "ldr r12, [r1], #4\n"
    "str r12, [r3], #4\n"
    "subs r2, r2, #4\n"
    "bne 1b\n"
    "ldr r1, [sp, #12]\n"
    "cmp r1, #0\n"
    "strne lr, [r1]\n"
    "add sp, sp, #8\n"
    "b 3f\n"
    "2:\n"
    // non-Coin stuff stays on Nintendo's resolver
    "pop {r0, r2}\n"
    "adr r12, PLUGIN_coin_homeNewsIconOriginalWord\n"
    "ldr r12, [r12]\n"
    "blx r12\n"
    "3:\n"
    // we replaced the old BL + mov r0,#0x40 pair
    "mov r0, #0x40\n"
    "adr r12, PLUGIN_coin_homeNewsIconResumeWord\n"
    "ldr pc, [r12]\n"

    ".balign 4\n"
    ".global PLUGIN_coin_homeNewsIcon\n"
    "PLUGIN_coin_homeNewsIcon:\n"
    // baked 48x48 Coin icon
    ".include \"../../../coin_home_icon.inc\"\n"
    ".global PLUGIN_coin_homeNewsIconEnd\n"
    "PLUGIN_coin_homeNewsIconEnd:\n"
);

__asm__(
    ".section .plugin_coin,\"ax\",%progbits\n"
    ".balign 4\n"
    // runs through the NewsList applet's K11 alias, dont touch Loader VA data here
    ".global PLUGIN_coin_newslistIconBaseWord\n"
    "PLUGIN_coin_newslistIconBaseWord:\n"
    ".word 0\n"
    ".global PLUGIN_coin_newslistIconOriginalWord\n"
    "PLUGIN_coin_newslistIconOriginalWord:\n"
    ".word 0\n"
    ".global PLUGIN_coin_newslistIconResumeWord\n"
    "PLUGIN_coin_newslistIconResumeWord:\n"
    ".word 0\n"

    ".global PLUGIN_coin_NewslistIconHook\n"
    ".type PLUGIN_coin_NewslistIconHook, %function\n"
    "PLUGIN_coin_NewslistIconHook:\n"
    // always run Nintendo's resolver first, then replace only Coin bitmaps
    "push {r4-r9}\n"
    "ldr r6, [sp, #24]\n"      // original max
    "ldr r7, [sp, #28]\n"      // original outSize
    "mov r4, r1\n"             // processID pointer
    "mov r5, r3\n"             // destination
    // rebuild the resolver's two stack args
    "sub sp, sp, #8\n"
    "str r6, [sp]\n"
    "str r7, [sp, #4]\n"
    "adr r12, PLUGIN_coin_newslistIconOriginalWord\n"
    "ldr r12, [r12]\n"
    "blx r12\n"
    "add sp, sp, #8\n"
    // keep the old BL+MOV flags
    "mrs r9, cpsr\n"
    // only this post-resolver copy is Coin-specific
    "ldr r12, [r4]\n"
    "ldr lr, =0x6E696F63\n"
    "cmp r12, lr\n"
    "bne 2f\n"
    "ldr r12, [r4, #4]\n"
    "ldr lr, =0x00040030\n"
    "subs r12, r12, lr\n"
    "cmp r12, #3\n"
    "bhi 2f\n"
    "adr lr, PLUGIN_coin_newslistIconBaseWord\n"
    "ldr lr, [lr]\n"
    "cmp lr, #0\n"
    "beq 2f\n"
    "mov r8, #0x1200\n"
    "cmp r6, r8\n"
    "blo 2f\n"
    // iconBase + difficulty * 0x1200
    "add r1, lr, r12, lsl #12\n"
    "add r1, r1, r12, lsl #9\n"
    "mov r2, r8\n"
    "mov r3, r5\n"
    "1:\n"
    "ldr lr, [r1], #4\n"
    "str lr, [r3], #4\n"
    "subs r2, r2, #4\n"
    "bne 1b\n"
    "cmp r7, #0\n"
    "strne r8, [r7]\n"
    "2:\n"
    "msr cpsr_f, r9\n"
    "pop {r4-r9}\n"
    // the old mov r0,#0x40 became our literal slot
    "mov r0, #0x40\n"
    "adr r12, PLUGIN_coin_newslistIconResumeWord\n"
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

    ".global PLUGIN_coin_svcControlMemoryUnsafe\n"
    ".type PLUGIN_coin_svcControlMemoryUnsafe, %function\n"
    "PLUGIN_coin_svcControlMemoryUnsafe:\n"
    "str r4, [sp, #-4]!\n"
    "ldr r4, [sp, #4]\n"
    "svc 0xA3\n"
    "ldr r4, [sp], #4\n"
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

PLUGIN_CODE(coin) static bool PLUGIN_coin_FindFreeRange(u32 size, u32 *out)
{
    MemInfo memInfo;
    PageInfo pageInfo;
    u32 scan = COIN_SCRATCH_LOW;

    if (!out || !size || (size & 0xFFFu))
        return false;

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

            if (base < end && size <= end - base && size <= COIN_SCRATCH_HIGH - base)
            {
                *out = base;
                return true;
            }
        }

        scan = end;
    }

    return false;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_AllocAliasGuard(u32 address)
{
    u32 allocated = 0;
    Result result = PLUGIN_coin_svcControlMemoryUnsafe(
        &allocated,
        address,
        0x1000u,
        MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
        MEMPERM_READWRITE
    );
    return R_SUCCEEDED(result) && allocated == address;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_FreeAliasGuard(u32 address)
{
    u32 out;
    (void)PLUGIN_coin_svcControlMemoryUnsafe(
        &out,
        address,
        0x1000u,
        MEMOP_FREE | MEMOP_REGION_SYSTEM,
        MEMPERM_DONTCARE
    );
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_MapGuardedRange(
    Handle sourceProcess,
    u32 sourcePage,
    u32 size,
    u32 *mapBase
)
{
    u32 guardBase;
    u32 aliasBase;
    u32 rightGuard;
    u32 scanSize;

    if (!mapBase || !size || (sourcePage & 0xFFFu) || (size & 0xFFFu) ||
        size > 0xFFFFDFFFu)
    {
        return false;
    }

    scanSize = size + 0x2000u;
    if (!PLUGIN_coin_FindFreeRange(scanSize, &guardBase))
        return false;

    aliasBase = guardBase + 0x1000u;
    rightGuard = aliasBase + size;

    if (!PLUGIN_coin_AllocAliasGuard(guardBase))
        return false;

    if (!PLUGIN_coin_AllocAliasGuard(rightGuard))
    {
        PLUGIN_coin_FreeAliasGuard(guardBase);
        return false;
    }

    if (R_FAILED(PLUGIN_coin_svcMapProcessMemoryEx(
            CUR_PROCESS_HANDLE,
            aliasBase,
            sourceProcess,
            sourcePage,
            size,
            0)))
    {
        PLUGIN_coin_FreeAliasGuard(rightGuard);
        PLUGIN_coin_FreeAliasGuard(guardBase);
        return false;
    }

    *mapBase = aliasBase;
    return true;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_UnmapGuardedRange(u32 mapBase, u32 size)
{
    if (!mapBase || !size || (size & 0xFFFu))
        return;

    if (R_SUCCEEDED(PLUGIN_coin_svcUnmapProcessMemoryEx(
            CUR_PROCESS_HANDLE,
            mapBase,
            size)))
    {
        PLUGIN_coin_FreeAliasGuard(mapBase - 0x1000u);
        PLUGIN_coin_FreeAliasGuard(mapBase + size);
    }
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_MapOwnPage(u32 source, u32 *mapBase, u32 *mapped)
{
    if (!mapped || !PLUGIN_coin_MapGuardedRange(
            CUR_PROCESS_HANDLE,
            source & ~0xFFFu,
            0x1000u,
            mapBase))
    {
        return false;
    }

    *mapped = *mapBase + (source & 0xFFFu);
    return true;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_UnmapOwnPage(u32 mapBase)
{
    PLUGIN_coin_UnmapGuardedRange(mapBase, 0x1000u);
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

PLUGIN_CODE(coin) static bool PLUGIN_coin_WriteOwnCodeWord(u32 address, u32 value)
{
    u32 mapBase = 0;
    u32 mapped = 0;
    if (!PLUGIN_coin_MapOwnPage(address, &mapBase, &mapped))
        return false;

    *(u32*)mapped = value;
    PLUGIN_coin_svcFlushEntireDataCache();
    PLUGIN_coin_UnmapOwnPage(mapBase);
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_DecodeArmBlTarget(
    u32 instructionAddress,
    u32 instruction,
    u32 *target
)
{
    if (!target || (instruction & 0xFF000000u) != 0xEB000000u)
        return false;

    s32 imm24 = (s32)(instruction << 8) >> 8;
    s32 displacement = imm24 * 4;
    *target = (u32)((s32)(instructionAddress + 8u) + displacement);
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_MatchHomeNewsIconCall(
    u8 *code,
    u32 textSize,
    u32 offset,
    u32 *callAddress,
    u32 *originalTarget
)
{
    u32 *scan = (u32*)(code + offset);
    if (scan[0] != 0xE3A01C12u ||
        scan[1] != 0xE1A06003u ||
        scan[2] != 0xE88D0006u ||
        scan[3] != 0xE284301Cu ||
        scan[4] != 0xE1A02006u ||
        scan[5] != 0xE1A0100Cu ||
        (scan[6] & 0xFF000000u) != 0xEB000000u ||
        scan[7] != 0xE3A00040u ||
        scan[8] != 0xE3A01030u)
    {
        return false;
    }

    u32 call = 0x100000u + offset + 6u * sizeof(u32);
    u32 target = 0;
    if (!PLUGIN_coin_DecodeArmBlTarget(call, scan[6], &target) ||
        target < 0x100000u || target >= 0x100000u + textSize)
    {
        return false;
    }

    u32 resolverRaw = target - 0x100000u;
    if (textSize - resolverRaw < 9u * sizeof(u32))
        return false;

    u32 *resolver = (u32*)(code + resolverRaw);
    if (resolver[0] != 0xE92D41F0u ||
        resolver[1] != 0xE24DDD82u ||
        resolver[5] != 0xE1A07003u ||
        resolver[6] != 0xE8900060u ||
        resolver[7] != 0xE5910004u ||
        resolver[8] != 0xE5913000u)
    {
        return false;
    }

    *callAddress = call;
    *originalTarget = target;
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_PatchHomeNewsIcon(
    u8 *code,
    u32 callAddress,
    u32 originalTarget
)
{
    if (!callAddress || !originalTarget)
        return false;

    if (!PLUGIN_coin_WriteOwnCodeWord(
            (u32)&PLUGIN_coin_homeNewsIconOriginalWord,
            originalTarget) ||
        !PLUGIN_coin_WriteOwnCodeWord(
            (u32)&PLUGIN_coin_homeNewsIconResumeWord,
            callAddress + 8u))
    {
        return false;
    }

    u32 patch[2] = {0xE51FF004u, PLUGIN_coin_Phys(PLUGIN_coin_HomeNewsIconHook)};
    PLUGIN_coin_PatchWords(code, callAddress, patch, 2);
    return true;
}

PLUGIN_CODE(coin) void PLUGIN_coin_PatchHomeMenu(u8 *code, u32 textSize)
{
    if (!code || textSize < 0x100u)
    {
        PLUGIN_coin_ClearTransientHomePointer();
        return;
    }

    u32 coinCalcOffset = 0;
    u32 coinUIOffset = 0;
    u32 preserveBadgeOffset = 0;
    u32 newsCallAddress = 0;
    u32 newsOriginalTarget = 0;
    bool newsDuplicate = false;
    bool achievementsEnabled = PLUGIN_coin_AchievementsEnabledForLoader();
    u32 mainEndOffset = textSize - 0x100u;
    u32 scanEndOffset = textSize - 9u * sizeof(u32);

    // one scan finds every Home Menu site we need
    for (u32 offset = 0; offset <= scanEndOffset; offset += sizeof(u32))
    {
        u32 *scan = (u32*)(code + offset);

        if (offset < mainEndOffset)
        {
            if (!coinCalcOffset &&
                scan[0] == 0xE1D431D3u &&
                scan[1] == 0xE1D421D2u &&
                scan[2] == 0xE1D411F0u &&
                scan[3] == 0xE28D0018u)
            {
                coinCalcOffset = offset + 0x100000u;
            }

            if (!coinUIOffset &&
                (scan[0] & 0xFFFF00F0u) == 0xE1DD00B0u &&
                (scan[1] & 0xFFFF0000u) == 0xE28F0000u &&
                scan[2] == 0xE3A01010u)
            {
                coinUIOffset = offset + 0x100000u;
            }

            if (!preserveBadgeOffset &&
                scan[0] == 0xE5951024u &&
                scan[1] == 0xE591101Cu &&
                scan[2] == 0xE1500001u)
            {
                preserveBadgeOffset = offset + 0x100000u;
            }
        }

        if (achievementsEnabled && !newsDuplicate && scan[0] == 0xE3A01C12u)
        {
            u32 candidateCall = 0;
            u32 candidateTarget = 0;
            if (PLUGIN_coin_MatchHomeNewsIconCall(
                    code,
                    textSize,
                    offset,
                    &candidateCall,
                    &candidateTarget))
            {
                if (newsCallAddress)
                {
                    newsCallAddress = 0;
                    newsOriginalTarget = 0;
                    newsDuplicate = true;
                }
                else
                {
                    newsCallAddress = candidateCall;
                    newsOriginalTarget = candidateTarget;
                }
            }
        }
    }

    if (!coinCalcOffset || !coinUIOffset)
    {
        PLUGIN_coin_ClearTransientHomePointer();
        return;
    }

    u32 coinCalcRaw = coinCalcOffset - 0x100000u;
    u32 coinUIRaw = coinUIOffset - 0x100000u;
    if (coinCalcRaw > textSize || textSize - coinCalcRaw < 0x350u ||
        coinUIRaw > textSize || textSize - coinUIRaw < 0x10u)
    {
        PLUGIN_coin_ClearTransientHomePointer();
        return;
    }

    u32 *coinCalc = (u32*)(code + coinCalcRaw);
    if (coinCalc[0x24u / 4u] != 0xE0570005u ||
        coinCalc[0x28u / 4u] != 0xE0D80006u ||
        coinCalc[0x2Cu / 4u] != 0xE58D5028u ||
        coinCalc[0x30u / 4u] != 0xE58D6024u ||
        coinCalc[0x34u / 4u] != 0xBA00000Cu ||
        coinCalc[0x168u / 4u] != 0xE05EA000u ||
        coinCalc[0x16Cu / 4u] != 0xE3A03000u)
    {
        PLUGIN_coin_ClearTransientHomePointer();
        return;
    }

    for (u32 i = 0; i < sizeof(g_costPlus3Expected) / sizeof(g_costPlus3Expected[0]); i++)
    {
        if (coinCalc[0x188u / 4u + i] != g_costPlus3Expected[i])
        {
            PLUGIN_coin_ClearTransientHomePointer();
            return;
        }
    }

    // modern EUR/USA match this, old 7.x just has no site
    if (achievementsEnabled && !newsDuplicate && newsCallAddress)
        (void)PLUGIN_coin_PatchHomeNewsIcon(code, newsCallAddress, newsOriginalTarget);

    if (preserveBadgeOffset)
    {
        u32 preserveBadgeRaw = preserveBadgeOffset - 0x100000u;
        if (preserveBadgeRaw < 0x18u || preserveBadgeRaw > textSize ||
            textSize - preserveBadgeRaw < 0x38u)
        {
            preserveBadgeOffset = 0;
        }
    }

    u32 homePointer = coinCalcOffset + 0x3Cu;
    if (!PLUGIN_coin_WriteOwnCodeWord((u32)&PLUGIN_coin_homeUIReturn, coinUIOffset + 0x10u) ||
        !PLUGIN_coin_WriteOwnCodeWord((u32)&PLUGIN_coin_homePtr, homePointer))
    {
        PLUGIN_coin_ClearTransientHomePointer();
        return;
    }

    u32 stateMapBase;
    u32 stateMapped;
    if (!PLUGIN_coin_MapOwnPage((u32)&PLUGIN_coin_dat, &stateMapBase, &stateMapped))
    {
        PLUGIN_coin_ClearTransientHomePointer();
        return;
    }

    u32 stateDelta = stateMapped - (u32)&PLUGIN_coin_dat;
    u16 *mappedDat = (u16*)((u32)&PLUGIN_coin_dat + stateDelta);
    u32 *mappedBin = (u32*)((u32)PLUGIN_coin_bin + stateDelta);
    u16 *mappedChange = (u16*)((u32)PLUGIN_coin_change + stateDelta);
    u32 *mappedHandoffControl =
        (u32*)((u32)&PLUGIN_coin_handoffControl + stateDelta);
    *mappedHandoffControl = 0;
    mappedHandoffControl[1] = 0;
    mappedHandoffControl[2] = 0;
    mappedHandoffControl[3] = 0;
    mappedHandoffControl[4] = 0;
    mappedHandoffControl[5] = 0;
    mappedHandoffControl[6] = 0;
    if (!PLUGIN_coin_InitializeHomeMenuState(mappedDat, mappedBin, mappedChange, homePointer))
    {
        PLUGIN_coin_UnmapOwnPage(stateMapBase);
        PLUGIN_coin_ClearTransientHomePointer();
        return;
    }

    u32 patch[2] = {0xE51FF004u, PLUGIN_coin_Phys(PLUGIN_coin_homeLoaderPatch)};
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x348u, patch, 2);

    patch[1] = PLUGIN_coin_Phys(PLUGIN_coin_rtcDayGateHook);
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x24u, patch, 2);

    patch[1] = PLUGIN_coin_Phys(PLUGIN_coin_homeLoaderUIHook);
    PLUGIN_coin_PatchWords(code, coinUIOffset + 0x8u, patch, 2);

    u32 pointerBlock[13] = {
        0xEA000005u,
        PLUGIN_coin_Phys(&PLUGIN_coin_dat),
        PLUGIN_coin_Phys(PLUGIN_coin_bin),
        PLUGIN_coin_Phys(PLUGIN_coin_change),
        0x0061004Du,
        0x00650078u,
        0x00000064u,
        0xE51FF004u,
        PLUGIN_coin_Phys(PLUGIN_coin_preCoinHook),
        0xEA000001u,
        PLUGIN_coin_Phys(&PLUGIN_coin_handoffControl),
        NOP,
        0x1A00001Au,
    };

    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x38u, pointerBlock, 13);

    u32 historyDiagPatch[2] = {0xE51FF004u, PLUGIN_coin_Phys(PLUGIN_coin_historyDiagHook)};
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x168u, historyDiagPatch, 2);

    u32 costPlus3Patch[2] = {0xE51FF004u, PLUGIN_coin_Phys(PLUGIN_coin_costPlus3Hook)};
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x188u, costPlus3Patch, 2);

    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x1A4u, g_uncappedPatch, 16);
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x330u, g_coinPatch330, 2);
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x340u, g_coinPatch340, 2);

    if (preserveBadgeOffset)
    {
        PLUGIN_coin_PatchWords(code, preserveBadgeOffset - 0x18u, g_badgeReturnPatch, 2);
        PLUGIN_coin_PatchWords(code, preserveBadgeOffset + 0x34u, g_badgeBranchPatch, 1);
    }

    PLUGIN_coin_svcFlushEntireDataCache();
    PLUGIN_coin_UnmapOwnPage(stateMapBase);
}

PLUGIN_RODATA(coin) static const char g_coinNewslistIconPath[] = "/luma/coinachv/icn.bin";

PLUGIN_RODATA(coin) static const u32 g_coinNewslistResolverPrefix[24] = {
    0xE92D41F0u, 0xE24DDD82u, 0xE1A04000u, 0xE28D0A02u,
    0xE2800098u, 0xE1A07003u, 0xE8900060u, 0xE5910004u,
    0xE5913000u, 0xE1931000u, 0x1A000017u, 0xE5940010u,
    0xE1A02005u, 0xE3500000u, 0x0A000009u, 0xE3520C12u,
    0xE3A00C12u, 0x21A02000u, 0xE5862000u, 0xE5940010u,
    0xE28DDD82u, 0xE2801D12u, 0xE1A00007u, 0xE8BD41F0u,
};

PLUGIN_CODE(coin) bool PLUGIN_coin_PrepareHomeMenu(
    PluginMenuLoaderContext *context
)
{
    if (!context || !context->code || !context->textSize)
        return false;

    PLUGIN_coin_PatchHomeMenu(context->code, context->textSize);
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_MatchNewslistIconCall(
    volatile const u32 *code,
    u32 wordCount,
    u32 index,
    u32 textAddress,
    u32 textSize,
    u32 *originalTarget
)
{
    // USA 7.0 and current EUR share this full resolver call shape
    if (!code || !originalTarget || index < 11u || index + 12u >= wordCount)
        return false;

    volatile const u32 *site = code + index;
    if (site[-11] != 0xE92D4010u ||
        site[-10] != 0xE24DD010u ||
        site[-9]  != 0xE1A04000u ||
        site[-8]  != 0xE3A00000u ||
        site[-7]  != 0xE58D000Cu ||
        site[-6]  != 0xE59F0044u ||
        site[-5]  != 0xE28DC00Cu ||
        site[-4]  != 0xE3A03C12u ||
        site[-3]  != 0xE5900000u ||
        site[-2]  != 0xE88D1008u ||
        site[-1]  != 0xE284301Cu ||
        (site[0] & 0xFF000000u) != 0xEB000000u ||
        site[1]  != 0xE3A00040u ||
        site[2]  != 0xE3A01030u ||
        site[3]  != 0xE1A03001u ||
        site[4]  != 0xE58D0004u ||
        site[5]  != 0xE58D1000u ||
        site[6]  != 0xE58D0008u ||
        site[7]  != 0xE3A02C12u ||
        site[8]  != 0xE284101Cu ||
        site[9]  != 0xE1A00004u ||
        (site[10] & 0xFF000000u) != 0xEB000000u ||
        site[11] != 0xE28DD010u ||
        site[12] != 0xE8BD8010u)
    {
        return false;
    }

    u32 siteAddress = textAddress + index * sizeof(u32);
    u32 target = 0;
    if (!PLUGIN_coin_DecodeArmBlTarget(siteAddress, site[0], &target) ||
        target < textAddress ||
        target - textAddress > textSize - 24u * sizeof(u32) ||
        ((target - textAddress) & 3u))
    {
        return false;
    }

    // first 0x60 bytes match on the USA 7.0 and current EUR dumps
    volatile const u32 *resolver = code + (target - textAddress) / sizeof(u32);
    for (u32 i = 0; i < sizeof(g_coinNewslistResolverPrefix) / sizeof(g_coinNewslistResolverPrefix[0]); i++)
    {
        if (resolver[i] != g_coinNewslistResolverPrefix[i])
            return false;
    }

    *originalTarget = target;
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_FindNewslistIconCall(
    const CodeSetHeader *header,
    volatile const u32 *text,
    u32 *hookAddress,
    u32 *originalTarget
)
{
    if (!header || !text || !hookAddress || !originalTarget ||
        !header->text_size_total ||
        header->text_size_total > 0xFFFFFFFFu / 0x1000u)
    {
        return false;
    }

    u32 textSize = header->text_size_total * 0x1000u;
    if ((header->text_addr & 3u) || header->text_addr > 0xFFFFFFFFu - textSize)
        return false;

    u32 wordCount = textSize / sizeof(u32);
    u32 foundAddress = 0;
    u32 foundTarget = 0;

    for (u32 i = 11u; i + 12u < wordCount; i++)
    {
        u32 target = 0;
        if (!PLUGIN_coin_MatchNewslistIconCall(
                text,
                wordCount,
                i,
                header->text_addr,
                textSize,
                &target))
        {
            continue;
        }

        // more than one match = dont patch
        if (foundAddress)
            return false;

        foundAddress = header->text_addr + i * sizeof(u32);
        foundTarget = target;
    }

    if (!foundAddress)
        return false;

    *hookAddress = foundAddress;
    *originalTarget = foundTarget;
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_SetNewslistHookWords(
    u32 iconBase,
    u32 originalTarget,
    u32 resumeAddress
)
{
    // iconBase stays zero until the target pages are filled
    return originalTarget && resumeAddress &&
        PLUGIN_coin_WriteOwnCodeWord(
            (u32)&PLUGIN_coin_newslistIconBaseWord,
            iconBase
        ) &&
        PLUGIN_coin_WriteOwnCodeWord(
            (u32)&PLUGIN_coin_newslistIconOriginalWord,
            originalTarget
        ) &&
        PLUGIN_coin_WriteOwnCodeWord(
            (u32)&PLUGIN_coin_newslistIconResumeWord,
            resumeAddress
        );
}

PLUGIN_CODE(coin) bool PLUGIN_coin_PrepareNewsList(
    PluginMenuLoaderContext *context
)
{
    CodeSetHeader *header;
    volatile u32 *text;

    g_coinNewslistIconBase = 0;
    if (!context || !context->codeSet || !context->code ||
        !PLUGIN_coin_AchievementsEnabledForLoader())
    {
        return false;
    }

    header = context->codeSet;
    text = (volatile u32 *)context->code;

    u32 hookAddress = 0;
    u32 originalTarget = 0;
    if (!PLUGIN_coin_FindNewslistIconCall(
            header,
            text,
            &hookAddress,
            &originalTarget))
    {
        return false;
    }

    if (header->rw_size_total > 0xFFFFFFFFu / 0x1000u ||
        header->rw_size_total > 0xFFFFFFFFu - COIN_NEWSLIST_ICON_PAGES)
    {
        return false;
    }

    u32 textSize = header->text_size_total * 0x1000u;
    if (hookAddress < header->text_addr ||
        hookAddress - header->text_addr > textSize - 2u * sizeof(u32))
    {
        return false;
    }

    u32 rwSize = header->rw_size_total * 0x1000u;
    if (header->rw_addr > 0xFFFFFFFFu - rwSize)
        return false;

    u32 iconBase = header->rw_addr + rwSize;
    if (!iconBase || iconBase > 0xFFFFFFFFu - COIN_NEWSLIST_ICON_ALLOC_SIZE)
        return false;

    // patch the writable CodeSet source before svcCreateCodeSet makes RX text
    u32 hookIndex = (hookAddress - header->text_addr) / sizeof(u32);
    volatile u32 *site = text + hookIndex;
    u32 decodedTarget = 0;
    if (!PLUGIN_coin_DecodeArmBlTarget(hookAddress, site[0], &decodedTarget) ||
        decodedTarget != originalTarget ||
        site[1] != 0xE3A00040u)
    {
        return false;
    }

    // zero iconBase is the safe Nintendo-resolver fallback until assets are loaded
    if (!PLUGIN_coin_SetNewslistHookWords(
            0,
            originalTarget,
            hookAddress + 2u * sizeof(u32)))
    {
        return false;
    }

    site[0] = 0xE51FF004u;
    site[1] = PLUGIN_coin_Phys(PLUGIN_coin_NewslistIconHook);
    PLUGIN_coin_svcFlushEntireDataCache();

    // these extra icon pages belong to the NewsList applet
    header->rw_size_total += COIN_NEWSLIST_ICON_PAGES;
    g_coinNewslistIconBase = iconBase;
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_LoadNewslistIcons(Handle process, u32 iconBase)
{
    u32 scratch = 0;
    if (!PLUGIN_coin_MapGuardedRange(
            process,
            iconBase,
            COIN_NEWSLIST_ICON_ALLOC_SIZE,
            &scratch))
    {
        return false;
    }

    bool ok = false;
    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );

    if (R_SUCCEEDED(rc))
    {
        rc = COIN_HOST__FSUSER_OpenFile(
            &file,
            sd,
            COIN_HOST__fsMakePath(PATH_ASCII, g_coinNewslistIconPath),
            FS_OPEN_READ,
            0
        );
        if (R_SUCCEEDED(rc))
        {
            // header + four icons + EOF probe in one read
            u32 read = 0;
            u32 wanted = (2u * sizeof(u32)) + COIN_NEWSLIST_ICON_PACK_SIZE + 1u;
            rc = COIN_HOST__FSFILE_Read(file, &read, 0, (void*)scratch, wanted);
            if (R_SUCCEEDED(rc) &&
                read == (2u * sizeof(u32)) + COIN_NEWSLIST_ICON_PACK_SIZE)
            {
                volatile u32 *assetHeader = (volatile u32*)scratch;
                ok = assetHeader[0] == COIN_ASSET_VERSION_MAGIC;
            }
            COIN_HOST__FSFILE_Close(file);
        }
        COIN_HOST__FSUSER_CloseArchive(sd);
    }

    if (ok)
        PLUGIN_coin_svcFlushEntireDataCache();

    PLUGIN_coin_UnmapGuardedRange(scratch, COIN_NEWSLIST_ICON_ALLOC_SIZE);
    return ok;
}

PLUGIN_CODE(coin) void PLUGIN_coin_NewsListProcessCreated(
    PluginMenuLoaderContext *context
)
{
    u32 iconBase = g_coinNewslistIconBase;
    g_coinNewslistIconBase = 0;

    if (!context || !context->process || !iconBase)
        return;

    // process isnt started yet, fill its icon pages before publishing iconBase
    if (PLUGIN_coin_LoadNewslistIcons(context->process, iconBase))
    {
        (void)PLUGIN_coin_WriteOwnCodeWord(
            (u32)&PLUGIN_coin_newslistIconBaseWord,
            iconBase + (2u * sizeof(u32))
        );
    }
}

