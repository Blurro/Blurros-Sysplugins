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

extern u32 coin_loader_home_patch;
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
};

#define COIN_HOST__FSUSER_OpenArchive  ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))pluginTable_coin[0])
#define COIN_HOST__FSUSER_CloseArchive ((Result(*)(FS_Archive))pluginTable_coin[1])
#define COIN_HOST__FSUSER_OpenFile     ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))pluginTable_coin[2])
#define COIN_HOST__FSFILE_Read         ((Result(*)(Handle,u32*,u64,void*,u32))pluginTable_coin[3])
#define COIN_HOST__FSFILE_Close        ((Result(*)(Handle))pluginTable_coin[4])
#define COIN_HOST__fsMakePath          ((FS_Path(*)(FS_PathType,const void*))pluginTable_coin[5])

PLUGIN_RODATA(coin) static const char g_coinFilePath[] = "/luma/coins.bin";
PLUGIN_RODATA(coin) static const char g_gameCoinPath[] = "/gamecoin.dat";
PLUGIN_RODATA(coin) static const u32 g_gameCoinArchivePath[3] = {
    MEDIATYPE_NAND,
    0xF000000Bu,
    0x00048000u,
};

PLUGIN_BSS(coin) static bool g_coinDecryptFailed;

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

PLUGIN_CODE(coin) static u32 PLUGIN_coin_Decrypt(u32 enc, u32 chk, u32 key)
{
    u32 value = (enc & 0xFFFF0000u) | ((enc - (chk >> 16)) & 0xFFFFu);
    value = value - chk * 2u - key;

    if (chk != PLUGIN_coin_Checksum(value, key))
    {
        g_coinDecryptFailed = true;
        return 0;
    }

    return value ^ key;
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

    // if the patch isn't applied, ensure coins.bin pointer states 0x0 for when rosalina checks it
    if (R_SUCCEEDED(COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_WRITE,
        0
    )))
    {
        u32 zero = 0;
        PLUGIN_coin_FSFILE_Write(file, &written, 4, &zero, sizeof(zero), FS_WRITE_FLUSH);
        COIN_HOST__FSFILE_Close(file);
    }

    COIN_HOST__FSUSER_CloseArchive(sd);
}

PLUGIN_CODE(coin) bool PLUGIN_coin_InitializeHomeMenuState(
    u16 *coinDat,
    u32 *coinData,
    u32 homePointer
)
{
    // fetch system coins & my coins
    u16 systemCoins = 0;
    if (R_FAILED(PLUGIN_coin_GetPlayCoins(&systemCoins)))
        return false;
    *coinDat = systemCoins;

    FS_Archive sd;
    Handle file;
    Result rc;
    u32 written;
    u32 coins;

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

    u32 read = 0;
    COIN_HOST__FSFILE_Read(file, &read, 0, &coins, sizeof(coins));
    if (read != sizeof(coins))
    {
        // file doesn't exist, create it with system value
        coins = *coinDat;
        rc = PLUGIN_coin_FSFILE_Write(file, &written, 0, &coins, sizeof(coins), FS_WRITE_FLUSH);
        if (R_FAILED(rc) || written != sizeof(coins))
        {
            COIN_HOST__FSFILE_Close(file);
            COIN_HOST__FSUSER_CloseArchive(sd);
            return false;
        }
    }

    // write the offset in homemenu that coin pointers are stored at
    rc = PLUGIN_coin_FSFILE_Write(
        file,
        &written,
        4,
        &homePointer,
        sizeof(homePointer),
        FS_WRITE_FLUSH
    );
    if (R_FAILED(rc) || written != sizeof(homePointer))
    {
        COIN_HOST__FSFILE_Close(file);
        COIN_HOST__FSUSER_CloseArchive(sd);
        return false;
    }

    coinData[0] = coins;

    // fetch recCoin, trueCoin, coins ever spent
    u32 data[5];
    u32 read2;
    rc = COIN_HOST__FSFILE_Read(file, &read2, 8, data, sizeof(data));

    if (R_SUCCEEDED(rc) && read2 == sizeof(data))
    {
        g_coinDecryptFailed = false;
        coinData[3] = PLUGIN_coin_Decrypt(data[3], data[4], data[2]);
        if (!g_coinDecryptFailed)
            coinData[2] = PLUGIN_coin_Decrypt(data[0], data[1], data[2]);

        if (g_coinDecryptFailed)
        {
            coinData[2] = *coinDat;
            coinData[3] = 0;
        }

        if (coinData[3] > coinData[2]) // shouldnt be possible but guard
            coinData[3] = coinData[2];

        // recommended coins update (unencrypted)
        u32 recommended;
        u32 read3;
        rc = COIN_HOST__FSFILE_Read(file, &read3, 28, &recommended, sizeof(recommended));

        if (R_SUCCEEDED(rc) && read3 == sizeof(recommended)) // gotta check read size or it 'successfully' returns rec[0] = 0 for some reason
        {
            coinData[1] = recommended;
            if (coinData[1] > coinData[2] - coinData[3])
                coinData[1] = coinData[2] - coinData[3];
        }
        else // file doesnt contain rec value
        {
            coinData[1] = coinData[2] - coinData[3];
        }
    }
    else
    {
        coinData[2] = *coinDat; // rosalina side handles file writing if not exist
        coinData[3] = 0;
        coinData[1] = coinData[2] - coinData[3];
    }

    COIN_HOST__FSFILE_Close(file);
    COIN_HOST__FSUSER_CloseArchive(sd);

    // prep coinsDat to increase by the amount coinsBin would've decreased by (makes homemenu hook set coinsSpent properly)
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