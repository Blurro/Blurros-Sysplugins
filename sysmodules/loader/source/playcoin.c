#include <3ds.h>
#include "memory.h"
#include "sysplugin_menu.h"

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
#define COIN_FILE_OLD_NO_ACHIEVEMENT_SIZE 0x40u
#define COIN_FILE_OLD_WITH_ACHIEVEMENT_SIZE 0x48u
#define COIN_FILE_ACCOUNTING_OFFSET      0x40u
#define COIN_FILE_ACHIEVEMENT_OFFSET     0x50u
#define COIN_FILE_NO_ACHIEVEMENT_SIZE    0x50u
#define COIN_FILE_WITH_ACHIEVEMENT_SIZE  0x58u
#define COIN_FILE_EXTENSION_SEED         0x31445843u
#define COIN_FILE_HOME_ACCOUNTING_SEED   0x314D4F48u
#define COIN_EXT_BLACKJACK_COUNTERS_WORD 0u
#define COIN_EXT_BLACKJACK_SURPLUS_WORD  1u
#define COIN_EXT_TODAY_WORD              6u
#define COIN_EXT_ACHIEVEMENTS_DISABLED   (1u << 31)
#define COIN_EXT_FLAT_WALLET             (1u << 30)
#define COIN_EXT_FLAT_CHECKPOINT_MASK    0x3FFF0000u
#define COIN_BLACKJACK_COUNTER_MAX       30000u
#define COIN_ACHIEVEMENT_MASK            0x0003FFFFu

extern Result PLUGIN_coin_svcSendSyncRequest(Handle handle);
extern bool PLUGIN_coin_PrepareHomeMenu(PluginMenuLoaderContext *context);
extern bool PLUGIN_coin_PrepareNewsList(PluginMenuLoaderContext *context);
extern bool PLUGIN_coin_PrepareSettings(PluginMenuLoaderContext *context);
extern void PLUGIN_coin_NewsListProcessCreated(PluginMenuLoaderContext *context);

PLUGIN_DATA(coin) void *pluginTable_coin[] = {
    (void*)FSUSER_OpenArchive,
    (void*)FSUSER_CloseArchive,
    (void*)FSUSER_OpenFile,
    (void*)FSFILE_Read,
    (void*)FSFILE_Close,
    (void*)fsMakePath,
    (void*)FSFILE_GetSize,
    (void*)PLUGIN_MENU_RegisterTitlePatch,
    (void*)PLUGIN_MENU_UnregisterTitlePatch,
    (void*)PLUGIN_MENU_RegisterHomePatch,
    (void*)PLUGIN_MENU_UnregisterHomePatch,
    (void*)PLUGIN_MENU_GetApiVersion,
    (void*)PLUGIN_MENU_MapPage,
    (void*)PLUGIN_MENU_UnmapPage,
    (void*)PLUGIN_MENU_TempAlloc,
    (void*)PLUGIN_MENU_TempFree,
};

#define COIN_HOST__FSUSER_OpenArchive  ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))pluginTable_coin[0])
#define COIN_HOST__FSUSER_CloseArchive ((Result(*)(FS_Archive))pluginTable_coin[1])
#define COIN_HOST__FSUSER_OpenFile     ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))pluginTable_coin[2])
#define COIN_HOST__FSFILE_Read         ((Result(*)(Handle,u32*,u64,void*,u32))pluginTable_coin[3])
#define COIN_HOST__FSFILE_Close        ((Result(*)(Handle))pluginTable_coin[4])
#define COIN_HOST__fsMakePath          ((FS_Path(*)(FS_PathType,const void*))pluginTable_coin[5])
#define COIN_HOST__FSFILE_GetSize      ((Result(*)(Handle,u64*))pluginTable_coin[6])
#define COIN_MENU__RegisterTitlePatch  ((bool(*)(PluginMenuLoaderTitlePatch*))pluginTable_coin[7])
#define COIN_MENU__UnregisterTitlePatch ((bool(*)(PluginMenuLoaderTitlePatch*))pluginTable_coin[8])
#define COIN_MENU__RegisterHomePatch   ((bool(*)(PluginMenuLoaderHomePatch*))pluginTable_coin[9])
#define COIN_MENU__UnregisterHomePatch ((bool(*)(PluginMenuLoaderHomePatch*))pluginTable_coin[10])
#define COIN_MENU__GetApiVersion       ((u32(*)(void))pluginTable_coin[11])
#define COIN_MENU__MapPage             ((bool(*)(Handle,u32,u32*,u32*))pluginTable_coin[12])
#define COIN_MENU__UnmapPage           ((void(*)(u32))pluginTable_coin[13])
#define COIN_MENU__TempAlloc           ((bool(*)(u32,u32*))pluginTable_coin[14])
#define COIN_MENU__TempFree            ((void(*)(u32,u32))pluginTable_coin[15])

PLUGIN_RODATA(coin) static const char g_coinFilePath[] = "/luma/coins.bin";
PLUGIN_RODATA(coin) static const char g_gameCoinPath[] = "/gamecoin.dat";
PLUGIN_RODATA(coin) static const u64 g_coinNewsListTitleIds[] = {
    0x0004003000008E02ULL,
    0x0004003000009702ULL,
    0x000400300000A002ULL,
    0x000400300000A802ULL,
    0x000400300000B002ULL,
    0x000400300000B802ULL,
};
PLUGIN_RODATA(coin) static const u64 g_coinSettingsTitleIds[] = {
    0x0004001000020000ULL, // Japan
    0x0004001000021000ULL, // USA
    0x0004001000022000ULL, // europe
    0x0004001000026000ULL, // China
    0x0004001000027000ULL, // Korea
    0x0004001000028000ULL, // Taiwan
};

PLUGIN_DATA(coin) static PluginMenuLoaderHomePatch g_coinHomePatch = {
    .prepare = PLUGIN_coin_PrepareHomeMenu,
    .next = NULL,
};

PLUGIN_DATA(coin) static PluginMenuLoaderTitlePatch g_coinNewsListPatch = {
    .titleIds = g_coinNewsListTitleIds,
    .titleIdCount = sizeof(g_coinNewsListTitleIds) / sizeof(g_coinNewsListTitleIds[0]),
    .prepare = PLUGIN_coin_PrepareNewsList,
    .processCreated = PLUGIN_coin_NewsListProcessCreated,
    .loaderFinished = NULL,
    .next = NULL,
    .menuEpoch = 0,
};
PLUGIN_DATA(coin) static PluginMenuLoaderTitlePatch g_coinSettingsPatch = {
    .titleIds = g_coinSettingsTitleIds,
    .titleIdCount = sizeof(g_coinSettingsTitleIds) / sizeof(g_coinSettingsTitleIds[0]),
    .prepare = PLUGIN_coin_PrepareSettings,
    .processCreated = NULL,
    .loaderFinished = NULL,
    .next = NULL,
    .menuEpoch = 0,
};
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

PLUGIN_CODE(coin) static Result PLUGIN_coin_FSFILE_SetSize(Handle file, u64 size)
{
    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    u32 *cmdbuf = (u32*)(tls + 0x80);

    cmdbuf[0] = 0x08050080u;
    cmdbuf[1] = (u32)size;
    cmdbuf[2] = (u32)(size >> 32);

    Result rc = PLUGIN_coin_svcSendSyncRequest(file);
    return R_FAILED(rc) ? rc : (Result)cmdbuf[1];
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

PLUGIN_CODE(coin) static u32 PLUGIN_coin_BlockChecksum(const u32 *data, u32 key)
{
    u32 sum = key ^ COIN_FILE_HOME_ACCOUNTING_SEED;
    for (u32 i = 0; i < 3u; i++)
        sum = PLUGIN_coin_Checksum(data[i] ^ i, sum ^ key);
    return sum;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_CalendarDate(u32 year, u32 month, u32 day)
{
    if (year < 2000u || year > 2127u || month < 1u || month > 12u || day < 1u)
        return false;
    u32 last = month == 2u ? 28u :
        (month == 4u || month == 6u || month == 9u || month == 11u ? 30u : 31u);
    if (month == 2u && !(year & 3u) && year != 2100u)
        last++;
    return day <= last;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_AccountingDate(u32 date)
{
    if (!date)
        return true; // HOME's unset date stays unset
    return PLUGIN_coin_CalendarDate(date & 0xFFFFu, (date >> 16) & 0xFFu, date >> 24);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_DecodeAccounting(
    const u32 *stored, u32 key, u32 *accounting)
{
    u32 data[3];
    u32 stream = key ^ COIN_FILE_HOME_ACCOUNTING_SEED;
    for (u32 i = 0; i < 3u; i++)
    {
        stream = PLUGIN_coin_Checksum(stream ^ i, key);
        data[i] = stored[i] ^ stream;
    }
    u32 packed = data[0] >> 16;
    u32 date = packed ? (2000u + (packed >> 9)) |
        (((packed >> 5) & 15u) << 16) | ((packed & 31u) << 24) : 0;
    if (stored[3] != PLUGIN_coin_BlockChecksum(data, key) ||
        !PLUGIN_coin_AccountingDate(date))
        return false;
    accounting[0] = 1;
    accounting[1] = data[0] & 0xFFFFu;
    accounting[2] = data[1];
    accounting[3] = data[2];
    accounting[4] = date;
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ImportAccounting(
    u32 *accounting, bool *ioFailed)
{
    *ioFailed = false;
    FS_Archive archive;
    FS_Path archivePath;
    archivePath.type = PATH_BINARY;
    archivePath.size = sizeof(g_gameCoinArchivePath);
    archivePath.data = g_gameCoinArchivePath;
    if (R_FAILED(COIN_HOST__FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, archivePath)))
    {
        *ioFailed = true;
        return false;
    }
    Handle file = 0;
    bool good = false;
    Result rc = COIN_HOST__FSUSER_OpenFile(&file, archive,
        COIN_HOST__fsMakePath(PATH_ASCII, g_gameCoinPath), FS_OPEN_READ, 0);
    if (R_SUCCEEDED(rc))
    {
        u32 record[5];
        u32 read = 0;
        rc = COIN_HOST__FSFILE_Read(file, &read, 0, record, sizeof(record));
        if (R_FAILED(rc))
            *ioFailed = true;
        if (!*ioFailed && read == sizeof(record) &&
            record[0] == 0x4F00u &&
            (record[1] & 0xFFFFu) <= 300u &&
            (record[1] >> 16) <= 11u &&
            PLUGIN_coin_AccountingDate(record[4]))
        {
            accounting[0] = 1;
            accounting[1] = (record[1] >> 16) > 10u ? 10u : (record[1] >> 16);
            accounting[2] = record[2];
            accounting[3] = record[3];
            accounting[4] = record[4];
            good = true;
        }
        if (R_FAILED(COIN_HOST__FSFILE_Close(file)))
            *ioFailed = true;
    }
    else
        *ioFailed = true;
    if (R_FAILED(COIN_HOST__FSUSER_CloseArchive(archive)))
        *ioFailed = true;
    return good && !*ioFailed;
}

// rewrite only the private 0x40..0x4f block
PLUGIN_CODE(coin) static bool PLUGIN_coin_WriteAccountingBlock(
    Handle file, u32 key, const u32 *accounting)
{
    u32 packed = 0;
    if (accounting[4])
        packed = ((accounting[4] & 0xFFFFu) - 2000u) << 9 |
            (((accounting[4] >> 16) & 0xFFu) << 5) | (accounting[4] >> 24);
    u32 data[3] = {accounting[1] | (packed << 16), accounting[2], accounting[3]};
    u32 stored[4];
    u32 stream = key ^ COIN_FILE_HOME_ACCOUNTING_SEED;
    for (u32 i = 0; i < 3u; i++)
    {
        stream = PLUGIN_coin_Checksum(stream ^ i, key);
        stored[i] = data[i] ^ stream;
    }
    stored[3] = PLUGIN_coin_BlockChecksum(data, key);
    u32 written = 0;
    return R_SUCCEEDED(PLUGIN_coin_FSFILE_Write(file, &written,
        COIN_FILE_ACCOUNTING_OFFSET, stored, sizeof(stored), 1u)) &&
        written == sizeof(stored);
}

// keep native step boundaries and use the verified Today bucket
PLUGIN_CODE(coin) static bool PLUGIN_coin_ResetAccounting(
    u32 *accounting, u32 today)
{
    bool ioFailed = false;
    if (!PLUGIN_coin_ImportAccounting(accounting, &ioFailed))
    {
        if (ioFailed)
            return false;
        accounting[0] = 1u;
        accounting[1] = 0u;
        accounting[2] = 0u;
        accounting[3] = 0u;
        accounting[4] = 0u;
    }
    accounting[1] = today > 0xFFFFu ? 0xFFFFu : today;
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ValidateExtendedData(const u32 *data)
{
    u32 counters = data[COIN_EXT_BLACKJACK_COUNTERS_WORD];
    u32 deposited = counters & 0xFFFFu;
    u32 qualified = counters >> 16;

    return deposited <= COIN_BLACKJACK_COUNTER_MAX &&
        qualified <= deposited &&
        (data[COIN_EXT_TODAY_WORD] &
            ~(0x0000FFFFu | COIN_EXT_ACHIEVEMENTS_DISABLED |
                COIN_EXT_FLAT_WALLET | COIN_EXT_FLAT_CHECKPOINT_MASK)) == 0;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ReadExtendedState(
    Handle file,
    u64 fileSize,
    u32 key,
    u32 *outBlackjackSurplus,
    bool *outAchievementsEnabled,
    u32 *outTodayBucket,
    bool *outFlatWallet,
    u32 *outFlatCheckpoint,
    u32 *outDayState,
    bool *ioFailed
)
{
    if (ioFailed)
        *ioFailed = false;
    if (!outBlackjackSurplus)
        return false;

    *outBlackjackSurplus = 0;
    if (outAchievementsEnabled)
        *outAchievementsEnabled = true;
    if (outTodayBucket)
        *outTodayBucket = 0;
    if (outFlatWallet)
        *outFlatWallet = false;
    if (outFlatCheckpoint)
        *outFlatCheckpoint = 0;
    if (outDayState)
        *outDayState = 0;

    // the extension has its own checksum, so a bad suffix doesn't break it
    if (fileSize < COIN_FILE_OLD_NO_ACHIEVEMENT_SIZE)
        return false;

    u32 stored[COIN_FILE_EXTENSION_WORDS + 1u];
    u32 data[COIN_FILE_EXTENSION_WORDS];
    u32 read = 0;
    Result rc = COIN_HOST__FSFILE_Read(file, &read,
        COIN_FILE_EXTENSION_OFFSET, stored, sizeof(stored));
    if (R_FAILED(rc) || read != sizeof(stored))
    {
        if (ioFailed)
            *ioFailed = true;
        return false;
    }
    if (!PLUGIN_coin_DecryptExtendedData(stored, key, data) ||
        !PLUGIN_coin_ValidateExtendedData(data))
    {
        return false;
    }

    *outBlackjackSurplus = data[COIN_EXT_BLACKJACK_SURPLUS_WORD];
    if (outTodayBucket)
        *outTodayBucket = (u16)data[COIN_EXT_TODAY_WORD];
    if (outFlatWallet)
        *outFlatWallet = (data[COIN_EXT_TODAY_WORD] & COIN_EXT_FLAT_WALLET) != 0;
    if (outFlatCheckpoint)
        *outFlatCheckpoint = (data[COIN_EXT_TODAY_WORD] &
            COIN_EXT_FLAT_CHECKPOINT_MASK) >> 16;
    if (outDayState)
        *outDayState = data[2];
    if (outAchievementsEnabled)
    {
        *outAchievementsEnabled =
            !(data[COIN_EXT_TODAY_WORD] & COIN_EXT_ACHIEVEMENTS_DISABLED);
    }
    return true;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_AchievementsEnabledForLoader(void)
{
    // missing/bad/old state keeps achievements on
    bool enabled = true;
    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
        return true;

    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_READ,
        0
    );
    if (R_SUCCEEDED(rc))
    {
        u64 fileSize = 0;
        u32 base[8];
        u32 read = 0;
        u32 lifetimeEarned = 0;
        u32 lifetimeSpent = 0;
        if (R_SUCCEEDED(COIN_HOST__FSFILE_GetSize(file, &fileSize)) &&
            R_SUCCEEDED(COIN_HOST__FSFILE_Read(
                file,
                &read,
                0,
                base,
                sizeof(base))) &&
            read == sizeof(base) &&
            base[0] <= 30000u &&
            base[7] <= 30000u &&
            PLUGIN_coin_Decrypt(base[2], base[3], base[4], &lifetimeEarned) &&
            PLUGIN_coin_Decrypt(base[5], base[6], base[4], &lifetimeSpent))
        {
            u32 blackjackSurplus = 0;
            (void)PLUGIN_coin_ReadExtendedState(
                file,
                fileSize,
                base[4],
                &blackjackSurplus,
                &enabled,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL
            );
        }
        COIN_HOST__FSFILE_Close(file);
    }

    COIN_HOST__FSUSER_CloseArchive(sd);
    return enabled;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_GetPlayCoins(
    u16 *out, u16 *outToday)
{
    if (!out || !outToday)
        return (Result)-1;

    *out = 0;
    *outToday = 0;

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

    // wallet and Today are adjacent halfwords, so read Today even if other native fields are bad
    u32 read = 0;
    u32 nativeWalletToday = 0;
    res = COIN_HOST__FSFILE_Read(file, &read, 4,
        &nativeWalletToday, sizeof(nativeWalletToday));
    if (R_SUCCEEDED(res) && read == sizeof(nativeWalletToday))
    {
        *out = (u16)nativeWalletToday;
        if (*out > 300u)
            *out = 300u;
        u32 today = nativeWalletToday >> 16;
        *outToday = (u16)(today > 10u ? 10u : today);
    }
    else
    {
        *out = 0;
        *outToday = 0;
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

    // clear the old HOME Menu pointer only on failed setup
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

PLUGIN_CODE(coin) static bool PLUGIN_coin_NormalizeCoinFile(
    Handle file, u64 *fileSize)
{
    u64 size = *fileSize;
    if (size == COIN_FILE_BASE_SIZE ||
        size == COIN_FILE_OLD_NO_ACHIEVEMENT_SIZE ||
        size == COIN_FILE_OLD_WITH_ACHIEVEMENT_SIZE ||
        size == COIN_FILE_NO_ACHIEVEMENT_SIZE ||
        size == COIN_FILE_WITH_ACHIEVEMENT_SIZE)
        return true;

    if (size > COIN_FILE_WITH_ACHIEVEMENT_SIZE)
    {
        if (R_FAILED(PLUGIN_coin_FSFILE_SetSize(file,
                COIN_FILE_WITH_ACHIEVEMENT_SIZE)))
            return false;
    }
    else
    {
        u8 zeros[COIN_FILE_WITH_ACHIEVEMENT_SIZE];
        volatile u8 *clear = zeros;
        for (u32 i = 0; i < sizeof(zeros); i++)
            clear[i] = 0;
        u32 written = 0;
        u32 length = COIN_FILE_WITH_ACHIEVEMENT_SIZE - (u32)size;
        if (R_FAILED(PLUGIN_coin_FSFILE_Write(file, &written, size,
                zeros, length, 1u)) || written != length)
            return false;
    }
    *fileSize = COIN_FILE_WITH_ACHIEVEMENT_SIZE;
    return true;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_InitializeHomeMenuState(
    u16 *coinDat,
    u32 *coinData,
    u16 *coinChange,
    u32 homePointer,
    u32 *accounting
)
{
    // grab vanilla coins before loading our extended state
    u16 systemCoins = 0;
    u16 nativeToday = 0;
    if (R_FAILED(PLUGIN_coin_GetPlayCoins(&systemCoins, &nativeToday)))
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

    u64 fileSize = 0;
    rc = COIN_HOST__FSFILE_GetSize(file, &fileSize);
    if (R_FAILED(rc))
    {
        COIN_HOST__FSFILE_Close(file);
        COIN_HOST__FSUSER_CloseArchive(sd);
        return false;
    }
    u64 originalSize = fileSize;
    if (!PLUGIN_coin_NormalizeCoinFile(file, &fileSize))
    {
        COIN_HOST__FSFILE_Close(file);
        COIN_HOST__FSUSER_CloseArchive(sd);
        return false;
    }

    u32 read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, 0, base, sizeof(base));
    if (R_FAILED(rc) || read != sizeof(base))
    {
        COIN_HOST__FSFILE_Close(file);
        COIN_HOST__FSUSER_CloseArchive(sd);
        return false;
    }

    // wallet is standalone, bad tracking data shouldnt wipe it
    bool walletValid = originalSize >= sizeof(u32) && base[0] <= 30000u;
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

    // hand HOME Menu its live pointer before releasing the file
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
    bool flatWallet = false;
    u32 flatCheckpoint = 0;
    u32 dayState = 0;
    bool baseValid = originalSize >= COIN_FILE_BASE_SIZE &&
        walletValid && base[7] <= 30000u &&
        PLUGIN_coin_Decrypt(base[2], base[3], base[4], &lifetimeEarned) &&
        PLUGIN_coin_Decrypt(base[5], base[6], base[4], &lifetimeSpent);
    if (baseValid)
    {
        u32 blackjackSurplus = 0;
        u32 todayBucket = 0;
        bool extensionIoFailed = false;
        bool extensionValid = PLUGIN_coin_ReadExtendedState(file, fileSize, base[4],
            &blackjackSurplus, NULL, &todayBucket,
            &flatWallet, &flatCheckpoint, &dayState,
            &extensionIoFailed);
        if (extensionIoFailed)
        {
            COIN_HOST__FSFILE_Close(file);
            COIN_HOST__FSUSER_CloseArchive(sd);
            return false;
        }
        volatile u64 accountingSize = fileSize;
        if (accountingSize >= COIN_FILE_NO_ACHIEVEMENT_SIZE)
        {
            u32 stored[4];
            u32 accountRead = 0;
            Result accountRc = COIN_HOST__FSFILE_Read(file, &accountRead,
                COIN_FILE_ACCOUNTING_OFFSET, stored, sizeof(stored));
            if (R_FAILED(accountRc))
            {
                // SD read failure isn't a checksum failure
                COIN_HOST__FSFILE_Close(file);
                COIN_HOST__FSUSER_CloseArchive(sd);
                return false;
            }
            if (accountRead != sizeof(stored) ||
                !PLUGIN_coin_DecodeAccounting(stored, base[4], accounting))
            {
                // recover this block from the verified Today bucket
                if (!PLUGIN_coin_ResetAccounting(accounting,
                        extensionValid ? todayBucket : 0u) ||
                    !PLUGIN_coin_WriteAccountingBlock(file, base[4], accounting))
                {
                    COIN_HOST__FSFILE_Close(file);
                    COIN_HOST__FSUSER_CloseArchive(sd);
                    return false;
                }
            }
        }
        else if (accountingSize == COIN_FILE_BASE_SIZE ||
            accountingSize == COIN_FILE_OLD_NO_ACHIEVEMENT_SIZE ||
            accountingSize == COIN_FILE_OLD_WITH_ACHIEVEMENT_SIZE)
        {
            if (!PLUGIN_coin_ResetAccounting(accounting,
                    extensionValid ? todayBucket : 0u))
            {
                COIN_HOST__FSFILE_Close(file);
                COIN_HOST__FSUSER_CloseArchive(sd);
                return false;
            }
        }
        else
        {
            // a short or unknown suffix needs a fresh private block
            if (!PLUGIN_coin_ResetAccounting(accounting,
                    extensionValid ? todayBucket : 0u) ||
                !PLUGIN_coin_WriteAccountingBlock(file, base[4], accounting))
            {
                COIN_HOST__FSFILE_Close(file);
                COIN_HOST__FSUSER_CloseArchive(sd);
                return false;
            }
        }
        // wipe Loader's one-shot invalid-base marker
        coinChange[1] = 0;

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
        u32 todayBucket = 0;
        bool extensionValid = false;
        u32 ignoredSurplus = 0;
        bool extensionIoFailed = false;
        extensionValid = PLUGIN_coin_ReadExtendedState(file, fileSize, base[4],
            &ignoredSurplus, NULL, &todayBucket,
            &flatWallet, &flatCheckpoint, &dayState,
            &extensionIoFailed);
        if (extensionIoFailed)
        {
            COIN_HOST__FSFILE_Close(file);
            COIN_HOST__FSUSER_CloseArchive(sd);
            return false;
        }

        // repair missing lifetime tracking from native data, with Today capped at 10
        bool privateValid = false;
        if (originalSize >= COIN_FILE_NO_ACHIEVEMENT_SIZE)
        {
            u32 stored[4];
            u32 accountRead = 0;
            rc = COIN_HOST__FSFILE_Read(file, &accountRead,
                COIN_FILE_ACCOUNTING_OFFSET, stored, sizeof(stored));
            if (R_FAILED(rc) || accountRead != sizeof(stored))
            {
                COIN_HOST__FSFILE_Close(file);
                COIN_HOST__FSUSER_CloseArchive(sd);
                return false;
            }
            privateValid = PLUGIN_coin_DecodeAccounting(stored, base[4], accounting);
        }
        if (!privateValid && !PLUGIN_coin_ResetAccounting(accounting,
                extensionValid ? todayBucket : nativeToday))
        {
            COIN_HOST__FSFILE_Close(file);
            COIN_HOST__FSUSER_CloseArchive(sd);
            return false;
        }

        // reset bad tracking from native, keep the wallet
        coinData[2] = *coinDat;
        coinData[3] = 0;
        coinData[1] = coinData[2];

        // 0x8000 skips fake past spending on first HOME pass, since real spends cap at 30k
        coinChange[1] = 0x8000u;
    }
    coinData[0] = coins;
    coinChange[3] = flatWallet ? 0u : 1u;
    accounting[12] = flatCheckpoint |
        (flatWallet ? COIN_EXT_FLAT_WALLET : 0u);
    accounting[16] = dayState;
    // fresh and legacy files wait for the first full save and native lock
    if (originalSize < COIN_FILE_NO_ACHIEVEMENT_SIZE)
        accounting[0] = 0;

    COIN_HOST__FSFILE_Close(file);
    COIN_HOST__FSUSER_CloseArchive(sd);

    // rebase vanilla coins around the extended wallet
    if (coins < 300)
        coins = *coinDat;
    else
        coins = (coins - 300) + *coinDat;

    if (coinData[0] > coins)
        *coinDat += (u16)(coinData[0] - coins);

    if (accounting[0] == 1u)
    {
        // after reboot use the trusted wallet baseline, since native may include fake awards
        *coinDat = (u16)(coinData[0] > 300u ? 300u : coinData[0]);
    }

    return true;
}

extern volatile u32 *PLUGIN_coin_HomeHandoff(void);
extern u32 PLUGIN_coin_HomePtmHistoryFunction(void);

PLUGIN_CODE(coin) static bool PLUGIN_coin_CatchupLeapYear(u32 year)
{
    return year % 4u == 0u && (year % 100u != 0u || year % 400u == 0u);
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_CatchupOrdinal(u32 date)
{
    u32 year = date & 0xFFFFu;
    u32 month = (date >> 16) & 0xFFu;
    u32 day = date >> 24;
    if (year < 2000u || year > 2127u || month < 1u || month > 12u)
        return 0;
    u32 limit = 31u;
    if (month == 2u)
        limit = 28u + (PLUGIN_coin_CatchupLeapYear(year) ? 1u : 0u);
    else if (month == 4u || month == 6u || month == 9u || month == 11u)
        limit = 30u;
    if (!day || day > limit)
        return 0;

    u32 ordinal = 1u;
    for (u32 y = 1900u; y < year; y++)
        ordinal += PLUGIN_coin_CatchupLeapYear(y) ? 366u : 365u;
    for (u32 m = 1u; m < month; m++)
    {
        if (m == 2u)
            ordinal += PLUGIN_coin_CatchupLeapYear(year) ? 29u : 28u;
        else
            ordinal += m == 4u || m == 6u || m == 9u || m == 11u ? 30u : 31u;
    }
    return ordinal + day - 1u;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_QueryHomeCatchup(void)
{
    volatile u32 *state = PLUGIN_coin_HomeHandoff();
    u32 queryAddress = PLUGIN_coin_HomePtmHistoryFunction();
    if (state[24] != 1u || !queryAddress ||
        state[5] != state[28] ||
        (state[12] != state[34] && state[11] != state[28]))
        return;

    u16 history[24];
    volatile u16 *clear = history;
    for (u32 i = 0; i < 24u; i++)
        clear[i] = 0;
    Result (*query)(u16 *, u32, u32, u32) =
        (Result (*)(u16 *, u32, u32, u32))queryAddress;
    Result rc = query(history, 24u, state[25], state[26]);
    if (R_FAILED(rc))
        return;

    u32 total = 0;
    for (u32 i = 0; i < 24u; i++)
        total += history[i];
    u32 boundary = state[29];
    if (boundary > total)
    {
        state[24] = 0;
        return;
    }

    u32 steps = total - boundary;
    u32 oldToday = state[30];
    u32 shadow = 0;
    while (oldToday + shadow < 0xFFFFu)
    {
        u32 count = oldToday + shadow;
        u32 cost = count < 10u ? 100u : 100u + 3u * (count - 9u);
        if (steps < cost)
            break;
        steps -= cost;
        shadow++;
    }

    u32 wallet = shadow;
    if (state[33] & 0x40000000u)
    {
        u32 code = state[33] & 0x3FFFu;
        if (code)
        {
            s64 flatBoundary = (s64)boundary + (s32)code - 8192;
            if (flatBoundary >= 0 && flatBoundary <= total)
                wallet = (total - (u32)flatBoundary) / 100u;
        }
    }

    state[31] = shadow;
    state[32] = wallet;
    __dmb();
    state[24] = 2u;
}

PLUGIN_CODE(coin) void PLUGIN_coin_PrepareHomeCatchup(
    const u8 *record, u32 oldTimeLow, u32 oldTimeHigh)
{
    volatile u32 *state = PLUGIN_coin_HomeHandoff();
    if ((state[24] == 1u || state[24] == 2u) &&
        state[5] != state[28])
        state[24] = 0u;
    if (state[24] || !record || state[7] != 1u || state[13] ||
        state[14] || *(const u32 *)record != 0x4F00u)
        return;

    u32 oldDate = *(const u32 *)(record + 16);
    u32 newDate = state[5];
    u32 oldOrdinal = PLUGIN_coin_CatchupOrdinal(oldDate);
    if (!oldOrdinal || PLUGIN_coin_CatchupOrdinal(newDate) != oldOrdinal + 1u ||
        state[11] != oldDate || state[8] != *(const u16 *)(record + 6) ||
        state[10] != *(const u32 *)(record + 12) ||
        (state[23] & 0x80000000u) ||
        (state[23] & 0x7FFFFFFFu) != oldOrdinal)
        return;

    state[25] = oldTimeLow;
    state[26] = oldTimeHigh;
    state[27] = oldDate;
    state[28] = newDate;
    state[29] = *(const u32 *)(record + 12);
    state[30] = *(const u16 *)(record + 6);
    state[33] = state[19];
    state[34] = state[12];
    state[24] = 1u;
    PLUGIN_coin_QueryHomeCatchup();
}

PLUGIN_CODE(coin) void PLUGIN_coin_RetryHomeCatchup(void)
{
    volatile u32 *state = PLUGIN_coin_HomeHandoff();
    if ((state[24] == 1u || state[24] == 2u) &&
        state[5] != state[28])
    {
        state[24] = 0u;
        return;
    }
    PLUGIN_coin_QueryHomeCatchup();
}

PLUGIN_CODE(coin) void PLUGIN_coin_ApplyHomeCatchup(
    u8 *record, u32 *wallet, u16 *nativeWallet, u16 *earned)
{
    volatile u32 *state = PLUGIN_coin_HomeHandoff();
    if (state[24] != 2u || !record || !wallet || !nativeWallet || !earned ||
        (state[12] != state[34] && state[11] != state[28]) ||
        *(u32 *)(record + 16) != state[28] ||
        *wallet > 30000u)
        return;

    state[34] = state[12];
    state[36] = *wallet;
    state[37] = *earned;
    state[38] = *nativeWallet;
    state[39] = (u32)wallet;
    state[40] = (u32)nativeWallet;
    state[41] = (u32)earned;
    state[35] = (u32)record;
    u32 room = 30000u - *wallet;
    u32 credited = state[32] < room ? state[32] : room;
    state[33] = credited;
    *wallet += credited;
    *earned = (u16)(*earned + state[31]);
    *nativeWallet = (u16)(*wallet > 300u ? 300u : *wallet);
    *(u16 *)(record + 4) = *nativeWallet;
    __dmb();
    state[24] = 3u;
}

PLUGIN_CODE(coin) void PLUGIN_coin_FinishHomeCatchupSave(u8 *record, Result result)
{
    volatile u32 *state = PLUGIN_coin_HomeHandoff();
    if (state[24] != 3u || state[35] != (u32)record)
        return;

    if (R_FAILED(result))
    {
        *(u32 *)state[39] = state[36];
        *(u16 *)state[40] = (u16)state[38];
        *(u16 *)state[41] = (u16)state[37];
        *(u16 *)(record + 4) = (u16)state[38];
        state[33] = 0;
        __dmb();
        state[24] = 2u;
        return;
    }

    if (state[12] == state[34] + 1u && state[11] == state[28])
    {
        __dmb();
        state[24] = 4u;
    }
}

PLUGIN_MAIN(coin) bool PLUGIN_coin_Main(void)
{
    if (!COIN_MENU__GetApiVersion)
        return false;

    u32 menuApiVersion = COIN_MENU__GetApiVersion();
    if (SYSPLUGIN_MENU_API_LOADER_REVISION(menuApiVersion) < SYSPLUGIN_MENU_LOADER_API_REVISION ||
        SYSPLUGIN_MENU_API_PUBLIC_REVISION(menuApiVersion) < SYSPLUGIN_MENU_PUBLIC_API_REVISION ||
        !COIN_MENU__RegisterTitlePatch || !COIN_MENU__UnregisterTitlePatch ||
        !COIN_MENU__RegisterHomePatch || !COIN_MENU__UnregisterHomePatch ||
        !COIN_MENU__MapPage || !COIN_MENU__UnmapPage ||
        !COIN_MENU__TempAlloc || !COIN_MENU__TempFree)
    {
        return false;
    }

    if (!COIN_MENU__RegisterHomePatch(&g_coinHomePatch))
        return false;

    if (!COIN_MENU__RegisterTitlePatch(&g_coinNewsListPatch))
    {
        (void)COIN_MENU__UnregisterHomePatch(&g_coinHomePatch);
        return false;
    }

    if (!COIN_MENU__RegisterTitlePatch(&g_coinSettingsPatch))
    {
        (void)COIN_MENU__UnregisterTitlePatch(&g_coinNewsListPatch);
        (void)COIN_MENU__UnregisterHomePatch(&g_coinHomePatch);
        return false;
    }

    return true;
}
