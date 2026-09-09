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
    u32 flags;
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
PLUGIN_RODATA(coin) static const char g_coinDisableProgressiveCostItem[] =
    "Disable Progressive Coin Cost";
PLUGIN_RODATA(coin) static const char g_coinEnableProgressiveCostItem[] =
    "Enable Progressive Coin Cost";
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
PLUGIN_RODATA(coin) static const char g_coinArtworkCredit[] =
    "*secrets* artwork courtesy of @AnasAbdin";
PLUGIN_RODATA(coin) static const char g_coinViewShort[] = "X: view";
PLUGIN_RODATA(coin) static const char g_coinAchievementOptionsPrompt[] = "Press X for options";
PLUGIN_RODATA(coin) static const char g_coinAchievementResendItem[] = "Resend if missing";
PLUGIN_RODATA(coin) static const char g_coinAchievementDeleteItem[] = "Delete from notifications";
PLUGIN_RODATA(coin) static const char g_coinConfirmTitle[] = "Are you sure?";
PLUGIN_RODATA(coin) static const char g_coinConfirmNo[] = "No";
PLUGIN_RODATA(coin) static const char g_coinConfirmYes[] = "Yes";
PLUGIN_RODATA(coin) static const char g_coinProgressiveCostExplain[] =
    "In PlayCoinz, each coin after the 10th coin\n"
    "progressively costs an extra +3 steps to earn.\n"
    "This means coin 11 takes 103 steps, coin 14\n"
    "takes 112 steps, and so on.\n"
    "Disabling returns to the stock 100 steps per\n"
    "coin system.";
PLUGIN_RODATA(coin) static const char g_coinProgressiveCostWarning[] =
    "Warning: This will desync from your tracked\n"
    "'recommended' coin balance, and disable\n"
    "achievements.\n"
    "You can turn both back on at any time.";
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
#define COIN_SETTINGS_VERSION           2u
#define COIN_SETTINGS_ACHIEVEMENTS      (1u << 0)
#define COIN_SETTINGS_PROGRESSIVE_COST  (1u << 1)
#define COIN_SETTINGS_VALID_MASK        (COIN_SETTINGS_ACHIEVEMENTS | COIN_SETTINGS_PROGRESSIVE_COST)
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
// shared NAND extdata path, low word comes before the usual 0x00048000 high word
PLUGIN_RODATA(coin) static const u32 g_gameCoinArchivePath[3] = {
    MEDIATYPE_NAND,
    0xF000000Bu,
    0x00048000u,
}; //type low high

// coins stuff
extern u16 g_coinDat;
extern u32 g_coinData[4];
extern u16 g_coinChange[4];
extern u32 g_coinProgressiveToday;
extern volatile CoinStepDiagnostics PLUGIN_coin_stepDiagnostics;
PLUGIN_DATA(coin) u32 g_coinOffset = 0;
PLUGIN_DATA(coin) static u32 g_lastCoins = 0;
PLUGIN_DATA(coin) static u32 g_lastRecommended = 0;
PLUGIN_DATA(coin) static u32 g_lastEverSpent = 0;
PLUGIN_DATA(coin) static u32 g_coinKey = 0x45454545u;
PLUGIN_DATA(coin) static bool g_coinsFailed = false;
PLUGIN_DATA(coin) static bool g_patchedHome = false;

// NEWS LED address comes from OperateOnProcessByName's 0x00100000 mapping
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
PLUGIN_BSS(coin) static bool g_coinProgressiveCostEnabled;
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
    settings.flags =
        (g_coinAchievementsEnabled ? COIN_SETTINGS_ACHIEVEMENTS : 0u) |
        (g_coinProgressiveCostEnabled ? COIN_SETTINGS_PROGRESSIVE_COST : 0u);
    (void)COIN_MENU__SaveData(COIN_PLUGIN_ID, &settings, sizeof(settings));
}

PLUGIN_CODE(coin) static void PLUGIN_coin_LoadMenuSettings(void)
{
    g_coinAchievementsEnabled = true;
    g_coinProgressiveCostEnabled = true;

    CoinMenuSettings settings;
    if (COIN_MENU__LoadData(COIN_PLUGIN_ID, &settings, sizeof(settings)))
    {
        if (settings.version == 1u && settings.flags <= 1u)
        {
            g_coinAchievementsEnabled = settings.flags != 0;
        }
        else if (settings.version == COIN_SETTINGS_VERSION &&
                 !(settings.flags & ~COIN_SETTINGS_VALID_MASK))
        {
            g_coinAchievementsEnabled =
                (settings.flags & COIN_SETTINGS_ACHIEVEMENTS) != 0;
            g_coinProgressiveCostEnabled =
                (settings.flags & COIN_SETTINGS_PROGRESSIVE_COST) != 0;
        }
    }

    g_coinChange[3] = g_coinProgressiveCostEnabled ? 1u : 0u;
}

#define PLAYCOIN_HELPERS_EARLY
#include "playcoin_helpers.c"
#undef PLAYCOIN_HELPERS_EARLY

PLUGIN_CODE(coin) static void PLUGIN_coin_SetProgressiveCostEnabled(bool enabled)
{
    u32 today = PLUGIN_coin_GetTodayWalked() + coinsEarned;
    g_coinProgressiveToday = today > 0xFFFFu ? 0xFFFFu : today;
    g_coinProgressiveCostEnabled = enabled;
    g_coinChange[3] = enabled ? 1u : 0u;
    COIN_HOST__svcFlushEntireDataCache();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_SyncProgressiveToday(void)
{
    g_coinProgressiveToday = PLUGIN_coin_GetTodayWalked();
    COIN_HOST__svcFlushEntireDataCache();
}

#define PLAYCOIN_SECRETS_CHECKS
#include "playcoin_secrets.c"
#undef PLAYCOIN_SECRETS_CHECKS

#define PLAYCOIN_HELPERS_STATE
#include "playcoin_helpers.c"
#undef PLAYCOIN_HELPERS_STATE

PLUGIN_CODE(coin) static void PLUGIN_coin_HandleCoins(void)
{
    bool earnedEvent = coinsEarned != 0;
    bool spentEvent = coinsEverSpent != g_lastEverSpent;
    bool balanceEvent = g_coinBalanceEvent;
    bool gambleEvent = g_coinGambleEvent;

    PLUGIN_coin_UpdateDayHistory();
    PLUGIN_coin_AddTodayWalked(coinsEarned);
    PLUGIN_coin_SyncProgressiveToday();

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

    // coins.bin: old 0x20 base, 0x20 extension, optional 8-byte secret mask
}

PLUGIN_CODE(coin) static void PLUGIN_coin_SetupCoins(void)
{
    // Loader already seeded vanilla state if coins.bin was bad
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
            // bad tracking data gets replaced only after Home Menu attaches
            // keep the old file intact until the full replacement write succeeds
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
                // bad suffix gets dropped, never risk the valid base
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
        {
            PLUGIN_coin_UpdateDayHistory();
            PLUGIN_coin_SyncProgressiveToday();
        }
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
        // keep Loader's recovered tracking baseline exactly as-is
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

    // normal coinsSpent tops at 300, still clamp cheated weirdness
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
    // shared NAND extdata 0xf000000b
    res = COIN_HOST__FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, pathData);
    if (R_FAILED(res)) //return if error
        return res;

    // open Nintendo's /gamecoin.dat
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

    // vanilla side still caps at 300, naturally
    u16 newAmount = amount > 300 ? 300 : amount;
    u16 savedAmount = newAmount > coinsSpent ? newAmount - coinsSpent : 0;
    // Home Menu consumes this vanilla-side balance
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

    // update the extended wallet too
    coinsBin = (u32)amount + coinsSpent;
    g_coinDat = newAmount; // save (coins) to coinsDat so that next coinsSpent recalc is correct
    return COIN_HOST__FSUSER_CloseArchive(archive);
}

#define PLAYCOIN_MENUS_EDITOR
#include "playcoin_menus.c"
#undef PLAYCOIN_MENUS_EDITOR



#define PLAYCOIN_SECRETS_DATA
#include "playcoin_secrets.c"
#undef PLAYCOIN_SECRETS_DATA

#define PLAYCOIN_HELPERS_TEXT
#include "playcoin_helpers.c"
#undef PLAYCOIN_HELPERS_TEXT

#define PLAYCOIN_SECRETS_NEWS
#include "playcoin_secrets.c"
#undef PLAYCOIN_SECRETS_NEWS

#define PLAYCOIN_MENUS_MAIN
#include "playcoin_menus.c"
#undef PLAYCOIN_MENUS_MAIN

#define PLAYCOIN_HELPERS_STEPS
#include "playcoin_helpers.c"
#undef PLAYCOIN_HELPERS_STEPS

#define PLAYCOIN_MENUS_DEBUG_AND_ACHIEVEMENTS
#include "playcoin_menus.c"
#undef PLAYCOIN_MENUS_DEBUG_AND_ACHIEVEMENTS


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

#define PLAYCOIN_HELPERS_MENU_ITEMS
#include "playcoin_helpers.c"
#undef PLAYCOIN_HELPERS_MENU_ITEMS

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
