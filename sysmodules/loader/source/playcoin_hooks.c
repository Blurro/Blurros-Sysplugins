#include <3ds.h>
#include "memory.h"

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
#define COIN_HOST__loaderHomePatch          ((u32)pluginTable_coin[6])
#define COIN_HOST__loaderCreateCodeSetPatch ((u32)pluginTable_coin[7])
#define COIN_HOST__loaderCreateProcessPatch ((u32)pluginTable_coin[8])
#define COIN_HOST__FSUSER_OpenArchive       ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))pluginTable_coin[0])
#define COIN_HOST__FSUSER_CloseArchive      ((Result(*)(FS_Archive))pluginTable_coin[1])
#define COIN_HOST__FSUSER_OpenFile          ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))pluginTable_coin[2])
#define COIN_HOST__FSFILE_Read              ((Result(*)(Handle,u32*,u64,void*,u32))pluginTable_coin[3])
#define COIN_HOST__FSFILE_Close             ((Result(*)(Handle))pluginTable_coin[4])
#define COIN_HOST__fsMakePath               ((FS_Path(*)(FS_PathType,const void*))pluginTable_coin[5])

#define COIN_NEWSLIST_TITLE_ID_JPN 0x0004003000008E02ULL
#define COIN_NEWSLIST_TITLE_ID_USA 0x0004003000009702ULL
#define COIN_NEWSLIST_TITLE_ID_EUR 0x000400300000A002ULL
#define COIN_NEWSLIST_TITLE_ID_CHN 0x000400300000A802ULL
#define COIN_NEWSLIST_TITLE_ID_KOR 0x000400300000B002ULL
#define COIN_NEWSLIST_TITLE_ID_TWN 0x000400300000B802ULL
#define COIN_NEWSLIST_ICON_PACK_SIZE 0x4800u
#define COIN_ASSET_VERSION_MAGIC 0x56584E33u
#define COIN_NEWSLIST_ICON_ALLOC_SIZE 0x5000u
#define COIN_NEWSLIST_ICON_PAGES 5u

extern void PLUGIN_coin_ClearTransientHomePointer(void);
extern bool PLUGIN_coin_InitializeHomeMenuState(u16 *coinDat, u32 *coinData, u16 *coinChange, u32 homePointer);
extern void PLUGIN_coin_PatchHomeMenu(u8 *code, u32 textSize);

extern u16 PLUGIN_coin_dat;
extern u32 PLUGIN_coin_bin[4];
extern u16 PLUGIN_coin_change[4];
extern u32 PLUGIN_coin_homePtr;
extern u32 PLUGIN_coin_handoffControl;
extern u32 PLUGIN_coin_homeUIReturn;
extern u32 PLUGIN_coin_loaderReturn;
extern void PLUGIN_coin_LoaderPatchCodeHook(void);
extern void PLUGIN_coin_homeLoaderPatch(void);
extern void PLUGIN_coin_homeLoaderUIHook(void);
extern void PLUGIN_coin_preCoinHook(void);
extern void PLUGIN_coin_historyDiagHook(void);
extern void PLUGIN_coin_costPlus3Hook(void);
extern void PLUGIN_coin_CreateCodeSetHook(void);
extern void PLUGIN_coin_CreateProcessHook(void);
extern void PLUGIN_coin_NewslistIconHook(void);
extern u32 PLUGIN_coin_newslistIconBaseWord;
extern u32 PLUGIN_coin_newslistIconOriginalWord;
extern u32 PLUGIN_coin_newslistIconResumeWord;
extern void PLUGIN_coin_HomeNewsIconHook(void);
extern u32 PLUGIN_coin_homeNewsIconOriginalWord;
extern u32 PLUGIN_coin_homeNewsIconResumeWord;

PLUGIN_DATA(coin) static u32 g_coinCreateCodeSetReturn;
PLUGIN_DATA(coin) static u32 g_coinCreateProcessReturn;
PLUGIN_DATA(coin) static bool g_coinNewslistPending;
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
    // Home Menu uses the same notification icon ABI as newslist
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

    ".global PLUGIN_coin_CreateCodeSetHook\n"
    ".type PLUGIN_coin_CreateCodeSetHook, %function\n"
    "PLUGIN_coin_CreateCodeSetHook:\n"
    // r0=codeset, r1=header, r2/r3=text/ro, stack=data
    "push {r0-r3, lr}\n"
    "mov r0, r1\n"
    "mov r1, r2\n"
    "bl PLUGIN_coin_PreCreateCodeSet\n"
    "pop {r0-r3, lr}\n"
    // inline the tiny svcCreateCodeSet wrapper
    "push {r0}\n"
    "ldr r0, [sp, #4]\n"
    "svc 0x73\n"
    "ldr r2, [sp]\n"
    "str r1, [r2]\n"
    "add sp, sp, #4\n"
    // replay cmp r0,#0 so the next bge sees the old flags
    "cmp r0, #0\n"
    "ldr r12, =g_coinCreateCodeSetReturn\n"
    "ldr pc, [r12]\n"

    ".global PLUGIN_coin_CreateProcessHook\n"
    ".type PLUGIN_coin_CreateProcessHook, %function\n"
    "PLUGIN_coin_CreateProcessHook:\n"
    // tiny svcCreateProcess wrapper
    "push {r0}\n"
    "svc 0x75\n"
    "ldr r2, [sp]\n"
    "str r1, [r2]\n"
    "add sp, sp, #4\n"
    // keep the host-visible result/output regs intact
    "push {r0-r3, lr}\n"
    "mov r1, r2\n"
    "bl PLUGIN_coin_PostCreateProcess\n"
    "pop {r0-r3, lr}\n"
    // replay the old mov r5,r0
    "mov r5, r0\n"
    "ldr r12, =g_coinCreateProcessReturn\n"
    "ldr pc, [r12]\n"

    // runs through newslist's K11 alias, dont touch Loader VA data here
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

        if (!newsDuplicate && scan[0] == 0xE3A01C12u)
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
    if (coinCalc[0x168u / 4u] != 0xE05EA000u ||
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
    if (!newsDuplicate && newsCallAddress)
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
    if (!PLUGIN_coin_InitializeHomeMenuState(mappedDat, mappedBin, mappedChange, homePointer))
    {
        PLUGIN_coin_UnmapOwnPage(stateMapBase);
        PLUGIN_coin_ClearTransientHomePointer();
        return;
    }

    u32 patch[2] = {0xE51FF004u, PLUGIN_coin_Phys(PLUGIN_coin_homeLoaderPatch)};
    PLUGIN_coin_PatchWords(code, coinCalcOffset + 0x348u, patch, 2);

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

PLUGIN_CODE(coin) static bool PLUGIN_coin_IsNewslistTitleId(u64 programId)
{
    // NEWS has regional titleids even though the resolver shape is shared
    return programId == COIN_NEWSLIST_TITLE_ID_JPN ||
           programId == COIN_NEWSLIST_TITLE_ID_USA ||
           programId == COIN_NEWSLIST_TITLE_ID_EUR ||
           programId == COIN_NEWSLIST_TITLE_ID_CHN ||
           programId == COIN_NEWSLIST_TITLE_ID_KOR ||
           programId == COIN_NEWSLIST_TITLE_ID_TWN;
}

PLUGIN_CODE(coin) void PLUGIN_coin_PreCreateCodeSet(
    CodeSetHeader *header,
    volatile u32 *text
)
{
    // clear only the pending handoff, not an already-live hook
    g_coinNewslistPending = false;
    g_coinNewslistIconBase = 0;

    if (!header || !PLUGIN_coin_IsNewslistTitleId(header->program_id))
        return;

    u32 hookAddress = 0;
    u32 originalTarget = 0;
    if (!PLUGIN_coin_FindNewslistIconCall(
            header,
            text,
            &hookAddress,
            &originalTarget))
    {
        return;
    }

    if (header->rw_size_total > 0xFFFFFFFFu / 0x1000u ||
        header->rw_size_total > 0xFFFFFFFFu - COIN_NEWSLIST_ICON_PAGES)
    {
        return;
    }

    u32 textSize = header->text_size_total * 0x1000u;
    if (hookAddress < header->text_addr ||
        hookAddress - header->text_addr > textSize - 2u * sizeof(u32))
    {
        return;
    }

    u32 rwSize = header->rw_size_total * 0x1000u;
    if (header->rw_addr > 0xFFFFFFFFu - rwSize)
        return;

    u32 iconBase = header->rw_addr + rwSize;
    if (!iconBase || iconBase > 0xFFFFFFFFu - COIN_NEWSLIST_ICON_ALLOC_SIZE)
        return;

    // patch the writable CodeSet source before svcCreateCodeSet makes RX text
    u32 hookIndex = (hookAddress - header->text_addr) / sizeof(u32);
    volatile u32 *site = text + hookIndex;
    u32 decodedTarget = 0;
    if (!PLUGIN_coin_DecodeArmBlTarget(hookAddress, site[0], &decodedTarget) ||
        decodedTarget != originalTarget ||
        site[1] != 0xE3A00040u)
    {
        return;
    }

    // zero iconBase is the safe Nintendo-resolver fallback until assets are loaded
    if (!PLUGIN_coin_SetNewslistHookWords(
            0,
            originalTarget,
            hookAddress + 2u * sizeof(u32)))
    {
        return;
    }

    site[0] = 0xE51FF004u;
    site[1] = PLUGIN_coin_Phys(PLUGIN_coin_NewslistIconHook);
    PLUGIN_coin_svcFlushEntireDataCache();

    // extra icon pages belong to newslist itself
    header->rw_size_total += COIN_NEWSLIST_ICON_PAGES;
    g_coinNewslistIconBase = iconBase;
    g_coinNewslistPending = true;
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

PLUGIN_CODE(coin) void PLUGIN_coin_PostCreateProcess(Result createResult, Handle *outProcessHandle)
{
    if (!g_coinNewslistPending)
        return;

    g_coinNewslistPending = false;
    u32 iconBase = g_coinNewslistIconBase;
    g_coinNewslistIconBase = 0;

    if (R_FAILED(createResult) || !outProcessHandle || !*outProcessHandle || !iconBase)
        return;

    Handle process = *outProcessHandle;

    // process isnt started yet, fill its icon pages before publishing iconBase
    if (PLUGIN_coin_LoadNewslistIcons(process, iconBase))
    {
        (void)PLUGIN_coin_WriteOwnCodeWord(
            (u32)&PLUGIN_coin_newslistIconBaseWord,
            iconBase + (2u * sizeof(u32))
        );
    }
}

PLUGIN_CODE(coin) static volatile u32 *PLUGIN_coin_FindCreateCodeSetPatch(
    volatile u32 *anchor,
    u32 maxWords
)
{
    volatile u32 *found = NULL;

    for (u32 i = 0; i + 2u < maxWords; i++)
    {
        volatile u32 *p = anchor + i;
        if ((p[0] & 0xFF000000u) == 0xEB000000u &&
            p[1] == 0xE3500000u &&
            (p[2] & 0xFF000000u) == 0xAA000000u)
        {
            if (found)
                return NULL;
            found = p;
        }
    }

    return found;
}

PLUGIN_CODE(coin) static volatile u32 *PLUGIN_coin_FindCreateProcessPatch(
    volatile u32 *anchor,
    u32 maxWords
)
{
    volatile u32 *found = NULL;

    for (u32 i = 0; i + 3u < maxWords; i++)
    {
        volatile u32 *p = anchor + i;
        if ((p[0] & 0xFF000000u) == 0xEB000000u &&
            p[1] == 0xE1A05000u &&
            (p[2] & 0xFFFFF000u) == 0xE59D0000u &&
            (p[3] & 0xFF000000u) == 0xEB000000u)
        {
            if (found)
                return NULL;
            found = p;
        }
    }

    return found;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_InstallAllLoaderHooks(void)
{
    u32 returnMapBase = 0;
    u32 returnMapped = 0;
    u32 homeMapBase = 0;
    u32 homeAddress = 0;
    u32 codeSetMapBase = 0;
    u32 codeSetAddress = 0;
    u32 processMapBase = 0;
    u32 processAddress = 0;
    bool ok = false;

    // markers land on the expression, scan forward for the actual BL shape
    if (!PLUGIN_coin_MapOwnPage((u32)&PLUGIN_coin_loaderReturn, &returnMapBase, &returnMapped))
        goto done;
    if (!PLUGIN_coin_MapOwnPage(COIN_HOST__loaderHomePatch, &homeMapBase, &homeAddress))
        goto done;
    if (!PLUGIN_coin_MapOwnPage(COIN_HOST__loaderCreateCodeSetPatch, &codeSetMapBase, &codeSetAddress))
        goto done;
    if (!PLUGIN_coin_MapOwnPage(COIN_HOST__loaderCreateProcessPatch, &processMapBase, &processAddress))
        goto done;

    volatile u32 *home = (volatile u32*)homeAddress;

    u32 codeSetWords = (0x1000u - (COIN_HOST__loaderCreateCodeSetPatch & 0xFFFu)) / 4u;
    if (codeSetWords > 0x40u)
        codeSetWords = 0x40u;
    volatile u32 *codeSet = PLUGIN_coin_FindCreateCodeSetPatch(
        (volatile u32*)codeSetAddress,
        codeSetWords
    );

    u32 processWords = (0x1000u - (COIN_HOST__loaderCreateProcessPatch & 0xFFFu)) / 4u;
    if (processWords > 0x40u)
        processWords = 0x40u;
    volatile u32 *process = PLUGIN_coin_FindCreateProcessPatch(
        (volatile u32*)processAddress,
        processWords
    );

    if ((home[0] & 0xFFFFF000u) != 0xE59F2000u ||
        home[1] != 0xE7872003u ||
        !codeSet || !process)
    {
        goto done;
    }

    u32 codeSetHost = COIN_HOST__loaderCreateCodeSetPatch +
        ((u32)codeSet - codeSetAddress);
    u32 processHost = COIN_HOST__loaderCreateProcessPatch +
        ((u32)process - processAddress);

    *(u32*)returnMapped = COIN_HOST__loaderHomePatch + 0x2Cu;
    g_coinCreateCodeSetReturn = codeSetHost + 8u;
    g_coinCreateProcessReturn = processHost + 8u;

    home[0] = 0xE51FF004u;
    home[1] = (u32)PLUGIN_coin_LoaderPatchCodeHook;

    codeSet[0] = 0xE51FF004u;
    codeSet[1] = (u32)PLUGIN_coin_CreateCodeSetHook;

    process[0] = 0xE51FF004u;
    process[1] = (u32)PLUGIN_coin_CreateProcessHook;

    PLUGIN_coin_svcFlushEntireDataCache();
    PLUGIN_coin_svcInvalidateEntireInstructionCache();
    ok = true;

done:
    PLUGIN_coin_UnmapOwnPage(processMapBase);
    PLUGIN_coin_UnmapOwnPage(codeSetMapBase);
    PLUGIN_coin_UnmapOwnPage(homeMapBase);
    PLUGIN_coin_UnmapOwnPage(returnMapBase);
    return ok;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_InstallHooks(void)
{
    return PLUGIN_coin_InstallAllLoaderHooks();
}
