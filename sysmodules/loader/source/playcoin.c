#include <3ds.h>
#include "memory.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define COIN_FILE_BASE_SIZE              0x20u
#define COIN_FILE_EXTENSION_OFFSET       0x20u
#define COIN_FILE_EXTENSION_WORDS        7u
#define COIN_FILE_ACHIEVEMENT_OFFSET     0x40u
#define COIN_FILE_NO_ACHIEVEMENT_SIZE    0x40u
#define COIN_FILE_WITH_ACHIEVEMENT_SIZE  0x48u
#define COIN_FILE_EXTENSION_SEED         0x31445843u
#define COIN_EXT_BLACKJACK_COUNTERS_WORD 0u
#define COIN_EXT_BLACKJACK_SURPLUS_WORD  1u
#define COIN_EXT_TODAY_WORD              6u
#define COIN_BLACKJACK_COUNTER_MAX       30000u
#define COIN_ACHIEVEMENT_MASK            0x0003FFFFu

extern u32 coin_loader_home_patch;
extern u32 coin_loader_createcodeset_call;
extern u32 coin_loader_createprocess_call;
extern bool PLUGIN_coin_InstallHooks(void);
extern Result PLUGIN_coin_svcSendSyncRequest(Handle handle);

PLUGIN_DATA(coin) void *pluginTable_coin[] = {
    (void*)FSUSER_OpenArchive,
    (void*)FSUSER_CloseArchive,
    (void*)FSUSER_OpenFile,
    (void*)FSFILE_Read,
    (void*)FSFILE_Close,
    (void*)fsMakePath,
    (void*)&coin_loader_home_patch,
    (void*)&coin_loader_createcodeset_call,
    (void*)&coin_loader_createprocess_call,
    (void*)FSFILE_GetSize,
};

#define COIN_HOST__FSUSER_OpenArchive  ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))pluginTable_coin[0])
#define COIN_HOST__FSUSER_CloseArchive ((Result(*)(FS_Archive))pluginTable_coin[1])
#define COIN_HOST__FSUSER_OpenFile     ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))pluginTable_coin[2])
#define COIN_HOST__FSFILE_Read         ((Result(*)(Handle,u32*,u64,void*,u32))pluginTable_coin[3])
#define COIN_HOST__FSFILE_Close        ((Result(*)(Handle))pluginTable_coin[4])
#define COIN_HOST__fsMakePath          ((FS_Path(*)(FS_PathType,const void*))pluginTable_coin[5])
#define COIN_HOST__FSFILE_GetSize      ((Result(*)(Handle,u64*))pluginTable_coin[9])

PLUGIN_RODATA(coin) static const char g_coinFilePath[] = "/luma/coins.bin";
PLUGIN_RODATA(coin) static const char g_gameCoinPath[] = "/gamecoin.dat";
PLUGIN_RODATA(coin) static const u32 g_gameCoinArchivePath[3] = {
    MEDIATYPE_NAND,
    0xF000000Bu,
    0x00048000u,
};

PLUGIN_CODE(coin) static Result PLUGIN_coin_FSFILE_Write(
    Handle file,
    u32 *bytesWritten,
    u64 offset,
    const void *buffer,
    u32 size,
    u32 flags
)
{
    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    u32 *cmdbuf = (u32*)(tls + 0x80);

    cmdbuf[0] = 0x08030102;
    cmdbuf[1] = (u32)offset;
    cmdbuf[2] = (u32)(offset >> 32);
    cmdbuf[3] = size;
    cmdbuf[4] = flags;
    cmdbuf[5] = (size << 4) | 0xAu;
    cmdbuf[6] = (u32)buffer;

    Result res = PLUGIN_coin_svcSendSyncRequest(file);
    if (R_FAILED(res))
        return res;

    if (bytesWritten)
        *bytesWritten = cmdbuf[2];

    return (Result)cmdbuf[1];
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_Checksum(u32 value, u32 key)
{
    value ^= key;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_Decrypt(
    u32 enc,
    u32 chk,
    u32 key,
    u32 *outValue
)
{
    u32 value = (enc & 0xFFFF0000u) | ((enc - (chk >> 16)) & 0xFFFFu);
    value = value - chk * 2u - key;

    if (chk != PLUGIN_coin_Checksum(value, key))
        return false;

    *outValue = value ^ key;
    return true;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_SaturatingAdd(u32 left, u32 right)
{
    u32 sum = left + right;
    return sum < left ? 0xFFFFFFFFu : sum;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_ExtendedDataChecksum(
    const u32 *data,
    u32 key
)
{
    u32 checksum = key ^ COIN_FILE_EXTENSION_SEED;
    for (u32 i = 0; i < COIN_FILE_EXTENSION_WORDS; i++)
        checksum = PLUGIN_coin_Checksum(data[i] ^ i, checksum ^ key);
    return checksum;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_DecryptExtendedData(
    const u32 *stored,
    u32 key,
    u32 *data
)
{
    u32 stream = key ^ COIN_FILE_EXTENSION_SEED;
    for (u32 i = 0; i < COIN_FILE_EXTENSION_WORDS; i++)
    {
        stream = PLUGIN_coin_Checksum(stream ^ i, key);
        data[i] = stored[i] ^ stream;
    }

    return stored[COIN_FILE_EXTENSION_WORDS] ==
        PLUGIN_coin_ExtendedDataChecksum(data, key);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ValidateExtendedData(const u32 *data)
{
    u32 counters = data[COIN_EXT_BLACKJACK_COUNTERS_WORD];
    u32 deposited = counters & 0xFFFFu;
    u32 qualified = counters >> 16;

    return deposited <= COIN_BLACKJACK_COUNTER_MAX &&
        qualified <= deposited &&
        (data[COIN_EXT_TODAY_WORD] & 0xFFFF0000u) == 0;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_ReadBlackjackSurplus(
    Handle file,
    u64 fileSize,
    u32 key
)
{
    if (fileSize != COIN_FILE_NO_ACHIEVEMENT_SIZE &&
        fileSize != COIN_FILE_WITH_ACHIEVEMENT_SIZE)
    {
        return 0;
    }

    u32 stored[COIN_FILE_EXTENSION_WORDS + 1u];
    u32 data[COIN_FILE_EXTENSION_WORDS];
    u32 read = 0;
    if (R_FAILED(COIN_HOST__FSFILE_Read(
            file,
            &read,
            COIN_FILE_EXTENSION_OFFSET,
            stored,
            sizeof(stored))) ||
        read != sizeof(stored) ||
        !PLUGIN_coin_DecryptExtendedData(stored, key, data) ||
        !PLUGIN_coin_ValidateExtendedData(data))
    {
        return 0;
    }

    if (fileSize == COIN_FILE_WITH_ACHIEVEMENT_SIZE)
    {
        u32 achievement[2];
        u32 mask = 0;
        read = 0;
        if (R_FAILED(COIN_HOST__FSFILE_Read(
                file,
                &read,
                COIN_FILE_ACHIEVEMENT_OFFSET,
                achievement,
                sizeof(achievement))) ||
            read != sizeof(achievement) ||
            !PLUGIN_coin_Decrypt(
                achievement[0], achievement[1], key, &mask) ||
            (mask & ~COIN_ACHIEVEMENT_MASK))
        {
            return 0;
        }
    }

    return data[COIN_EXT_BLACKJACK_SURPLUS_WORD];
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_GetPlayCoins(u16 *out)
{
    if (!out)
        return (Result)-1;

    *out = 0;

    FS_Archive archive;
    Handle file;
    FS_Path pathData;

    pathData.type = PATH_BINARY;
    pathData.size = sizeof(g_gameCoinArchivePath);
    pathData.data = g_gameCoinArchivePath;

    Result res = COIN_HOST__FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, pathData);
    if (R_FAILED(res))
        return res;

    res = COIN_HOST__FSUSER_OpenFile(
        &file,
        archive,
        COIN_HOST__fsMakePath(PATH_ASCII, g_gameCoinPath),
        FS_OPEN_READ,
        0
    );
    if (R_FAILED(res))
    {
        COIN_HOST__FSUSER_CloseArchive(archive);
        return res;
    }

    u32 read;
    res = COIN_HOST__FSFILE_Read(file, &read, 4, out, sizeof(*out));
    if (R_SUCCEEDED(res) && read == sizeof(*out) && *out > 300)
        *out = 300;
    else if (R_FAILED(res) || read != sizeof(*out))
    {
        *out = 0;
        if (R_SUCCEEDED(res))
            res = (Result)-1;
    }

    COIN_HOST__FSFILE_Close(file);
    COIN_HOST__FSUSER_CloseArchive(archive);
    return res;
}

PLUGIN_CODE(coin) void PLUGIN_coin_ClearTransientHomePointer(void)
{
    FS_Archive sd;
    Handle file;
    u32 written;

    if (R_FAILED(COIN_HOST__FSUSER_OpenArchive(
            &sd,
            ARCHIVE_SDMC,
            COIN_HOST__fsMakePath(PATH_EMPTY, NULL))))
    {
        return;
    }

    // clear the old Home Menu pointer only on failed setup
    if (R_SUCCEEDED(COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_WRITE,
        0
    )))
    {
        u32 zero = 0;
        PLUGIN_coin_FSFILE_Write(file, &written, 4, &zero, sizeof(zero), 0);
        COIN_HOST__FSFILE_Close(file);
    }

    COIN_HOST__FSUSER_CloseArchive(sd);
}

PLUGIN_CODE(coin) bool PLUGIN_coin_InitializeHomeMenuState(
    u16 *coinDat,
    u32 *coinData,
    u16 *coinChange,
    u32 homePointer
)
{
    // grab vanilla coins before loading our extended state
    u16 systemCoins = 0;
    if (R_FAILED(PLUGIN_coin_GetPlayCoins(&systemCoins)))
        return false;
    *coinDat = systemCoins;

    FS_Archive sd;
    Handle file;
    Result rc;
    u32 written;

    rc = COIN_HOST__FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, COIN_HOST__fsMakePath(PATH_EMPTY, NULL));
    if (R_FAILED(rc))
        return false;

    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
        0
    );
    if (R_FAILED(rc))
    {
        COIN_HOST__FSUSER_CloseArchive(sd);
        return false;
    }

    // old 32-byte base state
    u32 base[8];
    volatile u32 *baseWords = base;
    for (u32 i = 0; i < 8u; i++)
        baseWords[i] = 0;

    u32 read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, 0, base, sizeof(base));
    bool baseReadSucceeded = R_SUCCEEDED(rc);
    u64 fileSize = 0;
    bool sizeValid = R_SUCCEEDED(COIN_HOST__FSFILE_GetSize(file, &fileSize));

    // wallet is standalone, bad tracking data shouldnt wipe it
    bool walletValid = baseReadSucceeded && read >= sizeof(u32) && base[0] <= 30000u;
    u32 coins;
    if (walletValid)
    {
        coins = base[0];
    }
    else
    {
        // bad wallet falls back to vanilla only
        coins = *coinDat;
        rc = PLUGIN_coin_FSFILE_Write(file, &written, 0, &coins, sizeof(coins), 0);
        if (R_FAILED(rc) || written != sizeof(coins))
        {
            COIN_HOST__FSFILE_Close(file);
            COIN_HOST__FSUSER_CloseArchive(sd);
            return false;
        }
    }

    // hand Home Menu its live pointer before releasing the file
    rc = PLUGIN_coin_FSFILE_Write(
        file,
        &written,
        4,
        &homePointer,
        sizeof(homePointer),
        0
    );
    if (R_FAILED(rc) || written != sizeof(homePointer))
    {
        COIN_HOST__FSFILE_Close(file);
        COIN_HOST__FSUSER_CloseArchive(sd);
        return false;
    }

    // bad/missing extension just means no Blackjack surplus yet
    u32 lifetimeEarned = 0;
    u32 lifetimeSpent = 0;
    bool baseValid = sizeValid && fileSize >= COIN_FILE_BASE_SIZE &&
        baseReadSucceeded && read == sizeof(base) &&
        walletValid && base[7] <= 30000u &&
        PLUGIN_coin_Decrypt(base[2], base[3], base[4], &lifetimeEarned) &&
        PLUGIN_coin_Decrypt(base[5], base[6], base[4], &lifetimeSpent);

    if (baseValid)
    {
        // wipe Loader's one-shot invalid-base marker
        coinChange[1] = 0;

        u32 blackjackSurplus = PLUGIN_coin_ReadBlackjackSurplus(
            file,
            fileSize,
            base[4]
        );
        u32 capacity = PLUGIN_coin_SaturatingAdd(lifetimeEarned, blackjackSurplus);
        if (lifetimeSpent > capacity)
            lifetimeSpent = capacity;

        coinData[2] = lifetimeEarned;
        coinData[3] = lifetimeSpent;
        coinData[1] = base[7];
        u32 available = capacity - lifetimeSpent;
        if (coinData[1] > available)
            coinData[1] = available;
        if (coinData[1] > 30000u)
            coinData[1] = 30000u;
    }
    else
    {
        // bad tracking state resets to vanilla, but keep a sane wallet
        coinData[2] = *coinDat;
        coinData[3] = 0;
        coinData[1] = coinData[2];

        // 0x8000 tells the first Home Menu pass to ignore fake historical spend
        // it cant clash with a real spend since tracked coins cap at 30000
        coinChange[1] = 0x8000u;
    }
    coinData[0] = coins;

    COIN_HOST__FSFILE_Close(file);
    COIN_HOST__FSUSER_CloseArchive(sd);

    // rebase vanilla coins around the extended wallet
    if (coins < 300)
        coins = *coinDat;
    else
        coins = (coins - 300) + *coinDat;

    if (coinData[0] > coins)
        *coinDat += (u16)(coinData[0] - coins);

    return true;
}

PLUGIN_MAIN(coin) bool PLUGIN_coin_Main(void)
{
    return PLUGIN_coin_InstallHooks();
}
