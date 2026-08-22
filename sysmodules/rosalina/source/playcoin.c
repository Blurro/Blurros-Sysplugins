#include <3ds.h>
#include "draw.h"
#include "menu.h"
#include "process_patches.h"
#include "menus/miscellaneous.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

typedef void (*BlurTickFunc)(u64 delta);

typedef struct BlurFeatureRegistration
{
    u32 pluginId;
    const char *title;
    void (*callback)(void);
    struct BlurFeatureRegistration *next;
} BlurFeatureRegistration;

typedef struct
{
    u32 valid;
    u32 liveTotal;
    u32 liveBoundary;
    u32 historyBoundary;
    u32 historyTotal;
    u32 coinsToday;
} CoinStepDiagnostics;

typedef struct PluginMenuRegistration
{
    u32 pluginId;
    const char *title;
    void (*callback)(void);
    u32 color;
    struct PluginMenuRegistration *next;
} PluginMenuRegistration;

extern bool PLUGIN_blur_AddTickFunc(BlurTickFunc func, s64 intervalNs);
extern bool PLUGIN_blur_AddFeatureItem(
    BlurFeatureRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void)
);
extern void PLUGIN_blur_DrawFeatureFrame(const char *title);
extern bool PLUGIN_MENU_AddItem(
    PluginMenuRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void),
    u32 color
);

PLUGIN_DATA(coin) void *pluginTable_coin[] = {
    (void*)PLUGIN_blur_AddTickFunc,
    (void*)OperateOnProcessByName,
    (void*)FSUSER_OpenArchive,
    (void*)FSUSER_CloseArchive,
    (void*)FSUSER_OpenFile,
    (void*)FSUSER_CreateDirectory,
    (void*)FSFILE_Read,
    (void*)FSFILE_Write,
    (void*)FSFILE_Close,
    (void*)fsMakePath,
    (void*)svcConvertVAToPA,
    (void*)svcFlushEntireDataCache,
    (void*)svcInvalidateEntireInstructionCache,
    (void*)Draw_Lock,
    (void*)Draw_Unlock,
    (void*)Draw_ClearFramebuffer,
    (void*)Draw_DrawString,
    (void*)Draw_DrawFormattedString,
    (void*)Draw_FlushFramebuffer,
    (void*)waitInputWithTimeout,
    (void*)&menuShouldExit,
    (void*)srvGetServiceHandle,
    (void*)svcSendSyncRequest,
    (void*)svcCloseHandle,
    (void*)PLUGIN_MENU_AddItem,
    (void*)&miscellaneousMenu,
    (void*)PLUGIN_blur_AddFeatureItem,
    (void*)PLUGIN_blur_DrawFeatureFrame,
};

#define COIN_BLUR__AddTickFunc            ((bool(*)(BlurTickFunc,s64))pluginTable_coin[0])
#define COIN_HOST__FSUSER_OpenArchive     ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))pluginTable_coin[2])
#define COIN_HOST__FSUSER_CloseArchive    ((Result(*)(FS_Archive))pluginTable_coin[3])
#define COIN_HOST__FSUSER_OpenFile        ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))pluginTable_coin[4])
#define COIN_HOST__FSUSER_CreateDirectory ((Result(*)(FS_Archive,FS_Path,u32))pluginTable_coin[5])
#define COIN_HOST__FSFILE_Read            ((Result(*)(Handle,u32*,u64,void*,u32))pluginTable_coin[6])
#define COIN_HOST__FSFILE_Write           ((Result(*)(Handle,u32*,u64,const void*,u32,u32))pluginTable_coin[7])
#define COIN_HOST__FSFILE_Close           ((Result(*)(Handle))pluginTable_coin[8])
#define COIN_HOST__fsMakePath             ((FS_Path(*)(FS_PathType,const void*))pluginTable_coin[9])
#define COIN_HOST__Draw_Lock              ((void(*)(void))pluginTable_coin[13])
#define COIN_HOST__Draw_Unlock            ((void(*)(void))pluginTable_coin[14])
#define COIN_HOST__Draw_ClearFramebuffer  ((void(*)(void))pluginTable_coin[15])
#define COIN_HOST__Draw_DrawString        ((u32(*)(u32,u32,u32,const char*))pluginTable_coin[16])
#define COIN_HOST__Draw_DrawFormattedString ((u32(*)(u32,u32,u32,const char*,...))pluginTable_coin[17])
#define COIN_HOST__Draw_FlushFramebuffer  ((void(*)(void))pluginTable_coin[18])
#define COIN_HOST__waitInputWithTimeout   ((u32(*)(s32))pluginTable_coin[19])
#define COIN_HOST__menuShouldExit         (*(bool*)pluginTable_coin[20])
#define COIN_HOST__srvGetServiceHandle    ((Result(*)(Handle*,const char*))pluginTable_coin[21])
#define COIN_HOST__svcSendSyncRequest     ((Result(*)(Handle))pluginTable_coin[22])
#define COIN_HOST__svcCloseHandle         ((Result(*)(Handle))pluginTable_coin[23])
#define COIN_MENU__AddItem                ((bool(*)(PluginMenuRegistration*,u32,const char*,void(*)(void),u32))pluginTable_coin[24])
#define COIN_HOST__miscellaneousMenu      ((Menu*)pluginTable_coin[25])
#define COIN_BLUR__AddFeatureItem          ((bool(*)(BlurFeatureRegistration*,u32,const char*,void(*)(void)))pluginTable_coin[26])
#define COIN_BLUR__DrawFeatureFrame        ((void(*)(const char*))pluginTable_coin[27])
#define COIN_PLUGIN_ID                    0x6E696F63u
#define COIN_MAX_MISC_ITEMS               24u

#define COIN_FRAME_COLOR       RGB565(31, 18, 31)
#define COIN_FRAME_TITLE_COLOR RGB565(31, 63, 20)

PLUGIN_RODATA(coin) static const char g_coinFilePath[] = "/luma/coins.bin";
PLUGIN_RODATA(coin) static const char g_lumaPath[] = "/luma";
PLUGIN_RODATA(coin) static const char g_gameCoinPath[] = "/gamecoin.dat";
PLUGIN_RODATA(coin) const char g_coinMenuProcessName[] = "menu";
PLUGIN_RODATA(coin) const char g_coinMenuTitle[] = "Set the number of Play Coins";
PLUGIN_RODATA(coin) static const char g_coinPageTitle[] = "Coin Setter Menu";
PLUGIN_RODATA(coin) static const char g_coinDebugTitle[] = "Play Coin Debug";
PLUGIN_RODATA(coin) static const char g_coinDiagWaiting[] = "Waiting for Home Menu coin state...";
PLUGIN_RODATA(coin) static const char g_coinLiveGroup[] = "Live gate";
PLUGIN_RODATA(coin) static const char g_coinHistoryGroup[] = "History accounting";
PLUGIN_RODATA(coin) static const char g_coinProgressionGroup[] = "Progression";
PLUGIN_RODATA(coin) static const char g_coinTotalFmt[] = "Total: %lu";
PLUGIN_RODATA(coin) static const char g_coinBoundaryFmt[] = "Boundary: %lu";
PLUGIN_RODATA(coin) static const char g_coinLiveProgressFmt[] = "Progress: %lu / 100";
PLUGIN_RODATA(coin) static const char g_coinNextCheckFmt[] = "Next query: %lu steps";
PLUGIN_RODATA(coin) static const char g_coinDayBaselineFmt[] = "Day baseline: %lu";
PLUGIN_RODATA(coin) static const char g_coinSpentBoundaryFmt[] = "Spent boundary: %lu";
PLUGIN_RODATA(coin) static const char g_coinHistoryRemainderFmt[] = "Unspent: %lu steps";
PLUGIN_RODATA(coin) static const char g_coinTodayFmt[] = "Coins today: %lu";
PLUGIN_RODATA(coin) static const char g_coinNextCoinFmt[] = "Next coin #%lu: %lu steps";
PLUGIN_RODATA(coin) static const char g_coinHistoryNeededFmt[] = "History needed: %lu steps";
PLUGIN_RODATA(coin) static const char g_coinFrameCorner[] = "+";
PLUGIN_RODATA(coin) static const char g_coinFrameSide[] = "|";
PLUGIN_RODATA(coin) static const char g_coinFrameRail[] = "----------------------";
PLUGIN_RODATA(coin) static const char g_psServiceName[] = "ps:ps";
PLUGIN_RODATA(coin) static const char g_setCoinsFormat[] = "Set Play Coins: %d";
PLUGIN_RODATA(coin) static const char g_controlsText[] = "DPAD Up/Down: +-1\nDPAD Right/Left: +-10\nA: Apply";
PLUGIN_RODATA(coin) static const char g_successText[] = "Play Coins successfully set.";
PLUGIN_RODATA(coin) static const char g_errorFormat[] = "Error: 0x%08lx";
PLUGIN_RODATA(coin) static const char g_recommendedText[] = "Recommended:";
PLUGIN_RODATA(coin) static const char g_recommendedFormat[] = "Press Y to return to tracked coins: %lu";
PLUGIN_RODATA(coin) static const char g_warningText[] = "Warning:";
PLUGIN_RODATA(coin) static const char g_warningLifetimeText[] = "Coins are higher than lifetime earnings!";
PLUGIN_RODATA(coin) static const char g_warningSecretText[] = "this may affect when... *secrets* are found";
PLUGIN_RODATA(coin) static const char g_lifetimeSpentFormat[] = "lifetime spent: %lu";
PLUGIN_RODATA(coin) static const char g_backText[] = "press B to go back";
PLUGIN_RODATA(coin) static const char g_noteText[] = "Note:";
PLUGIN_RODATA(coin) static const char g_reentryText[] = "Changes appear upon Home Menu re-entry";
PLUGIN_RODATA(coin) static const char g_clearSetLine[] = "Set Play Coins:         ";
PLUGIN_RODATA(coin) static const char g_emptyResultLine[] = "                                ";
PLUGIN_RODATA(coin) static const char g_clearResultLine[] = "                            ";
//its on nand so mediatype nand and extdata id is 0xf000000b, for some reason the low comes before high here,
//high is always 00048000 for nand extdata https://www.3dbrew.org/wiki/Extdata
PLUGIN_RODATA(coin) static const u32 g_gameCoinArchivePath[3] = {
    MEDIATYPE_NAND,
    0xF000000Bu,
    0x00048000u,
}; //type low high

// coins stuff
extern u16 g_coinDat;
extern u32 g_coinData[4];
extern u16 g_coinChange[3];
extern volatile CoinStepDiagnostics PLUGIN_coin_stepDiagnostics;
PLUGIN_DATA(coin) u32 g_coinOffset = 0;
PLUGIN_DATA(coin) static u32 g_lastCoins = 0;
PLUGIN_DATA(coin) static u32 g_lastEverSpent = 0;
PLUGIN_DATA(coin) static u32 g_coinKey = 0x45454545u;
PLUGIN_DATA(coin) static bool g_coinsFailed = false;
PLUGIN_DATA(coin) static bool g_patchedHome = false;
PLUGIN_BSS(coin) static PluginMenuRegistration g_coinMenuRegistration;
PLUGIN_BSS(coin) static BlurFeatureRegistration g_coinBlurFeatureRegistration;

#define coinsBin       g_coinData[0]
#define coinsRec       g_coinData[1]
#define coinsTrue      g_coinData[2]
#define coinsEverSpent g_coinData[3]
#define coinsEarned    g_coinChange[0]
#define coinsSpent     g_coinChange[1]

extern bool PLUGIN_coin_AttachHomeMenu(void);

PLUGIN_CODE(coin) static u32 PLUGIN_coin_Checksum(u32 value, u32 key)
{
    value ^= key;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_Encrypt(
    u32 coins,
    u32 *outEncrypted,
    u32 *outChecksum,
    u32 *outKey
)
{
    u32 key = g_coinKey;
    u32 encrypted = coins ^ key;
    *outKey = key;

    u32 checksum = PLUGIN_coin_Checksum(encrypted, key);
    *outChecksum = checksum;
    encrypted = encrypted + checksum * 2u + key;
    *outEncrypted = (encrypted & 0xFFFF0000u) |
        ((encrypted + (checksum >> 16)) & 0xFFFFu);
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_GenerateRandomBytes(void *out, u32 size)
{
    Handle handle;
    Result rc = COIN_HOST__srvGetServiceHandle(&handle, g_psServiceName);
    if (R_FAILED(rc))
        return rc;

    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    u32 *cmdbuf = (u32*)(tls + 0x80);

    cmdbuf[0] = 0x000D0042;
    cmdbuf[1] = size;
    cmdbuf[2] = (size << 4) | 0xCu;
    cmdbuf[3] = (u32)out;

    rc = COIN_HOST__svcSendSyncRequest(handle);
    if (R_SUCCEEDED(rc))
        rc = (Result)cmdbuf[1];

    COIN_HOST__svcCloseHandle(handle);
    return rc;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_HandleCoins(void) // essentially the save func on a timer
{
    // increment true count file by coinsearned, modify rec count value
    FS_Archive sd;
    Handle file;
    u32 written;
    u32 data[3];

    COIN_HOST__FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, COIN_HOST__fsMakePath(PATH_EMPTY, NULL));
    Result rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
        0
    );

    if (R_FAILED(rc))
    {
        COIN_HOST__FSUSER_CreateDirectory(sd, COIN_HOST__fsMakePath(PATH_ASCII, g_lumaPath), 0);
        COIN_HOST__FSUSER_OpenFile(
            &file,
            sd,
            COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
            0
        );
    } // create if not exist

    if (coinsEarned > 0 || !g_patchedHome) // ensure we write files at the beginning since loader does no writes
    {
        // file update (also the only place coinsTrue value ever changes)
        coinsTrue += coinsEarned; // coinsEarned are per-interval (between handlecoins calls) so bake into coinsTrue here

        if (!g_patchedHome) { // runs once after load
            PLUGIN_coin_GenerateRandomBytes(&g_coinKey, sizeof(g_coinKey));
        }

        PLUGIN_coin_Encrypt(coinsTrue, &data[0], &data[1], &data[2]);
        COIN_HOST__FSFILE_Write(file, &written, 8, data, sizeof(data), FS_WRITE_FLUSH);
    }

    if (coinsEverSpent != g_lastEverSpent || !g_patchedHome) // must run after initial !patchedhome genbytes prior
    {
        u32 spent[2];
        g_lastEverSpent = coinsEverSpent; // unlike coinsTrue, coinsEverSpent is updated in home menu hook
        PLUGIN_coin_Encrypt(g_lastEverSpent, &spent[0], &spent[1], &data[2]); // data[2] ignored here
        COIN_HOST__FSFILE_Write(file, &written, 20, spent, sizeof(spent), FS_WRITE_FLUSH); // write data[0] and data[1] only (encrypted + checksum, key is the word before these two)
    }

    // recommended coins update
    coinsRec += coinsEarned;
    if (coinsRec > 30000)
        coinsRec = 30000;

    COIN_HOST__FSFILE_Write(file, &written, 28, &coinsRec, sizeof(coinsRec), FS_WRITE_FLUSH);
    coinsEarned = 0;

    // update coins.bin with coinsBin value
    g_lastCoins = coinsBin;
    COIN_HOST__FSFILE_Write(file, &written, 0, &g_lastCoins, sizeof(g_lastCoins), FS_WRITE_FLUSH);
    COIN_HOST__FSFILE_Close(file);
    COIN_HOST__FSUSER_CloseArchive(sd);

    // ---- FILE FORMAT
    // coinbin
    // homepointer
    // encrypted truecoin
    // truecoin checksum
    // key
    // encrypted spent
    // spent checksum
    // coinrec
}

PLUGIN_CODE(coin) static void PLUGIN_coin_SetupCoins(void)
{
    FS_Archive sd;
    Handle file;
    u32 read = 0;

    COIN_HOST__FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, COIN_HOST__fsMakePath(PATH_EMPTY, NULL));
    Result rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_WRITE,
        0
    );

    if (R_SUCCEEDED(rc))
    {
        COIN_HOST__FSFILE_Read(file, &read, 0, &g_lastCoins, sizeof(g_lastCoins));
        COIN_HOST__FSFILE_Read(file, &read, 4, &g_coinOffset, sizeof(g_coinOffset));
        COIN_HOST__FSFILE_Read(file, &read, 16, &g_coinKey, sizeof(g_coinKey));
        COIN_HOST__FSFILE_Close(file);

        if (g_coinOffset <= 0x100000u || !PLUGIN_coin_AttachHomeMenu())
            g_coinsFailed = true;
    }
    else
    {
        g_coinsFailed = true;
    }

    COIN_HOST__FSUSER_CloseArchive(sd);

    if (g_coinsFailed)
        return;

    g_lastEverSpent = coinsEverSpent;
    PLUGIN_coin_HandleCoins();
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_UpdatePlayCoins(void)
{
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
    u16 systemCoins = g_coinDat;
    res = COIN_HOST__FSFILE_Read(file, &read, 4, &systemCoins, sizeof(systemCoins));
    if (R_FAILED(res) || read != sizeof(systemCoins))
        systemCoins = g_coinDat; // fallback value

    // coinsSpent can never exceed 300, this is the count of coins spent before returning to home menu
    // - in normal cases that is, but if coins are cheated higher we need to safeguard
    coinsSpent = 0;
    if (g_coinDat > systemCoins)
    {
        u32 spent = g_coinDat - systemCoins;
        coinsSpent = spent > 300 ? 300 : (u16)spent;
    }

    COIN_HOST__FSFILE_Close(file);
    COIN_HOST__FSUSER_CloseArchive(archive);
    return res;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_SetPlayCoins(u16 amount)
{
    FS_Archive archive; //extdata archive
    Handle file; //gamecoin file handle
    Result res; //result variable
    FS_Path pathData;

    pathData.type = PATH_BINARY; //binary path because titleid
    pathData.size = sizeof(g_gameCoinArchivePath); //3*sizeof(u32)
    pathData.data = g_gameCoinArchivePath; //data
    //shared extdata archive https://www.3dbrew.org/wiki/Extdata#NAND_Shared_Extdata has the f000000b archive
    res = COIN_HOST__FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, pathData);
    if (R_FAILED(res)) //return if error
        return res;

    // open /gamecoin.dat in extdata archive
    // https://www.3dbrew.org/wiki/Extdata#Shared_Extdata_0xf000000b_gamecoin.dat
    res = COIN_HOST__FSUSER_OpenFile(
        &file,
        archive,
        COIN_HOST__fsMakePath(PATH_ASCII, g_gameCoinPath),
        FS_OPEN_WRITE,
        0
    ); //open for writing, no attributes necessary
    if (R_FAILED(res)) //return if error
    {
        COIN_HOST__FSUSER_CloseArchive(archive);
        return res;
    }

    // ----- new coin uncap method requires capping amount to 300 ironically  
    u16 newAmount = amount > 300 ? 300 : amount;
    u16 savedAmount = newAmount > coinsSpent ? newAmount - coinsSpent : 0;
    // save (coins - amount spent) to file, this will be consumed by home menu
    res = COIN_HOST__FSFILE_Write(file, NULL, 4, &savedAmount, sizeof(savedAmount), 0);
    if (R_FAILED(res))
    {
        COIN_HOST__FSFILE_Close(file);
        COIN_HOST__FSUSER_CloseArchive(archive);
        return res;
    }

    res = COIN_HOST__FSFILE_Close(file);
    if (R_FAILED(res)) //return if error
    {
        COIN_HOST__FSUSER_CloseArchive(archive);
        return res;
    }

    // ---- change new coinsbin count
    coinsBin = (u32)amount + coinsSpent;
    g_coinDat = newAmount; // save (coins) to coinsDat so that next coinsSpent recalc is correct
    return COIN_HOST__FSUSER_CloseArchive(archive);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawEditor(
    u16 playCoins,
    u32 trueCoinDisplay,
    u32 recommendedDisplay,
    bool showResult,
    Result res
)
{
    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    COIN_HOST__Draw_DrawString(10, 8, COIN_FRAME_COLOR, g_coinFrameCorner);
    COIN_HOST__Draw_DrawString(16, 8, COIN_FRAME_COLOR, g_coinFrameRail);
    COIN_HOST__Draw_DrawString(148, 8, COIN_FRAME_COLOR, g_coinFrameCorner);
    COIN_HOST__Draw_DrawString(10, 16, COIN_FRAME_COLOR, g_coinFrameSide);
    COIN_HOST__Draw_DrawString(148, 16, COIN_FRAME_COLOR, g_coinFrameSide);
    COIN_HOST__Draw_DrawString(10, 24, COIN_FRAME_COLOR, g_coinFrameCorner);
    COIN_HOST__Draw_DrawString(16, 24, COIN_FRAME_COLOR, g_coinFrameRail);
    COIN_HOST__Draw_DrawString(148, 24, COIN_FRAME_COLOR, g_coinFrameCorner);
    COIN_HOST__Draw_DrawString(20, 16, COIN_FRAME_TITLE_COLOR, g_coinPageTitle);
    COIN_HOST__Draw_DrawFormattedString(20, 40, COLOR_WHITE, g_setCoinsFormat, playCoins);
    COIN_HOST__Draw_DrawString(20, 60, COLOR_WHITE, g_controlsText);

    if (showResult)
    {
        if (R_SUCCEEDED(res))
            COIN_HOST__Draw_DrawString(20, 100, COLOR_GREEN, g_successText);
        else
            COIN_HOST__Draw_DrawFormattedString(20, 100, COLOR_RED, g_errorFormat, res);
    }
    else
    {
        COIN_HOST__Draw_DrawString(20, 100, COLOR_GRAY, g_emptyResultLine);
    }

    if (playCoins != recommendedDisplay)
    {
        COIN_HOST__Draw_DrawString(20, 170, COLOR_YELLOW, g_recommendedText);
        COIN_HOST__Draw_DrawFormattedString(
            20,
            180,
            COLOR_WHITE,
            g_recommendedFormat,
            recommendedDisplay
        );
    }

    if (playCoins > trueCoinDisplay)
    {
        COIN_HOST__Draw_DrawString(20, 200, COLOR_ORANGE, g_warningText);
        COIN_HOST__Draw_DrawString(20, 210, COLOR_WHITE, g_warningLifetimeText);
        COIN_HOST__Draw_DrawString(20, 220, COLOR_GRAY, g_warningSecretText);
    }

    COIN_HOST__Draw_DrawFormattedString(
        180,
        40,
        COLOR_GRAY,
        g_lifetimeSpentFormat,
        coinsEverSpent + coinsSpent
    );
    COIN_HOST__Draw_DrawString(20, 120, COLOR_GRAY, g_backText);
    COIN_HOST__Draw_DrawString(20, 140, COLOR_TITLE, g_noteText);
    COIN_HOST__Draw_DrawString(20, 150, COLOR_WHITE, g_reentryText);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) void PLUGIN_coin_EditPlayCoins(void)
{
    // ----- fix current coins
    PLUGIN_coin_UpdatePlayCoins();

    u16 playCoins = coinsBin > coinsSpent ? (u16)(coinsBin - coinsSpent) : 0;
    Result res = 0;
    u32 trueCoinDisplay = coinsTrue + coinsEarned; // coinstrue (aka coins ever earned) doesnt decrease
    u32 recommendedDisplay = coinsRec + coinsEarned > coinsSpent ?
        coinsRec + coinsEarned - coinsSpent : 0;
    bool previousWarning = playCoins > trueCoinDisplay;
    bool previousRecommended = playCoins != recommendedDisplay;

    PLUGIN_coin_DrawEditor(playCoins, trueCoinDisplay, recommendedDisplay, false, res);

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);

        if (pressed & KEY_A)
        {
            res = PLUGIN_coin_SetPlayCoins(playCoins);
            PLUGIN_coin_DrawEditor(playCoins, trueCoinDisplay, recommendedDisplay, true, res);
        }
        else if (pressed & KEY_B)
        {
            return;
        }
        else
        {
            bool updated = false;

            if (pressed & KEY_DUP)
            {
                playCoins = playCoins < 30000 ? playCoins + 1 : 0;
                updated = true;
            }
            else if (pressed & KEY_DDOWN)
            {
                playCoins = playCoins > 0 ? playCoins - 1 : 30000;
                updated = true;
            }
            else if (pressed & KEY_DRIGHT)
            {
                playCoins = playCoins + 10 > 30000 ? 30000 : playCoins + 10;
                updated = true;
            }
            else if (pressed & KEY_DLEFT)
            {
                playCoins = playCoins < 10 ? 0 : playCoins - 10;
                updated = true;
            }
            else if ((pressed & KEY_Y) && playCoins != recommendedDisplay)
            {
                playCoins = (u16)recommendedDisplay;
                updated = true;
            }

            if (updated)
            {
                bool currentWarning = playCoins > trueCoinDisplay;
                bool currentRecommended = playCoins != recommendedDisplay;

                if (currentWarning != previousWarning || currentRecommended != previousRecommended)
                {
                    PLUGIN_coin_DrawEditor(
                        playCoins,
                        trueCoinDisplay,
                        recommendedDisplay,
                        false,
                        res
                    );
                    previousWarning = currentWarning;
                    previousRecommended = currentRecommended;
                }

                COIN_HOST__Draw_Lock();
                COIN_HOST__Draw_DrawString(20, 40, COLOR_WHITE, g_clearSetLine);
                COIN_HOST__Draw_DrawFormattedString(20, 40, COLOR_WHITE, g_setCoinsFormat, playCoins);
                COIN_HOST__Draw_DrawString(20, 100, COLOR_WHITE, g_clearResultLine);
                COIN_HOST__Draw_FlushFramebuffer();
                COIN_HOST__Draw_Unlock();
            }
        }
    } while (!COIN_HOST__menuShouldExit);
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_NextStepCost(u32 coinsToday)
{
    return coinsToday < 10u ? 100u : 100u + 3u * (coinsToday - 9u);
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_TotalSpentSteps(u32 coinsToday)
{
    u32 total = 0;
    u32 cost = 100;

    for (u32 coin = 0; coin < coinsToday; coin++)
    {
        if (total > 0xFFFFFFFFu - cost)
            return 0xFFFFFFFFu;

        total += cost;
        if (coin >= 9u && cost <= 0xFFFFFFFCu)
            cost += 3u;
    }

    return total;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawDebug(void)
{
    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    COIN_BLUR__DrawFeatureFrame(g_coinDebugTitle);

    volatile CoinStepDiagnostics *source = &PLUGIN_coin_stepDiagnostics;
    if (source->valid != 1u)
    {
        COIN_HOST__Draw_DrawString(20, 42, COLOR_GRAY, g_coinDiagWaiting);
    }
    else
    {
        CoinStepDiagnostics d;
        d.valid = source->valid;
        d.liveTotal = source->liveTotal;
        d.liveBoundary = source->liveBoundary;
        d.historyBoundary = source->historyBoundary;
        d.historyTotal = source->historyTotal;
        d.coinsToday = source->coinsToday;

        u32 liveProgress = d.liveTotal >= d.liveBoundary ? d.liveTotal - d.liveBoundary : 0u;
        u32 historyProgress = d.historyTotal >= d.historyBoundary ? d.historyTotal - d.historyBoundary : 0u;
        u32 nextCheck = liveProgress >= 100u ? 0u : 100u - liveProgress;
        u32 nextCost = PLUGIN_coin_NextStepCost(d.coinsToday);
        u32 remaining = historyProgress >= nextCost ? 0u : nextCost - historyProgress;
        u32 spentSteps = PLUGIN_coin_TotalSpentSteps(d.coinsToday);
        u32 dayBaseline = d.historyBoundary >= spentSteps ? d.historyBoundary - spentSteps : 0u;

        COIN_HOST__Draw_DrawString(20, 36, COLOR_GRAY, g_coinLiveGroup);
        COIN_HOST__Draw_DrawFormattedString(20, 49, COLOR_WHITE, g_coinTotalFmt, d.liveTotal);
        COIN_HOST__Draw_DrawFormattedString(20, 62, COLOR_WHITE, g_coinBoundaryFmt, d.liveBoundary);
        COIN_HOST__Draw_DrawFormattedString(20, 75, COLOR_CYAN, g_coinLiveProgressFmt, liveProgress);
        COIN_HOST__Draw_DrawFormattedString(20, 88, COLOR_YELLOW, g_coinNextCheckFmt, nextCheck);

        COIN_HOST__Draw_DrawString(20, 104, COLOR_GRAY, g_coinHistoryGroup);
        COIN_HOST__Draw_DrawFormattedString(20, 117, COLOR_WHITE, g_coinTotalFmt, d.historyTotal);
        COIN_HOST__Draw_DrawFormattedString(20, 130, COLOR_WHITE, g_coinDayBaselineFmt, dayBaseline);
        COIN_HOST__Draw_DrawFormattedString(20, 143, COLOR_WHITE, g_coinSpentBoundaryFmt, d.historyBoundary);
        COIN_HOST__Draw_DrawFormattedString(20, 156, COLOR_CYAN, g_coinHistoryRemainderFmt, historyProgress);

        COIN_HOST__Draw_DrawString(20, 172, COLOR_GRAY, g_coinProgressionGroup);
        COIN_HOST__Draw_DrawFormattedString(20, 185, COLOR_WHITE, g_coinTodayFmt, d.coinsToday);
        COIN_HOST__Draw_DrawFormattedString(20, 198, COLOR_YELLOW, g_coinNextCoinFmt, d.coinsToday + 1u, nextCost);
        COIN_HOST__Draw_DrawFormattedString(20, 211, COLOR_YELLOW, g_coinHistoryNeededFmt, remaining);
    }

    COIN_HOST__Draw_DrawString(20, 228, RGB565(15, 31, 15), g_backText);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenDebug(void)
{
    PLUGIN_coin_DrawDebug();

    do
    {
        if (COIN_HOST__waitInputWithTimeout(50) & KEY_B)
            return;
    } while (!COIN_HOST__menuShouldExit);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OnBlurTick(u64 delta)
{
    (void)delta;

    if (!g_coinsFailed)
    {
        if (!g_patchedHome)
        {
            PLUGIN_coin_SetupCoins();
            g_patchedHome = true;
        }
        else if (coinsBin != g_lastCoins)
        {
            PLUGIN_coin_HandleCoins();
        }
    }
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_StringEquals(const char *a, const char *b)
{
    if (!a || !b)
        return false;

    const volatile char *left = a;
    const volatile char *right = b;
    while (*left && *left == *right)
    {
        left++;
        right++;
    }

    return *left == *right;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_CopyMenuItem(MenuItem *dst, const MenuItem *src)
{
    dst->title = src->title;
    dst->action_type = src->action_type;
    if (src->action_type == METHOD)
        dst->method = src->method;
    else
        dst->menu = src->menu;
    dst->visibility = src->visibility;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_ClearMenuItem(MenuItem *item)
{
    item->title = NULL;
    item->action_type = MENU_END;
    item->method = NULL;
    item->visibility = NULL;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RemoveBuiltInMenuItem(void)
{
    Menu *menu = COIN_HOST__miscellaneousMenu;

    for (u32 i = 0; i < COIN_MAX_MISC_ITEMS && menu->items[i].action_type != MENU_END; )
    {
        MenuItem *item = &menu->items[i];
        if (item->action_type == METHOD &&
            item->method != PLUGIN_coin_EditPlayCoins &&
            PLUGIN_coin_StringEquals(item->title, g_coinMenuTitle))
        {
            u32 j = i;
            while (j + 1u < COIN_MAX_MISC_ITEMS)
            {
                PLUGIN_coin_CopyMenuItem(&menu->items[j], &menu->items[j + 1u]);
                if (menu->items[j].action_type == MENU_END)
                    break;
                j++;
            }

            if (j + 1u == COIN_MAX_MISC_ITEMS)
                PLUGIN_coin_ClearMenuItem(&menu->items[COIN_MAX_MISC_ITEMS - 1u]);
        }
        else
        {
            i++;
        }
    }
}

PLUGIN_MAIN(coin) bool PLUGIN_coin_Main(void)
{
    if (!COIN_BLUR__AddTickFunc || !COIN_BLUR__AddFeatureItem ||
        !COIN_BLUR__DrawFeatureFrame || !COIN_MENU__AddItem)
    {
        return false;
    }

    if (!COIN_BLUR__AddTickFunc(PLUGIN_coin_OnBlurTick, 1000000000LL))
        return false;

    if (!COIN_BLUR__AddFeatureItem(
            &g_coinBlurFeatureRegistration,
            COIN_PLUGIN_ID,
            g_coinDebugTitle,
            PLUGIN_coin_OpenDebug))
    {
        return false;
    }

    if (COIN_MENU__AddItem(
            &g_coinMenuRegistration,
            COIN_PLUGIN_ID,
            g_coinMenuTitle,
            PLUGIN_coin_EditPlayCoins,
            RGB565(31, 63, 20)))
    {
        PLUGIN_coin_RemoveBuiltInMenuItem();
    }

    return true;
}