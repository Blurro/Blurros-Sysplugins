#include <3ds.h>
#include "draw.h"
#include "menu.h"
#include "sysplugin_menu.h"
#include "process_patches.h"
#include "csvc.h"
#include "menus/miscellaneous.h"
#include "utils.h"

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

typedef struct
{
    u16 title[32];
    u16 notificationMessage[256];
    u16 menuMessage[256];
    u8 difficulty;
    u8 imageIndex;
    u16 reserved;
} CoinAchievement;

typedef struct
{
    u32 magic;
    u32 count;
    u32 entrySize;
} CoinAchievementFileHeader;

typedef struct
{
    u32 magic;
    u32 version;
} CoinAssetFileHeader;

typedef struct
{
    u32 magic;
    u32 version;
    u32 compressedSize;
} CoinPackedAssetHeader;

typedef struct
{
    u32 version;
    u32 dataOffset;
    u32 dataSize;
} CoinPackedAssetLocation;

typedef struct
{
    u32 version;
    u32 achievementsEnabled;
} CoinMenuSettings;

typedef char CoinAchievementSizeCheck[(sizeof(CoinAchievement) == 1092u) ? 1 : -1];
typedef char CoinAssetFileHeaderSizeCheck[(sizeof(CoinAssetFileHeader) == 8u) ? 1 : -1];
typedef char CoinPackedAssetHeaderSizeCheck[(sizeof(CoinPackedAssetHeader) == 12u) ? 1 : -1];

typedef struct
{
    char magic[8];
    u32 formatVersion;
    s32 serviceResult;
    s32 totalResult;
    u32 totalNotifications;
    s32 arrivedResult;
    u32 totalArrived;
    s32 dbHeaderResult;
    u32 slotsScanned;
    u32 recordSize;
    u32 maxMessageSize;
    u8 dbHeader[0x10];
} CoinNewsDumpHeader;

typedef struct
{
    u32 slot;
    s32 headerResult;
    s32 messageResult;
    u32 messageSize;
    NotificationHeader header;
} CoinNewsDumpRecord;

typedef char CoinNewsHeaderSizeMustBe70[(sizeof(NotificationHeader) == 0x70) ? 1 : -1];
typedef char CoinNewsDumpHeaderSizeMustBe40[(sizeof(CoinNewsDumpHeader) == 0x40) ? 1 : -1];
typedef char CoinNewsDumpRecordSizeMustBe80[(sizeof(CoinNewsDumpRecord) == 0x80) ? 1 : -1];

typedef struct
{
    u32 logicalIndex;
    NotificationHeader header;
} CoinNewsAnchor;

PLUGIN_BSS(coin) static u8 g_coinNewsMessageBuffer[0x1780];


extern bool PLUGIN_blur_AddTickFunc(BlurTickFunc func, s64 intervalNs);
extern bool PLUGIN_blur_RemoveTickFunc(BlurTickFunc func);
extern bool PLUGIN_blur_AddFeatureItem(
    BlurFeatureRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void)
);
extern void PLUGIN_blur_DrawFeatureFrame(const char *title);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_AddTickFunc);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_RemoveTickFunc);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_AddFeatureItem);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_DrawFeatureFrame);

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
    (void*)FSFILE_GetSize,
    (void*)osGetTime,
    (void*)dateTimeToString,
    (void*)PLUGIN_blur_RemoveTickFunc,
    (void*)FSFILE_SetSize,
    (void*)PLUGIN_MENU_OpenPluginFile,
    (void*)PLUGIN_MENU_UnpackLz10File,
    (void*)PLUGIN_MENU_ClosePluginFile,
    (void*)PLUGIN_MENU_TempAlloc,
    (void*)PLUGIN_MENU_TempFree,
    (void*)FSUSER_CreateFile,
    (void*)PLUGIN_MENU_LoadData,
    (void*)PLUGIN_MENU_SaveData,
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
#define COIN_HOST__FSFILE_GetSize          ((Result(*)(Handle,u64*))pluginTable_coin[28])
#define COIN_HOST__osGetTime               ((u64(*)(void))pluginTable_coin[29])
#define COIN_HOST__dateTimeToString        ((int(*)(char*,u64,bool))pluginTable_coin[30])


#define COIN_BLUR__RemoveTickFunc          ((bool(*)(BlurTickFunc))pluginTable_coin[31])
#define COIN_HOST__FSFILE_SetSize          ((Result(*)(Handle,u64))pluginTable_coin[32])
#define COIN_MENU__OpenPluginFile          ((bool(*)(u32,PluginMenuFileContext*))pluginTable_coin[33])
#define COIN_MENU__UnpackLz10File          ((Result(*)(const PluginMenuFileContext*,u32,u32,const char*))pluginTable_coin[34])
#define COIN_MENU__ClosePluginFile         ((void(*)(PluginMenuFileContext*))pluginTable_coin[35])
#define COIN_MENU__TempAlloc               ((bool(*)(u32,u32*))pluginTable_coin[36])
#define COIN_MENU__TempFree                ((void(*)(u32,u32))pluginTable_coin[37])
#define COIN_HOST__FSUSER_CreateFile       ((Result(*)(FS_Archive,FS_Path,u32,u64))pluginTable_coin[38])
#define COIN_MENU__LoadData                ((bool(*)(u32,void*,u32))pluginTable_coin[39])
#define COIN_MENU__SaveData                ((bool(*)(u32,const void*,u32))pluginTable_coin[40])
#define COIN_HOST__OperateOnProcessByName  ((Result(*)(const char*,OperateOnProcessCb))pluginTable_coin[1])
#define COIN_HOST__svcFlushEntireDataCache ((void(*)(void))pluginTable_coin[11])
#define COIN_HOST__svcInvalidateEntireInstructionCache ((void(*)(void))pluginTable_coin[12])
#define COIN_PLUGIN_ID                    0x6E696F63u
#define COIN_MAX_MISC_ITEMS               24u
#define COIN_ASSET_VERSION_MAGIC          0x56584E33u
#define COIN_PACKED_ASSET_COUNT           6u
#define COIN_ASSET_ICN                    0u
#define COIN_ASSET_ACHV                   1u
#define COIN_ASSET_EASYTOP                2u
#define COIN_ASSET_MEDIUMTOP              3u
#define COIN_ASSET_HARDTOP                4u
#define COIN_ASSET_EXTREMTOP              5u
#define COIN_ASSET_ALL_MASK               ((1u << COIN_PACKED_ASSET_COUNT) - 1u)

#define COIN_FRAME_COLOR       RGB565(31, 18, 31)
#define COIN_FRAME_TITLE_COLOR RGB565(31, 63, 20)
#define COIN_ACHV_EASY_COLOR    RGB565(3, 45, 8)
#define COIN_ACHV_HARD_COLOR    RGB565(4, 19, 27)

PLUGIN_RODATA(coin) static const char g_coinFilePath[] = "/luma/coins.bin";
PLUGIN_RODATA(coin) static const char g_lumaPath[] = "/luma";
PLUGIN_RODATA(coin) static const char g_gameCoinPath[] = "/gamecoin.dat";
PLUGIN_RODATA(coin) const char g_coinMenuProcessName[] = "menu";
PLUGIN_RODATA(coin) const char g_coinMenuTitle[] = "PlayCoinz Menu";
PLUGIN_RODATA(coin) static const char g_coinBuiltInMenuTitle[] = "Set the number of Play Coins";
PLUGIN_RODATA(coin) static const char g_coinPageTitle[] = "Coin Setter Menu";
PLUGIN_RODATA(coin) static const char g_coinAchievementsPageTitle[] = "View Achievements";
PLUGIN_RODATA(coin) static const char g_coinOptionsPageTitle[] = "PlayCoinz Options";
PLUGIN_RODATA(coin) static const char g_coinViewAchievementsItem[] = "View earned achievements";
PLUGIN_RODATA(coin) static const char g_coinSetCoinsItem[] = "Set the number of Play Coins";
PLUGIN_RODATA(coin) static const char g_coinOptionsItem[] = "Options...";
PLUGIN_RODATA(coin) static const char g_coinDisableAchievementsItem[] = "Disable Achievements";
PLUGIN_RODATA(coin) static const char g_coinEnableAchievementsItem[] = "Enable Achievements";
PLUGIN_RODATA(coin) static const char g_coinTierEasy[] = "Easy";
PLUGIN_RODATA(coin) static const char g_coinTierMedium[] = "Medium";
PLUGIN_RODATA(coin) static const char g_coinTierHard[] = "Hard";
PLUGIN_RODATA(coin) static const char g_coinTierExtreme[] = "Extreme";
PLUGIN_RODATA(coin) static const char g_coinTierLocked[] = "-------";
PLUGIN_RODATA(coin) static const char g_coinUnknownAchievement[] = "???";
PLUGIN_RODATA(coin) static const char g_coinMenuDots[] = "...";
PLUGIN_RODATA(coin) static const char g_coinMenuCursor[] = ">";
PLUGIN_RODATA(coin) static const char g_coinMenuUnselected[] = " ";
PLUGIN_RODATA(coin) static const char g_coinMenuClearRow[] =
    "                                                  ";
PLUGIN_RODATA(coin) static const char g_coinBackShort[] = "B: go back";
PLUGIN_RODATA(coin) static const char g_coinViewShort[] = "X: view";
PLUGIN_RODATA(coin) static const char g_coinAchievementOptionsPrompt[] = "Press X for options";
PLUGIN_RODATA(coin) static const char g_coinAchievementResendItem[] = "Resend if missing";
PLUGIN_RODATA(coin) static const char g_coinAchievementDeleteItem[] = "Delete from notifications";
PLUGIN_RODATA(coin) static const char g_coinConfirmTitle[] = "Are you sure?";
PLUGIN_RODATA(coin) static const char g_coinConfirmNo[] = "No";
PLUGIN_RODATA(coin) static const char g_coinConfirmYes[] = "Yes";
PLUGIN_RODATA(coin) static const char g_coinResultAlreadyExists[] =
    "This already exists in your notifications!";
PLUGIN_RODATA(coin) static const char g_coinResultDone[] = "Done!";
PLUGIN_RODATA(coin) static const char g_coinResultNotFound[] = "Notification not found!";
PLUGIN_RODATA(coin) static const char g_coinDebugTitle[] = "Play Coin Debug";
PLUGIN_RODATA(coin) static const char g_coinDebugControls[] = "B: back";
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
PLUGIN_RODATA(coin) static const char g_coinBucketGroup[] = "Rolling walk buckets";
PLUGIN_RODATA(coin) static const char g_coinBucketNowFmt[] = "Now: %s";
PLUGIN_RODATA(coin) static const char g_coinBucketTodayFmt[] = "Today: %u";
PLUGIN_RODATA(coin) static const char g_coinBucketPrevFmt[] = "D-%lu: %u";
PLUGIN_RODATA(coin) static const char g_coinBucketTotalFmt[] = "7-day: %lu";
PLUGIN_RODATA(coin) static const char g_coinBucketActiveFmt[] = "Day ID: %lu";
PLUGIN_RODATA(coin) static const char g_coinBucketNoClock[] = "Clock unavailable";
PLUGIN_RODATA(coin) static const char g_coinBucketClearLine[] = "                           ";
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

#define COIN_NEWS_EPOCH_OFFSET_MS 3155673600000ULL
#define COIN_NOTIFICATION_IMAGE_MAX 0x10000u
#define COIN_NOTIFICATION_SCRATCH_SIZE 0x10000u
#define COIN_NOTIFICATION_SCRATCH_LOW  0x10000000u
#define COIN_NOTIFICATION_SCRATCH_HIGH 0x14000000u
#define COIN_NOTIFICATION_TICK_NS      5000000000LL
#define COIN_NEWS_LED_WORD_COUNT       50u
#define COIN_DIAG_IDLE                 0u
#define COIN_DIAG_WAIT_TRIGGER         1u
#define COIN_DIAG_WAIT_RESTORE         2u
#define COIN_ACHIEVEMENT_MAGIC         0x56484341u
#define COIN_ACHIEVEMENT_COUNT         18u
#define COIN_ACHIEVEMENT_MASK          0x0003FFFFu
#define COIN_SETTINGS_VERSION           1u
#define COIN_FILE_BASE_SIZE             0x20u
#define COIN_FILE_EXTENSION_OFFSET      0x20u
#define COIN_FILE_EXTENSION_WORDS       7u
#define COIN_FILE_EXTENSION_SIZE        0x20u
#define COIN_FILE_ACHIEVEMENT_OFFSET    0x40u
#define COIN_FILE_NO_ACHIEVEMENT_SIZE   0x40u
#define COIN_FILE_WITH_ACHIEVEMENT_SIZE 0x48u
#define COIN_FILE_EXTENSION_SEED        0x31445843u
#define COIN_BLACKJACK_COUNTER_MAX       30000u
#define COIN_EXT_BLACKJACK_COUNTERS_WORD 0u
#define COIN_EXT_BLACKJACK_SURPLUS_WORD  1u
#define COIN_EXT_LAST_DAY_WORD           2u
#define COIN_EXT_DAY_HISTORY_WORD        3u
#define COIN_EXT_TODAY_WORD              6u
#define COIN_DAY_HISTORY_COUNT           6u
#define COIN_DEBUG_BUCKET_X              155u
#define COIN_DEBUG_BUCKET_NOW_Y          49u
#define COIN_NEWS_MAX_NOTIFICATIONS    100u
#define COIN_ACHIEVEMENT_ROW_COUNT      25u
#define COIN_ACHIEVEMENT_VISIBLE_ROWS   14u
#define COIN_ACHIEVEMENT_ITEM_TOP_Y     45u
#define COIN_ACHIEVEMENT_ITEM_SPACING_Y 11u
#define COIN_ACHIEVEMENT_TOP_DOTS_Y     34u
#define COIN_ACHIEVEMENT_PROMPT_Y       221u
#define COIN_NEWS_EMPTY_SLOT_RESULT    ((Result)0xC8A12805u)
#define COIN_NEWS_UNUSABLE_SLOT_RESULT ((Result)0xC8A0F842u)
#define COIN_NEWS_ANCHOR_MAX            12u
#define COIN_NEWS_DB_HEADER_SIZE        0x10u
#define COIN_NEWS_DB_RECORD_SIZE        0x70u
#define COIN_NEWS_DB_SIZE               0x2BD0u
#define COIN_NEWS_RAW_DELETE_SPECIFIC   1u
#define COIN_NEWS_RAW_DELETE_ALL        2u
PLUGIN_RODATA(coin) static const char g_coinNewsName[] = "news";
PLUGIN_RODATA(coin) static const char g_coinNewsServiceName[] = "news:s";
PLUGIN_RODATA(coin) static const char g_coinAchievementPath[] = "/luma/coinachv/achv.bin";
PLUGIN_RODATA(coin) static const char g_coinAchievementDirPath[] = "/luma/coinachv";
PLUGIN_RODATA(coin) static const char g_coinIcnPath[] = "/luma/coinachv/icn.bin";
PLUGIN_DATA(coin) static char g_coinNewsDumpPath[] = "/luma/newsdump00.bin";
PLUGIN_RODATA(coin) static const char g_coinEasyTopImagePath[] = "/luma/coinachv/easytop.bin";
PLUGIN_RODATA(coin) static const char g_coinMediumTopImagePath[] = "/luma/coinachv/mediumtop.bin";
PLUGIN_RODATA(coin) static const char g_coinHardTopImagePath[] = "/luma/coinachv/hardtop.bin";
PLUGIN_RODATA(coin) static const char g_coinExtremeTopImagePath[] = "/luma/coinachv/extremtop.bin";
PLUGIN_RODATA(coin) static const u64 g_coinAchievementProcessIds[4] = {
    0x000400306E696F63ULL,
    0x000400316E696F63ULL,
    0x000400326E696F63ULL,
    0x000400336E696F63ULL,
};
PLUGIN_RODATA(coin) static const u8 g_coinAchievementImageCounts[4] = {5u, 5u, 5u, 3u};
PLUGIN_RODATA(coin) static const char g_coinAchievementCipherAlphabet[] =
    "6PLAzyZ0rp4MiQvwnmYkHeF8s2VoSJB5KWbNOG73ltTDgfaUE9jqRCcX1xduhI";
PLUGIN_RODATA(coin) static const u16 g_coinMonthStart[12] = {
    0u, 31u, 59u, 90u, 120u, 151u, 181u, 212u, 243u, 273u, 304u, 334u,
};

PLUGIN_RODATA(coin) static const u32 g_coinNewsBlueLedWords[COIN_NEWS_LED_WORD_COUNT] = {
    0x00FF3C50, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0,
    0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0, 0xDEF0FF00,
    0x66008CBF, 0x00FF3C50, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0xDEF0FF00, 0xFF008CBF,
    0x8CBFDEF0, 0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0,
    0xDEF0FF00, 0x00008CBF,
};
PLUGIN_RODATA(coin) static const u32 g_coinNewsYellowLedWords[COIN_NEWS_LED_WORD_COUNT] = {
    0x00FF3C50, 0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0,
    0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0, 0xDEF0FF00,
    0x66008CBF, 0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0,
    0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0, 0xDEF0FF00,
    0x66008CBF, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00FF3C50, 0xDEF0FF00, 0xFF008CBF,
    0x8CBFDEF0, 0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0,
    0xDEF0FF00, 0x00008CBF, 0xDEF0FF00, 0xFF008CBF,
    0x8CBFDEF0, 0xDEF0FF00, 0xFF008CBF, 0x8CBFDEF0,
    0xDEF0FF00, 0x00008CBF, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000,
};
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
PLUGIN_DATA(coin) static u32 g_lastRecommended = 0;
PLUGIN_DATA(coin) static u32 g_lastEverSpent = 0;
PLUGIN_DATA(coin) static u32 g_coinKey = 0x45454545u;
PLUGIN_DATA(coin) static bool g_coinsFailed = false;
PLUGIN_DATA(coin) static bool g_patchedHome = false;

// The LED address is in OperateOnProcessByName's fixed 0x00100000 mapping.
PLUGIN_BSS(coin) static PluginMenuRegistration g_coinMenuRegistration;
PLUGIN_BSS(coin) static BlurFeatureRegistration g_coinBlurFeatureRegistration;
PLUGIN_BSS(coin) static u32 g_coinNewsLedAddress;
PLUGIN_BSS(coin) static bool g_coinNewsLedActive;
PLUGIN_BSS(coin) static volatile u32 g_coinHardDiagState;
PLUGIN_BSS(coin) static volatile u32 g_coinPendingAchievementMask;
PLUGIN_BSS(coin) static volatile Result g_coinHardDiagLastResult;
PLUGIN_BSS(coin) static volatile Result g_coinHardDiagWindowResult;
PLUGIN_BSS(coin) static volatile u32 g_coinHardDiagCompletedSerial;
PLUGIN_BSS(coin) static u32 g_coinNotificationScratchAddress;
PLUGIN_BSS(coin) static bool g_coinAchievementSavePresent;
PLUGIN_BSS(coin) static bool g_coinAchievementSaveValid;
PLUGIN_BSS(coin) static u32 g_coinAchievementSavedMask;
PLUGIN_BSS(coin) static bool g_coinAchievementsEnabled;
PLUGIN_BSS(coin) static u32 g_coinExtendedData[COIN_FILE_EXTENSION_WORDS];
PLUGIN_BSS(coin) static bool g_coinExtendedDirty;
PLUGIN_BSS(coin) static bool g_coinBalanceEvent;
PLUGIN_BSS(coin) static bool g_coinGambleEvent;
PLUGIN_BSS(coin) static char (*g_coinAchievementTitles)[40];
PLUGIN_BSS(coin) static char *g_coinAchievementDetailTitle;
PLUGIN_BSS(coin) static char *g_coinAchievementDetailMessage;
PLUGIN_BSS(coin) static CoinNewsAnchor *g_coinNewsAnchors;
PLUGIN_BSS(coin) static Result *g_coinNewsLogicalResults;
PLUGIN_BSS(coin) static u32 g_coinNewsAnchorCount;
PLUGIN_BSS(coin) static u32 g_coinNewsExpectedTotal;
PLUGIN_BSS(coin) static u32 g_coinNewsRawDeleteMode;
PLUGIN_BSS(coin) static u16 *g_coinNewsRawDeleteTitle;
PLUGIN_BSS(coin) static u32 g_coinNewsRawRemoved;

#define coinsBin       g_coinData[0]
#define coinsRec       g_coinData[1]
#define coinsTrue      g_coinData[2]
#define coinsEverSpent g_coinData[3]
#define coinsEarned    g_coinChange[0]
#define coinsSpent     g_coinChange[1]

extern bool PLUGIN_coin_AttachHomeMenu(void);
extern Result PLUGIN_coin_TriggerAchievement(u32 achievementIndex);

PLUGIN_CODE(coin) static void PLUGIN_coin_SaveMenuSettings(void)
{
    CoinMenuSettings settings;
    settings.version = COIN_SETTINGS_VERSION;
    settings.achievementsEnabled = g_coinAchievementsEnabled ? 1u : 0u;
    (void)COIN_MENU__SaveData(COIN_PLUGIN_ID, &settings, sizeof(settings));
}

PLUGIN_CODE(coin) static void PLUGIN_coin_LoadMenuSettings(void)
{
    g_coinAchievementsEnabled = true;

    CoinMenuSettings settings;
    if (COIN_MENU__LoadData(COIN_PLUGIN_ID, &settings, sizeof(settings)) &&
        settings.version == COIN_SETTINGS_VERSION &&
        settings.achievementsEnabled <= 1u)
    {
        g_coinAchievementsEnabled = settings.achievementsEnabled != 0;
    }
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_Checksum(u32 value, u32 key)
{
    value ^= key;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_EncryptWithKey(
    u32 value,
    u32 key,
    u32 *outEncrypted,
    u32 *outChecksum
)
{
    u32 encrypted = value ^ key;
    u32 checksum = PLUGIN_coin_Checksum(encrypted, key);
    *outChecksum = checksum;
    encrypted = encrypted + checksum * 2u + key;
    *outEncrypted = (encrypted & 0xFFFF0000u) |
        ((encrypted + (checksum >> 16)) & 0xFFFFu);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_DecryptWithKey(
    u32 encrypted,
    u32 checksum,
    u32 key,
    u32 *outValue
)
{
    u32 value = (encrypted & 0xFFFF0000u) |
        ((encrypted - (checksum >> 16)) & 0xFFFFu);
    value = value - checksum * 2u - key;

    if (PLUGIN_coin_Checksum(value, key) != checksum)
        return false;

    *outValue = value ^ key;
    return true;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_Encrypt(
    u32 coins,
    u32 *outEncrypted,
    u32 *outChecksum,
    u32 *outKey
)
{
    *outKey = g_coinKey;
    PLUGIN_coin_EncryptWithKey(coins, g_coinKey, outEncrypted, outChecksum);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_ResetExtendedData(void)
{
    volatile u32 *data = g_coinExtendedData;
    data[0] = 0;
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    data[4] = 0;
    data[5] = 0;
    data[6] = 0;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_SaturatingAdd(u32 left, u32 right)
{
    u32 sum = left + right;
    return sum < left ? 0xFFFFFFFFu : sum;
}

PLUGIN_CODE(coin) static u16 PLUGIN_coin_GetBlackjackDepositedCounter(void)
{
    return (u16)g_coinExtendedData[COIN_EXT_BLACKJACK_COUNTERS_WORD];
}

PLUGIN_CODE(coin) static u16 PLUGIN_coin_GetBlackjackQualifiedCounter(void)
{
    return (u16)(g_coinExtendedData[COIN_EXT_BLACKJACK_COUNTERS_WORD] >> 16);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_SetBlackjackCounters(
    u32 deposited,
    u32 qualified
)
{
    if (deposited > COIN_BLACKJACK_COUNTER_MAX)
        deposited = COIN_BLACKJACK_COUNTER_MAX;
    if (qualified > deposited)
        qualified = deposited;

    g_coinExtendedData[COIN_EXT_BLACKJACK_COUNTERS_WORD] =
        deposited | (qualified << 16);
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_GetBlackjackSurplusCounter(void)
{
    return g_coinExtendedData[COIN_EXT_BLACKJACK_SURPLUS_WORD];
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_EconomyCapacity(void)
{
    return PLUGIN_coin_SaturatingAdd(
        coinsTrue,
        PLUGIN_coin_GetBlackjackSurplusCounter()
    );
}

PLUGIN_CODE(coin) static void PLUGIN_coin_ClampTrackedAccounting(void)
{
    u32 capacity = PLUGIN_coin_EconomyCapacity();
    if (coinsEverSpent > capacity)
        coinsEverSpent = capacity;

    u32 available = capacity - coinsEverSpent;
    if (coinsRec > available)
        coinsRec = available;
    if (coinsRec > 30000u)
        coinsRec = 30000u;
}

// Blackjack passes finalized whole-Play-Coin values here. Chip conversion,
// fee rounding, and wager settlement remain the casino's responsibility.
PLUGIN_CODE(coin) u32 PLUGIN_coin_GetBlackjackDeposited(void)
{
    return PLUGIN_coin_GetBlackjackDepositedCounter();
}

PLUGIN_CODE(coin) u32 PLUGIN_coin_GetBlackjackQualified(void)
{
    return PLUGIN_coin_GetBlackjackQualifiedCounter();
}

PLUGIN_CODE(coin) u32 PLUGIN_coin_GetBlackjackSurplus(void)
{
    return PLUGIN_coin_GetBlackjackSurplusCounter();
}

PLUGIN_CODE(coin) bool PLUGIN_coin_RecordBlackjackDeposit(
    u32 depositedCoins,
    u32 destroyedFeeCoins
)
{
    if (g_coinsFailed || !g_patchedHome || destroyedFeeCoins > depositedCoins)
        return false;

    // coinsRec tracks legitimate provenance, not whether the raw Play Coins may
    // be spent.  A casino deposit can therefore exceed coinsRec (for example
    // after coins.bin has been cheated), but only the legitimate overlap is
    // allowed to advance tracked spending or Blackjack-backed progress.
    u32 trackedCoins = depositedCoins < coinsRec ? depositedCoins : coinsRec;

    // The casino's deposit fee is 5%.  Apply the same fee proportion to only
    // the legitimate part of a mixed deposit, flooring to whole Play Coins.
    // destroyedFeeCoins is already the casino's finalized fee, so cap against
    // it defensively rather than applying 5% to the fee a second time.
    u32 trackedFeeCoins = trackedCoins / 20u;
    if (trackedFeeCoins > destroyedFeeCoins)
        trackedFeeCoins = destroyedFeeCoins;

    coinsRec -= trackedCoins;
    coinsEverSpent = PLUGIN_coin_SaturatingAdd(coinsEverSpent, trackedFeeCoins);

    u32 backedCoins = trackedCoins - trackedFeeCoins;
    u32 deposited = PLUGIN_coin_GetBlackjackDepositedCounter();
    u32 qualified = PLUGIN_coin_GetBlackjackQualifiedCounter();
    deposited = PLUGIN_coin_SaturatingAdd(deposited, backedCoins);
    PLUGIN_coin_SetBlackjackCounters(deposited, qualified);
    PLUGIN_coin_ClampTrackedAccounting();
    if (trackedCoins)
        g_coinExtendedDirty = true;
    return true;
}

// Returns the whole-coin amount added to the achievement-only qualified total.
PLUGIN_CODE(coin) u32 PLUGIN_coin_RecordBlackjackQualified(u32 qualifiedCoins)
{
    if (g_coinsFailed || !g_patchedHome || !qualifiedCoins)
        return 0;

    u32 deposited = PLUGIN_coin_GetBlackjackDepositedCounter();
    u32 qualified = PLUGIN_coin_GetBlackjackQualifiedCounter();
    u32 remaining = deposited - qualified;
    u32 credited = qualifiedCoins < remaining ? qualifiedCoins : remaining;
    if (!credited)
        return 0;

    PLUGIN_coin_SetBlackjackCounters(deposited, qualified + credited);
    g_coinExtendedDirty = true;
    g_coinGambleEvent = true;
    return credited;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_RecordBlackjackWithdrawal(
    u32 receivedCoins,
    u32 netSurplusCoins
)
{
    if (g_coinsFailed || !g_patchedHome || netSurplusCoins > receivedCoins)
        return false;

    u32 newSurplus = PLUGIN_coin_SaturatingAdd(
        PLUGIN_coin_GetBlackjackSurplusCounter(),
        netSurplusCoins
    );
    u32 capacity = PLUGIN_coin_SaturatingAdd(coinsTrue, newSurplus);
    if (coinsEverSpent > capacity)
        return false;

    u32 available = capacity - coinsEverSpent;
    if (coinsRec > available || receivedCoins > available - coinsRec ||
        coinsRec > 30000u || receivedCoins > 30000u - coinsRec)
    {
        return false;
    }

    g_coinExtendedData[COIN_EXT_BLACKJACK_SURPLUS_WORD] = newSurplus;
    coinsRec += receivedCoins;
    g_coinExtendedDirty = true;
    g_coinBalanceEvent = true;
    return true;
}

PLUGIN_CODE(coin) static u16 PLUGIN_coin_GetHistoryDay(u32 index)
{
    u32 word = g_coinExtendedData[COIN_EXT_DAY_HISTORY_WORD + (index >> 1)];
    return (u16)(word >> ((index & 1u) << 4));
}

PLUGIN_CODE(coin) static void PLUGIN_coin_SetHistoryDay(u32 index, u16 value)
{
    u32 *word = &g_coinExtendedData[COIN_EXT_DAY_HISTORY_WORD + (index >> 1)];
    u32 shift = (index & 1u) << 4;
    *word = (*word & ~(0xFFFFu << shift)) | ((u32)value << shift);
}

PLUGIN_CODE(coin) static u16 PLUGIN_coin_GetTodayWalked(void)
{
    return (u16)g_coinExtendedData[COIN_EXT_TODAY_WORD];
}

PLUGIN_CODE(coin) static void PLUGIN_coin_SetTodayWalked(u16 value)
{
    g_coinExtendedData[COIN_EXT_TODAY_WORD] =
        (g_coinExtendedData[COIN_EXT_TODAY_WORD] & 0xFFFF0000u) | value;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_IsLeapYear(u32 year)
{
    if (year & 3u)
        return false;

    u32 rem = year;
    while (rem >= 100u)
        rem -= 100u;
    if (rem)
        return true;

    rem = year;
    while (rem >= 400u)
        rem -= 400u;
    return rem == 0u;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_CurrentCalendarDay(void)
{
    char date[20];
    if (COIN_HOST__dateTimeToString(date, COIN_HOST__osGetTime(), false) < 10)
        return 0;

    u32 year = (u32)(date[0] - '0') * 1000u +
        (u32)(date[1] - '0') * 100u +
        (u32)(date[2] - '0') * 10u +
        (u32)(date[3] - '0');
    u32 month = (u32)(date[5] - '0') * 10u + (u32)(date[6] - '0');
    u32 day = (u32)(date[8] - '0') * 10u + (u32)(date[9] - '0');

    if (year < 1900u || month < 1u || month > 12u || day < 1u || day > 31u)
        return 0;

    // One-based day ordinal leaves zero as the uninitialized sentinel.
    u32 ordinal = 1u;
    for (u32 y = 1900u; y < year; y++)
        ordinal += PLUGIN_coin_IsLeapYear(y) ? 366u : 365u;

    ordinal += g_coinMonthStart[month - 1u] + day - 1u;
    if (month > 2u && PLUGIN_coin_IsLeapYear(year))
        ordinal++;
    return ordinal;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_UpdateDayHistory(void)
{
    u32 currentDay = PLUGIN_coin_CurrentCalendarDay();
    if (!currentDay)
        return false;

    u32 lastDay = g_coinExtendedData[COIN_EXT_LAST_DAY_WORD];
    if (!lastDay)
    {
        g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] = currentDay;
        PLUGIN_coin_SetTodayWalked(0);
        g_coinExtendedDirty = true;
        return true;
    }

    if (currentDay == lastDay)
        return false;

    u16 oldToday = PLUGIN_coin_GetTodayWalked();
    if (currentDay < lastDay || currentDay - lastDay >= 7u)
    {
        for (u32 i = 0; i < COIN_DAY_HISTORY_COUNT; i++)
            PLUGIN_coin_SetHistoryDay(i, 0);
    }
    else
    {
        u32 delta = currentDay - lastDay;
        for (s32 i = (s32)COIN_DAY_HISTORY_COUNT - 1; i >= 0; i--)
        {
            if ((u32)i >= delta)
                PLUGIN_coin_SetHistoryDay((u32)i, PLUGIN_coin_GetHistoryDay((u32)i - delta));
            else if ((u32)i + 1u == delta)
                PLUGIN_coin_SetHistoryDay((u32)i, oldToday);
            else
                PLUGIN_coin_SetHistoryDay((u32)i, 0);
        }
    }

    g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] = currentDay;
    PLUGIN_coin_SetTodayWalked(0);
    g_coinExtendedDirty = true;
    return true;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_AddTodayWalked(u32 earned)
{
    if (!earned)
        return;

    PLUGIN_coin_UpdateDayHistory();
    u32 today = PLUGIN_coin_GetTodayWalked() + earned;
    if (today > 0xFFFFu)
        today = 0xFFFFu;
    PLUGIN_coin_SetTodayWalked((u16)today);
    g_coinExtendedDirty = true;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_SevenDayWalked(void)
{
    u32 total = PLUGIN_coin_GetTodayWalked();
    for (u32 i = 0; i < COIN_DAY_HISTORY_COUNT; i++)
        total += PLUGIN_coin_GetHistoryDay(i);
    return total;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_TryAchievement(u32 index, bool condition)
{
    if (!condition || index >= COIN_ACHIEVEMENT_COUNT)
        return;

    u32 bit = 1u << index;
    if ((g_coinAchievementSavedMask | g_coinPendingAchievementMask) & bit)
        return;

    (void)PLUGIN_coin_TriggerAchievement(index);
}

PLUGIN_CODE(coin) void PLUGIN_coin_CheckBalanceAchievements(void)
{
    if (!g_coinAchievementsEnabled)
        return;

    u32 qualified = PLUGIN_coin_GetBlackjackQualifiedCounter();

    PLUGIN_coin_TryAchievement(0u, coinsRec >= 301u && coinsTrue >= 301u);
    PLUGIN_coin_TryAchievement(1u, coinsRec >= 500u && coinsTrue >= 500u);
    PLUGIN_coin_TryAchievement(2u, coinsRec >= 777u);
    PLUGIN_coin_TryAchievement(4u, coinsRec >= 999u);
    PLUGIN_coin_TryAchievement(6u,
        coinsRec >= 1000u && coinsTrue >= 1000u && coinsEverSpent >= 300u);
    PLUGIN_coin_TryAchievement(7u, coinsRec >= 2000u && coinsTrue >= 2000u);
    PLUGIN_coin_TryAchievement(8u,
        coinsRec >= 5000u && coinsTrue >= 5000u && qualified >= 1000u);
    PLUGIN_coin_TryAchievement(9u, coinsRec >= 9999u);
    PLUGIN_coin_TryAchievement(11u, coinsRec >= 15000u && coinsTrue >= 15000u);
    PLUGIN_coin_TryAchievement(12u, coinsRec >= 20000u);
    PLUGIN_coin_TryAchievement(15u, coinsRec >= 30000u);
    PLUGIN_coin_TryAchievement(17u,
        coinsRec >= 30000u && coinsTrue >= 30000u && qualified >= 30000u);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_CheckWalkAchievements(void)
{
    if (!g_coinAchievementsEnabled)
        return;

    PLUGIN_coin_CheckBalanceAchievements();

    u32 today = PLUGIN_coin_GetTodayWalked();
    PLUGIN_coin_TryAchievement(3u, today >= 50u);
    PLUGIN_coin_TryAchievement(5u, today >= 75u);
    PLUGIN_coin_TryAchievement(10u, today >= 100u);

    u32 week = PLUGIN_coin_SevenDayWalked();
    PLUGIN_coin_TryAchievement(14u, week >= 500u);
    PLUGIN_coin_TryAchievement(16u, week >= 777u);
}

PLUGIN_CODE(coin) void PLUGIN_coin_CheckSpendAchievements(void)
{
    if (!g_coinAchievementsEnabled)
        return;

    PLUGIN_coin_TryAchievement(6u,
        coinsRec >= 1000u && coinsTrue >= 1000u && coinsEverSpent >= 300u);
    PLUGIN_coin_TryAchievement(13u, coinsEverSpent >= 10000u);
}

PLUGIN_CODE(coin) void PLUGIN_coin_CheckGambleAchievements(void)
{
    if (!g_coinAchievementsEnabled)
        return;

    u32 qualified = PLUGIN_coin_GetBlackjackQualifiedCounter();
    PLUGIN_coin_TryAchievement(8u,
        coinsRec >= 5000u && coinsTrue >= 5000u && qualified >= 1000u);
    PLUGIN_coin_TryAchievement(17u,
        coinsRec >= 30000u && coinsTrue >= 30000u && qualified >= 30000u);
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

PLUGIN_CODE(coin) static void PLUGIN_coin_EncryptExtendedData(
    const u32 *data,
    u32 key,
    u32 *stored
)
{
    u32 stream = key ^ COIN_FILE_EXTENSION_SEED;
    for (u32 i = 0; i < COIN_FILE_EXTENSION_WORDS; i++)
    {
        stream = PLUGIN_coin_Checksum(stream ^ i, key);
        stored[i] = data[i] ^ stream;
    }
    stored[COIN_FILE_EXTENSION_WORDS] = PLUGIN_coin_ExtendedDataChecksum(data, key);
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

    return stored[COIN_FILE_EXTENSION_WORDS] == PLUGIN_coin_ExtendedDataChecksum(data, key);
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

PLUGIN_CODE(coin) static Result PLUGIN_coin_WriteExtendedData(Handle file, u32 key)
{
    u32 stored[COIN_FILE_EXTENSION_WORDS + 1u];
    PLUGIN_coin_EncryptExtendedData(g_coinExtendedData, key, stored);

    u32 written = 0;
    Result rc = COIN_HOST__FSFILE_Write(
        file,
        &written,
        COIN_FILE_EXTENSION_OFFSET,
        stored,
        sizeof(stored),
        FS_WRITE_FLUSH
    );
    if (R_SUCCEEDED(rc) && written != sizeof(stored))
        rc = (Result)-29;
    return rc;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ReadExtendedData(Handle file, u32 key)
{
    u32 stored[COIN_FILE_EXTENSION_WORDS + 1u];
    u32 read = 0;
    return R_SUCCEEDED(COIN_HOST__FSFILE_Read(
            file,
            &read,
            COIN_FILE_EXTENSION_OFFSET,
            stored,
            sizeof(stored))) &&
        read == sizeof(stored) &&
        PLUGIN_coin_DecryptExtendedData(stored, key, g_coinExtendedData) &&
        PLUGIN_coin_ValidateExtendedData(g_coinExtendedData);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ReadSavedAchievementMask(
    Handle file,
    u32 key,
    u32 *outMask
)
{
    u32 stored[2];
    u32 read = 0;
    u32 mask = 0;
    if (R_FAILED(COIN_HOST__FSFILE_Read(
            file,
            &read,
            COIN_FILE_ACHIEVEMENT_OFFSET,
            stored,
            sizeof(stored))) ||
        read != sizeof(stored) ||
        !PLUGIN_coin_DecryptWithKey(stored[0], stored[1], key, &mask) ||
        (mask & ~COIN_ACHIEVEMENT_MASK))
    {
        return false;
    }

    *outMask = mask;
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ReadAndValidateBase(
    Handle file,
    u32 base[8]
)
{
    u32 read = 0;
    u32 lifetimeEarned = 0;
    u32 lifetimeSpent = 0;
    if (R_FAILED(COIN_HOST__FSFILE_Read(file, &read, 0, base, 8u * sizeof(u32))) ||
        read != 8u * sizeof(u32) ||
        base[0] > 30000u || base[1] <= 0x100000u || base[7] > 30000u ||
        !PLUGIN_coin_DecryptWithKey(base[2], base[3], base[4], &lifetimeEarned) ||
        !PLUGIN_coin_DecryptWithKey(base[5], base[6], base[4], &lifetimeSpent))
    {
        return false;
    }

    return true;
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

PLUGIN_CODE(coin) static Result PLUGIN_coin_WriteFullState(
    Handle file,
    u32 wallet,
    u32 recommended,
    u32 lifetimeEarned,
    u32 lifetimeSpent
)
{
    u32 state[18];
    state[0] = wallet;
    state[1] = g_coinOffset;
    PLUGIN_coin_EncryptWithKey(lifetimeEarned, g_coinKey, &state[2], &state[3]);
    state[4] = g_coinKey;
    PLUGIN_coin_EncryptWithKey(lifetimeSpent, g_coinKey, &state[5], &state[6]);
    state[7] = recommended;
    PLUGIN_coin_EncryptExtendedData(g_coinExtendedData, g_coinKey, &state[8]);

    u32 words = 16u;
    if (g_coinAchievementSavePresent && g_coinAchievementSaveValid)
    {
        PLUGIN_coin_EncryptWithKey(
            g_coinAchievementSavedMask & COIN_ACHIEVEMENT_MASK,
            g_coinKey,
            &state[16],
            &state[17]
        );
        words = 18u;
    }

    u32 written = 0;
    u32 size = words * sizeof(u32);
    Result rc = COIN_HOST__FSFILE_Write(file, &written, 0, state, size, FS_WRITE_FLUSH);
    if (R_SUCCEEDED(rc) && written != size)
        rc = (Result)-29;
    if (R_SUCCEEDED(rc))
        rc = COIN_HOST__FSFILE_SetSize(file, size);
    return rc;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_HandleCoins(void)
{
    bool earnedEvent = coinsEarned != 0;
    bool spentEvent = coinsEverSpent != g_lastEverSpent;
    bool balanceEvent = g_coinBalanceEvent;
    bool gambleEvent = g_coinGambleEvent;

    PLUGIN_coin_UpdateDayHistory();
    PLUGIN_coin_AddTodayWalked(coinsEarned);

    if (coinsEarned > 0 || !g_patchedHome)
    {
        coinsTrue = PLUGIN_coin_SaturatingAdd(coinsTrue, coinsEarned);
        if (!g_patchedHome)
            (void)PLUGIN_coin_GenerateRandomBytes(&g_coinKey, sizeof(g_coinKey));
    }

    coinsRec = PLUGIN_coin_SaturatingAdd(coinsRec, coinsEarned);
    PLUGIN_coin_ClampTrackedAccounting();

    u32 savedWallet = coinsBin;
    u32 savedRecommended = coinsRec;
    u32 savedLifetimeEarned = coinsTrue;
    u32 savedLifetimeSpent = coinsEverSpent;

    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
    {
        g_coinsFailed = true;
        return;
    }

    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
        0
    );
    if (R_FAILED(rc))
    {
        (void)COIN_HOST__FSUSER_CreateDirectory(
            sd,
            COIN_HOST__fsMakePath(PATH_ASCII, g_lumaPath),
            0
        );
        rc = COIN_HOST__FSUSER_OpenFile(
            &file,
            sd,
            COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
            0
        );
    }

    if (R_SUCCEEDED(rc))
    {
        rc = PLUGIN_coin_WriteFullState(
            file,
            savedWallet,
            savedRecommended,
            savedLifetimeEarned,
            savedLifetimeSpent
        );
        COIN_HOST__FSFILE_Close(file);
    }
    COIN_HOST__FSUSER_CloseArchive(sd);

    if (R_FAILED(rc))
    {
        g_coinsFailed = true;
        return;
    }

    coinsEarned = 0;
    g_lastCoins = savedWallet;
    g_lastRecommended = savedRecommended;
    g_lastEverSpent = savedLifetimeSpent;
    g_coinExtendedDirty = false;
    g_coinBalanceEvent = false;
    g_coinGambleEvent = false;

    // Achievement checks are event-driven and run only after the new state is saved.
    if (earnedEvent)
        PLUGIN_coin_CheckWalkAchievements();
    if (spentEvent)
        PLUGIN_coin_CheckSpendAchievements();
    if (balanceEvent)
        PLUGIN_coin_CheckBalanceAchievements();
    if (gambleEvent)
        PLUGIN_coin_CheckGambleAchievements();

    // ---- FILE FORMAT
    // 0x00-0x1F original coin data.
    // 0x20 packed u16 deposited/qualified Blackjack achievement counters.
    // 0x24 u32 lifetime net withdrawn Blackjack surplus.
    // 0x28-0x3B date-backed walking buckets, then the extension checksum.
    // 0x40-0x47 optional encrypted achievement mask + checksum
}

PLUGIN_CODE(coin) static void PLUGIN_coin_SetupCoins(void)
{
    // Loader runs before Rosalina and seeds its live Coin state from the
    // vanilla /gamecoin.dat value whenever no valid extended coins.bin exists.
    // Remember that case explicitly so the first Rosalina save starts from the
    // player's vanilla balance rather than trusting any incomplete file data.
    bool initializeFromVanilla = false;

    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
    {
        g_coinsFailed = true;
        return;
    }

    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_READ | FS_OPEN_WRITE,
        0
    );

    if (R_SUCCEEDED(rc))
    {
        g_coinAchievementSavePresent = false;
        g_coinAchievementSaveValid = false;
        g_coinAchievementSavedMask = 0;
        g_coinExtendedDirty = false;
        g_coinBalanceEvent = false;
        g_coinGambleEvent = false;
        PLUGIN_coin_ResetExtendedData();

        u64 fileSize = 0;
        rc = COIN_HOST__FSFILE_GetSize(file, &fileSize);
        if (R_SUCCEEDED(rc) && fileSize >= 8u)
        {
            u32 pointerRead = 0;
            rc = COIN_HOST__FSFILE_Read(
                file,
                &pointerRead,
                4u,
                &g_coinOffset,
                sizeof(g_coinOffset)
            );
            if (R_SUCCEEDED(rc) && pointerRead != sizeof(g_coinOffset))
                rc = (Result)-30;
        }

        u32 base[8];
        bool baseValid = R_SUCCEEDED(rc) &&
            fileSize >= COIN_FILE_BASE_SIZE &&
            PLUGIN_coin_ReadAndValidateBase(file, base);

        if (!baseValid)
        {
            // The original tracking state is not recoverable. Loader has already
            // preserved any sane standalone wallet word and seeded live tracking
            // from vanilla state. Rebuild the persistent tracking baseline only
            // after Home Menu attachment succeeds.
            //
            // Do NOT truncate here. The old file remains intact until
            // PLUGIN_coin_WriteFullState() has successfully written the complete
            // replacement; that function then sets the exact final size.
            initializeFromVanilla = true;
            PLUGIN_coin_ResetExtendedData();
            g_coinAchievementSavePresent = false;
            g_coinAchievementSaveValid = false;
            g_coinAchievementSavedMask = 0;
        }
        else
        {
            g_lastCoins = base[0];
            g_lastRecommended = base[7];
            g_coinOffset = base[1];
            g_coinKey = base[4];

            bool suffixValid = fileSize == COIN_FILE_BASE_SIZE;
            if (fileSize == COIN_FILE_NO_ACHIEVEMENT_SIZE ||
                fileSize == COIN_FILE_WITH_ACHIEVEMENT_SIZE)
            {
                suffixValid = PLUGIN_coin_ReadExtendedData(file, g_coinKey);
                if (suffixValid && fileSize == COIN_FILE_WITH_ACHIEVEMENT_SIZE)
                {
                    u32 savedMask = 0;
                    suffixValid = PLUGIN_coin_ReadSavedAchievementMask(
                        file,
                        g_coinKey,
                        &savedMask
                    );
                    if (suffixValid)
                    {
                        g_coinAchievementSavedMask = savedMask;
                        g_coinAchievementSavePresent = true;
                        g_coinAchievementSaveValid = true;
                    }
                }
            }

            if (!suffixValid)
            {
                // Any malformed data after a valid base is discarded as one
                // suffix. Never overwrite the base if this truncate fails.
                PLUGIN_coin_ResetExtendedData();
                g_coinAchievementSavePresent = false;
                g_coinAchievementSaveValid = false;
                g_coinAchievementSavedMask = 0;
                rc = COIN_HOST__FSFILE_SetSize(file, COIN_FILE_BASE_SIZE);
                if (R_FAILED(rc))
                    g_coinsFailed = true;
            }
        }

        if (!g_coinsFailed)
            PLUGIN_coin_UpdateDayHistory();
        if (R_FAILED(COIN_HOST__FSFILE_Close(file)))
            g_coinsFailed = true;

        if (!g_coinsFailed &&
            (g_coinOffset <= 0x100000u || !PLUGIN_coin_AttachHomeMenu()))
        {
            g_coinsFailed = true;
        }
    }
    else
    {
        g_coinsFailed = true;
    }

    COIN_HOST__FSUSER_CloseArchive(sd);

    if (g_coinsFailed)
        return;

    if (initializeFromVanilla)
    {
        // Loader validated coins.bin first and already seeded coinsTrue/coinsRec
        // from the raw gamecoin.dat value that existed before Home Menu's extended
        // wallet compensation. Keep that baseline exactly as supplied. Re-deriving
        // it here from coinsBin/g_coinDat would make a sane cheated wallet alter the
        // recovered tracked balance. Pending coinsEarned is added normally below.
        coinsEverSpent = 0;
    }

    PLUGIN_coin_ClampTrackedAccounting();
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

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawFrame(const char *title)
{
    COIN_HOST__Draw_DrawString(10, 8, COIN_FRAME_COLOR, g_coinFrameCorner);
    COIN_HOST__Draw_DrawString(16, 8, COIN_FRAME_COLOR, g_coinFrameRail);
    COIN_HOST__Draw_DrawString(148, 8, COIN_FRAME_COLOR, g_coinFrameCorner);
    COIN_HOST__Draw_DrawString(10, 16, COIN_FRAME_COLOR, g_coinFrameSide);
    COIN_HOST__Draw_DrawString(148, 16, COIN_FRAME_COLOR, g_coinFrameSide);
    COIN_HOST__Draw_DrawString(10, 24, COIN_FRAME_COLOR, g_coinFrameCorner);
    COIN_HOST__Draw_DrawString(16, 24, COIN_FRAME_COLOR, g_coinFrameRail);
    COIN_HOST__Draw_DrawString(148, 24, COIN_FRAME_COLOR, g_coinFrameCorner);
    COIN_HOST__Draw_DrawString(20, 16, COIN_FRAME_TITLE_COLOR, title);
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
    PLUGIN_coin_DrawFrame(g_coinPageTitle);
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



PLUGIN_CODE(coin) static void PLUGIN_coin_DecodeAchievementString(u16 *text, u32 capacity)
{
    if (!text)
        return;

    for (u32 i = 0; i < capacity && text[i]; i++)
    {
        u16 character = text[i];
        for (u32 j = 0; j < 62u; j++)
        {
            if (character != (u16)(u8)g_coinAchievementCipherAlphabet[j])
                continue;

            u32 previous = j ? j - 1u : 61u;
            text[i] = (u16)(u8)g_coinAchievementCipherAlphabet[previous];
            break;
        }
    }
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_HasU16Terminator(const u16 *text, u32 capacity)
{
    if (!text || !capacity || !text[0])
        return false;

    for (u32 i = 1; i < capacity; i++)
    {
        if (!text[i])
            return true;
    }

    return false;
}

PLUGIN_CODE(coin) static const char *PLUGIN_coin_PackedAssetPath(u32 assetIndex)
{
    // Keep this as branches rather than a compiler-generated host .rodata pointer table.
    if (assetIndex < COIN_ASSET_MEDIUMTOP)
    {
        if (assetIndex < COIN_ASSET_ACHV)
            return assetIndex == COIN_ASSET_ICN ? g_coinIcnPath : NULL;
        return assetIndex == COIN_ASSET_ACHV ? g_coinAchievementPath : g_coinEasyTopImagePath;
    }

    if (assetIndex < COIN_ASSET_EXTREMTOP)
        return assetIndex == COIN_ASSET_MEDIUMTOP ? g_coinMediumTopImagePath : g_coinHardTopImagePath;
    return assetIndex == COIN_ASSET_EXTREMTOP ? g_coinExtremeTopImagePath : NULL;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ReadExactFile(
    Handle file,
    u64 offset,
    void *buffer,
    u32 size
)
{
    u32 read = 0;
    return size &&
        R_SUCCEEDED(COIN_HOST__FSFILE_Read(file, &read, offset, buffer, size)) &&
        read == size;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_ParsePackedAssets(
    const PluginMenuFileContext *source,
    CoinPackedAssetLocation locations[COIN_PACKED_ASSET_COUNT]
)
{
    if (!source || !source->file || !locations || source->metadataSize < 8u)
        return (Result)0xD8A0B001u;

    CoinAssetFileHeader pluginVersion;
    if (!PLUGIN_coin_ReadExactFile(
            source->file,
            source->metadataOffset,
            &pluginVersion,
            sizeof(pluginVersion)) ||
        pluginVersion.magic != COIN_ASSET_VERSION_MAGIC)
    {
        return (Result)0xD8A0B002u;
    }

    u64 cursor = (u64)source->metadataOffset + sizeof(pluginVersion);
    u64 metadataEnd = (u64)source->metadataOffset + source->metadataSize;

    for (u32 i = 0; i < COIN_PACKED_ASSET_COUNT; i++)
    {
        if (i < COIN_ASSET_EASYTOP)
        {
            CoinPackedAssetHeader header;
            if (cursor + sizeof(header) > metadataEnd ||
                !PLUGIN_coin_ReadExactFile(source->file, cursor, &header, sizeof(header)) ||
                header.magic != COIN_ASSET_VERSION_MAGIC ||
                header.compressedSize < 4u ||
                cursor + sizeof(header) + header.compressedSize > metadataEnd)
            {
                return (Result)0xD8A0B003u;
            }

            locations[i].version = header.version;
            locations[i].dataOffset = (u32)(cursor + sizeof(header));
            locations[i].dataSize = header.compressedSize;
            cursor += sizeof(header) + header.compressedSize;
            continue;
        }

        CoinAssetFileHeader header;
        u32 count = 0;
        if (cursor + sizeof(header) + sizeof(count) > metadataEnd ||
            !PLUGIN_coin_ReadExactFile(source->file, cursor, &header, sizeof(header)) ||
            header.magic != COIN_ASSET_VERSION_MAGIC ||
            !PLUGIN_coin_ReadExactFile(
                source->file,
                cursor + sizeof(header),
                &count,
                sizeof(count)))
        {
            return (Result)0xD8A0B003u;
        }

        u32 expectedCount = g_coinAchievementImageCounts[i - COIN_ASSET_EASYTOP];
        if (count != expectedCount || !count || count > 5u)
            return (Result)0xD8A0B003u;

        u32 sizes[5];
        u64 sizeTableOffset = cursor + sizeof(header) + sizeof(count);
        u32 sizeTableSize = count * sizeof(u32);
        if (sizeTableOffset + sizeTableSize > metadataEnd ||
            !PLUGIN_coin_ReadExactFile(source->file, sizeTableOffset, sizes, sizeTableSize))
        {
            return (Result)0xD8A0B003u;
        }

        u64 rawSize = sizeof(count) + sizeTableSize;
        for (u32 j = 0; j < count; j++)
        {
            if (!sizes[j] || sizes[j] > COIN_NOTIFICATION_IMAGE_MAX)
                return (Result)0xD8A0B003u;
            rawSize += sizes[j];
        }

        u64 installedSize = sizeof(header) + rawSize;
        if (cursor + installedSize > metadataEnd || installedSize > 0xFFFFFFFFu)
            return (Result)0xD8A0B003u;

        locations[i].version = header.version;
        locations[i].dataOffset = (u32)cursor;
        locations[i].dataSize = (u32)installedSize;
        cursor += installedSize;
    }

    return 0;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_GetInstalledAssetVersion(
    FS_Archive archive,
    const char *path,
    u32 *version
)
{
    if (!archive || !path || !version)
        return false;

    Handle file = 0;
    if (R_FAILED(COIN_HOST__FSUSER_OpenFile(
            &file,
            archive,
            COIN_HOST__fsMakePath(PATH_ASCII, path),
            FS_OPEN_READ,
            0)))
    {
        return false;
    }

    CoinAssetFileHeader header;
    bool ok = PLUGIN_coin_ReadExactFile(file, 0, &header, sizeof(header)) &&
        header.magic == COIN_ASSET_VERSION_MAGIC;
    COIN_HOST__FSFILE_Close(file);

    if (ok)
        *version = header.version;
    return ok;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_EnsureAchievementAssetDirectory(FS_Archive archive)
{
    if (!archive)
        return;

    (void)COIN_HOST__FSUSER_CreateDirectory(
        archive,
        COIN_HOST__fsMakePath(PATH_ASCII, g_lumaPath),
        0
    );
    (void)COIN_HOST__FSUSER_CreateDirectory(
        archive,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinAchievementDirPath),
        0
    );
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_CopyPackedAssetRange(
    const PluginMenuFileContext *source,
    u32 sourceOffset,
    u32 sourceSize,
    const char *outputPath,
    u8 *scratch
)
{
    if (!source || !source->archive || !source->file || !sourceSize || !outputPath)
        return (Result)0xD8A0B004u;

    FS_Path path = COIN_HOST__fsMakePath(PATH_ASCII, outputPath);
    (void)COIN_HOST__FSUSER_CreateFile(source->archive, path, 0, sourceSize);

    Handle output = 0;
    Result rc = COIN_HOST__FSUSER_OpenFile(
        &output,
        source->archive,
        path,
        FS_OPEN_WRITE,
        0
    );
    if (R_FAILED(rc))
        return rc;

    rc = COIN_HOST__FSFILE_SetSize(output, sourceSize);
    if (R_FAILED(rc))
        goto cleanup;

    u32 copied = 0;
    while (copied < sourceSize)
    {
        u32 chunk = sourceSize - copied;
        if (chunk > 0x1000u)
            chunk = 0x1000u;

        if (!PLUGIN_coin_ReadExactFile(
                source->file,
                (u64)sourceOffset + copied,
                scratch,
                chunk))
        {
            rc = (Result)0xD8A0B007u;
            goto cleanup;
        }

        u32 written = 0;
        rc = COIN_HOST__FSFILE_Write(
            output,
            &written,
            copied,
            scratch,
            chunk,
            FS_WRITE_FLUSH
        );
        if (R_FAILED(rc) || written != chunk)
        {
            rc = R_FAILED(rc) ? rc : (Result)0xD8A0B008u;
            goto cleanup;
        }

        copied += chunk;
    }

cleanup:
    COIN_HOST__FSFILE_Close(output);
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_UnpackAssetMaskWithContext(
    const PluginMenuFileContext *source,
    const CoinPackedAssetLocation locations[COIN_PACKED_ASSET_COUNT],
    u32 assetMask
)
{
    u32 scratchBase = 0;
    u8 *scratch = NULL;
    assetMask &= COIN_ASSET_ALL_MASK;
    if (!assetMask)
        return 0;
    if (!source || !source->archive || !source->file || !locations)
        return (Result)0xD8A0B004u;

    PLUGIN_coin_EnsureAchievementAssetDirectory(source->archive);
    if (assetMask & ~((1u << COIN_ASSET_EASYTOP) - 1u))
    {
        if (!COIN_MENU__TempAlloc(0x1000u, &scratchBase))
            return (Result)-8;
        scratch = (u8 *)scratchBase;
    }

    Result result = 0;
    for (u32 i = 0; i < COIN_PACKED_ASSET_COUNT; i++)
    {
        if (!(assetMask & (1u << i)))
            continue;

        Result rc;
        if (i < COIN_ASSET_EASYTOP)
        {
            rc = COIN_MENU__UnpackLz10File(
                source,
                locations[i].dataOffset,
                locations[i].dataSize,
                PLUGIN_coin_PackedAssetPath(i)
            );
        }
        else
        {
            rc = PLUGIN_coin_CopyPackedAssetRange(
                source,
                locations[i].dataOffset,
                locations[i].dataSize,
                PLUGIN_coin_PackedAssetPath(i),
                scratch
            );
        }
        if (R_FAILED(rc))
        {
            result = rc;
            break;
        }

        u32 installedVersion = 0;
        if (!PLUGIN_coin_GetInstalledAssetVersion(
                source->archive,
                PLUGIN_coin_PackedAssetPath(i),
                &installedVersion) ||
            installedVersion != locations[i].version)
        {
            result = (Result)0xD8A0B005u;
            break;
        }
    }

    if (scratchBase)
        COIN_MENU__TempFree(scratchBase, 0x1000u);
    return result;
}

// Deliberately kept as one recallable helper for future lazy recovery paths.
// It opens the active Rosalina Coin entry, finds its packed metadata assets,
// creates /luma/coinachv if needed, and unpacks exactly the requested files.
PLUGIN_CODE(coin) static Result PLUGIN_coin_UnpackAssetMask(u32 assetMask)
{
    PluginMenuFileContext source;
    if (!COIN_MENU__OpenPluginFile(COIN_PLUGIN_ID, &source))
        return (Result)0xD8A0B006u;

    CoinPackedAssetLocation locations[COIN_PACKED_ASSET_COUNT];
    Result rc = PLUGIN_coin_ParsePackedAssets(&source, locations);
    if (R_SUCCEEDED(rc))
        rc = PLUGIN_coin_UnpackAssetMaskWithContext(&source, locations, assetMask);

    COIN_MENU__ClosePluginFile(&source);
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_EnsurePackedAssets(void)
{
    PluginMenuFileContext source;
    if (!COIN_MENU__OpenPluginFile(COIN_PLUGIN_ID, &source))
        return (Result)0xD8A0B006u;

    CoinPackedAssetLocation locations[COIN_PACKED_ASSET_COUNT];
    Result rc = PLUGIN_coin_ParsePackedAssets(&source, locations);
    if (R_FAILED(rc))
    {
        COIN_MENU__ClosePluginFile(&source);
        return rc;
    }

    u32 mismatchMask = 0;
    for (u32 i = 0; i < COIN_PACKED_ASSET_COUNT; i++)
    {
        u32 installedVersion = 0;
        if (!PLUGIN_coin_GetInstalledAssetVersion(
                source.archive,
                PLUGIN_coin_PackedAssetPath(i),
                &installedVersion) ||
            installedVersion != locations[i].version)
        {
            mismatchMask |= 1u << i;
        }
    }

    if (mismatchMask)
        rc = PLUGIN_coin_UnpackAssetMaskWithContext(&source, locations, mismatchMask);

    COIN_MENU__ClosePluginFile(&source);
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_ReadAchievement(
    u32 achievementIndex,
    CoinAchievement *achievement
)
{
    if (achievementIndex >= COIN_ACHIEVEMENT_COUNT || !achievement)
        return (Result)-26;

    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
        return rc;

    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinAchievementPath),
        FS_OPEN_READ,
        0
    );
    if (R_FAILED(rc))
        goto cleanup_archive;

    u64 fileSize = 0;
    rc = COIN_HOST__FSFILE_GetSize(file, &fileSize);
    if (R_FAILED(rc))
        goto cleanup_file;

    u64 expectedSize = sizeof(CoinAssetFileHeader) +
        sizeof(CoinAchievementFileHeader) +
        (u64)COIN_ACHIEVEMENT_COUNT * sizeof(CoinAchievement);
    if (fileSize != expectedSize)
    {
        rc = (Result)-20;
        goto cleanup_file;
    }

    CoinAssetFileHeader assetHeader;
    u32 read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, 0, &assetHeader, sizeof(assetHeader));
    if (R_FAILED(rc) || read != sizeof(assetHeader) ||
        assetHeader.magic != COIN_ASSET_VERSION_MAGIC)
    {
        rc = R_FAILED(rc) ? rc : (Result)-21;
        goto cleanup_file;
    }

    CoinAchievementFileHeader header;
    read = 0;
    rc = COIN_HOST__FSFILE_Read(
        file,
        &read,
        sizeof(CoinAssetFileHeader),
        &header,
        sizeof(header)
    );
    if (R_FAILED(rc) || read != sizeof(header))
    {
        rc = R_FAILED(rc) ? rc : (Result)-21;
        goto cleanup_file;
    }

    if (header.magic != COIN_ACHIEVEMENT_MAGIC ||
        header.count != COIN_ACHIEVEMENT_COUNT ||
        header.entrySize != sizeof(CoinAchievement))
    {
        rc = (Result)-22;
        goto cleanup_file;
    }

    u64 recordOffset = sizeof(CoinAssetFileHeader) +
        sizeof(CoinAchievementFileHeader) +
        (u64)achievementIndex * sizeof(CoinAchievement);
    read = 0;
    rc = COIN_HOST__FSFILE_Read(
        file,
        &read,
        recordOffset,
        achievement,
        sizeof(*achievement)
    );
    if (R_FAILED(rc) || read != sizeof(*achievement))
    {
        rc = R_FAILED(rc) ? rc : (Result)-23;
        goto cleanup_file;
    }

    if (!PLUGIN_coin_HasU16Terminator(achievement->title, 32u) ||
        !PLUGIN_coin_HasU16Terminator(achievement->notificationMessage, 256u) ||
        !PLUGIN_coin_HasU16Terminator(achievement->menuMessage, 256u) ||
        achievement->difficulty > 3u ||
        achievement->imageIndex >= g_coinAchievementImageCounts[achievement->difficulty] ||
        achievement->reserved)
    {
        rc = (Result)-24;
        goto cleanup_file;
    }

    PLUGIN_coin_DecodeAchievementString(achievement->title, 32u);
    PLUGIN_coin_DecodeAchievementString(achievement->notificationMessage, 256u);
    PLUGIN_coin_DecodeAchievementString(achievement->menuMessage, 256u);

    rc = 0;

cleanup_file:
    COIN_HOST__FSFILE_Close(file);
cleanup_archive:
    COIN_HOST__FSUSER_CloseArchive(sd);
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_RecordAchievementEarned(u32 achievementIndex)
{
    if (achievementIndex >= COIN_ACHIEVEMENT_COUNT)
        return (Result)-26;

    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
        return rc;

    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_READ | FS_OPEN_WRITE,
        0
    );
    if (R_FAILED(rc))
        goto cleanup_archive;

    u64 fileSize = 0;
    rc = COIN_HOST__FSFILE_GetSize(file, &fileSize);
    if (R_FAILED(rc))
        goto cleanup_file;
    if (fileSize < COIN_FILE_BASE_SIZE)
    {
        rc = (Result)-27;
        goto cleanup_file;
    }

    u32 key = 0;
    u32 read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, 0x10u, &key, sizeof(key));
    if (R_FAILED(rc) || read != sizeof(key))
    {
        rc = R_FAILED(rc) ? rc : (Result)-30;
        goto cleanup_file;
    }

    if (fileSize < COIN_FILE_NO_ACHIEVEMENT_SIZE)
    {
        PLUGIN_coin_ResetExtendedData();
        rc = PLUGIN_coin_WriteExtendedData(file, key);
        if (R_FAILED(rc))
            goto cleanup_file;
        fileSize = COIN_FILE_NO_ACHIEVEMENT_SIZE;
    }

    u32 earnedMask = 0;
    if (fileSize >= COIN_FILE_WITH_ACHIEVEMENT_SIZE)
    {
        u32 stored[2];
        read = 0;
        rc = COIN_HOST__FSFILE_Read(file, &read, COIN_FILE_ACHIEVEMENT_OFFSET, stored, sizeof(stored));
        if (R_FAILED(rc) || read != sizeof(stored))
        {
            rc = R_FAILED(rc) ? rc : (Result)-30;
            goto cleanup_file;
        }
        if (!PLUGIN_coin_DecryptWithKey(stored[0], stored[1], key, &earnedMask))
        {
            rc = (Result)-28;
            goto cleanup_file;
        }
    }

    earnedMask &= COIN_ACHIEVEMENT_MASK;
    earnedMask |= 1u << achievementIndex;

    u32 stored[2];
    PLUGIN_coin_EncryptWithKey(earnedMask, key, &stored[0], &stored[1]);
    u32 written = 0;
    rc = COIN_HOST__FSFILE_Write(
        file,
        &written,
        COIN_FILE_ACHIEVEMENT_OFFSET,
        stored,
        sizeof(stored),
        FS_WRITE_FLUSH
    );
    if (R_SUCCEEDED(rc) && written != sizeof(stored))
        rc = (Result)-29;
    if (R_SUCCEEDED(rc))
    {
        g_coinAchievementSavedMask = earnedMask;
        g_coinAchievementSavePresent = true;
        g_coinAchievementSaveValid = true;
    }

cleanup_file:
    COIN_HOST__FSFILE_Close(file);
cleanup_archive:
    COIN_HOST__FSUSER_CloseArchive(sd);
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_ReadAchievementEarnedMask(u32 *outMask)
{
    if (!outMask)
        return (Result)-26;

    *outMask = 0;
    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
        return rc;

    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
        FS_OPEN_READ,
        0
    );
    if (R_FAILED(rc))
        goto cleanup_archive;

    u64 fileSize = 0;
    rc = COIN_HOST__FSFILE_GetSize(file, &fileSize);
    if (R_FAILED(rc))
        goto cleanup_file;
    if (fileSize < COIN_FILE_WITH_ACHIEVEMENT_SIZE)
    {
        rc = 0;
        goto cleanup_file;
    }

    u32 key = 0;
    u32 stored[2];
    u32 read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, 0x10u, &key, sizeof(key));
    if (R_FAILED(rc) || read != sizeof(key))
    {
        rc = R_FAILED(rc) ? rc : (Result)-30;
        goto cleanup_file;
    }

    read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, COIN_FILE_ACHIEVEMENT_OFFSET, stored, sizeof(stored));
    if (R_FAILED(rc) || read != sizeof(stored))
    {
        rc = R_FAILED(rc) ? rc : (Result)-30;
        goto cleanup_file;
    }

    u32 mask = 0;
    if (!PLUGIN_coin_DecryptWithKey(stored[0], stored[1], key, &mask) ||
        (mask & ~COIN_ACHIEVEMENT_MASK))
    {
        rc = (Result)-28;
        goto cleanup_file;
    }

    *outMask = mask;
    rc = 0;

cleanup_file:
    COIN_HOST__FSFILE_Close(file);
cleanup_archive:
    COIN_HOST__FSUSER_CloseArchive(sd);
    return rc;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_CopyAsciiText(
    char *out,
    u32 capacity,
    const u16 *text
)
{
    if (!out || !capacity)
        return;

    u32 pos = 0;
    if (text)
    {
        for (u32 i = 0; text[i] && pos + 1u < capacity; i++)
        {
            u16 ch = text[i];
            if (ch == 0xE024u || ch == 0xE026u || ch == 0xE075u)
                continue;
            if (ch == 0x00ABu)
                ch = '<';
            else if (ch == 0x00BBu)
                ch = '>';

            if (ch == '\n' || (ch >= 0x20u && ch <= 0x7Eu))
                out[pos++] = (char)ch;
            else
                out[pos++] = '?';
        }
    }
    out[pos] = 0;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_U16Len(const u16 *text)
{
    u32 len = 0;
    while (text[len])
        len++;
    return len;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_U16FixedEquals(
    const u16 *left,
    const u16 *right,
    u32 maxUnits
)
{
    if (!left || !right)
        return false;

    for (u32 i = 0; i < maxUnits; i++)
    {
        if (left[i] != right[i])
            return false;
        if (!left[i])
            return true;
    }

    return false;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_EnsureNotificationScratch(void)
{
    if (g_coinNotificationScratchAddress)
        return 0;
    if (!COIN_MENU__TempAlloc(COIN_NOTIFICATION_SCRATCH_SIZE, &g_coinNotificationScratchAddress))
        return (Result)-8;
    return 0;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_ReleaseNotificationScratch(void)
{
    if (!g_coinNotificationScratchAddress)
        return 0;
    COIN_MENU__TempFree(g_coinNotificationScratchAddress, COIN_NOTIFICATION_SCRATCH_SIZE);
    g_coinNotificationScratchAddress = 0;
    return 0;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_MatchNewsLedWords(
    volatile const u32 *candidate,
    const u32 *pattern
)
{
    for (u32 i = 0; i < COIN_NEWS_LED_WORD_COUNT; i++)
    {
        if (candidate[i] != pattern[i])
            return false;
    }
    return true;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_SetNewsLedYellowCallback(
    Handle processHandle,
    u32 textSize,
    u32 roSize,
    u32 rwSize
)
{
    if (!g_coinNewsLedAddress)
    {
        u32 totalSize = textSize + roSize + rwSize;
        u32 wordCount = totalSize / sizeof(u32);
        volatile u32 *base = (volatile u32*)0x00100000u;
        volatile u32 *found = NULL;

        // The complete bank is identical in old USA and current EUR NEWS, while
        // its address moves. The following blue-record word is a unique guard.
        for (u32 i = 0; i + COIN_NEWS_LED_WORD_COUNT + 1u <= wordCount; i++)
        {
            volatile u32 *candidate = base + i;
            bool blue = PLUGIN_coin_MatchNewsLedWords(candidate, g_coinNewsBlueLedWords);
            bool yellow = PLUGIN_coin_MatchNewsLedWords(candidate, g_coinNewsYellowLedWords);

            if ((!blue && !yellow) ||
                candidate[COIN_NEWS_LED_WORD_COUNT] != 0x00FF5050u)
            {
                continue;
            }

            if (found)
            {
                COIN_HOST__svcCloseHandle(processHandle);
                return (Result)-2;
            }
            found = candidate;
        }

        if (!found)
        {
            COIN_HOST__svcCloseHandle(processHandle);
            return (Result)-2;
        }

        g_coinNewsLedAddress = (u32)found;
    }

    volatile u32 *dst = (volatile u32*)g_coinNewsLedAddress;
    for (u32 i = 0; i < COIN_NEWS_LED_WORD_COUNT; i++)
        dst[i] = g_coinNewsYellowLedWords[i];

    COIN_HOST__svcFlushEntireDataCache();
    COIN_HOST__svcInvalidateEntireInstructionCache();
    COIN_HOST__svcCloseHandle(processHandle);
    return 0;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_RestoreNewsLedBlueCallback(
    Handle processHandle,
    u32 textSize,
    u32 roSize,
    u32 rwSize
)
{
    (void)textSize;
    (void)roSize;
    (void)rwSize;

    if (!g_coinNewsLedAddress)
    {
        COIN_HOST__svcCloseHandle(processHandle);
        return (Result)-2;
    }

    // Restore the exact address selected by this window's scan.
    volatile u32 *dst = (volatile u32*)g_coinNewsLedAddress;
    for (u32 i = 0; i < COIN_NEWS_LED_WORD_COUNT; i++)
        dst[i] = g_coinNewsBlueLedWords[i];
    g_coinNewsLedAddress = 0;

    COIN_HOST__svcFlushEntireDataCache();
    COIN_HOST__svcInvalidateEntireInstructionCache();
    COIN_HOST__svcCloseHandle(processHandle);
    return 0;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_BeginNewsLedWindow(void)
{
    Result rc = 0;

    if (!g_coinNewsLedActive)
    {
        rc = COIN_HOST__OperateOnProcessByName(g_coinNewsName, PLUGIN_coin_SetNewsLedYellowCallback);
        if (R_FAILED(rc))
            return rc;
        g_coinNewsLedActive = true;
    }

    return 0;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_EndNewsLedWindow(void)
{
    if (g_coinNewsLedActive)
    {
        Result rc = COIN_HOST__OperateOnProcessByName(
            g_coinNewsName,
            PLUGIN_coin_RestoreNewsLedBlueCallback
        );

        if (R_SUCCEEDED(rc))
            g_coinNewsLedActive = false;
        else if (rc == (Result)-1)
        {
            // NEWS vanished, so its process image and the patch vanished too.
            g_coinNewsLedActive = false;
            g_coinNewsLedAddress = 0;
        }
        else
            return rc;
    }

    return PLUGIN_coin_ReleaseNotificationScratch();
}

PLUGIN_CODE(coin) static const char *PLUGIN_coin_AchievementImagePackPath(u32 difficulty)
{
    // Avoid a compiler-generated pointer lookup table outside plugin rodata.
    if (difficulty < 2u)
        return difficulty == 0u ? g_coinEasyTopImagePath : g_coinMediumTopImagePath;
    if (difficulty < 4u)
        return difficulty == 2u ? g_coinHardTopImagePath : g_coinExtremeTopImagePath;
    return NULL;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_LoadAchievementImage(
    const CoinAchievement *achievement,
    u32 *outImageSize
)
{
    if (!achievement || !outImageSize || achievement->difficulty > 3u)
        return (Result)-25;

    const char *packPath = PLUGIN_coin_AchievementImagePackPath(achievement->difficulty);
    if (!packPath)
        return (Result)-25;

    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
        return rc;

    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, packPath),
        FS_OPEN_READ,
        0
    );
    if (R_FAILED(rc))
        goto cleanup_archive;

    u64 fileSize = 0;
    rc = COIN_HOST__FSFILE_GetSize(file, &fileSize);
    if (R_FAILED(rc))
        goto cleanup_file;

    CoinAssetFileHeader assetHeader;
    u32 read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, 0, &assetHeader, sizeof(assetHeader));
    if (R_FAILED(rc) || read != sizeof(assetHeader) ||
        assetHeader.magic != COIN_ASSET_VERSION_MAGIC)
    {
        rc = R_FAILED(rc) ? rc : (Result)-3;
        goto cleanup_file;
    }

    u32 count = 0;
    read = 0;
    rc = COIN_HOST__FSFILE_Read(
        file,
        &read,
        sizeof(CoinAssetFileHeader),
        &count,
        sizeof(count)
    );
    if (R_FAILED(rc) || read != sizeof(count))
    {
        rc = R_FAILED(rc) ? rc : (Result)-3;
        goto cleanup_file;
    }

    u32 expectedCount = g_coinAchievementImageCounts[achievement->difficulty];
    if (count != expectedCount || count == 0u || count > 5u || achievement->imageIndex >= count)
    {
        rc = (Result)-3;
        goto cleanup_file;
    }

    u32 sizes[5];
    read = 0;
    rc = COIN_HOST__FSFILE_Read(
        file,
        &read,
        sizeof(CoinAssetFileHeader) + sizeof(count),
        sizes,
        count * sizeof(u32)
    );
    if (R_FAILED(rc) || read != count * sizeof(u32))
    {
        rc = R_FAILED(rc) ? rc : (Result)-3;
        goto cleanup_file;
    }

    u64 imageOffset = sizeof(CoinAssetFileHeader) +
        sizeof(count) + (u64)count * sizeof(u32);
    u64 expectedFileSize = imageOffset;
    u32 imageSize = 0;
    for (u32 i = 0; i < count; i++)
    {
        if (sizes[i] == 0u || sizes[i] > COIN_NOTIFICATION_IMAGE_MAX)
        {
            rc = (Result)-3;
            goto cleanup_file;
        }

        if (i < achievement->imageIndex)
            imageOffset += sizes[i];
        if (i == achievement->imageIndex)
            imageSize = sizes[i];
        expectedFileSize += sizes[i];
    }

    if (expectedFileSize != fileSize || !imageSize)
    {
        rc = (Result)-3;
        goto cleanup_file;
    }

    rc = PLUGIN_coin_EnsureNotificationScratch();
    if (R_FAILED(rc))
        goto cleanup_file;

    read = 0;
    rc = COIN_HOST__FSFILE_Read(
        file,
        &read,
        imageOffset,
        (void*)g_coinNotificationScratchAddress,
        imageSize
    );
    if (R_FAILED(rc) || read != imageSize)
    {
        rc = R_FAILED(rc) ? rc : (Result)-6;
        goto cleanup_file;
    }

    const u8 *image = (const u8*)g_coinNotificationScratchAddress;
    if (imageSize < 4u || image[0] != 0xFFu || image[1] != 0xD8u ||
        image[imageSize - 2u] != 0xFFu || image[imageSize - 1u] != 0xD9u)
    {
        rc = (Result)-7;
        goto cleanup_file;
    }

    *outImageSize = imageSize;
    rc = 0;

cleanup_file:
    if (file)
        COIN_HOST__FSFILE_Close(file);
cleanup_archive:
    COIN_HOST__FSUSER_CloseArchive(sd);
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_SendDiagnosticNotification(
    const CoinAchievement *achievement
)
{
    if (!achievement || achievement->difficulty > 3u)
        return (Result)-25;

    u32 imageSize = 0;
    Result rc = PLUGIN_coin_LoadAchievementImage(achievement, &imageSize);
    if (R_FAILED(rc))
    {
        u32 assetIndex = COIN_ASSET_EASYTOP + achievement->difficulty;
        Result unpackRc = PLUGIN_coin_UnpackAssetMask(1u << assetIndex);
        if (R_FAILED(unpackRc))
            return unpackRc;
        rc = PLUGIN_coin_LoadAchievementImage(achievement, &imageSize);
        if (R_FAILED(rc))
            return rc;
    }

    Handle newsHandle = 0;
    rc = COIN_HOST__srvGetServiceHandle(&newsHandle, g_coinNewsServiceName);
    if (R_FAILED(rc))
        return rc;

    NotificationHeader hdr;
    volatile u8 *hdrBytes = (volatile u8*)&hdr;
    for (u32 i = 0; i < sizeof(hdr); i++)
        hdrBytes[i] = 0;

    hdr.dataSet = true;
    hdr.unread = true;
    hdr.enableJPEG = true;
    hdr.processID = g_coinAchievementProcessIds[achievement->difficulty];
    // osGetTime uses 1900 while NEWS stores milliseconds from 2000.
    u64 now = COIN_HOST__osGetTime();
    hdr.time = now >= COIN_NEWS_EPOCH_OFFSET_MS ? now - COIN_NEWS_EPOCH_OFFSET_MS : 0;

    u32 titleLen = PLUGIN_coin_U16Len(achievement->title);
    if (titleLen >= 32u)
        titleLen = 31u;
    for (u32 i = 0; i < titleLen; i++)
        hdr.title[i] = achievement->title[i];
    hdr.title[titleLen] = 0;

    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    u32 *cmdbuf = (u32*)(tls + 0x80);
    u32 messageSize = (PLUGIN_coin_U16Len(achievement->notificationMessage) + 1u) * sizeof(u16);

    cmdbuf[0] = IPC_MakeHeader(0x1, 3, 6);
    cmdbuf[1] = sizeof(NotificationHeader);
    cmdbuf[2] = messageSize;
    cmdbuf[3] = imageSize;
    cmdbuf[4] = IPC_Desc_Buffer(sizeof(NotificationHeader), IPC_BUFFER_R);
    cmdbuf[5] = (u32)&hdr;
    cmdbuf[6] = IPC_Desc_Buffer(messageSize, IPC_BUFFER_R);
    cmdbuf[7] = (u32)achievement->notificationMessage;
    cmdbuf[8] = IPC_Desc_Buffer(imageSize, IPC_BUFFER_R);
    cmdbuf[9] = g_coinNotificationScratchAddress;

    rc = COIN_HOST__svcSendSyncRequest(newsHandle);
    if (R_SUCCEEDED(rc))
        rc = (Result)cmdbuf[1];
    COIN_HOST__svcCloseHandle(newsHandle);
    return rc;
}
PLUGIN_CODE(coin) static u32 *PLUGIN_coin_NewsDumpCommandBuffer(void)
{
    u32 *cmdbuf;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(cmdbuf));
    return (u32 *)((u8 *)cmdbuf + 0x80);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_NewsDumpZero(void *ptr, u32 size)
{
    volatile u8 *p = (volatile u8 *)ptr;
    for (u32 i = 0; i < size; i++)
        p[i] = 0;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_NewsDumpGetTotal(Handle news, u32 *out)
{
    u32 *cmdbuf = PLUGIN_coin_NewsDumpCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x5, 0, 0);
    Result rc = COIN_HOST__svcSendSyncRequest(news);
    if (R_SUCCEEDED(rc))
    {
        rc = (Result)cmdbuf[1];
        if (R_SUCCEEDED(rc))
            *out = cmdbuf[2];
    }
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_NewsDumpGetTotalArrived(Handle news, u32 *out)
{
    u32 *cmdbuf = PLUGIN_coin_NewsDumpCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x14, 0, 0);
    Result rc = COIN_HOST__svcSendSyncRequest(news);
    if (R_SUCCEEDED(rc))
    {
        rc = (Result)cmdbuf[1];
        if (R_SUCCEEDED(rc))
            *out = cmdbuf[2];
    }
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_NewsDumpGetDbHeader(Handle news, u8 out[0x10])
{
    u32 *cmdbuf = PLUGIN_coin_NewsDumpCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0xA, 1, 2);
    cmdbuf[1] = 0x10;
    cmdbuf[2] = IPC_Desc_Buffer(0x10, IPC_BUFFER_W);
    cmdbuf[3] = (u32)out;
    Result rc = COIN_HOST__svcSendSyncRequest(news);
    if (R_SUCCEEDED(rc))
        rc = (Result)cmdbuf[1];
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_NewsDumpGetHeader(
    Handle news,
    u32 slot,
    NotificationHeader *out
)
{
    u32 *cmdbuf = PLUGIN_coin_NewsDumpCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0xB, 2, 2);
    cmdbuf[1] = slot;
    cmdbuf[2] = sizeof(NotificationHeader);
    cmdbuf[3] = IPC_Desc_Buffer(sizeof(NotificationHeader), IPC_BUFFER_W);
    cmdbuf[4] = (u32)out;
    Result rc = COIN_HOST__svcSendSyncRequest(news);
    if (R_SUCCEEDED(rc))
        rc = (Result)cmdbuf[1];
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_NewsDumpGetMessage(
    Handle news,
    u32 slot,
    void *out,
    u32 *outSize
)
{
    u32 *cmdbuf = PLUGIN_coin_NewsDumpCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0xC, 2, 2);
    cmdbuf[1] = slot;
    cmdbuf[2] = 0x1780;
    cmdbuf[3] = IPC_Desc_Buffer(0x1780, IPC_BUFFER_W);
    cmdbuf[4] = (u32)out;
    Result rc = COIN_HOST__svcSendSyncRequest(news);
    if (R_SUCCEEDED(rc))
    {
        rc = (Result)cmdbuf[1];
        if (R_SUCCEEDED(rc))
        {
            u32 size = cmdbuf[2];
            *outSize = size <= 0x1780 ? size : 0x1780;
        }
    }
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_NewsDumpWrite(
    Handle file,
    u64 *offset,
    const void *data,
    u32 size,
    u32 flags
)
{
    u32 written = 0;
    Result rc = COIN_HOST__FSFILE_Write(file, &written, *offset, data, size, flags);
    if (R_FAILED(rc))
        return rc;
    if (written != size)
        return (Result)0xD900182Fu;
    *offset += written;
    return 0;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_OpenNextNewsDump(FS_Archive sd, Handle *outFile)
{
    (void)COIN_HOST__FSUSER_CreateDirectory(
        sd,
        COIN_HOST__fsMakePath(PATH_ASCII, g_lumaPath),
        0
    );

    u32 tens = 0;
    u32 ones = 0;
    for (u32 i = 0; i < 100; i++)
    {
        g_coinNewsDumpPath[14] = (char)('0' + tens);
        g_coinNewsDumpPath[15] = (char)('0' + ones);

        Handle probe = 0;
        Result rc = COIN_HOST__FSUSER_OpenFile(
            &probe,
            sd,
            COIN_HOST__fsMakePath(PATH_ASCII, g_coinNewsDumpPath),
            FS_OPEN_READ,
            0
        );

        if (R_SUCCEEDED(rc))
        {
            COIN_HOST__FSFILE_Close(probe);
            ones++;
            if (ones == 10)
            {
                ones = 0;
                tens++;
            }
            continue;
        }

        return COIN_HOST__FSUSER_OpenFile(
            outFile,
            sd,
            COIN_HOST__fsMakePath(PATH_ASCII, g_coinNewsDumpPath),
            FS_OPEN_WRITE | FS_OPEN_CREATE,
            0
        );
    }

    return (Result)0xD900182Fu;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_DumpNewsDb(void)
{
    // Temporary diagnostic copied from the previously hardware-working NEWS dumper.
    // This snapshots NEWS through news:s instead of trying to open its system save archive.
    FS_Archive sd;
    Handle file = 0;
    Handle news = 0;
    u64 offset = 0;

    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
        return rc;

    rc = PLUGIN_coin_OpenNextNewsDump(sd, &file);
    if (R_FAILED(rc))
    {
        COIN_HOST__FSUSER_CloseArchive(sd);
        return rc;
    }

    CoinNewsDumpHeader dump;
    PLUGIN_coin_NewsDumpZero(&dump, sizeof(dump));
    dump.magic[0] = 'N';
    dump.magic[1] = 'W';
    dump.magic[2] = 'S';
    dump.magic[3] = 'D';
    dump.magic[4] = 'M';
    dump.magic[5] = 'P';
    dump.magic[6] = '1';
    dump.formatVersion = 1;
    dump.slotsScanned = 100;
    dump.recordSize = sizeof(CoinNewsDumpRecord);
    dump.maxMessageSize = 0x1780;

    dump.serviceResult = COIN_HOST__srvGetServiceHandle(&news, g_coinNewsServiceName);
    if (R_SUCCEEDED(dump.serviceResult))
    {
        dump.totalResult = PLUGIN_coin_NewsDumpGetTotal(news, &dump.totalNotifications);
        dump.arrivedResult = PLUGIN_coin_NewsDumpGetTotalArrived(news, &dump.totalArrived);
        dump.dbHeaderResult = PLUGIN_coin_NewsDumpGetDbHeader(news, dump.dbHeader);
    }

    rc = PLUGIN_coin_NewsDumpWrite(file, &offset, &dump, sizeof(dump), 0);
    if (R_FAILED(rc))
        goto done;

    if (R_FAILED(dump.serviceResult))
    {
        rc = dump.serviceResult;
        goto done;
    }

    for (u32 slot = 0; slot < 100; slot++)
    {
        CoinNewsDumpRecord rec;
        PLUGIN_coin_NewsDumpZero(&rec, sizeof(rec));
        rec.slot = slot;
        rec.messageResult = (Result)-1;

        rec.headerResult = PLUGIN_coin_NewsDumpGetHeader(news, slot, &rec.header);
        if (R_SUCCEEDED(rec.headerResult))
        {
            rec.messageResult = PLUGIN_coin_NewsDumpGetMessage(
                news,
                slot,
                g_coinNewsMessageBuffer,
                &rec.messageSize
            );
            if (R_FAILED(rec.messageResult))
                rec.messageSize = 0;
        }

        rc = PLUGIN_coin_NewsDumpWrite(file, &offset, &rec, sizeof(rec), 0);
        if (R_FAILED(rc))
            goto done;

        if (rec.messageSize)
        {
            rc = PLUGIN_coin_NewsDumpWrite(
                file,
                &offset,
                g_coinNewsMessageBuffer,
                rec.messageSize,
                0
            );
            if (R_FAILED(rc))
                goto done;
        }
    }

done:
    if (news)
        COIN_HOST__svcCloseHandle(news);

    if (file)
    {
        u32 footer = 0x21444E45u; // "END!"
        if (R_SUCCEEDED(rc))
            rc = PLUGIN_coin_NewsDumpWrite(file, &offset, &footer, sizeof(footer), FS_WRITE_FLUSH);
        COIN_HOST__FSFILE_Close(file);
    }

    COIN_HOST__FSUSER_CloseArchive(sd);
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_GetNewsHeader(
    Handle newsHandle,
    u32 newsId,
    NotificationHeader *header
)
{
    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    u32 *cmdbuf = (u32*)(tls + 0x80);

    cmdbuf[0] = IPC_MakeHeader(0xBu, 2, 2);
    cmdbuf[1] = newsId;
    cmdbuf[2] = sizeof(NotificationHeader);
    cmdbuf[3] = IPC_Desc_Buffer(sizeof(NotificationHeader), IPC_BUFFER_W);
    cmdbuf[4] = (u32)header;

    Result rc = COIN_HOST__svcSendSyncRequest(newsHandle);
    if (R_SUCCEEDED(rc))
        rc = (Result)cmdbuf[1];
    return rc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_WriteNewsDbSavedata(Handle newsHandle)
{
    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    u32 *cmdbuf = (u32*)(tls + 0x80);

    cmdbuf[0] = IPC_MakeHeader(0x13u, 0, 0);
    Result rc = COIN_HOST__svcSendSyncRequest(newsHandle);
    if (R_SUCCEEDED(rc))
        rc = (Result)cmdbuf[1];
    return rc;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_RawTitleEquals(
    volatile const u16 *raw,
    const u16 *expected
)
{
    for (u32 i = 0; i < 32u; i++)
    {
        if (raw[i] != expected[i])
            return false;
    }
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_RawHeaderKeyEquals(
    volatile const NotificationHeader *raw,
    const NotificationHeader *expected
)
{
    if (!raw || !expected)
        return false;
    if (raw->processID != expected->processID || raw->time != expected->time)
        return false;
    return PLUGIN_coin_RawTitleEquals(raw->title, expected->title);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_RawHeaderHasBlankIdentity(
    volatile const NotificationHeader *header
)
{
    if (!header || header->processID || header->time)
        return false;

    for (u32 i = 0; i < 32u; i++)
        if (header->title[i])
            return false;
    return true;
}

PLUGIN_CODE(coin) static volatile NotificationHeader *PLUGIN_coin_RawNewsHeader(
    volatile u8 *dbBase,
    u32 physicalId
)
{
    return (volatile NotificationHeader*)(
        dbBase + COIN_NEWS_DB_HEADER_SIZE + physicalId * COIN_NEWS_DB_RECORD_SIZE
    );
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_RawNewsValidCount(volatile u8 *dbBase)
{
    u32 count = 0;
    for (u32 i = 0; i < COIN_NEWS_MAX_NOTIFICATIONS; i++)
    {
        volatile NotificationHeader *header = PLUGIN_coin_RawNewsHeader(dbBase, i);
        if (header->dataSet)
            count++;
    }
    return count;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_MapAnchorsToPhysicalIds(
    volatile u8 *dbBase,
    u32 *physicalIds
)
{
    for (u32 a = 0; a < g_coinNewsAnchorCount; a++)
    {
        u32 found = COIN_NEWS_MAX_NOTIFICATIONS;
        for (u32 i = 0; i < COIN_NEWS_MAX_NOTIFICATIONS; i++)
        {
            volatile NotificationHeader *raw = PLUGIN_coin_RawNewsHeader(dbBase, i);
            if (!raw->dataSet ||
                !PLUGIN_coin_RawHeaderKeyEquals(raw, &g_coinNewsAnchors[a].header))
                continue;

            if (found != COIN_NEWS_MAX_NOTIFICATIONS)
                return false;
            found = i;
        }

        if (found == COIN_NEWS_MAX_NOTIFICATIONS)
            return false;
        if (physicalIds)
            physicalIds[a] = found;
    }
    return true;
}

PLUGIN_CODE(coin) static volatile u8 *PLUGIN_coin_FindRawNewsDb(
    u32 mappedStart,
    u32 mappedEnd,
    u32 *physicalIds
)
{
    if (!g_coinNewsAnchorCount || mappedEnd <= mappedStart + COIN_NEWS_DB_SIZE)
        return NULL;

    volatile u8 *foundDb = NULL;
    CoinNewsAnchor *firstAnchor = &g_coinNewsAnchors[0];

    for (u32 addr = (mappedStart + 3u) & ~3u;
         addr + COIN_NEWS_DB_RECORD_SIZE <= mappedEnd;
         addr += 4u)
    {
        volatile NotificationHeader *candidate = (volatile NotificationHeader*)addr;
        if (!PLUGIN_coin_RawHeaderKeyEquals(candidate, &firstAnchor->header))
            continue;

        for (u32 physical = 0; physical < COIN_NEWS_MAX_NOTIFICATIONS; physical++)
        {
            u32 recordOffset = COIN_NEWS_DB_HEADER_SIZE +
                physical * COIN_NEWS_DB_RECORD_SIZE;
            if (addr < mappedStart + recordOffset)
                continue;

            u32 dbAddr = addr - recordOffset;
            if (dbAddr < mappedStart || dbAddr + COIN_NEWS_DB_SIZE > mappedEnd)
                continue;

            volatile u8 *dbBase = (volatile u8*)dbAddr;
            if (dbBase[0] != 1u)
                continue;
            if (PLUGIN_coin_RawNewsValidCount(dbBase) != g_coinNewsExpectedTotal)
                continue;
            if (!PLUGIN_coin_MapAnchorsToPhysicalIds(dbBase, NULL))
                continue;

            if (foundDb && foundDb != dbBase)
                return NULL;
            foundDb = dbBase;
        }
    }

    if (!foundDb)
        return NULL;
    if (!PLUGIN_coin_MapAnchorsToPhysicalIds(foundDb, physicalIds))
        return NULL;
    return foundDb;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ValidateIdPermutation32(volatile const u32 *ids)
{
    u32 seen0 = 0, seen1 = 0, seen2 = 0, seen3 = 0;
    for (u32 i = 0; i < COIN_NEWS_MAX_NOTIFICATIONS; i++)
    {
        u32 id = ids[i];
        if (id >= COIN_NEWS_MAX_NOTIFICATIONS)
            return false;
        u32 bit = 1u << (id & 31u);
        u32 word = id >> 5;
        if (word == 0u)
        {
            if (seen0 & bit) return false;
            seen0 |= bit;
        }
        else if (word == 1u)
        {
            if (seen1 & bit) return false;
            seen1 |= bit;
        }
        else if (word == 2u)
        {
            if (seen2 & bit) return false;
            seen2 |= bit;
        }
        else
        {
            if (seen3 & bit) return false;
            seen3 |= bit;
        }
    }
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ValidateIdPermutation8(volatile const u8 *ids)
{
    u32 seen0 = 0, seen1 = 0, seen2 = 0, seen3 = 0;
    for (u32 i = 0; i < COIN_NEWS_MAX_NOTIFICATIONS; i++)
    {
        u32 id = ids[i];
        if (id >= COIN_NEWS_MAX_NOTIFICATIONS)
            return false;
        u32 bit = 1u << (id & 31u);
        u32 word = id >> 5;
        if (word == 0u)
        {
            if (seen0 & bit) return false;
            seen0 |= bit;
        }
        else if (word == 1u)
        {
            if (seen1 & bit) return false;
            seen1 |= bit;
        }
        else if (word == 2u)
        {
            if (seen2 & bit) return false;
            seen2 |= bit;
        }
        else
        {
            if (seen3 & bit) return false;
            seen3 |= bit;
        }
    }
    return true;
}

PLUGIN_CODE(coin) static volatile u8 *PLUGIN_coin_FindNewsIdArray(
    u32 mappedStart,
    u32 mappedEnd,
    const u32 *physicalIds,
    u32 *outWidth
)
{
    volatile u8 *found = NULL;
    u32 foundWidth = 0;

    for (u32 addr = (mappedStart + 3u) & ~3u;
         addr + COIN_NEWS_MAX_NOTIFICATIONS * sizeof(u32) <= mappedEnd;
         addr += 4u)
    {
        volatile u32 *ids = (volatile u32*)addr;
        bool anchorsMatch = true;
        for (u32 a = 0; a < g_coinNewsAnchorCount; a++)
        {
            if (ids[g_coinNewsAnchors[a].logicalIndex] != physicalIds[a])
            {
                anchorsMatch = false;
                break;
            }
        }
        if (!anchorsMatch || !PLUGIN_coin_ValidateIdPermutation32(ids))
            continue;

        if (found && found != (volatile u8*)ids)
            return NULL;
        found = (volatile u8*)ids;
        foundWidth = 4u;
    }

    if (!found)
    {
        for (u32 addr = mappedStart;
             addr + COIN_NEWS_MAX_NOTIFICATIONS <= mappedEnd;
             addr++)
        {
            volatile u8 *ids = (volatile u8*)addr;
            bool anchorsMatch = true;
            for (u32 a = 0; a < g_coinNewsAnchorCount; a++)
            {
                if (ids[g_coinNewsAnchors[a].logicalIndex] != (u8)physicalIds[a])
                {
                    anchorsMatch = false;
                    break;
                }
            }
            if (!anchorsMatch || !PLUGIN_coin_ValidateIdPermutation8(ids))
                continue;

            if (found && found != ids)
                return NULL;
            found = ids;
            foundWidth = 1u;
        }
    }

    if (found && outWidth)
        *outWidth = foundWidth;
    return found;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_NewsIdGet(
    volatile u8 *idBase,
    u32 width,
    u32 logicalIndex
)
{
    if (width == 4u)
        return ((volatile u32*)idBase)[logicalIndex];
    return idBase[logicalIndex];
}

PLUGIN_CODE(coin) static void PLUGIN_coin_NewsIdSet(
    volatile u8 *idBase,
    u32 width,
    u32 logicalIndex,
    u32 value
)
{
    if (width == 4u)
        ((volatile u32*)idBase)[logicalIndex] = value;
    else
        idBase[logicalIndex] = (u8)value;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_RawNewsIdBefore(
    volatile u8 *dbBase,
    u32 first,
    u32 second
)
{
    volatile NotificationHeader *a = PLUGIN_coin_RawNewsHeader(dbBase, first);
    volatile NotificationHeader *b = PLUGIN_coin_RawNewsHeader(dbBase, second);
    bool aValid = a->dataSet != 0;
    bool bValid = b->dataSet != 0;

    if (aValid != bValid)
        return aValid;
    if (!aValid)
        return first < second;
    if (a->time != b->time)
        return a->time < b->time;
    return first < second;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_ResortRawNewsIds(
    volatile u8 *dbBase,
    volatile u8 *idBase,
    u32 width
)
{
    for (u32 i = 1; i < COIN_NEWS_MAX_NOTIFICATIONS; i++)
    {
        u32 key = PLUGIN_coin_NewsIdGet(idBase, width, i);
        u32 j = i;
        while (j > 0)
        {
            u32 previous = PLUGIN_coin_NewsIdGet(idBase, width, j - 1u);
            if (!PLUGIN_coin_RawNewsIdBefore(dbBase, key, previous))
                break;
            PLUGIN_coin_NewsIdSet(idBase, width, j, previous);
            j--;
        }
        PLUGIN_coin_NewsIdSet(idBase, width, j, key);
    }
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_RawLogicalSlotIsUnusableZombie(
    volatile u8 *dbBase,
    volatile u8 *idBase,
    u32 width,
    u32 physicalId
)
{
    volatile NotificationHeader *header = PLUGIN_coin_RawNewsHeader(dbBase, physicalId);
    if (!header->dataSet || !PLUGIN_coin_RawHeaderHasBlankIdentity(header))
        return false;

    u32 limit = g_coinNewsExpectedTotal;
    if (limit > COIN_NEWS_MAX_NOTIFICATIONS)
        limit = COIN_NEWS_MAX_NOTIFICATIONS;

    for (u32 logical = 0; logical < limit; logical++)
    {
        if (g_coinNewsLogicalResults[logical] != COIN_NEWS_UNUSABLE_SLOT_RESULT)
            continue;
        if (PLUGIN_coin_NewsIdGet(idBase, width, logical) == physicalId)
            return true;
    }
    return false;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_RawDeleteNewsCallback(
    Handle processHandle,
    u32 textSize,
    u32 roSize,
    u32 rwSize
)
{
    u32 mappedStart = 0x00100000u;
    u32 mappedEnd = mappedStart + textSize + roSize + rwSize;
    u32 physicalIds[COIN_NEWS_ANCHOR_MAX];
    u32 idWidth = 0;

    volatile u8 *dbBase = PLUGIN_coin_FindRawNewsDb(
        mappedStart,
        mappedEnd,
        physicalIds
    );
    if (!dbBase)
    {
        COIN_HOST__svcCloseHandle(processHandle);
        return (Result)-40;
    }

    volatile u8 *idBase = PLUGIN_coin_FindNewsIdArray(
        mappedStart,
        mappedEnd,
        physicalIds,
        &idWidth
    );
    if (!idBase)
    {
        COIN_HOST__svcCloseHandle(processHandle);
        return (Result)-41;
    }

    u32 removed = 0;
    for (u32 physical = 0; physical < COIN_NEWS_MAX_NOTIFICATIONS; physical++)
    {
        volatile NotificationHeader *header = PLUGIN_coin_RawNewsHeader(dbBase, physical);
        if (!header->dataSet)
            continue;

        bool remove = false;
        if (g_coinNewsRawDeleteMode == COIN_NEWS_RAW_DELETE_SPECIFIC)
        {
            remove = (u32)header->processID == COIN_PLUGIN_ID &&
                PLUGIN_coin_RawTitleEquals(header->title, g_coinNewsRawDeleteTitle);
        }
        else if (g_coinNewsRawDeleteMode == COIN_NEWS_RAW_DELETE_ALL)
        {
            remove = (u32)header->processID == COIN_PLUGIN_ID;
            if (!remove)
            {
                remove = PLUGIN_coin_RawLogicalSlotIsUnusableZombie(
                    dbBase,
                    idBase,
                    idWidth,
                    physical
                );
            }
        }

        if (!remove)
            continue;

        volatile u8 *bytes = (volatile u8*)header;
        for (u32 i = 0; i < sizeof(NotificationHeader); i++)
            bytes[i] = 0;
        removed++;
    }

    if (removed)
        PLUGIN_coin_ResortRawNewsIds(dbBase, idBase, idWidth);

    g_coinNewsRawRemoved = removed;
    COIN_HOST__svcFlushEntireDataCache();
    COIN_HOST__svcCloseHandle(processHandle);
    return 0;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_PrepareRawNewsDelete(
    Handle newsHandle,
    u32 mode,
    const u16 *specificTitle
)
{
    g_coinNewsAnchorCount = 0;
    g_coinNewsExpectedTotal = 0;
    g_coinNewsRawDeleteMode = mode;
    g_coinNewsRawRemoved = 0;

    for (u32 i = 0; i < 32u; i++)
        g_coinNewsRawDeleteTitle[i] = specificTitle ? specificTitle[i] : 0;

    Result rc = PLUGIN_coin_NewsDumpGetTotal(newsHandle, &g_coinNewsExpectedTotal);
    if (R_FAILED(rc))
        return rc;
    if (g_coinNewsExpectedTotal > COIN_NEWS_MAX_NOTIFICATIONS)
        return (Result)-42;

    for (u32 logical = 0; logical < COIN_NEWS_MAX_NOTIFICATIONS; logical++)
    {
        NotificationHeader header;
        rc = PLUGIN_coin_GetNewsHeader(newsHandle, logical, &header);
        g_coinNewsLogicalResults[logical] = rc;

        if (R_SUCCEEDED(rc))
        {
            if (g_coinNewsAnchorCount < COIN_NEWS_ANCHOR_MAX)
            {
                CoinNewsAnchor *anchor = &g_coinNewsAnchors[g_coinNewsAnchorCount++];
                anchor->logicalIndex = logical;
                volatile u8 *dst = (volatile u8*)&anchor->header;
                const u8 *src = (const u8*)&header;
                for (u32 i = 0; i < sizeof(NotificationHeader); i++)
                    dst[i] = src[i];
            }
            continue;
        }

        if (rc == COIN_NEWS_EMPTY_SLOT_RESULT ||
            rc == COIN_NEWS_UNUSABLE_SLOT_RESULT)
            continue;
        return rc;
    }

    if (!g_coinNewsExpectedTotal)
        return 0;
    if (!g_coinNewsAnchorCount)
        return (Result)-43;
    return 0;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_DeleteNewsRaw(
    u32 mode,
    const u16 *specificTitle,
    u32 *outRemoved
)
{
    u32 scratchBase = 0;
    if (!COIN_MENU__TempAlloc(0x1000u, &scratchBase))
        return (Result)-8;
    g_coinNewsAnchors = (CoinNewsAnchor *)scratchBase;
    g_coinNewsLogicalResults = (Result *)(scratchBase + sizeof(CoinNewsAnchor) * COIN_NEWS_ANCHOR_MAX);
    g_coinNewsRawDeleteTitle = (u16 *)((u8 *)g_coinNewsLogicalResults + sizeof(Result) * COIN_NEWS_MAX_NOTIFICATIONS);

    if (outRemoved)
        *outRemoved = 0;

    Handle newsHandle = 0;
    Result rc = COIN_HOST__srvGetServiceHandle(&newsHandle, g_coinNewsServiceName);
    if (R_FAILED(rc))
        goto cleanup_scratch;

    rc = PLUGIN_coin_PrepareRawNewsDelete(newsHandle, mode, specificTitle);
    if (R_FAILED(rc) || !g_coinNewsExpectedTotal)
        goto cleanup;

    rc = COIN_HOST__OperateOnProcessByName(g_coinNewsName, PLUGIN_coin_RawDeleteNewsCallback);
    if (R_FAILED(rc))
        goto cleanup;

    if (g_coinNewsRawRemoved)
    {
        rc = PLUGIN_coin_WriteNewsDbSavedata(newsHandle);
        if (R_FAILED(rc))
            goto cleanup;

        u32 verifyTotal = 0;
        Result verifyRc = PLUGIN_coin_NewsDumpGetTotal(newsHandle, &verifyTotal);
        if (R_FAILED(verifyRc))
            rc = verifyRc;
        else if (verifyTotal + g_coinNewsRawRemoved != g_coinNewsExpectedTotal)
            rc = (Result)-44;
    }

cleanup:
    COIN_HOST__svcCloseHandle(newsHandle);
    if (outRemoved)
        *outRemoved = g_coinNewsRawRemoved;
cleanup_scratch:
    g_coinNewsAnchors = NULL;
    g_coinNewsLogicalResults = NULL;
    g_coinNewsRawDeleteTitle = NULL;
    COIN_MENU__TempFree(scratchBase, 0x1000u);
    return rc;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_AchievementNewsMatches(
    const NotificationHeader *header,
    const CoinAchievement *achievement
)
{
    if (!header || !achievement || achievement->difficulty > 3u)
        return false;

    // The Coin namespace plus the exact achievement title uniquely identifies it.
    // Using the low process-ID word also finds older legacy Coin notifications.
    if ((u32)header->processID != COIN_PLUGIN_ID)
        return false;

    return PLUGIN_coin_U16FixedEquals(header->title, achievement->title, 32u);
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_ScanAchievementNews(
    u32 achievementIndex,
    bool deleteMatches,
    u32 *outMatches
)
{
    if (outMatches)
        *outMatches = 0;
    if (achievementIndex >= COIN_ACHIEVEMENT_COUNT)
        return (Result)-25;

    CoinAchievement achievement;
    Result rc = PLUGIN_coin_ReadAchievement(achievementIndex, &achievement);
    if (R_FAILED(rc))
    {
        Result unpackRc = PLUGIN_coin_UnpackAssetMask(1u << COIN_ASSET_ACHV);
        if (R_FAILED(unpackRc))
            return unpackRc;
        rc = PLUGIN_coin_ReadAchievement(achievementIndex, &achievement);
        if (R_FAILED(rc))
            return rc;
    }

    if (deleteMatches)
        return PLUGIN_coin_DeleteNewsRaw(
            COIN_NEWS_RAW_DELETE_SPECIFIC,
            achievement.title,
            outMatches
        );

    Handle newsHandle = 0;
    rc = COIN_HOST__srvGetServiceHandle(&newsHandle, g_coinNewsServiceName);
    if (R_FAILED(rc))
        return rc;

    u32 matches = 0;
    Result scanRc = 0;
    for (u32 newsId = 0; newsId < COIN_NEWS_MAX_NOTIFICATIONS; newsId++)
    {
        NotificationHeader header;
        rc = PLUGIN_coin_GetNewsHeader(newsHandle, newsId, &header);
        if (rc == COIN_NEWS_EMPTY_SLOT_RESULT ||
            rc == COIN_NEWS_UNUSABLE_SLOT_RESULT)
            continue;
        if (R_FAILED(rc))
        {
            scanRc = rc;
            break;
        }
        if (PLUGIN_coin_AchievementNewsMatches(&header, &achievement))
            matches++;
    }

    COIN_HOST__svcCloseHandle(newsHandle);
    if (outMatches)
        *outMatches = matches;
    return scanRc;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_WipeCoinNews(u32 *outRemoved)
{
    // Use the proven raw NEWS DB deletion path. Public header-setter deletion
    // only clears visible fields and leaves malformed occupied zombie records.
    // Raw deletion zeros the physical 0x70 records, repairs the live sorted-ID
    // table, persists the database, and verifies the notification count drop.
    return PLUGIN_coin_DeleteNewsRaw(COIN_NEWS_RAW_DELETE_ALL, NULL, outRemoved);
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_FirstPendingAchievement(u32 mask)
{
    for (u32 i = 0; i < COIN_ACHIEVEMENT_COUNT; i++)
    {
        if (mask & (1u << i))
            return i;
    }
    return COIN_ACHIEVEMENT_COUNT;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_HardNotificationTick(u64 delta)
{
    (void)delta;

    if (g_coinHardDiagState == COIN_DIAG_IDLE)
        return;

    if (g_coinHardDiagState == COIN_DIAG_WAIT_RESTORE)
    {
        // A newly queued achievement rearms the full five-second send deadline.
        if (g_coinPendingAchievementMask)
        {
            g_coinHardDiagState = COIN_DIAG_WAIT_TRIGGER;
            (void)COIN_BLUR__AddTickFunc(
                PLUGIN_coin_HardNotificationTick,
                COIN_NOTIFICATION_TICK_NS
            );
            return;
        }

        Result cleanupRc = PLUGIN_coin_EndNewsLedWindow();
        if (R_SUCCEEDED(cleanupRc))
        {
            g_coinHardDiagState = COIN_DIAG_IDLE;
            if (!COIN_BLUR__RemoveTickFunc(PLUGIN_coin_HardNotificationTick))
            {
                g_coinHardDiagLastResult = (Result)-12;
                g_coinHardDiagCompletedSerial++;
            }
        }
        else
        {
            g_coinHardDiagLastResult = cleanupRc;
            g_coinHardDiagCompletedSerial++;
            (void)COIN_BLUR__AddTickFunc(
                PLUGIN_coin_HardNotificationTick,
                COIN_NOTIFICATION_TICK_NS
            );
        }
        return;
    }

    u32 pending = g_coinPendingAchievementMask & COIN_ACHIEVEMENT_MASK;
    if (!pending)
    {
        g_coinHardDiagState = COIN_DIAG_WAIT_RESTORE;
        (void)COIN_BLUR__AddTickFunc(
            PLUGIN_coin_HardNotificationTick,
            COIN_NOTIFICATION_TICK_NS
        );
        return;
    }

    if (!g_coinNewsLedActive)
        g_coinHardDiagWindowResult = PLUGIN_coin_BeginNewsLedWindow();

    u32 achievementIndex = PLUGIN_coin_FirstPendingAchievement(pending);
    Result sendRc = 0;
    if (achievementIndex >= COIN_ACHIEVEMENT_COUNT)
    {
        sendRc = (Result)-26;
    }
    else
    {
        u32 bit = 1u << achievementIndex;
        CoinAchievement achievement;
        sendRc = PLUGIN_coin_ReadAchievement(achievementIndex, &achievement);
        if (R_FAILED(sendRc))
        {
            Result unpackRc = PLUGIN_coin_UnpackAssetMask(1u << COIN_ASSET_ACHV);
            if (R_SUCCEEDED(unpackRc))
                sendRc = PLUGIN_coin_ReadAchievement(achievementIndex, &achievement);
            else
                sendRc = unpackRc;
        }
        if (R_SUCCEEDED(sendRc))
        {
            sendRc = PLUGIN_coin_SendDiagnosticNotification(&achievement);
            if (R_SUCCEEDED(sendRc))
                sendRc = PLUGIN_coin_RecordAchievementEarned(achievementIndex);
        }

        // Pending prevents duplicate scheduling while NEWS delivery is in flight.
        // Persistent earned state is committed only after NEWS accepted the notification.
        g_coinPendingAchievementMask &= ~bit;
    }

    g_coinHardDiagLastResult = R_FAILED(g_coinHardDiagWindowResult) ?
        g_coinHardDiagWindowResult : sendRc;
    g_coinHardDiagCompletedSerial++;

    g_coinHardDiagState = g_coinPendingAchievementMask ?
        COIN_DIAG_WAIT_TRIGGER : COIN_DIAG_WAIT_RESTORE;

    // Restart after IPC for five full seconds before the next send or restore.
    (void)COIN_BLUR__AddTickFunc(PLUGIN_coin_HardNotificationTick, COIN_NOTIFICATION_TICK_NS);
}

PLUGIN_CODE(coin) Result PLUGIN_coin_TriggerAchievement(u32 achievementIndex)
{
    if (achievementIndex >= COIN_ACHIEVEMENT_COUNT)
        return (Result)-26;

    Result rc = PLUGIN_coin_EnsurePackedAssets();
    if (R_FAILED(rc))
        return rc;

    // Fetch and validate only this record before queueing notification delivery.
    CoinAchievement achievement;
    rc = PLUGIN_coin_ReadAchievement(achievementIndex, &achievement);
    if (R_FAILED(rc))
        return rc;

    // If the exact NEWS item already exists, delivery has already happened.
    // Commit the earned bit now without creating a duplicate notification.
    u32 existingNotifications = 0;
    rc = PLUGIN_coin_ScanAchievementNews(achievementIndex, false, &existingNotifications);
    if (R_FAILED(rc))
        return rc;
    if (existingNotifications)
        return PLUGIN_coin_RecordAchievementEarned(achievementIndex);

    // Turn NEWS yellow immediately; notification stays on the five-second callback.
    Result yellowRc = PLUGIN_coin_BeginNewsLedWindow();
    (void)PLUGIN_coin_EnsureNotificationScratch();
    g_coinHardDiagWindowResult = yellowRc;

    u32 bit = 1u << achievementIndex;
    g_coinPendingAchievementMask |= bit;
    g_coinHardDiagState = COIN_DIAG_WAIT_TRIGGER;

    // Register once or rearm the existing temporary callback to five seconds.
    if (!COIN_BLUR__AddTickFunc(PLUGIN_coin_HardNotificationTick, COIN_NOTIFICATION_TICK_NS))
    {
        g_coinPendingAchievementMask &= ~bit;
        if (!g_coinPendingAchievementMask)
        {
            Result cleanupRc = PLUGIN_coin_EndNewsLedWindow();
            g_coinHardDiagState = COIN_DIAG_IDLE;
            if (R_FAILED(cleanupRc))
                return cleanupRc;
        }
        return (Result)-10;
    }

    return 0;
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
    u32 observedDiagSerial = g_coinHardDiagCompletedSerial;

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

        if (observedDiagSerial != g_coinHardDiagCompletedSerial)
        {
            observedDiagSerial = g_coinHardDiagCompletedSerial;
            res = g_coinHardDiagLastResult;
            // The notification confirms success, so surface only async errors.
            PLUGIN_coin_DrawEditor(
                playCoins,
                trueCoinDisplay,
                recommendedDisplay,
                R_FAILED(res),
                res
            );
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

PLUGIN_CODE(coin) static void PLUGIN_coin_ClearDebugBucketLine(u32 y)
{
    COIN_HOST__Draw_DrawString(
        COIN_DEBUG_BUCKET_X,
        y,
        COLOR_BLACK,
        g_coinBucketClearLine
    );
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawDebugTimestampNoLock(void)
{
    char now[20];
    int nowLen = COIN_HOST__dateTimeToString(now, COIN_HOST__osGetTime(), false);

    PLUGIN_coin_ClearDebugBucketLine(COIN_DEBUG_BUCKET_NOW_Y);
    if (nowLen > 0)
    {
        COIN_HOST__Draw_DrawFormattedString(
            COIN_DEBUG_BUCKET_X,
            COIN_DEBUG_BUCKET_NOW_Y,
            COLOR_WHITE,
            g_coinBucketNowFmt,
            now
        );
    }
    else
    {
        COIN_HOST__Draw_DrawString(
            COIN_DEBUG_BUCKET_X,
            COIN_DEBUG_BUCKET_NOW_Y,
            COLOR_RED,
            g_coinBucketNoClock
        );
    }
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawDebugBucketValuesNoLock(bool clearRows)
{
    if (clearRows)
    {
        PLUGIN_coin_ClearDebugBucketLine(62u);
        for (u32 i = 0; i < COIN_DAY_HISTORY_COUNT; i++)
            PLUGIN_coin_ClearDebugBucketLine(75u + i * 13u);
        PLUGIN_coin_ClearDebugBucketLine(153u);
        PLUGIN_coin_ClearDebugBucketLine(166u);
    }

    COIN_HOST__Draw_DrawFormattedString(
        COIN_DEBUG_BUCKET_X,
        62u,
        COLOR_CYAN,
        g_coinBucketTodayFmt,
        (u32)PLUGIN_coin_GetTodayWalked()
    );
    for (u32 i = 0; i < COIN_DAY_HISTORY_COUNT; i++)
    {
        COIN_HOST__Draw_DrawFormattedString(
            COIN_DEBUG_BUCKET_X,
            75u + i * 13u,
            COLOR_WHITE,
            g_coinBucketPrevFmt,
            i + 1u,
            (u32)PLUGIN_coin_GetHistoryDay(i)
        );
    }
    COIN_HOST__Draw_DrawFormattedString(
        COIN_DEBUG_BUCKET_X,
        153u,
        COLOR_YELLOW,
        g_coinBucketTotalFmt,
        PLUGIN_coin_SevenDayWalked()
    );
    COIN_HOST__Draw_DrawFormattedString(
        COIN_DEBUG_BUCKET_X,
        166u,
        COLOR_GRAY,
        g_coinBucketActiveFmt,
        g_coinExtendedData[COIN_EXT_LAST_DAY_WORD]
    );
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawDebugLiveRows(bool dayChanged)
{
    COIN_HOST__Draw_Lock();
    PLUGIN_coin_DrawDebugTimestampNoLock();
    if (dayChanged)
        PLUGIN_coin_DrawDebugBucketValuesNoLock(true);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawDebug(void)
{
    PLUGIN_coin_UpdateDayHistory();

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

    COIN_HOST__Draw_DrawString(COIN_DEBUG_BUCKET_X, 36u, COLOR_GRAY, g_coinBucketGroup);
    PLUGIN_coin_DrawDebugTimestampNoLock();
    PLUGIN_coin_DrawDebugBucketValuesNoLock(false);

    COIN_HOST__Draw_DrawString(20, 228, RGB565(15, 31, 15), g_coinDebugControls);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenDebug(void)
{
    PLUGIN_coin_DrawDebug();
    u32 redrawTicks = 0;

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return;

        redrawTicks++;
        if (redrawTicks >= 20u)
        {
            redrawTicks = 0;
            bool dayChanged = PLUGIN_coin_UpdateDayHistory();
            PLUGIN_coin_DrawDebugLiveRows(dayChanged);
        }
    } while (!COIN_HOST__menuShouldExit);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawSimpleMenuItem(
    u32 y,
    bool selected,
    const char *text,
    u32 color
)
{
    COIN_HOST__Draw_DrawString(
        14,
        y,
        selected ? COIN_FRAME_TITLE_COLOR : COLOR_GRAY,
        selected ? g_coinMenuCursor : g_coinMenuUnselected
    );
    COIN_HOST__Draw_DrawString(24, y, color, text);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_HasEarnedAchievements(void)
{
    u32 earnedMask = 0;
    return R_SUCCEEDED(PLUGIN_coin_ReadAchievementEarnedMask(&earnedMask)) &&
        (earnedMask & COIN_ACHIEVEMENT_MASK) != 0;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_PlayCoinzMenuCount(bool showAchievements)
{
    return showAchievements ? 3u : 2u;
}

PLUGIN_CODE(coin) static const char *PLUGIN_coin_GetPlayCoinzMenuTitle(
    u32 selected,
    bool showAchievements
)
{
    if (selected == 0u)
        return g_coinSetCoinsItem;
    if (showAchievements && selected == 1u)
        return g_coinViewAchievementsItem;
    if (selected == (showAchievements ? 2u : 1u))
        return g_coinOptionsItem;
    return NULL;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_PlayCoinzMenuY(u32 selected)
{
    return 40u + selected * 15u;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawPlayCoinzMenu(
    u32 selected,
    bool showAchievements
)
{
    u32 count = PLUGIN_coin_PlayCoinzMenuCount(showAchievements);

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinMenuTitle);

    for (u32 i = 0; i < count; i++)
    {
        const char *title = PLUGIN_coin_GetPlayCoinzMenuTitle(i, showAchievements);
        if (title)
        {
            PLUGIN_coin_DrawSimpleMenuItem(
                PLUGIN_coin_PlayCoinzMenuY(i),
                selected == i,
                title,
                selected == i ? COLOR_CYAN : COLOR_WHITE
            );
        }
    }

    COIN_HOST__Draw_DrawString(20, 120, COLOR_GRAY, g_coinBackShort);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RedrawPlayCoinzSelection(
    u32 oldSelected,
    u32 selected,
    bool showAchievements
)
{
    const char *oldTitle = PLUGIN_coin_GetPlayCoinzMenuTitle(oldSelected, showAchievements);
    const char *newTitle = PLUGIN_coin_GetPlayCoinzMenuTitle(selected, showAchievements);
    if (!oldTitle || !newTitle)
        return;

    u32 oldY = PLUGIN_coin_PlayCoinzMenuY(oldSelected);
    u32 newY = PLUGIN_coin_PlayCoinzMenuY(selected);

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_coinMenuClearRow);
    COIN_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_coinMenuClearRow);
    PLUGIN_coin_DrawSimpleMenuItem(oldY, false, oldTitle, COLOR_WHITE);
    PLUGIN_coin_DrawSimpleMenuItem(newY, true, newTitle, COLOR_CYAN);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}


PLUGIN_CODE(coin) static u32 PLUGIN_coin_AchievementRowForIndex(u32 achievementIndex)
{
    if (achievementIndex < 5u)
        return achievementIndex + 1u;
    if (achievementIndex < 10u)
        return achievementIndex + 3u;
    if (achievementIndex < 15u)
        return achievementIndex + 5u;
    return achievementIndex + 7u;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_AchievementIndexForRow(u32 row)
{
    for (u32 i = 0; i < COIN_ACHIEVEMENT_COUNT; i++)
    {
        if (PLUGIN_coin_AchievementRowForIndex(i) == row)
            return i;
    }
    return COIN_ACHIEVEMENT_COUNT;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_AchievementTierForRow(u32 row)
{
    if (row <= 5u)
        return 0u;
    if (row >= 7u && row <= 12u)
        return 1u;
    if (row >= 14u && row <= 19u)
        return 2u;
    return 3u;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_AchievementTierMask(u32 tier)
{
    if (tier == 0u)
        return 0x0000001Fu;
    if (tier == 1u)
        return 0x000003E0u;
    if (tier == 2u)
        return 0x00007C00u;
    return 0x00038000u;
}

PLUGIN_CODE(coin) static const char *PLUGIN_coin_AchievementTierName(u32 tier)
{
    if (tier == 0u)
        return g_coinTierEasy;
    if (tier == 1u)
        return g_coinTierMedium;
    if (tier == 2u)
        return g_coinTierHard;
    return g_coinTierExtreme;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_AchievementTierColor(u32 tier)
{
    if (tier == 0u)
        return COIN_ACHV_EASY_COLOR;
    if (tier == 1u)
        return COLOR_YELLOW;
    if (tier == 2u)
        return COIN_ACHV_HARD_COLOR;
    return COLOR_RED;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_LoadAchievementTitles(u32 earnedMask)
{
    for (u32 i = 0; i < COIN_ACHIEVEMENT_COUNT; i++)
    {
        g_coinAchievementTitles[i][0] = 0;
        if (!(earnedMask & (1u << i)))
            continue;

        CoinAchievement achievement;
        if (R_SUCCEEDED(PLUGIN_coin_ReadAchievement(i, &achievement)))
            PLUGIN_coin_CopyAsciiText(g_coinAchievementTitles[i], 40u,
                                      achievement.title);
    }
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_AdjustAchievementFirst(
    u32 first,
    u32 selectedRow
)
{
    u32 maxFirst = COIN_ACHIEVEMENT_ROW_COUNT - COIN_ACHIEVEMENT_VISIBLE_ROWS;

    // Keep one contextual row above the selected achievement whenever possible.
    if (selectedRow > 0u && selectedRow <= first)
        first = selectedRow - 1u;
    if (selectedRow >= first + COIN_ACHIEVEMENT_VISIBLE_ROWS)
        first = selectedRow - COIN_ACHIEVEMENT_VISIBLE_ROWS + 1u;
    if (first > maxFirst)
        first = maxFirst;
    if (selectedRow > 0u && first >= selectedRow)
        first = selectedRow - 1u;
    return first;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawAchievementRow(
    u32 row,
    u32 y,
    u32 selectedAchievement,
    u32 earnedMask
)
{
    if (row == 0u || row == 7u || row == 14u || row == 21u)
    {
        u32 tier = PLUGIN_coin_AchievementTierForRow(row);
        const char *title = (earnedMask & PLUGIN_coin_AchievementTierMask(tier)) ?
            PLUGIN_coin_AchievementTierName(tier) : g_coinTierLocked;
        COIN_HOST__Draw_DrawString(24, y, PLUGIN_coin_AchievementTierColor(tier), title);
        return;
    }

    if (row == 6u || row == 13u || row == 20u)
        return;

    u32 achievementIndex = PLUGIN_coin_AchievementIndexForRow(row);
    if (achievementIndex >= COIN_ACHIEVEMENT_COUNT)
        return;

    bool selected = achievementIndex == selectedAchievement;
    bool earned = (earnedMask & (1u << achievementIndex)) != 0;
    const char *title = earned && g_coinAchievementTitles[achievementIndex][0] ?
        g_coinAchievementTitles[achievementIndex] : g_coinUnknownAchievement;

    COIN_HOST__Draw_DrawString(
        14,
        y,
        selected ? COIN_FRAME_TITLE_COLOR : COLOR_GRAY,
        selected ? g_coinMenuCursor : g_coinMenuUnselected
    );
    COIN_HOST__Draw_DrawString(24, y, earned ? COLOR_WHITE : COLOR_GRAY, title);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawAchievements(
    u32 first,
    u32 selectedAchievement,
    u32 earnedMask
)
{
    u32 shown = COIN_ACHIEVEMENT_ROW_COUNT - first;
    if (shown > COIN_ACHIEVEMENT_VISIBLE_ROWS)
        shown = COIN_ACHIEVEMENT_VISIBLE_ROWS;

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinAchievementsPageTitle);

    if (first)
        COIN_HOST__Draw_DrawString(24, COIN_ACHIEVEMENT_TOP_DOTS_Y, COLOR_GRAY, g_coinMenuDots);

    for (u32 i = 0; i < shown; i++)
        PLUGIN_coin_DrawAchievementRow(
            first + i,
            COIN_ACHIEVEMENT_ITEM_TOP_Y + i * COIN_ACHIEVEMENT_ITEM_SPACING_Y,
            selectedAchievement,
            earnedMask
        );

    if (first + shown < COIN_ACHIEVEMENT_ROW_COUNT)
        COIN_HOST__Draw_DrawString(
            24,
            COIN_ACHIEVEMENT_ITEM_TOP_Y +
                COIN_ACHIEVEMENT_VISIBLE_ROWS * COIN_ACHIEVEMENT_ITEM_SPACING_Y,
            COLOR_GRAY,
            g_coinMenuDots
        );

    COIN_HOST__Draw_DrawString(24, COIN_ACHIEVEMENT_PROMPT_Y, COLOR_GRAY, g_coinViewShort);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RedrawAchievementSelection(
    u32 first,
    u32 oldSelectedAchievement,
    u32 selectedAchievement,
    u32 earnedMask
)
{
    u32 oldRow = PLUGIN_coin_AchievementRowForIndex(oldSelectedAchievement);
    u32 newRow = PLUGIN_coin_AchievementRowForIndex(selectedAchievement);
    if (oldRow < first || oldRow >= first + COIN_ACHIEVEMENT_VISIBLE_ROWS ||
        newRow < first || newRow >= first + COIN_ACHIEVEMENT_VISIBLE_ROWS)
        return;

    u32 oldY = COIN_ACHIEVEMENT_ITEM_TOP_Y +
        (oldRow - first) * COIN_ACHIEVEMENT_ITEM_SPACING_Y;
    u32 newY = COIN_ACHIEVEMENT_ITEM_TOP_Y +
        (newRow - first) * COIN_ACHIEVEMENT_ITEM_SPACING_Y;

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_coinMenuClearRow);
    COIN_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_coinMenuClearRow);
    // Explicitly paint over the old cursor glyph before redrawing the row.
    COIN_HOST__Draw_DrawString(14, oldY, COLOR_BLACK, g_coinMenuCursor);
    PLUGIN_coin_DrawAchievementRow(oldRow, oldY, selectedAchievement, earnedMask);
    PLUGIN_coin_DrawAchievementRow(newRow, newY, selectedAchievement, earnedMask);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawAchievementDetail(u32 achievementIndex)
{
    CoinAchievement achievement;
    u32 titleColor = COLOR_WHITE;
    g_coinAchievementDetailTitle[0] = 0;
    g_coinAchievementDetailMessage[0] = 0;

    if (R_SUCCEEDED(PLUGIN_coin_ReadAchievement(achievementIndex, &achievement)))
    {
        PLUGIN_coin_CopyAsciiText(
            g_coinAchievementDetailTitle,
            40u,
            achievement.title
        );
        PLUGIN_coin_CopyAsciiText(
            g_coinAchievementDetailMessage,
            512u,
            achievement.menuMessage
        );
        if (achievement.difficulty <= 3u)
            titleColor = PLUGIN_coin_AchievementTierColor(achievement.difficulty);
    }

    if (!g_coinAchievementDetailTitle[0])
    {
        g_coinAchievementDetailTitle[0] = '?';
        g_coinAchievementDetailTitle[1] = '?';
        g_coinAchievementDetailTitle[2] = '?';
        g_coinAchievementDetailTitle[3] = 0;
    }

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinAchievementsPageTitle);
    COIN_HOST__Draw_DrawString(20, 42, titleColor, g_coinAchievementDetailTitle);
    COIN_HOST__Draw_DrawString(20, 62, COLOR_WHITE, g_coinAchievementDetailMessage);
    COIN_HOST__Draw_DrawString(20, 200, COLOR_GRAY, g_coinAchievementOptionsPrompt);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawAchievementOptionMenu(u32 selected)
{
    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinAchievementsPageTitle);
    PLUGIN_coin_DrawSimpleMenuItem(
        45u,
        selected == 0u,
        g_coinAchievementResendItem,
        COLOR_WHITE
    );
    PLUGIN_coin_DrawSimpleMenuItem(
        60u,
        selected == 1u,
        g_coinAchievementDeleteItem,
        COLOR_WHITE
    );
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RedrawAchievementOptionSelection(
    u32 oldSelected,
    u32 selected
)
{
    u32 oldY = oldSelected == 0u ? 45u : 60u;
    u32 newY = selected == 0u ? 45u : 60u;
    const char *oldText = oldSelected == 0u ?
        g_coinAchievementResendItem : g_coinAchievementDeleteItem;
    const char *newText = selected == 0u ?
        g_coinAchievementResendItem : g_coinAchievementDeleteItem;

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_coinMenuClearRow);
    COIN_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_coinMenuClearRow);
    PLUGIN_coin_DrawSimpleMenuItem(oldY, false, oldText, COLOR_WHITE);
    PLUGIN_coin_DrawSimpleMenuItem(newY, true, newText, COLOR_WHITE);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawConfirmItem(
    u32 y,
    bool selected,
    const char *text
)
{
    COIN_HOST__Draw_DrawString(
        14,
        y,
        selected ? COLOR_CYAN : COLOR_GRAY,
        selected ? g_coinMenuCursor : g_coinMenuUnselected
    );
    COIN_HOST__Draw_DrawString(24, y, selected ? COLOR_CYAN : COLOR_WHITE, text);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawAchievementConfirm(u32 selected)
{
    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinConfirmTitle);
    PLUGIN_coin_DrawConfirmItem(55u, selected == 0u, g_coinConfirmNo);
    PLUGIN_coin_DrawConfirmItem(70u, selected == 1u, g_coinConfirmYes);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RedrawAchievementConfirmSelection(
    u32 oldSelected,
    u32 selected
)
{
    u32 oldY = oldSelected == 0u ? 55u : 70u;
    u32 newY = selected == 0u ? 55u : 70u;
    const char *oldText = oldSelected == 0u ? g_coinConfirmNo : g_coinConfirmYes;
    const char *newText = selected == 0u ? g_coinConfirmNo : g_coinConfirmYes;

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_coinMenuClearRow);
    COIN_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_coinMenuClearRow);
    PLUGIN_coin_DrawConfirmItem(oldY, false, oldText);
    PLUGIN_coin_DrawConfirmItem(newY, true, newText);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ConfirmAchievementAction(void)
{
    u32 selected = 0;
    PLUGIN_coin_DrawAchievementConfirm(selected);

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return false;
        if (pressed & (KEY_DUP | KEY_DDOWN))
        {
            u32 oldSelected = selected;
            selected = selected ? 0u : 1u;
            PLUGIN_coin_RedrawAchievementConfirmSelection(oldSelected, selected);
        }
        else if (pressed & KEY_A)
        {
            return selected == 1u;
        }
    } while (!COIN_HOST__menuShouldExit);

    return false;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawAchievementActionResult(
    const char *message,
    bool error,
    Result rc
)
{
    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinAchievementsPageTitle);
    if (error)
        COIN_HOST__Draw_DrawFormattedString(20, 65, COLOR_RED, g_errorFormat, (u32)rc);
    else
        COIN_HOST__Draw_DrawString(20, 65, COLOR_WHITE, message);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();

    do
    {
        if (COIN_HOST__waitInputWithTimeout(50) & KEY_B)
            return;
    } while (!COIN_HOST__menuShouldExit);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RunAchievementAction(
    u32 achievementIndex,
    bool resend
)
{
    u32 matches = 0;
    Result rc = PLUGIN_coin_ScanAchievementNews(
        achievementIndex,
        !resend,
        &matches
    );

    if (R_FAILED(rc))
    {
        PLUGIN_coin_DrawAchievementActionResult(NULL, true, rc);
        return;
    }

    if (resend)
    {
        if (matches)
        {
            PLUGIN_coin_DrawAchievementActionResult(
                g_coinResultAlreadyExists,
                false,
                0
            );
            return;
        }

        rc = PLUGIN_coin_TriggerAchievement(achievementIndex);
        if (R_FAILED(rc))
        {
            PLUGIN_coin_DrawAchievementActionResult(NULL, true, rc);
            return;
        }

        PLUGIN_coin_DrawAchievementActionResult(g_coinResultDone, false, 0);
        return;
    }

    PLUGIN_coin_DrawAchievementActionResult(
        matches ? g_coinResultDone : g_coinResultNotFound,
        false,
        0
    );
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenAchievementOptions(u32 achievementIndex)
{
    u32 selected = 0;
    PLUGIN_coin_DrawAchievementOptionMenu(selected);

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return;
        if (pressed & KEY_DDOWN)
        {
            u32 oldSelected = selected;
            selected = selected ? 0u : 1u;
            if (selected != oldSelected)
                PLUGIN_coin_RedrawAchievementOptionSelection(oldSelected, selected);
        }
        else if (pressed & KEY_DUP)
        {
            u32 oldSelected = selected;
            selected = selected ? 0u : 1u;
            if (selected != oldSelected)
                PLUGIN_coin_RedrawAchievementOptionSelection(oldSelected, selected);
        }
        else if (pressed & KEY_A)
        {
            bool resend = selected == 0u;
            if (PLUGIN_coin_ConfirmAchievementAction())
                PLUGIN_coin_RunAchievementAction(achievementIndex, resend);
            PLUGIN_coin_DrawAchievementOptionMenu(selected);
        }
    } while (!COIN_HOST__menuShouldExit);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenAchievementDetail(u32 achievementIndex)
{
    PLUGIN_coin_DrawAchievementDetail(achievementIndex);
    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return;
        if (pressed & KEY_X)
        {
            PLUGIN_coin_OpenAchievementOptions(achievementIndex);
            PLUGIN_coin_DrawAchievementDetail(achievementIndex);
        }
    } while (!COIN_HOST__menuShouldExit);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenAchievements(void)
{
    if (R_FAILED(PLUGIN_coin_EnsurePackedAssets()))
        return;

    u32 scratchBase = 0;
    if (!COIN_MENU__TempAlloc(0x1000u, &scratchBase))
        return;
    g_coinAchievementTitles = (char (*)[40])scratchBase;
    g_coinAchievementDetailTitle = (char *)(scratchBase + COIN_ACHIEVEMENT_COUNT * 40u);
    g_coinAchievementDetailMessage = g_coinAchievementDetailTitle + 40u;

    u32 earnedMask = 0;
    if (R_FAILED(PLUGIN_coin_ReadAchievementEarnedMask(&earnedMask)))
        earnedMask = 0;
    earnedMask &= COIN_ACHIEVEMENT_MASK;
    PLUGIN_coin_LoadAchievementTitles(earnedMask);

    u32 selectedAchievement = 0;
    u32 first = 0;
    PLUGIN_coin_DrawAchievements(first, selectedAchievement, earnedMask);

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            break;
        if (pressed & KEY_DDOWN)
        {
            u32 oldSelected = selectedAchievement;
            u32 oldFirst = first;
            selectedAchievement = selectedAchievement + 1u < COIN_ACHIEVEMENT_COUNT ?
                selectedAchievement + 1u : 0u;
            first = PLUGIN_coin_AdjustAchievementFirst(
                first,
                PLUGIN_coin_AchievementRowForIndex(selectedAchievement)
            );
            if (first != oldFirst)
                PLUGIN_coin_DrawAchievements(first, selectedAchievement, earnedMask);
            else if (selectedAchievement != oldSelected)
                PLUGIN_coin_RedrawAchievementSelection(
                    first, oldSelected, selectedAchievement, earnedMask
                );
        }
        else if (pressed & KEY_DUP)
        {
            u32 oldSelected = selectedAchievement;
            u32 oldFirst = first;
            selectedAchievement = selectedAchievement ?
                selectedAchievement - 1u : COIN_ACHIEVEMENT_COUNT - 1u;
            first = PLUGIN_coin_AdjustAchievementFirst(
                first,
                PLUGIN_coin_AchievementRowForIndex(selectedAchievement)
            );
            if (first != oldFirst)
                PLUGIN_coin_DrawAchievements(first, selectedAchievement, earnedMask);
            else if (selectedAchievement != oldSelected)
                PLUGIN_coin_RedrawAchievementSelection(
                    first, oldSelected, selectedAchievement, earnedMask
                );
        }
        else if (pressed & KEY_X)
        {
            if (earnedMask & (1u << selectedAchievement))
            {
                PLUGIN_coin_OpenAchievementDetail(selectedAchievement);
                PLUGIN_coin_DrawAchievements(first, selectedAchievement, earnedMask);
            }
        }
    } while (!COIN_HOST__menuShouldExit);

    g_coinAchievementTitles = NULL;
    g_coinAchievementDetailTitle = NULL;
    g_coinAchievementDetailMessage = NULL;
    COIN_MENU__TempFree(scratchBase, 0x1000u);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawOptions(void)
{
    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinOptionsPageTitle);
    PLUGIN_coin_DrawSimpleMenuItem(45u, true,
        g_coinAchievementsEnabled ? g_coinDisableAchievementsItem :
            g_coinEnableAchievementsItem,
        g_coinAchievementsEnabled ? COLOR_WHITE : COLOR_GREEN);
    COIN_HOST__Draw_DrawString(20, 120, COLOR_GRAY, g_coinBackShort);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenOptions(void)
{
    PLUGIN_coin_DrawOptions();

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            break;
        if (pressed & KEY_A)
        {
            if (g_coinAchievementsEnabled)
            {
                if (PLUGIN_coin_ConfirmAchievementAction())
                    g_coinAchievementsEnabled = false;
            }
            else
            {
                g_coinAchievementsEnabled = true;
            }
            PLUGIN_coin_DrawOptions();
        }
    } while (!COIN_HOST__menuShouldExit);

    PLUGIN_coin_SaveMenuSettings();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenPlayCoinzMenu(void)
{
    bool showAchievements = PLUGIN_coin_HasEarnedAchievements();
    u32 selected = 0;
    PLUGIN_coin_DrawPlayCoinzMenu(selected, showAchievements);

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return;

        u32 count = PLUGIN_coin_PlayCoinzMenuCount(showAchievements);

        if (pressed & KEY_DDOWN)
        {
            u32 oldSelected = selected;
            selected = selected + 1u < count ? selected + 1u : 0u;
            if (selected != oldSelected)
                PLUGIN_coin_RedrawPlayCoinzSelection(
                    oldSelected,
                    selected,
                    showAchievements
                );
        }
        else if (pressed & KEY_DUP)
        {
            u32 oldSelected = selected;
            selected = selected ? selected - 1u : count - 1u;
            if (selected != oldSelected)
                PLUGIN_coin_RedrawPlayCoinzSelection(
                    oldSelected,
                    selected,
                    showAchievements
                );
        }
        else if (pressed & KEY_A)
        {
            if (selected == 0u)
            {
                PLUGIN_coin_EditPlayCoins();
            }
            else if (showAchievements && selected == 1u)
            {
                PLUGIN_coin_OpenAchievements();
            }
            else
            {
                PLUGIN_coin_OpenOptions();
            }

            bool newShowAchievements = PLUGIN_coin_HasEarnedAchievements();
            u32 newCount = PLUGIN_coin_PlayCoinzMenuCount(newShowAchievements);
            if (selected >= newCount)
                selected = newCount - 1u;
            showAchievements = newShowAchievements;
            PLUGIN_coin_DrawPlayCoinzMenu(selected, showAchievements);
        }
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
            if (!g_coinsFailed && R_FAILED(PLUGIN_coin_EnsurePackedAssets()))
                g_coinsFailed = true;
            g_patchedHome = true;
        }
        else
        {
            PLUGIN_coin_UpdateDayHistory();
            if (coinsBin != g_lastCoins || coinsEarned ||
                coinsEverSpent != g_lastEverSpent || g_coinExtendedDirty)
            {
                PLUGIN_coin_HandleCoins();
            }
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
            PLUGIN_coin_StringEquals(item->title, g_coinBuiltInMenuTitle))
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
    if (!COIN_BLUR__AddTickFunc || !COIN_BLUR__RemoveTickFunc ||
        !COIN_BLUR__AddFeatureItem ||
        !COIN_BLUR__DrawFeatureFrame || !COIN_MENU__AddItem ||
        !COIN_MENU__OpenPluginFile || !COIN_MENU__UnpackLz10File ||
        !COIN_MENU__ClosePluginFile || !COIN_MENU__LoadData ||
        !COIN_MENU__SaveData || !COIN_HOST__dateTimeToString)
    {
        return false;
    }

    PLUGIN_coin_LoadMenuSettings();

    if (!COIN_BLUR__AddTickFunc(PLUGIN_coin_OnBlurTick, 1000000000LL))
        return false;

    if (!COIN_BLUR__AddFeatureItem(
            &g_coinBlurFeatureRegistration,
            COIN_PLUGIN_ID,
            g_coinDebugTitle,
            PLUGIN_coin_OpenDebug))
    {
        COIN_BLUR__RemoveTickFunc(PLUGIN_coin_OnBlurTick);
        return false;
    }

    if (COIN_MENU__AddItem(
            &g_coinMenuRegistration,
            COIN_PLUGIN_ID,
            g_coinMenuTitle,
            PLUGIN_coin_OpenPlayCoinzMenu,
            RGB565(31, 63, 20)))
    {
        PLUGIN_coin_RemoveBuiltInMenuItem();
    }

    return true;
}