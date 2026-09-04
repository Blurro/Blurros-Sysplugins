/*
 * IgnoreCfgNor Nexus3DS Loader sysplugin v1.01
 * Reimplements the behavior of lifehackerhansol/Luma3DS ignore-cfgnor
 * commit ef25b3e4f23784dc192d5ae645963f34a54d3967 without modifying boot.firm.
 *
 * Nexus3DS/Luma3DS-derived work. GNU GPL v3 or later.
 */
#include <3ds.h>

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id) __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id) __attribute__((section(".plugin_" #id "_entry"), used))

#define CFGN_K11_VA_BASE        0x70000000u
#define CFGN_O3DS_SYSTEM_TOP    0x26C00000u
#define CFGN_N3DS_SYSTEM_TOP    0x2E000000u
#define CFGN_ALIAS_BIT          0x80000000u
#define CFGN_BASE_SCAN_LIMIT    0x00040000u

extern void PLUGIN_CFGN_KernelHook(void);
extern u32 PLUGIN_CFGN_lookupVa;
extern u32 PLUGIN_CFGN_resumeVa;
extern u32 PLUGIN_CFGN_switchR2;
extern u32 PLUGIN_CFGN_cleanupVa;

extern u32 PLUGIN_CFGN_svcConvertVAToPA(const void *va, bool writeCheck);
extern void PLUGIN_CFGN_svcFlushEntireDataCache(void);
extern void PLUGIN_CFGN_svcInvalidateEntireInstructionCache(void);
extern Result PLUGIN_CFGN_svcGetSystemInfo(s64 *out, u32 type, s32 param);

/*
 * This stub executes in K11 through Nexus's global physical alias.  At the
 * patched site SendSyncRequestHook has already proved r4 is a KClientSession;
 * r8 is the current thread TLS base and cmdbuf starts at r8+0x80.
 *
 * For cfg:nor we reproduce lifehackerhansol/Luma3DS ignore-cfgnor:
 *     skip = true;
 *     cmdbuf[1] = -1;
 * with res still zero.  For every other service we recreate the two original
 * instructions and resume the stock switch dispatcher.
 */
__asm__(
    ".section .plugin_CFGN,\"ax\",%progbits\n"
    ".balign 4\n"

    ".global PLUGIN_CFGN_lookupVa\n"
    "PLUGIN_CFGN_lookupVa:\n"
    ".word 0\n"
    ".global PLUGIN_CFGN_resumeVa\n"
    "PLUGIN_CFGN_resumeVa:\n"
    ".word 0\n"
    ".global PLUGIN_CFGN_switchR2\n"
    "PLUGIN_CFGN_switchR2:\n"
    ".word 0\n"
    ".global PLUGIN_CFGN_cleanupVa\n"
    "PLUGIN_CFGN_cleanupVa:\n"
    ".word 0\n"

    ".global PLUGIN_CFGN_KernelHook\n"
    ".type PLUGIN_CFGN_KernelHook, %function\n"
    "PLUGIN_CFGN_KernelHook:\n"
    "push {r0-r3, r12, lr}\n"
    "ldr r0, [r4, #20]\n"                 /* clientSession->parentSession */
    "adr r12, PLUGIN_CFGN_lookupVa\n"
    "ldr r12, [r12]\n"
    "blx r12\n"                            /* SessionInfo_Lookup */
    "cmp r0, #0\n"
    "beq 2f\n"
    "ldr r1, [r0, #4]\n"                  /* info->name[0..3] */
    "ldr r2, =0x3A676663\n"                /* \"cfg:\" little-endian */
    "cmp r1, r2\n"
    "bne 2f\n"
    "ldr r1, [r0, #8]\n"                  /* info->name[4..7] */
    "ldr r2, =0x00726F6E\n"                /* \"nor\\0\" little-endian */
    "cmp r1, r2\n"
    "bne 2f\n"

    /* Matched cfg:nor: synthesize the same failed IPC reply as the fork,
       then enter the stock SendSyncRequestHook skip-cleanup tail.  r6 is the
       compiler's skip boolean at that tail; [sp,#8] already contains res=0. */
    "pop {r0-r3, r12, lr}\n"
    "mvn r0, #0\n"
    "str r0, [r8, #0x84]\n"                /* cmdbuf[1] = -1 */
    "mov r6, #1\n"                         /* skip = true */
    "adr r12, PLUGIN_CFGN_cleanupVa\n"
    "ldr pc, [r12]\n"

    /* Not cfg:nor: restore caller scratch regs, recreate the two overwritten
       instructions, and resume at the original cmp r3,r2. */
    "2:\n"
    "pop {r0-r3, r12, lr}\n"
    "ldr r3, [r8, #0x80]\n"
    "adr r12, PLUGIN_CFGN_switchR2\n"
    "ldr r2, [r12]\n"
    "adr r12, PLUGIN_CFGN_resumeVa\n"
    "ldr pc, [r12]\n"

    ".global PLUGIN_CFGN_svcConvertVAToPA\n"
    ".type PLUGIN_CFGN_svcConvertVAToPA, %function\n"
    "PLUGIN_CFGN_svcConvertVAToPA:\n"
    "svc 0x90\n"
    "bx lr\n"

    ".global PLUGIN_CFGN_svcFlushEntireDataCache\n"
    ".type PLUGIN_CFGN_svcFlushEntireDataCache, %function\n"
    "PLUGIN_CFGN_svcFlushEntireDataCache:\n"
    "svc 0x92\n"
    "bx lr\n"

    ".global PLUGIN_CFGN_svcInvalidateEntireInstructionCache\n"
    ".type PLUGIN_CFGN_svcInvalidateEntireInstructionCache, %function\n"
    "PLUGIN_CFGN_svcInvalidateEntireInstructionCache:\n"
    "svc 0x94\n"
    "bx lr\n"

    ".global PLUGIN_CFGN_svcGetSystemInfo\n"
    ".type PLUGIN_CFGN_svcGetSystemInfo, %function\n"
    "PLUGIN_CFGN_svcGetSystemInfo:\n"
    "push {r0}\n"
    "svc 0x2A\n"
    "pop {r3}\n"
    "str r1, [r3]\n"
    "str r2, [r3, #4]\n"
    "bx lr\n"
    ".ltorg\n"
);

PLUGIN_CODE(CFGN) static u32 PLUGIN_CFGN_AliasOf(const void *pointer)
{
    u32 pa = PLUGIN_CFGN_svcConvertVAToPA(pointer, false);
    return pa ? (pa | CFGN_ALIAS_BIT) : 0;
}

PLUGIN_CODE(CFGN) static bool PLUGIN_CFGN_IsK11Base(volatile const u32 *words)
{
    return words[0] == 0xEA00000Au &&
           words[1] == 0xEA000010u &&
           words[2] == 0xEA000017u &&
           words[10] == 0x00000001u;
}

PLUGIN_CODE(CFGN) static u32 PLUGIN_CFGN_FindK11Base(u32 topAlias)
{
    if (topAlias < CFGN_ALIAS_BIT + 0x1000u)
        return 0;

    u32 page = (topAlias - 0x1000u) & ~0xFFFu;
    u32 minimum = topAlias > CFGN_ALIAS_BIT + CFGN_BASE_SCAN_LIMIT
                    ? topAlias - CFGN_BASE_SCAN_LIMIT
                    : CFGN_ALIAS_BIT;

    for (;;)
    {
        if (PLUGIN_CFGN_IsK11Base((volatile const u32 *)page))
            return page;

        if (page <= minimum || page < CFGN_ALIAS_BIT + 0x1000u)
            break;

        page -= 0x1000u;
    }

    return 0;
}

PLUGIN_CODE(CFGN) static bool PLUGIN_CFGN_IsSendSyncStart(volatile const u32 *w)
{
    return w[0]  == 0xE92D4FF0u &&
           w[1]  == 0xE3E04C7Fu &&
           w[2]  == 0xE1A05000u &&
           (w[3] & 0xFFFFF000u) == 0xE59F3000u &&
           (w[4] & 0xFFFFF000u) == 0xE59F6000u &&
           w[7]  == 0xE5943F05u &&
           (w[8] & 0xFFFFF000u) == 0xE59F7000u &&
           w[9]  == 0xE0830000u &&
           w[10] == 0xE7933001u &&
           w[12] == 0xE24DD054u &&
           w[13] == 0xE1A01005u &&
           w[14] == 0xE1A09003u &&
           w[15] == 0xE1A0A000u &&
           w[16] == 0xE12FFF32u &&
           w[17] == 0xE3A02000u &&
           w[18] == 0xE5943F01u &&
           w[19] == 0xE2504000u &&
           w[20] == 0xE5933094u &&
           w[21] == 0xE58D2008u;
}

PLUGIN_CODE(CFGN) static u32 PLUGIN_CFGN_FindSendSync(u32 k11BaseAlias, u32 topAlias)
{
    u32 found = 0;

    for (u32 address = k11BaseAlias; address + 22u * 4u <= topAlias; address += 4u)
    {
        if (!PLUGIN_CFGN_IsSendSyncStart((volatile const u32 *)address))
            continue;

        if (found)
            return 0; /* ambiguous signature: refuse to patch */
        found = address;
    }

    return found;
}

PLUGIN_CODE(CFGN) static u32 PLUGIN_CFGN_NormalVa(u32 k11BaseAlias, u32 alias)
{
    return CFGN_K11_VA_BASE + (alias - k11BaseAlias);
}

PLUGIN_CODE(CFGN) static u32 PLUGIN_CFGN_DecodeArmBl(u32 instructionVa, u32 instruction)
{
    if ((instruction & 0xFF000000u) != 0xEB000000u)
        return 0;

    s32 imm24 = (s32)(instruction << 8) >> 8;
    return (u32)((s32)(instructionVa + 8u) + imm24 * 4);
}

PLUGIN_CODE(CFGN) static u32 PLUGIN_CFGN_ReadLiteral(u32 instructionAlias, u32 instruction)
{
    if ((instruction & 0xFFFF0000u) != 0xE59F0000u &&
        (instruction & 0xFFFF0000u) != 0xE51F0000u)
        return 0;

    u32 literal = instructionAlias + 8u;
    u32 imm = instruction & 0xFFFu;
    if (instruction & (1u << 23))
        literal += imm;
    else
        literal -= imm;

    return *(volatile const u32 *)literal;
}

PLUGIN_CODE(CFGN) static u32 PLUGIN_CFGN_FindSkipCleanup(u32 sendSyncAlias, u32 k11BaseAlias)
{
    volatile const u32 *w = (volatile const u32 *)sendSyncAlias;
    u32 foundAlias = 0;

    /* Compiler-generated common tail for skip=true:
       release clientSession; if (!skip) call SendSyncRequest(handle);
       otherwise return res from [sp,#8]. */
    for (u32 i = 0; i < 0x500u / 4u; i++)
    {
        if (w[i + 0] != 0xE5943000u ||
            w[i + 1] != 0xE1A00004u ||
            w[i + 2] != 0xE5933010u ||
            w[i + 3] != 0xE12FFF33u ||
            w[i + 4] != 0xE3560000u ||
            (w[i + 5] & 0xFF000000u) != 0x0A000000u ||
            w[i + 6] != 0xE59D0008u ||
            w[i + 7] != 0xE28DD054u ||
            w[i + 8] != 0xE8BD8FF0u)
        {
            continue;
        }

        u32 alias = sendSyncAlias + i * 4u;
        if (foundAlias)
            return 0;
        foundAlias = alias;
    }

    return foundAlias ? PLUGIN_CFGN_NormalVa(k11BaseAlias, foundAlias) : 0;
}

PLUGIN_CODE(CFGN) static bool PLUGIN_CFGN_WriteOwnSlot(u32 *slot, u32 value)
{
    u32 alias = PLUGIN_CFGN_AliasOf(slot);
    if ((alias & CFGN_ALIAS_BIT) == 0)
        return false;

    *(volatile u32 *)alias = value;
    return true;
}

PLUGIN_CODE(CFGN) static bool PLUGIN_CFGN_Install(void)
{
    s64 model = 0;
    if (R_FAILED(PLUGIN_CFGN_svcGetSystemInfo(&model, 0x10000u, 0x201)))
        return false;

    bool isN3DS = model != 0;
    u32 systemTop = isN3DS ? CFGN_N3DS_SYSTEM_TOP : CFGN_O3DS_SYSTEM_TOP;
    u32 topAlias = CFGN_ALIAS_BIT | systemTop;

    /* K11 is packed immediately below the model-specific system-memory top.
       Find its page signature from there instead of embedding a build-specific
       SysPluginLoader_Main address. */
    u32 k11BaseAlias = PLUGIN_CFGN_FindK11Base(topAlias);
    if (!k11BaseAlias || k11BaseAlias >= topAlias)
        return false;

    u32 sendSyncAlias = PLUGIN_CFGN_FindSendSync(k11BaseAlias, topAlias);
    if (!sendSyncAlias)
        return false;

    volatile u32 *sendSync = (volatile u32 *)sendSyncAlias;
    volatile u32 *patch = 0;

    /* The first switch compare begins immediately after KClientSession validation. */
    for (u32 i = 32; i < 96; i++)
    {
        if (sendSync[i] == 0xE5983080u &&
            (sendSync[i + 1] & 0xFFFFF000u) == 0xE59F2000u &&
            sendSync[i + 2] == 0xE1530002u)
        {
            if (patch)
                return false;
            patch = &sendSync[i];
        }
    }

    if (!patch)
        return false;

    u32 switchR2 = PLUGIN_CFGN_ReadLiteral((u32)&patch[1], patch[1]);
    if (switchR2 != 0x000C0080u)
        return false;

    /* Decode SessionInfo_Lookup from one of the stock per-service lookup sites. */
    u32 lookupVa = 0;
    u32 sendSyncVa = PLUGIN_CFGN_NormalVa(k11BaseAlias, sendSyncAlias);
    for (u32 i = 0; i < 0x500u / 4u; i++)
    {
        if (sendSync[i] != 0xE5940014u ||
            (sendSync[i + 1] & 0xFF000000u) != 0xEB000000u ||
            sendSync[i + 2] != 0xE3500000u)
        {
            continue;
        }

        u32 candidate = PLUGIN_CFGN_DecodeArmBl(sendSyncVa + (i + 1u) * 4u, sendSync[i + 1]);
        if (candidate < CFGN_K11_VA_BASE || candidate >= CFGN_K11_VA_BASE + (topAlias - k11BaseAlias))
            continue;

        u32 candidateAlias = k11BaseAlias + (candidate - CFGN_K11_VA_BASE);
        volatile const u32 *candidateWords = (volatile const u32 *)candidateAlias;
        if (candidateWords[0] != 0xE92D4070u || candidateWords[1] != 0xE1A06000u)
            continue;

        if (lookupVa && lookupVa != candidate)
            return false;
        lookupVa = candidate;
    }

    if (!lookupVa)
        return false;

    u32 cleanupVa = PLUGIN_CFGN_FindSkipCleanup(sendSyncAlias, k11BaseAlias);
    if (!cleanupVa)
        return false;

    u32 patchAlias = (u32)patch;
    u32 resumeVa = PLUGIN_CFGN_NormalVa(k11BaseAlias, patchAlias + 8u);
    u32 hookAlias = PLUGIN_CFGN_AliasOf(PLUGIN_CFGN_KernelHook);
    if ((hookAlias & CFGN_ALIAS_BIT) == 0)
        return false;

    if (!PLUGIN_CFGN_WriteOwnSlot(&PLUGIN_CFGN_lookupVa, lookupVa) ||
        !PLUGIN_CFGN_WriteOwnSlot(&PLUGIN_CFGN_resumeVa, resumeVa) ||
        !PLUGIN_CFGN_WriteOwnSlot(&PLUGIN_CFGN_switchR2, switchR2) ||
        !PLUGIN_CFGN_WriteOwnSlot(&PLUGIN_CFGN_cleanupVa, cleanupVa))
    {
        return false;
    }

    /* Slots must be globally visible before K11 can ever enter the new stub. */
    PLUGIN_CFGN_svcFlushEntireDataCache();

    /* Exact two-word absolute jump.  We validated both overwritten semantics above. */
    patch[0] = 0xE51FF004u; /* ldr pc, [pc, #-4] */
    patch[1] = hookAlias;

    PLUGIN_CFGN_svcFlushEntireDataCache();
    PLUGIN_CFGN_svcInvalidateEntireInstructionCache();
    return true;
}

PLUGIN_MAIN(CFGN) bool PLUGIN_CFGN_Main(void)
{
    return PLUGIN_CFGN_Install();
}
