#include <3ds.h>
#include "process_patches.h"
#include "csvc.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))

extern void *pluginTable_coin[];
extern u16 g_coinDat;
extern u32 g_coinData[4];
extern u16 g_coinChange[4];
extern u32 g_coinProgressiveEarnedThisCalc;
extern u32 g_coinCalculationValidity;
extern u32 g_coinInvalidBasePending;
extern volatile u32 g_coinHistoryQueryTime[2];
extern u32 g_coinOffset;
extern u16 g_coinEarnedAppliedCounter;
extern const char g_coinMenuProcessName[];

typedef struct
{
    u32 valid;
    u32 liveTotal;
    u32 liveBoundary;
    u32 historyBoundary;
    u32 historyTotal;
    u32 coinsToday;
} CoinStepDiagnostics;

extern volatile CoinStepDiagnostics PLUGIN_coin_stepDiagnostics;

#define COIN_HOST__OperateOnProcessByName ((Result(*)(const char*,OperateOnProcessCb))pluginTable_coin[1])
#define COIN_HOST__svcConvertVAToPA ((u32(*)(const void*,bool))pluginTable_coin[10])
#define COIN_HOST__svcFlushEntireDataCache ((void(*)(void))pluginTable_coin[11])
#define COIN_HOST__svcInvalidateEntireInstructionCache ((void(*)(void))pluginTable_coin[12])
#define COIN_HOST__svcCloseHandle ((Result(*)(Handle))pluginTable_coin[23])
#define COIN_HOST__svcControlProcess ((Result(*)(Handle,ProcessOp,u32,u32))pluginTable_coin[41])
#define COIN_HOST__svcSleepThread ((void(*)(s64))pluginTable_coin[42])
#define COIN_HOST__OpenProcessByName ((Result(*)(const char*,Handle*))pluginTable_coin[43])

#define COIN_HANDOFF_RETRY_COUNT 100u
#define COIN_HANDOFF_RETRY_NS    (1 * 1000 * 1000LL)

PLUGIN_DATA(coin) static volatile u32 *g_coinHandoffControl = NULL;

PLUGIN_CODE(coin) static u32 PLUGIN_coin_Phys(const void *pointer)
{
    return COIN_HOST__svcConvertVAToPA(pointer, false) | 0x80000000u;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_UnlockProcessThreads(Handle processHandle)
{
    Result rc = COIN_HOST__svcControlProcess(
        processHandle,
        PROCESSOP_SCHEDULE_THREADS,
        0,
        0
    );
    if (R_FAILED(rc))
    {
        rc = COIN_HOST__svcControlProcess(
            processHandle,
            PROCESSOP_SCHEDULE_THREADS,
            0,
            0
        );
    }
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_LockProcessAtCoinBoundary(
    Handle processHandle,
    volatile u32 *handoffControl,
    bool *threadsLocked
)
{
    *threadsLocked = false;
    for (u32 attempt = 0; attempt < COIN_HANDOFF_RETRY_COUNT; attempt++)
    {
        Result rc = COIN_HOST__svcControlProcess(
            processHandle,
            PROCESSOP_SCHEDULE_THREADS,
            1,
            0
        );
        if (R_FAILED(rc))
            return rc;
        *threadsLocked = true;

        __dmb();
        if (*handoffControl == 0)
            return 0;

        rc = PLUGIN_coin_UnlockProcessThreads(processHandle);
        if (R_FAILED(rc))
            return rc;
        *threadsLocked = false;
        COIN_HOST__svcSleepThread(COIN_HANDOFF_RETRY_NS);
    }

    return (Result)-1;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_PatchMenuCallback(
    Handle processHandle,
    u32 textSize,
    u32 roSize,
    u32 rwSize
)
{
    (void)textSize;
    (void)roSize;
    (void)rwSize;

    u32 *loaderPointers = (u32*)g_coinOffset;
    Result rc = 0;
    bool threadsLocked = false;

    if (loaderPointers[3] != 0x0061004Du ||
        loaderPointers[4] != 0x00650078u ||
        loaderPointers[5] != 0x00000064u ||
        loaderPointers[8] != 0xEA000001u ||
        loaderPointers[10] != 0xE1A00000u ||
        (loaderPointers[9] & 0x80000003u) != 0x80000000u)
    {
        rc = (Result)-1;
        goto cleanup;
    }

    volatile u32 *handoffControl = (volatile u32*)loaderPointers[9];
    rc = PLUGIN_coin_LockProcessAtCoinBoundary(
        processHandle,
        handoffControl,
        &threadsLocked
    );
    if (R_FAILED(rc))
        goto cleanup;

    u16 *loaderDat = (u16*)loaderPointers[0];
    u32 *loaderData = (u32*)loaderPointers[1];
    u16 *loaderChange = (u16*)loaderPointers[2];

    g_coinDat = *loaderDat;
    g_coinData[0] = loaderData[0];
    g_coinData[1] = loaderData[1];
    g_coinData[2] = loaderData[2];
    g_coinData[3] = loaderData[3];
    g_coinChange[0] = loaderChange[0];
    g_coinEarnedAppliedCounter = 0;
    g_coinChange[1] = 0;
    g_coinChange[2] = 0;
    g_coinProgressiveEarnedThisCalc = 0;
    g_coinCalculationValidity = 0;
    g_coinInvalidBasePending = (loaderChange[1] & 0x8000u) != 0;

    volatile u32 *loaderDiagnostics = (volatile u32*)((u8*)loaderChange + 8);
    PLUGIN_coin_stepDiagnostics.valid = loaderDiagnostics[0];
    PLUGIN_coin_stepDiagnostics.liveTotal = loaderDiagnostics[1];
    PLUGIN_coin_stepDiagnostics.liveBoundary = loaderDiagnostics[2];
    PLUGIN_coin_stepDiagnostics.historyBoundary = loaderDiagnostics[3];
    PLUGIN_coin_stepDiagnostics.historyTotal = loaderDiagnostics[4];
    PLUGIN_coin_stepDiagnostics.coinsToday = loaderDiagnostics[5];
    g_coinHistoryQueryTime[0] = loaderDiagnostics[13];
    g_coinHistoryQueryTime[1] = loaderDiagnostics[14];

    *(u32*)g_coinOffset = PLUGIN_coin_Phys(&g_coinDat);
    *(u32*)(g_coinOffset + 4) = PLUGIN_coin_Phys(g_coinData);
    *(u32*)(g_coinOffset + 8) = PLUGIN_coin_Phys(g_coinChange);

    COIN_HOST__svcFlushEntireDataCache();
    COIN_HOST__svcInvalidateEntireInstructionCache();
    g_coinHandoffControl = handoffControl;

cleanup:
    if (threadsLocked)
    {
        Result unlockRc = PLUGIN_coin_UnlockProcessThreads(processHandle);
        if (R_SUCCEEDED(rc) && R_FAILED(unlockRc))
            rc = unlockRc;
    }
    COIN_HOST__svcCloseHandle(processHandle);
    return rc;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_AttachHomeMenu(void)
{
    return R_SUCCEEDED(COIN_HOST__OperateOnProcessByName(
        g_coinMenuProcessName,
        PLUGIN_coin_PatchMenuCallback
    ));
}

PLUGIN_CODE(coin) u32 PLUGIN_coin_RtcDayDecision(void)
{
    if (!g_coinHandoffControl)
        return 0;

    __dmb();
    return g_coinHandoffControl[2];
}

PLUGIN_CODE(coin) bool PLUGIN_coin_RtcDayDecisionPending(void)
{
    return PLUGIN_coin_RtcDayDecision() != 0u;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_RtcExactNextDayPending(void)
{
    return PLUGIN_coin_RtcDayDecision() == 1u;
}

PLUGIN_CODE(coin) u32 PLUGIN_coin_RtcPreviousProgressiveRemainder(void)
{
    if (!g_coinHandoffControl || PLUGIN_coin_RtcDayDecision() != 1u)
        return 0;

    __dmb();
    return g_coinHandoffControl[3];
}

PLUGIN_CODE(coin) u32 PLUGIN_coin_RtcDecisionCalendarStamp(void)
{
    if (!g_coinHandoffControl)
        return 0;

    __dmb();
    return g_coinHandoffControl[4];
}

PLUGIN_CODE(coin) void PLUGIN_coin_ConsumeRtcDayDecision(void)
{
    if (!g_coinHandoffControl)
        return;

    g_coinHandoffControl[2] = 0;
    g_coinHandoffControl[3] = 0;
    g_coinHandoffControl[4] = 0;
    __dmb();
}

PLUGIN_CODE(coin) bool PLUGIN_coin_LockHomeState(Handle *processHandleOut)
{
    if (!processHandleOut || !g_coinHandoffControl)
        return false;

    Handle processHandle = 0;
    Result rc = COIN_HOST__OpenProcessByName(
        g_coinMenuProcessName,
        &processHandle
    );
    if (R_FAILED(rc))
        return false;

    bool threadsLocked = false;
    rc = PLUGIN_coin_LockProcessAtCoinBoundary(
        processHandle,
        g_coinHandoffControl,
        &threadsLocked
    );
    if (R_FAILED(rc))
    {
        if (threadsLocked)
            (void)PLUGIN_coin_UnlockProcessThreads(processHandle);
        COIN_HOST__svcCloseHandle(processHandle);
        return false;
    }

    *processHandleOut = processHandle;
    return true;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_UnlockHomeState(Handle processHandle)
{
    if (!processHandle)
        return false;

    COIN_HOST__svcFlushEntireDataCache();
    __dmb();
    Result rc = PLUGIN_coin_UnlockProcessThreads(processHandle);
    COIN_HOST__svcCloseHandle(processHandle);
    return R_SUCCEEDED(rc);
}
