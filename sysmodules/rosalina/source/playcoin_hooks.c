#include <3ds.h>
#include "process_patches.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))

extern void *pluginTable_coin[];
extern u16 g_coinDat;
extern u32 g_coinData[4];
extern u16 g_coinChange[4];
extern u32 g_coinOffset;
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

PLUGIN_CODE(coin) static u32 PLUGIN_coin_Phys(const void *pointer)
{
    return COIN_HOST__svcConvertVAToPA(pointer, false) | 0x80000000u;
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

    if (loaderPointers[3] != 0x0061004Du ||
        loaderPointers[4] != 0x00650078u ||
        loaderPointers[5] != 0x00000064u)
    {
        COIN_HOST__svcCloseHandle(processHandle);
        return (Result)-1;
    }

    // copy Loader's temporary state into Rosalina-owned storage
    u16 *loaderDat = (u16*)loaderPointers[0];
    u32 *loaderData = (u32*)loaderPointers[1];
    u16 *loaderChange = (u16*)loaderPointers[2];

    g_coinDat = *loaderDat;
    g_coinData[0] = loaderData[0];
    g_coinData[1] = loaderData[1];
    g_coinData[2] = loaderData[2];
    g_coinData[3] = loaderData[3];
    g_coinChange[0] = loaderChange[0];

    volatile u32 *loaderDiagnostics = (volatile u32*)((u8*)loaderChange + 8);
    PLUGIN_coin_stepDiagnostics.valid = loaderDiagnostics[0];
    PLUGIN_coin_stepDiagnostics.liveTotal = loaderDiagnostics[1];
    PLUGIN_coin_stepDiagnostics.liveBoundary = loaderDiagnostics[2];
    PLUGIN_coin_stepDiagnostics.historyBoundary = loaderDiagnostics[3];
    PLUGIN_coin_stepDiagnostics.historyTotal = loaderDiagnostics[4];
    PLUGIN_coin_stepDiagnostics.coinsToday = loaderDiagnostics[5];

    // point Home Menu at Rosalina now that the state is copied
    *(u32*)g_coinOffset = PLUGIN_coin_Phys(&g_coinDat);
    *(u32*)(g_coinOffset + 4) = PLUGIN_coin_Phys(g_coinData);
    *(u32*)(g_coinOffset + 8) = PLUGIN_coin_Phys(g_coinChange);

    COIN_HOST__svcFlushEntireDataCache();
    COIN_HOST__svcInvalidateEntireInstructionCache();
    COIN_HOST__svcCloseHandle(processHandle);
    return 0;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_AttachHomeMenu(void)
{
    return R_SUCCEEDED(COIN_HOST__OperateOnProcessByName(
        g_coinMenuProcessName,
        PLUGIN_coin_PatchMenuCallback
    ));
}
