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

typedef char CoinNewsHeaderSizeMustBe70[(sizeof(NotificationHeader) == 0x70) ? 1 : -1];

typedef struct
{
    u32 logicalIndex;
    NotificationHeader header;
} CoinNewsAnchor;

extern bool PLUGIN_blur_AddTickFunc(BlurTickFunc func, s64 intervalNs);
extern bool PLUGIN_blur_RemoveTickFunc(BlurTickFunc func);
extern bool PLUGIN_blur_AddFeatureItem(
    BlurFeatureRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void)
);
extern void PLUGIN_blur_DrawFeatureFrame(const char *title);
extern bool PLUGIN_blur_SleepTryEnterIo(void);
extern void PLUGIN_blur_SleepLeaveIo(void);
extern u32 PLUGIN_blur_SleepCycleCounter(void);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_AddTickFunc);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_RemoveTickFunc);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_AddFeatureItem);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_DrawFeatureFrame);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_SleepTryEnterIo);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_SleepLeaveIo);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_blur_SleepCycleCounter);

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
    (void*)svcControlProcess,
    (void*)svcSleepThread,
    (void*)OpenProcessByName,
    (void*)RecursiveLock_Init,
    (void*)RecursiveLock_Lock,
    (void*)RecursiveLock_Unlock,
    (void*)PLUGIN_blur_SleepTryEnterIo,
    (void*)PLUGIN_blur_SleepLeaveIo,
    (void*)PLUGIN_MENU_RemoveItem,
    (void*)PLUGIN_MENU_ExtractRawFile,
    (void*)PLUGIN_MENU_GetApiVersion,
    (void*)PLUGIN_blur_SleepCycleCounter,
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
#define COIN_HOST__svcControlProcess       ((Result(*)(Handle,ProcessOp,u32,u32))pluginTable_coin[41])
#define COIN_HOST__RecursiveLock_Init      ((void(*)(RecursiveLock*))pluginTable_coin[44])
#define COIN_HOST__RecursiveLock_Lock      ((void(*)(RecursiveLock*))pluginTable_coin[45])
#define COIN_HOST__RecursiveLock_Unlock    ((void(*)(RecursiveLock*))pluginTable_coin[46])
#define COIN_HOST__Sleep_TryEnterIo        ((bool(*)(void))pluginTable_coin[47])
#define COIN_HOST__Sleep_LeaveIo           ((void(*)(void))pluginTable_coin[48])
#define COIN_BLUR__SleepCycleCounter       ((u32(*)(void))pluginTable_coin[52])
#define COIN_MENU__RemoveItem             ((bool(*)(PluginMenuRegistration*))pluginTable_coin[49])
#define COIN_MENU__ExtractRawFile          ((Result(*)(const PluginMenuFileContext*,u32,u32,const char*))pluginTable_coin[50])
#define COIN_MENU__GetApiVersion           ((u32(*)(void))pluginTable_coin[51])
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
PLUGIN_RODATA(coin) static const char g_coinPtmU[] = "ptm:u";
PLUGIN_RODATA(coin) const char g_coinMenuProcessName[] = "menu";
PLUGIN_RODATA(coin) const char g_coinMenuTitle[] = "PlayCoinz Menu";
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
PLUGIN_RODATA(coin) static const char g_coinCleanStaleNotificationsItem[] =
    "Clean Stale Notifications";
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
PLUGIN_RODATA(coin) static const char g_coinAchievementsDisableExplain[] =
    "In PlayCoinz you can earn Achievements,\n"
    "alerting with custom notifications upon\n"
    "earning them.\n"
    "\n"
    "Disabling will prevent awarding these and\n"
    "hide references to them.\n"
    "\n"
    "You can delete notifications from the\n"
    "Achievements page beforehand.";
PLUGIN_RODATA(coin) static const char g_coinCleanStaleNotificationsExplain[] =
    "If you have earned notifications from an older\n"
    "PlayCoinz save, this will delete any you have\n"
    "NOT earned according to your current save file.\n"
    "\n"
    "Deleting notifications of Achievements you\n"
    "currently own can be done from the View\n"
    "Achievements page.";
PLUGIN_RODATA(coin) static const char g_coinResultAlreadyExists[] =
    "This already exists in your notifications!";
PLUGIN_RODATA(coin) static const char g_coinResultDone[] = "Done!";
PLUGIN_RODATA(coin) static const char g_coinResultNotFound[] = "Notification not found!";
PLUGIN_RODATA(coin) static const char g_coinDebugTitle[] = "Play Coin Debug";
PLUGIN_RODATA(coin) static const char g_coinDebugControls[] = "B: back";
PLUGIN_RODATA(coin) static const char g_coinDiagWaiting[] = "Waiting for Home Menu coin state...";
PLUGIN_RODATA(coin) static const char g_coinHistoryWaiting[] = "waiting for query";
PLUGIN_RODATA(coin) static const char g_coinLiveGroup[] = "HOME day state";
PLUGIN_RODATA(coin) static const char g_coinHistoryGroup[] = "Accounting";
PLUGIN_RODATA(coin) static const char g_coinHistoryQueryGroup[] = "Last history query";
PLUGIN_RODATA(coin) static const char g_coinProgressionGroup[] = "Progression";
PLUGIN_RODATA(coin) static const char g_coinTotalFmt[] = "HOME total: %lu";
PLUGIN_RODATA(coin) static const char g_coinBoundaryFmt[] = "Boundary: %lu";
PLUGIN_RODATA(coin) static const char g_coinLiveProgressFmt[] = "Progress: %lu / 100";
PLUGIN_RODATA(coin) static const char g_coinNextCheckFmt[] = "Next query: %lu steps";
PLUGIN_RODATA(coin) static const char g_coinDayBaselineFmt[] = "Day baseline: %lu";
PLUGIN_RODATA(coin) static const char g_coinSpentBoundaryFmt[] = "Spent boundary: %lu";
PLUGIN_RODATA(coin) static const char g_coinHistoryRemainderFmt[] = "Unspent: %lu steps";
PLUGIN_RODATA(coin) static const char g_coinTodayFmt[] = "Coins today: %lu";
PLUGIN_RODATA(coin) static const char g_coinProgressiveTodayFmt[] =
    "(+3 mode) Coins today: %lu";
PLUGIN_RODATA(coin) static const char g_coinNextCoinFmt[] = "Coin #%lu cost: %lu";
PLUGIN_RODATA(coin) static const char g_coinHistoryNeededFmt[] = "Needed: %lu steps";
PLUGIN_RODATA(coin) static const char g_coinHistoryQueryFmt[] = "Total: %lu";
PLUGIN_RODATA(coin) static const char g_coinHistoryQueryNone[] = "Not queried this boot";
PLUGIN_RODATA(coin) static const char g_coinHistoryQueryTimeFmt[] = "%s";
PLUGIN_RODATA(coin) static const char g_coinHistoryQueryTimeBad[] = "Date unavailable";
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
PLUGIN_RODATA(coin) static const char g_pendingSpendText[] =
    "Please return to HOME to set coins.";
PLUGIN_RODATA(coin) static const char g_errorFormat[] = "Error: 0x%08lx";
PLUGIN_RODATA(coin) static const char g_recommendedText[] = "Recommended:";
PLUGIN_RODATA(coin) static const char g_recommendedFormat[] = "Press Y to return to tracked coins: %lu";
PLUGIN_RODATA(coin) static const char g_progressiveDisabledEditorText[] =
    "(progressive cost mode disabled)";
PLUGIN_RODATA(coin) static const char g_achievementsDisabledEditorText[] =
    "(achievements disabled)";
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
#define COIN_FILE_OLD_NO_ACHIEVEMENT_SIZE 0x40u
#define COIN_FILE_OLD_WITH_ACHIEVEMENT_SIZE 0x48u
#define COIN_FILE_ACCOUNTING_OFFSET     0x40u
#define COIN_FILE_ACHIEVEMENT_OFFSET    0x50u
#define COIN_FILE_NO_ACHIEVEMENT_SIZE   0x50u
#define COIN_FILE_WITH_ACHIEVEMENT_SIZE 0x58u
#define COIN_FILE_EXTENSION_SEED        0x31445843u
#define COIN_FILE_HOME_ACCOUNTING_SEED  0x314D4F48u
#define COIN_BLACKJACK_COUNTER_MAX       30000u
#define COIN_EXT_BLACKJACK_COUNTERS_WORD 0u
#define COIN_EXT_BLACKJACK_SURPLUS_WORD  1u
#define COIN_EXT_LAST_DAY_WORD           2u
#define COIN_EXT_DAY_REBASE_PRESERVE     (1u << 31)
#define COIN_EXT_DAY_VALUE_MASK          0x7FFFFFFFu
#define COIN_EXT_DAY_HISTORY_WORD        3u
#define COIN_EXT_TODAY_WORD              6u
#define COIN_EXT_ACHIEVEMENTS_DISABLED   (1u << 31)
#define COIN_EXT_FLAT_WALLET             (1u << 30)
#define COIN_EXT_FLAT_CHECKPOINT_MASK    0x3FFF0000u
#define COIN_EXT_FLAT_CHECKPOINT_SHIFT   16u
#define COIN_DAY_HISTORY_COUNT           6u
#define COIN_RTC_DAY_WAITING              0u
#define COIN_RTC_DAY_EXACT_NEXT           1u
#define COIN_RTC_DAY_REJECT               2u
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
PLUGIN_RODATA(coin) static const char g_coinNewsName[] = "news";
PLUGIN_RODATA(coin) static const char g_coinNewsServiceName[] = "news:s";
PLUGIN_RODATA(coin) static const char g_coinAchievementPath[] = "/luma/coinachv/achv.bin";
PLUGIN_RODATA(coin) static const char g_coinAchievementDirPath[] = "/luma/coinachv";
PLUGIN_RODATA(coin) static const char g_coinIcnPath[] = "/luma/coinachv/icn.bin";
PLUGIN_RODATA(coin) static const char g_coinEasyTopImagePath[] = "/luma/coinachv/easytop.bin";
PLUGIN_RODATA(coin) static const char g_coinMediumTopImagePath[] = "/luma/coinachv/mediumtop.bin";
PLUGIN_RODATA(coin) static const char g_coinHardTopImagePath[] = "/luma/coinachv/hardtop.bin";
PLUGIN_RODATA(coin) static const char g_coinExtremeTopImagePath[] = "/luma/coinachv/extremtop.bin";
PLUGIN_DATA(coin) static const char *g_coinPackedAssetPaths[COIN_PACKED_ASSET_COUNT] = {
    g_coinIcnPath,
    g_coinAchievementPath,
    g_coinEasyTopImagePath,
    g_coinMediumTopImagePath,
    g_coinHardTopImagePath,
    g_coinExtremeTopImagePath,
};
PLUGIN_DATA(coin) static const char *g_coinAchievementImagePackPaths[4] = {
    g_coinEasyTopImagePath,
    g_coinMediumTopImagePath,
    g_coinHardTopImagePath,
    g_coinExtremeTopImagePath,
};
PLUGIN_DATA(coin) static const char *g_coinAchievementTierNames[4] = {
    g_coinTierEasy,
    g_coinTierMedium,
    g_coinTierHard,
    g_coinTierExtreme,
};
PLUGIN_RODATA(coin) static const u32 g_coinAchievementTierMasks[4] = {
    0x0000001Fu,
    0x000003E0u,
    0x00007C00u,
    0x00038000u,
};
PLUGIN_RODATA(coin) static const u32 g_coinAchievementTierColors[4] = {
    COIN_ACHV_EASY_COLOR,
    COLOR_YELLOW,
    COIN_ACHV_HARD_COLOR,
    COLOR_RED,
};
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
}; // type low high

// coins stuff
extern volatile u16 g_coinDat;
extern volatile u32 g_coinData[4];
extern volatile u16 g_coinChange[4];
extern volatile u32 g_coinProgressiveToday;
extern volatile u32 g_coinProgressiveState[4];
extern volatile CoinStepDiagnostics PLUGIN_coin_stepDiagnostics;
extern volatile u32 g_coinHistoryQueryTime[2];
extern volatile u32 g_coinProgressiveFloorActive;
PLUGIN_DATA(coin) u32 g_coinOffset = 0;
PLUGIN_DATA(coin) u16 g_coinEarnedAppliedCounter = 0;
PLUGIN_DATA(coin) static u32 g_lastCoins = 0;
PLUGIN_DATA(coin) static u32 g_lastEverSpent = 0;
PLUGIN_DATA(coin) static u32 g_coinKey = 0x45454545u;
PLUGIN_DATA(coin) static bool g_coinsFailed = false;
PLUGIN_DATA(coin) static bool g_patchedHome = false;

// NEWS LED address comes from OperateOnProcessByName's 0x00100000 mapping
PLUGIN_BSS(coin) static PluginMenuRegistration g_coinMenuRegistration;
PLUGIN_BSS(coin) static bool g_coinMenuRegistered;
PLUGIN_BSS(coin) static BlurFeatureRegistration g_coinBlurFeatureRegistration;
PLUGIN_BSS(coin) static RecursiveLock g_coinStateLock;
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
PLUGIN_BSS(coin) static bool g_coinSavePending;
PLUGIN_BSS(coin) static bool g_coinDayTransitionAuthorized;
PLUGIN_BSS(coin) static bool g_coinEarnedEventPending;
PLUGIN_BSS(coin) static bool g_coinCompletedWalkPending;
PLUGIN_BSS(coin) static u32 g_coinCompletedDay;
PLUGIN_BSS(coin) static u32 g_coinCompletedWeek;
PLUGIN_BSS(coin) static u32 g_coinExtendedData[COIN_FILE_EXTENSION_WORDS];
PLUGIN_BSS(coin) static u32 g_coinAccountingSavedGeneration;
PLUGIN_BSS(coin) static bool g_coinAssetsReady;
PLUGIN_BSS(coin) static bool g_coinStartupAttached;
PLUGIN_BSS(coin) static bool g_coinExtendedDirty;
PLUGIN_BSS(coin) static bool g_coinMilestoneEvent;
PLUGIN_BSS(coin) static u32 g_coinMaintenanceTicks;
PLUGIN_BSS(coin) static u32 g_coinAssetRetryTicks;
PLUGIN_BSS(coin) static u32 g_coinFlatModeChangeGeneration;
PLUGIN_BSS(coin) static u32 g_coinCatchupAppliedGeneration;
PLUGIN_BSS(coin) static u32 g_coinNativeSentinelDay;
PLUGIN_BSS(coin) static u32 g_coinLastSleepCycle;
PLUGIN_BSS(coin) static char (*g_coinAchievementTitles)[40];
PLUGIN_BSS(coin) static char *g_coinAchievementDetailTitle;
PLUGIN_BSS(coin) static char *g_coinAchievementDetailMessage;
PLUGIN_BSS(coin) static CoinNewsAnchor *g_coinNewsAnchors;
PLUGIN_BSS(coin) static u32 g_coinNewsAnchorCount;
PLUGIN_BSS(coin) static u32 g_coinNewsExpectedTotal;
PLUGIN_BSS(coin) static const u16 *g_coinNewsRawDeleteTitle;
PLUGIN_BSS(coin) static u32 g_coinNewsRawRemoved;

#define coinsBin       g_coinData[0]
#define coinsRec       g_coinData[1]
#define coinsTrue      g_coinData[2]
#define coinsEverSpent g_coinData[3]
#define coinsEarned    g_coinChange[0]
#define coinsSpent     g_coinChange[1]

extern bool PLUGIN_coin_AttachHomeMenu(bool *incompatible);
extern bool PLUGIN_coin_HomeMenuLoaded(void);
PLUGIN_CODE(coin) static void PLUGIN_coin_OnBlurTick(u64 delta);
extern bool PLUGIN_coin_LockHomeState(Handle *processHandleOut);
extern bool PLUGIN_coin_UnlockHomeState(Handle processHandle);
extern bool PLUGIN_coin_CopyHomeAccounting(u32 out[5], u32 *generation);
extern bool PLUGIN_coin_HomeAccountingChanged(u32 savedGeneration);
extern u32 PLUGIN_coin_HomeCatchupState(void);
extern bool PLUGIN_coin_CopyHomeCatchup(u32 out[5]);
extern void PLUGIN_coin_AcknowledgeHomeCatchup(u32 generation);
extern bool PLUGIN_coin_SetHomeFlatMode(bool flat);
extern void PLUGIN_coin_PublishSavedExtension(u32 generation, u32 todayWord,
    u32 dayState);
extern bool PLUGIN_coin_SettingsRequestPending(void);
extern bool PLUGIN_coin_CopySettingsRequest(u32 out[4]);
extern void PLUGIN_coin_FinishSettingsRequest(const u32 accounting[5], bool saved, u32 *generation);
extern void PLUGIN_coin_PublishAccountingReady(void);
extern bool PLUGIN_coin_HomeAccountingReady(void);
extern u32 PLUGIN_coin_RtcDayDecision(void);
extern bool PLUGIN_coin_RtcDayDecisionPending(void);
extern bool PLUGIN_coin_RtcExactNextDayPending(void);
extern u32 PLUGIN_coin_RtcPreviousProgressiveRemainder(void);
extern u32 PLUGIN_coin_RtcPreviousCoinsToday(void);
extern u32 PLUGIN_coin_RtcDecisionCalendarStamp(void);
extern u32 PLUGIN_coin_RtcObservedCalendarStamp(void);
extern void PLUGIN_coin_ConsumeRtcDayDecision(void);
extern Result PLUGIN_coin_TriggerAchievement(u32 achievementIndex);
static void PLUGIN_coin_SyncAchievementSettingToExtendedData(void);
static void PLUGIN_coin_HandleCoins(void);
static u32 *PLUGIN_coin_CommandBuffer(void);

PLUGIN_CODE(coin) static void PLUGIN_coin_SaveMenuSettings(void)
{
    CoinMenuSettings settings;
    settings.version = COIN_SETTINGS_VERSION;
    settings.flags =
        (g_coinAchievementsEnabled ? COIN_SETTINGS_ACHIEVEMENTS : 0u) |
        (g_coinProgressiveCostEnabled ? COIN_SETTINGS_PROGRESSIVE_COST : 0u);
    (void)COIN_MENU__SaveData(COIN_PLUGIN_ID, &settings, sizeof(settings));

    // Loader needs both Settings before HOME starts
    PLUGIN_coin_SyncAchievementSettingToExtendedData();
    if (g_patchedHome && !g_coinsFailed && g_coinExtendedDirty)
        PLUGIN_coin_HandleCoins();
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

static u16 PLUGIN_coin_GetTodayWalked(void);
static void PLUGIN_coin_SetTodayWalked(u16 value);
static bool PLUGIN_coin_ReadCalendarNow(u32 *dayOut, u32 *stampOut);
static u32 PLUGIN_coin_CurrentCalendarDay(void);

#define PLAYCOIN_HELPERS_EARLY
#include "playcoin_helpers.c"
#undef PLAYCOIN_HELPERS_EARLY

PLUGIN_CODE(coin) static u32 PLUGIN_coin_PendingEarned(void)
{
    return (u16)(coinsEarned - g_coinEarnedAppliedCounter);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_SetProgressiveCostEnabled(bool enabled)
{
    Handle homeProcess = 0;
    if (g_patchedHome && !PLUGIN_coin_LockHomeState(&homeProcess))
        return false;

    u32 today = PLUGIN_coin_GetTodayWalked() + PLUGIN_coin_PendingEarned();
    g_coinProgressiveToday = today > 0xFFFFu ? 0xFFFFu : today;
    g_coinProgressiveFloorActive =
        (g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] &
            COIN_EXT_DAY_REBASE_PRESERVE) != 0u;
    g_coinProgressiveCostEnabled = enabled;
    g_coinChange[3] = enabled ? 1u : 0u;
    PLUGIN_coin_SetHomeFlatMode(!enabled);
    u32 accounting[5];
    if (homeProcess &&
        PLUGIN_coin_CopyHomeAccounting(accounting, &g_coinFlatModeChangeGeneration))
    {
        if (g_coinExtendedData[COIN_EXT_TODAY_WORD] &
            COIN_EXT_FLAT_CHECKPOINT_MASK)
            g_coinExtendedDirty = true;
        g_coinExtendedData[COIN_EXT_TODAY_WORD] &=
            ~COIN_EXT_FLAT_CHECKPOINT_MASK;
    }
    COIN_HOST__svcFlushEntireDataCache();

    return !homeProcess || PLUGIN_coin_UnlockHomeState(homeProcess);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_SyncProgressiveToday(void)
{
    g_coinProgressiveToday = PLUGIN_coin_GetTodayWalked();
    g_coinProgressiveFloorActive =
        (g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] &
            COIN_EXT_DAY_REBASE_PRESERVE) != 0u;
    COIN_HOST__svcFlushEntireDataCache();
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_HomeCounterExceedsToday(u32 currentDay, u32 currentStamp)
{
    u32 dayState = g_coinExtendedData[COIN_EXT_LAST_DAY_WORD];
    u32 trackedDay = dayState & COIN_EXT_DAY_VALUE_MASK;

    // keep preserved Today isolated from a rebased HOME day
    if (dayState & COIN_EXT_DAY_REBASE_PRESERVE)
        return false;

    return currentDay && currentStamp && (!trackedDay || currentDay >= trackedDay) &&
        PLUGIN_coin_RtcObservedCalendarStamp() == currentStamp &&
        (PLUGIN_coin_stepDiagnostics.valid & 1u) != 0u &&
        PLUGIN_coin_stepDiagnostics.coinsToday > PLUGIN_coin_GetTodayWalked();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_ClampTodayToHomeCounter(u32 currentDay, u32 currentStamp)
{
    if (!PLUGIN_coin_HomeCounterExceedsToday(currentDay, currentStamp))
        return;

    // HOME is a floor, only day rotation can lower Today
    u32 homeToday = PLUGIN_coin_stepDiagnostics.coinsToday;
    if (homeToday > 0xFFFFu)
        homeToday = 0xFFFFu;

    PLUGIN_coin_SetTodayWalked((u16)homeToday);
    g_coinExtendedDirty = true;
}

#define PLAYCOIN_SECRETS_CHECKS
#include "playcoin_secrets.c"
#undef PLAYCOIN_SECRETS_CHECKS

#define PLAYCOIN_HELPERS_STATE
#include "playcoin_helpers.c"
#undef PLAYCOIN_HELPERS_STATE

PLUGIN_CODE(coin) static void PLUGIN_coin_CloseFileArchive(
    Handle file,
    FS_Archive archive
)
{
    COIN_HOST__FSFILE_Close(file);
    COIN_HOST__FSUSER_CloseArchive(archive);
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_TakeEarnedBatch(void)
{
    u16 produced = coinsEarned;
    u16 applied = g_coinEarnedAppliedCounter;
    g_coinEarnedAppliedCounter = produced;
    return (u16)(produced - applied);
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_SettingsDayOrdinal(u32 date)
{
    if (!PLUGIN_coin_ValidAccountingDate(date) || !date)
        return 0;
    u32 year = date & 0xFFFFu;
    u32 month = (date >> 16) & 0xFFu;
    u32 day = date >> 24;
    u32 ordinal = 1u;
    for (u32 y = 1900u; y < year; y++)
        ordinal += PLUGIN_coin_IsLeapYear(y) ? 366u : 365u;
    ordinal += g_coinMonthStart[month - 1u] + day - 1u;
    if (month > 2u && PLUGIN_coin_IsLeapYear(year))
        ordinal++;
    return ordinal;
}

// save our Settings checkpoint before letting Nintendo write gamecoin.dat
PLUGIN_CODE(coin) static void PLUGIN_coin_HandleSettingsRequest(void)
{
    if (!PLUGIN_coin_SettingsRequestPending())
        return;
    if (PLUGIN_coin_HomeCatchupState())
        return;

    Handle homeProcess = 0;
    if (!PLUGIN_coin_LockHomeState(&homeProcess))
        return;

    u32 request[4];
    u32 oldAccounting[5];
    u32 oldGeneration = 0;
    if (!PLUGIN_coin_CopySettingsRequest(request) ||
        !PLUGIN_coin_CopyHomeAccounting(oldAccounting, &oldGeneration))
    {
        PLUGIN_coin_FinishSettingsRequest(NULL, false, NULL);
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        return;
    }

    u32 day = PLUGIN_coin_SettingsDayOrdinal(request[3]);
    if (!day || request[0] > 0xFFFFu)
    {
        PLUGIN_coin_FinishSettingsRequest(NULL, false, NULL);
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        return;
    }

    u32 oldExtension[COIN_FILE_EXTENSION_WORDS];
    for (u32 i = 0; i < COIN_FILE_EXTENSION_WORDS; i++)
        oldExtension[i] = g_coinExtendedData[i];
    bool oldDirty = g_coinExtendedDirty;
    bool oldEarnedEvent = g_coinEarnedEventPending;
    u16 oldApplied = g_coinEarnedAppliedCounter;
    u32 oldTrue = coinsTrue;
    u32 oldRec = coinsRec;

    // count any HOME earnings just before Settings opened toward the old day
    u32 oldDay = g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] & COIN_EXT_DAY_VALUE_MASK;
    u32 earnedBatch = PLUGIN_coin_TakeEarnedBatch();
    if (earnedBatch)
    {
        if (oldDay)
        {
            u32 today = PLUGIN_coin_GetTodayWalked() + earnedBatch;
            PLUGIN_coin_SetTodayWalked((u16)(today > 0xFFFFu ? 0xFFFFu : today));
        }
        coinsTrue = PLUGIN_coin_SaturatingAdd(coinsTrue, earnedBatch);
        coinsRec = PLUGIN_coin_SaturatingAdd(coinsRec, earnedBatch);
        PLUGIN_coin_ClampTrackedAccounting();
        g_coinExtendedDirty = true;
        g_coinEarnedEventPending = true;
    }

    g_coinDayTransitionAuthorized = true;
    (void)PLUGIN_coin_UpdateDayHistoryForDay(day);
    g_coinDayTransitionAuthorized = false;
    g_coinExtendedData[COIN_EXT_TODAY_WORD] &=
        ~COIN_EXT_FLAT_CHECKPOINT_MASK;

    u32 oldPrivateDay = PLUGIN_coin_SettingsDayOrdinal(oldAccounting[4]);
    u32 accountedToday = day > oldPrivateDay ? request[0] : oldAccounting[1];
    u32 accounting[5] = {1u, accountedToday, request[1], request[2], request[3]};

    Result rc;
    FS_Archive sd;
    Handle file = 0;
    rc = COIN_HOST__FSUSER_OpenArchive(&sd, ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL));
    if (R_SUCCEEDED(rc))
    {
        rc = COIN_HOST__FSUSER_OpenFile(&file, sd,
            COIN_HOST__fsMakePath(PATH_ASCII, g_coinFilePath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
        if (R_SUCCEEDED(rc))
        {
            rc = PLUGIN_coin_WriteFullState(file, coinsBin, coinsRec,
                coinsTrue, coinsEverSpent, accounting);
            Result closeRc = COIN_HOST__FSFILE_Close(file);
            if (R_SUCCEEDED(rc) && R_FAILED(closeRc))
                rc = closeRc;
        }
        Result archiveCloseRc = COIN_HOST__FSUSER_CloseArchive(sd);
        if (R_SUCCEEDED(rc) && R_FAILED(archiveCloseRc))
            rc = archiveCloseRc;
    }

    if (R_SUCCEEDED(rc))
    {
        u32 generation = 0;
        // Settings used the old remainder, so keep the old Today or the new day's zero
        g_coinProgressiveState[0] = PLUGIN_coin_GetTodayWalked();
        g_coinProgressiveState[1] = 0;
        g_coinProgressiveState[2] = 0;
        g_coinProgressiveState[3] = 0;
        g_coinProgressiveFloorActive =
            (g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] &
                COIN_EXT_DAY_REBASE_PRESERVE) != 0u;
        PLUGIN_coin_stepDiagnostics.valid = 0;
        PLUGIN_coin_FinishSettingsRequest(accounting, true, &generation);
        PLUGIN_coin_PublishSavedExtension(generation,
            g_coinExtendedData[COIN_EXT_TODAY_WORD],
            g_coinExtendedData[COIN_EXT_LAST_DAY_WORD]);
        // Settings may rewrite the native record even on a same-day correction
        g_coinNativeSentinelDay = 0;
        g_coinAccountingSavedGeneration = generation;
        g_lastCoins = coinsBin;
        g_coinExtendedDirty = false;
        // the normal pass still owns pending achievement events and spending
        g_coinSavePending = g_coinEarnedEventPending ||
            g_coinCompletedWalkPending || g_coinMilestoneEvent ||
            coinsEverSpent != g_lastEverSpent;
        if (!g_coinSavePending)
            g_lastEverSpent = coinsEverSpent;
    }
    else
    {
        for (u32 i = 0; i < COIN_FILE_EXTENSION_WORDS; i++)
            g_coinExtendedData[i] = oldExtension[i];
        coinsTrue = oldTrue;
        coinsRec = oldRec;
        g_coinEarnedAppliedCounter = oldApplied;
        g_coinEarnedEventPending = oldEarnedEvent;
        g_coinExtendedDirty = oldDirty;
        PLUGIN_coin_FinishSettingsRequest(NULL, false, NULL);
    }
    bool unlocked = PLUGIN_coin_UnlockHomeState(homeProcess);
    if (!unlocked)
        g_coinSavePending = true;
}

// write the private snapshot before the native lock
PLUGIN_CODE(coin) static Result PLUGIN_coin_InstallNativeSentinel(const u32 accounting[5])
{
    FS_Archive archive;
    Handle file;
    Result rc = PLUGIN_coin_OpenGameCoinFile(&archive, &file,
        FS_OPEN_READ | FS_OPEN_WRITE);
    if (R_FAILED(rc))
        return rc;

    u32 record[5];
    u32 read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, 0, record, sizeof(record));
    if (R_SUCCEEDED(rc) && (read != sizeof(record) || record[0] != 0x4F00u ||
        (record[1] & 0xFFFFu) > 300u ||
        (record[1] >> 16) > 11u || !PLUGIN_coin_ValidAccountingDate(record[4])))
        rc = (Result)-28;

    if (R_SUCCEEDED(rc) && (record[1] >> 16) != 11u)
    {
        // don't overwrite a newer native transaction
        if (record[2] != accounting[2] || record[3] != accounting[3] ||
            record[4] != accounting[4])
            rc = (Result)-28;
        else
        {
            u16 sentinel = 11u;
            u32 written = 0;
            rc = COIN_HOST__FSFILE_Write(file, &written, 6u,
                &sentinel, sizeof(sentinel), FS_WRITE_FLUSH);
            if (R_SUCCEEDED(rc) && written != sizeof(sentinel))
                rc = (Result)-29;
        }
    }

    Result closeRc = COIN_HOST__FSFILE_Close(file);
    Result archiveRc = COIN_HOST__FSUSER_CloseArchive(archive);
    if (R_SUCCEEDED(rc) && R_FAILED(closeRc))
        rc = closeRc;
    if (R_SUCCEEDED(rc) && R_FAILED(archiveRc))
        rc = archiveRc;
    return rc;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_HandleCoins(void)
{
    if (!g_patchedHome)
        (void)PLUGIN_coin_GenerateRandomBytes(&g_coinKey, sizeof(g_coinKey));

    Handle homeProcess = 0;
    if (!PLUGIN_coin_LockHomeState(&homeProcess))
    {
        g_coinSavePending = true;
        return;
    }

    u32 catchupState = PLUGIN_coin_HomeCatchupState();
    if (catchupState >= 1u && catchupState <= 3u)
    {
        g_coinSavePending = true;
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        return;
    }
    u32 catchup[5];
    bool hasCatchup = PLUGIN_coin_CopyHomeCatchup(catchup);
    if (catchupState == 4u && !hasCatchup)
    {
        g_coinSavePending = true;
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        return;
    }

    u32 currentDay = 0;
    u32 currentStamp = 0;
    (void)PLUGIN_coin_ReadCalendarNow(&currentDay, &currentStamp);

    bool otherWork = hasCatchup || !g_patchedHome ||
        !PLUGIN_coin_HomeAccountingReady() ||
        coinsBin != g_lastCoins || PLUGIN_coin_PendingEarned() ||
        g_coinSavePending || coinsEverSpent != g_lastEverSpent ||
        g_coinExtendedDirty ||
        PLUGIN_coin_HomeAccountingChanged(g_coinAccountingSavedGeneration) ||
        PLUGIN_coin_HomeCounterExceedsToday(currentDay, currentStamp);
    bool dayHistoryWork = PLUGIN_coin_DayHistoryNeedsUpdate(currentDay);
    u32 rtcDecision = PLUGIN_coin_RtcDayDecision();
    bool rtcDecisionValid =
        (rtcDecision == COIN_RTC_DAY_EXACT_NEXT || rtcDecision == COIN_RTC_DAY_REJECT) &&
        currentStamp && PLUGIN_coin_RtcDecisionCalendarStamp() == currentStamp;

    if (rtcDecision != COIN_RTC_DAY_WAITING && !rtcDecisionValid)
    {
        PLUGIN_coin_ConsumeRtcDayDecision();
        rtcDecision = COIN_RTC_DAY_WAITING;
        if (!otherWork && !dayHistoryWork)
        {
            (void)PLUGIN_coin_UnlockHomeState(homeProcess);
            return;
        }
    }

    if (rtcDecisionValid && !otherWork && !dayHistoryWork)
    {
        PLUGIN_coin_ConsumeRtcDayDecision();
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        return;
    }

    bool newCatchup = hasCatchup &&
        g_coinCatchupAppliedGeneration != catchup[4];
    u32 previousDayShadow = 0;
    if (newCatchup)
    {
        u32 oldDay = PLUGIN_coin_SettingsDayOrdinal(catchup[0]);
        u32 newDay = PLUGIN_coin_SettingsDayOrdinal(catchup[1]);
        u32 dayState = g_coinExtendedData[COIN_EXT_LAST_DAY_WORD];
        if (!oldDay || newDay != oldDay + 1u || newDay > currentDay ||
            (dayState & COIN_EXT_DAY_REBASE_PRESERVE) ||
            (dayState & COIN_EXT_DAY_VALUE_MASK) != oldDay ||
            PLUGIN_coin_PendingEarned() < catchup[2])
        {
            g_coinSavePending = true;
            (void)PLUGIN_coin_UnlockHomeState(homeProcess);
            return;
        }

        previousDayShadow = catchup[2];
        u32 previousDayBucket = PLUGIN_coin_GetTodayWalked() +
            previousDayShadow;
        if (previousDayBucket > 0xFFFFu)
            previousDayBucket = 0xFFFFu;
        u32 completedWeek = previousDayBucket;
        for (u32 i = 0; i < COIN_DAY_HISTORY_COUNT; i++)
            completedWeek += PLUGIN_coin_GetHistoryDay(i);

        // keep the completed window until its save goes through
        g_coinCompletedDay = previousDayBucket;
        g_coinCompletedWeek = completedWeek;
        g_coinCompletedWalkPending = true;

        if (PLUGIN_coin_GetTodayWalked() != previousDayBucket)
        {
            PLUGIN_coin_SetTodayWalked((u16)previousDayBucket);
            g_coinExtendedDirty = true;
        }
        g_coinCatchupAppliedGeneration = catchup[4];
    }

    g_coinDayTransitionAuthorized = rtcDecisionValid || newCatchup;
    PLUGIN_coin_UpdateDayHistoryForDay(currentDay);
    g_coinDayTransitionAuthorized = false;
    if (rtcDecision != COIN_RTC_DAY_WAITING)
        PLUGIN_coin_ConsumeRtcDayDecision();

    u32 earnedBatch = PLUGIN_coin_TakeEarnedBatch();
    if (earnedBatch)
        g_coinEarnedEventPending = true;
    u32 todayEarned = earnedBatch - previousDayShadow;

    bool spentEvent = coinsEverSpent != g_lastEverSpent;
    bool milestoneEvent = g_coinMilestoneEvent;

    PLUGIN_coin_AddTodayWalked(todayEarned, currentDay);
    PLUGIN_coin_ClampTodayToHomeCounter(currentDay, currentStamp);
    PLUGIN_coin_SyncProgressiveToday();

    if (earnedBatch > 0 || !g_patchedHome)
        coinsTrue = PLUGIN_coin_SaturatingAdd(coinsTrue, earnedBatch);

    coinsRec = PLUGIN_coin_SaturatingAdd(coinsRec, earnedBatch);
    PLUGIN_coin_ClampTrackedAccounting();

    u32 savedWallet = coinsBin;
    u32 savedRecommended = coinsRec;
    u32 savedLifetimeEarned = coinsTrue;
    u32 savedLifetimeSpent = coinsEverSpent;

    u32 accounting[5];
    u32 accountingGeneration = 0;
    if (!PLUGIN_coin_CopyHomeAccounting(accounting, &accountingGeneration))
    {
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        g_coinSavePending = true;
        return;
    }

    if (!g_coinProgressiveCostEnabled &&
        accountingGeneration != g_coinAccountingSavedGeneration)
    {
        u32 checkpoint = 0;
        u32 historyTotal = PLUGIN_coin_stepDiagnostics.historyTotal;
        u32 flatRemainder = g_coinProgressiveState[2];
        if (accountingGeneration > g_coinFlatModeChangeGeneration &&
            accounting[4] && PLUGIN_coin_RtcObservedCalendarStamp() == accounting[4] &&
            (PLUGIN_coin_stepDiagnostics.valid & 2u) &&
            g_coinProgressiveState[3] && flatRemainder < 100u &&
            historyTotal >= flatRemainder &&
            PLUGIN_coin_stepDiagnostics.historyBoundary == accounting[3])
        {
            s64 offset = (s64)(historyTotal - flatRemainder) - accounting[3];
            if (offset >= -8191 && offset <= 8191)
                checkpoint = (u32)(offset + 8192) <<
                    COIN_EXT_FLAT_CHECKPOINT_SHIFT;
        }
        u32 *todayWord = &g_coinExtendedData[COIN_EXT_TODAY_WORD];
        u32 updated = (*todayWord & ~COIN_EXT_FLAT_CHECKPOINT_MASK) | checkpoint;
        if (updated != *todayWord)
        {
            *todayWord = updated;
            g_coinExtendedDirty = true;
        }
    }

    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
    {
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        g_coinSavePending = true;
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
            savedLifetimeSpent,
            accounting
        );
        Result closeRc = COIN_HOST__FSFILE_Close(file);
        if (R_SUCCEEDED(rc) && R_FAILED(closeRc))
            rc = closeRc;
    }
    Result archiveCloseRc = COIN_HOST__FSUSER_CloseArchive(sd);
    if (R_SUCCEEDED(rc) && R_FAILED(archiveCloseRc))
        rc = archiveCloseRc;

    if (R_SUCCEEDED(rc) && !PLUGIN_coin_HomeAccountingReady())
    {
        rc = PLUGIN_coin_InstallNativeSentinel(accounting);
        if (R_SUCCEEDED(rc))
        {
            g_coinDat = (u16)(savedWallet > 300u ? 300u : savedWallet);
            PLUGIN_coin_PublishAccountingReady();
        }
    }

    if (R_SUCCEEDED(rc))
    {
        PLUGIN_coin_PublishSavedExtension(accountingGeneration,
            g_coinExtendedData[COIN_EXT_TODAY_WORD],
            g_coinExtendedData[COIN_EXT_LAST_DAY_WORD]);
        if (hasCatchup)
            PLUGIN_coin_AcknowledgeHomeCatchup(catchup[4]);
    }

    // keep HOME paused through the private save and native lock
    bool unlocked = PLUGIN_coin_UnlockHomeState(homeProcess);

    if (R_FAILED(rc) || !unlocked)
    {
        g_coinSavePending = true;
        return;
    }

    bool earnedEvent = g_coinEarnedEventPending;
    bool completedWalkEvent = g_coinCompletedWalkPending;
    u32 completedDay = g_coinCompletedDay;
    u32 completedWeek = g_coinCompletedWeek;
    g_coinSavePending = false;
    g_coinEarnedEventPending = false;
    g_coinCompletedWalkPending = false;
    g_lastCoins = savedWallet;
    g_lastEverSpent = savedLifetimeSpent;
    g_coinAccountingSavedGeneration = accountingGeneration;
    g_coinExtendedDirty = false;
    g_coinMilestoneEvent = false;

    // run achievement checks after the new coin state is saved
    if (earnedEvent)
        PLUGIN_coin_CheckWalkAchievements();
    if (completedWalkEvent)
        PLUGIN_coin_CheckWalkPeriodAchievements(completedDay, completedWeek);
    // check shared balance milestones once after any qualifying event
    if (!earnedEvent && (spentEvent || milestoneEvent))
        PLUGIN_coin_CheckBalanceAchievements();
    if (spentEvent)
        PLUGIN_coin_CheckSpendAchievements();

    // coins.bin: old 0x20 base, 0x20 extension, optional 8-byte secret mask
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_SetupCoins(void)
{
    // Loader already seeded vanilla state if coins.bin was bad
    bool initializeFromVanilla = false;
    if (g_coinStartupAttached)
        goto finalize_setup;

    FS_Archive sd;
    Handle file = 0;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &sd,
        ARCHIVE_SDMC,
        COIN_HOST__fsMakePath(PATH_EMPTY, NULL)
    );
    if (R_FAILED(rc))
        return false;

    bool retry = false;
    bool extensionVerified = false;

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
        g_coinMilestoneEvent = false;
        PLUGIN_coin_ResetExtendedData();

        u64 fileSize = 0;
        rc = COIN_HOST__FSFILE_GetSize(file, &fileSize);
        if (R_FAILED(rc))
        {
            retry = true;
            goto close_setup_file;
        }
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
        if (R_FAILED(rc))
        {
            retry = true;
            goto close_setup_file;
        }

        u32 base[8];
        bool readFailed = false;
        bool baseValid = fileSize >= COIN_FILE_BASE_SIZE &&
            PLUGIN_coin_ReadAndValidateBase(file, base, &readFailed);
        if (readFailed)
        {
            retry = true;
            goto close_setup_file;
        }

        if (!baseValid)
        {
            // Loader repaired lifetime tracking, so keep every other valid block
            initializeFromVanilla = true;
            u32 rawBaseRead = 0;
            bool canReadIndependentBlocks = fileSize >= COIN_FILE_BASE_SIZE;
            if (canReadIndependentBlocks)
            {
                rc = COIN_HOST__FSFILE_Read(file, &rawBaseRead,
                    0, base, sizeof(base));
                if (R_FAILED(rc) || rawBaseRead != sizeof(base))
                {
                    retry = true;
                    goto close_setup_file;
                }
            }
            // the complete extension can be checked on its own even if a later block is damaged
            bool hasExtension = fileSize >= COIN_FILE_OLD_NO_ACHIEVEMENT_SIZE;
            if (canReadIndependentBlocks)
            {
                g_coinKey = base[4];
                if (hasExtension)
                {
                    extensionVerified = PLUGIN_coin_ReadExtendedData(
                        file, g_coinKey, &readFailed);
                    if (readFailed)
                    {
                        retry = true;
                        goto close_setup_file;
                    }
                }
                if (!extensionVerified)
                    PLUGIN_coin_ResetExtendedData();

                if ((fileSize >= COIN_FILE_OLD_WITH_ACHIEVEMENT_SIZE &&
                        fileSize < COIN_FILE_NO_ACHIEVEMENT_SIZE) ||
                    fileSize >= COIN_FILE_WITH_ACHIEVEMENT_SIZE)
                {
                    u32 savedMask = 0;
                    bool maskValid = PLUGIN_coin_ReadSavedAchievementMask(
                            file, g_coinKey,
                            fileSize >= COIN_FILE_WITH_ACHIEVEMENT_SIZE ?
                                COIN_FILE_ACHIEVEMENT_OFFSET : COIN_FILE_ACCOUNTING_OFFSET,
                            &savedMask, &readFailed);
                    if (readFailed)
                    {
                        retry = true;
                        goto close_setup_file;
                    }
                    if (maskValid)
                    {
                        g_coinAchievementSavedMask = savedMask;
                        g_coinAchievementSavePresent = true;
                        g_coinAchievementSaveValid = true;
                    }
                }
            }
            else
                PLUGIN_coin_ResetExtendedData();
            g_coinExtendedDirty = true;
        }
        else
        {
            g_lastCoins = base[0];
            g_coinOffset = base[1];
            g_coinKey = base[4];

            // the complete extension can be checked on its own even if a later block is damaged
            bool hasExtension = fileSize >= COIN_FILE_OLD_NO_ACHIEVEMENT_SIZE;
            bool suffixValid = fileSize == COIN_FILE_BASE_SIZE;
            if (hasExtension)
            {
                extensionVerified = PLUGIN_coin_ReadExtendedData(
                    file, g_coinKey, &readFailed);
                if (readFailed)
                {
                    retry = true;
                    goto close_setup_file;
                }
                suffixValid = extensionVerified;
            }
            if ((fileSize >= COIN_FILE_OLD_WITH_ACHIEVEMENT_SIZE &&
                    fileSize < COIN_FILE_NO_ACHIEVEMENT_SIZE) ||
                fileSize >= COIN_FILE_WITH_ACHIEVEMENT_SIZE)
            {
                u32 savedMask = 0;
                bool maskValid = PLUGIN_coin_ReadSavedAchievementMask(
                        file,
                        g_coinKey,
                        fileSize >= COIN_FILE_WITH_ACHIEVEMENT_SIZE ?
                            COIN_FILE_ACHIEVEMENT_OFFSET : COIN_FILE_ACCOUNTING_OFFSET,
                        &savedMask, &readFailed);
                if (readFailed)
                {
                    retry = true;
                    goto close_setup_file;
                }
                if (maskValid)
                {
                    g_coinAchievementSavedMask = savedMask;
                    g_coinAchievementSavePresent = true;
                    g_coinAchievementSaveValid = true;
                }
            }

            if (!suffixValid)
            {
                // reset only the broken walking extension
                PLUGIN_coin_ResetExtendedData();
                g_coinExtendedDirty = true;
            }
        }

        PLUGIN_coin_SyncAchievementSettingToExtendedData();
close_setup_file:
        if (R_FAILED(COIN_HOST__FSFILE_Close(file)))
            retry = true;

        if (!retry)
        {
            if (g_coinOffset <= 0x100000u)
            {
                if (PLUGIN_coin_HomeMenuLoaded())
                    g_coinsFailed = true;
                else
                    retry = true;
            }
            else
            {
                bool incompatible = false;
                if (PLUGIN_coin_AttachHomeMenu(&incompatible))
                    g_coinStartupAttached = true;
                else if (incompatible)
                    g_coinsFailed = true;
                else
                    retry = true;
            }
        }
    }
    else
        retry = true;

    if (R_FAILED(COIN_HOST__FSUSER_CloseArchive(sd)))
        retry = true;

    if (retry || g_coinsFailed)
        return false;

finalize_setup:
    Handle homeProcess = 0;
    if (!PLUGIN_coin_LockHomeState(&homeProcess))
        return false;

    if (initializeFromVanilla)
    {
        coinsEverSpent = 0;
        u32 recovered[5];
        u32 recoveredGeneration = 0;
        if (PLUGIN_coin_CopyHomeAccounting(recovered, &recoveredGeneration))
        {
            // keep a valid walking bucket
            if (!extensionVerified)
            {
                PLUGIN_coin_SetTodayWalked((u16)recovered[1]);
                g_coinExtendedDirty = true;
            }
        }
    }

    PLUGIN_coin_ClampTrackedAccounting();
    if (PLUGIN_coin_SetHomeFlatMode(!g_coinProgressiveCostEnabled))
    {
        u32 homeAccounting[5];
        (void)PLUGIN_coin_CopyHomeAccounting(homeAccounting,
            &g_coinFlatModeChangeGeneration);
    }
    g_lastEverSpent = coinsEverSpent;
    if (!PLUGIN_coin_UnlockHomeState(homeProcess))
        return false;
    PLUGIN_coin_HandleCoins();
    return true;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_UpdatePlayCoins(void)
{
    FS_Archive archive;
    Handle file;
    Result res = PLUGIN_coin_OpenGameCoinFile(&archive, &file, FS_OPEN_READ);
    if (R_FAILED(res))
        return res;

    Handle homeProcess = 0;
    if (!PLUGIN_coin_LockHomeState(&homeProcess))
    {
        PLUGIN_coin_CloseFileArchive(file, archive);
        return (Result)-1;
    }

    u32 read = 0;
    u16 systemCoins = 0;
    res = COIN_HOST__FSFILE_Read(file, &read, 4, &systemCoins, sizeof(systemCoins));
    if (R_SUCCEEDED(res) && read == sizeof(systemCoins))
    {
        // normal coinsSpent tops at 300, still clamp cheated weirdness
        coinsSpent = 0;
        if (g_coinDat > systemCoins)
        {
            u32 spent = g_coinDat - systemCoins;
            coinsSpent = spent > 300 ? 300 : (u16)spent;
        }
        COIN_HOST__svcFlushEntireDataCache();
    }
    else if (R_SUCCEEDED(res))
    {
        res = (Result)-1;
    }

    bool unlocked = PLUGIN_coin_UnlockHomeState(homeProcess);
    PLUGIN_coin_CloseFileArchive(file, archive);
    if (!unlocked)
        return (Result)-1;
    return res;
}

PLUGIN_CODE(coin) static Result PLUGIN_coin_SetPlayCoins(u16 amount)
{
    FS_Archive archive; // extdata archive
    Handle file; // gamecoin file handle
    Result res; // result variable

    // open Nintendo's /gamecoin.dat
    res = PLUGIN_coin_OpenGameCoinFile(&archive, &file, FS_OPEN_WRITE);
    if (R_FAILED(res)) // return if error
        return res;

    Handle homeProcess = 0;
    if (!PLUGIN_coin_LockHomeState(&homeProcess))
    {
        PLUGIN_coin_CloseFileArchive(file, archive);
        return (Result)-1;
    }

    // vanilla side still caps at 300, naturally
    u16 newAmount = amount > 300 ? 300 : amount;
    u16 savedAmount = newAmount > coinsSpent ? newAmount - coinsSpent : 0;
    // HOME Menu consumes this vanilla-side balance
    u32 written = 0;
    res = COIN_HOST__FSFILE_Write(
        file,
        &written,
        4,
        &savedAmount,
        sizeof(savedAmount),
        FS_WRITE_FLUSH
    );
    if (R_SUCCEEDED(res) && written != sizeof(savedAmount))
        res = (Result)-1;
    if (R_FAILED(res))
    {
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        PLUGIN_coin_CloseFileArchive(file, archive);
        return res;
    }

    // save the file and shared counters together at the HOME boundary
    coinsBin = (u32)amount + coinsSpent;
    g_coinDat = newAmount; // save (coins) to coinsDat so that next coinsSpent recalc is correct
    COIN_HOST__svcFlushEntireDataCache();

    bool unlocked = PLUGIN_coin_UnlockHomeState(homeProcess);
    Result fileCloseResult = COIN_HOST__FSFILE_Close(file);
    Result archiveCloseResult = COIN_HOST__FSUSER_CloseArchive(archive);
    if (!unlocked)
        return (Result)-1;
    if (R_FAILED(fileCloseResult))
        return fileCloseResult;
    return archiveCloseResult;
}

// native checkpoints are separate, so get PTM total and history before rebasing
PLUGIN_CODE(coin) static bool PLUGIN_coin_ReadNativeStepSnapshot(
    u32 *totalOut, u32 *historyOut)
{
    if (!totalOut || !historyOut)
        return false;
    Handle ptm = 0;
    if (R_FAILED(COIN_HOST__srvGetServiceHandle(&ptm, g_coinPtmU)))
        return false;
    u32 *cmd = PLUGIN_coin_CommandBuffer();
    cmd[0] = 0x000C0000u;
    Result rc = COIN_HOST__svcSendSyncRequest(ptm);
    if (R_SUCCEEDED(rc))
        rc = (Result)cmd[1];
    u32 total = R_SUCCEEDED(rc) ? cmd[2] : 0;

    volatile u16 buckets[24];
    for (u32 i = 0; i < 24u; i++)
        buckets[i] = 0;
    u64 now = COIN_HOST__osGetTime();
    if (R_SUCCEEDED(rc) && now >= COIN_NEWS_EPOCH_OFFSET_MS)
    {
        u64 query = now - COIN_NEWS_EPOCH_OFFSET_MS;
        cmd[0] = 0x000B00C2u;
        cmd[1] = 24u;
        cmd[2] = (u32)query;
        cmd[3] = (u32)(query >> 32);
        cmd[4] = IPC_Desc_Buffer(sizeof(buckets), IPC_BUFFER_W);
        cmd[5] = (u32)buckets;
        rc = COIN_HOST__svcSendSyncRequest(ptm);
        if (R_SUCCEEDED(rc))
            rc = (Result)cmd[1];
    }
    else
        rc = (Result)-1;

    COIN_HOST__svcCloseHandle(ptm);
    if (R_FAILED(rc))
        return false;
    u32 history = 0;
    for (u32 i = 0; i < 24u; i++)
        history += buckets[i];
    *totalOut = total;
    *historyOut = history;
    return true;
}

// only the worker updates this stamp, never HOME's native load
PLUGIN_CODE(coin) static void PLUGIN_coin_MaintainNativeSentinel(u32 day, u32 stamp)
{
    if (!g_patchedHome || !PLUGIN_coin_HomeAccountingReady())
        return;

    if (!day || !stamp || day == g_coinNativeSentinelDay)
        return;

    Handle homeProcess = 0;
    if (!PLUGIN_coin_LockHomeState(&homeProcess))
        return;
    FS_Archive archive;
    Handle file;
    Result rc = PLUGIN_coin_OpenGameCoinFile(&archive, &file,
        FS_OPEN_READ | FS_OPEN_WRITE);
    if (R_FAILED(rc))
    {
        (void)PLUGIN_coin_UnlockHomeState(homeProcess);
        return;
    }
    u32 record[5];
    u32 read = 0;
    rc = COIN_HOST__FSFILE_Read(file, &read, 0, record, sizeof(record));
    if (R_SUCCEEDED(rc) && (read != sizeof(record) || record[0] != 0x4F00u ||
        (record[1] & 0xFFFFu) > 300u || (record[1] >> 16) > 11u ||
        !PLUGIN_coin_ValidAccountingDate(record[4])))
        rc = (Result)-28;

    u32 nativeCoinWord = R_SUCCEEDED(rc) ? record[1] : 0u;
    if (R_SUCCEEDED(rc))
    {
        u32 earned = record[1] >> 16;
        u32 wallet = record[1] & 0xFFFFu;
        u32 baseline = g_coinDat;
        if (baseline > 300u)
            rc = (Result)-28;
        else
        {
            if (earned <= 10u)
                wallet = wallet >= earned ? wallet - earned : 0u;
            if (wallet > baseline)
                wallet = baseline;
            record[1] = wallet | (11u << 16);
        }
    }

    bool rebase = R_SUCCEEDED(rc) && record[4] != stamp;
    if (rebase)
    {
        if (!PLUGIN_coin_ReadNativeStepSnapshot(&record[2], &record[3]))
            rc = (Result)-1;
        else
            record[4] = stamp;
    }
    u32 endDay = 0;
    u32 endStamp = 0;
    if (R_SUCCEEDED(rc) &&
        (!PLUGIN_coin_ReadCalendarNow(&endDay, &endStamp) || endDay != day ||
            endStamp != stamp))
        rc = (Result)-1;

    if (R_SUCCEEDED(rc))
    {
        u32 written = 0;
        if (record[1] != nativeCoinWord)
        {
            rc = COIN_HOST__FSFILE_Write(file, &written, 4u, &record[1],
                sizeof(record[1]), FS_WRITE_FLUSH);
            if (R_SUCCEEDED(rc) && written != sizeof(record[1]))
                rc = (Result)-29;
        }
        if (R_SUCCEEDED(rc) && rebase)
        {
            written = 0;
            rc = COIN_HOST__FSFILE_Write(file, &written, 8u, &record[2],
                3u * sizeof(u32), FS_WRITE_FLUSH);
            if (R_SUCCEEDED(rc) && written != 3u * sizeof(u32))
                rc = (Result)-29;
        }
    }
    Result closeRc = COIN_HOST__FSFILE_Close(file);
    Result archiveRc = COIN_HOST__FSUSER_CloseArchive(archive);
    if (R_SUCCEEDED(rc) && R_FAILED(closeRc))
        rc = closeRc;
    if (R_SUCCEEDED(rc) && R_FAILED(archiveRc))
        rc = archiveRc;
    if (!PLUGIN_coin_UnlockHomeState(homeProcess))
        rc = (Result)-1;
    if (R_SUCCEEDED(rc))
        g_coinNativeSentinelDay = day;
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

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenPlayCoinzMenuSerialized(void)
{
    COIN_HOST__RecursiveLock_Lock(&g_coinStateLock);
    if (!g_coinsFailed && COIN_HOST__Sleep_TryEnterIo())
    {
        PLUGIN_coin_OpenPlayCoinzMenu();
        COIN_HOST__Sleep_LeaveIo();
    }
    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenDebugSerialized(void)
{
    COIN_HOST__RecursiveLock_Lock(&g_coinStateLock);
    if (!g_coinsFailed && COIN_HOST__Sleep_TryEnterIo())
    {
        PLUGIN_coin_OpenDebug();
        COIN_HOST__Sleep_LeaveIo();
    }
    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
}


PLUGIN_CODE(coin) static void PLUGIN_coin_StopForIncompatibleHook(void)
{
    g_coinsFailed = true;
    if (g_coinMenuRegistered && COIN_MENU__RemoveItem(&g_coinMenuRegistration))
        g_coinMenuRegistered = false;
    g_coinBlurFeatureRegistration.callback = NULL;
    (void)COIN_BLUR__RemoveTickFunc(PLUGIN_coin_OnBlurTick);
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OnBlurTick(u64 delta)
{
    (void)delta;
    COIN_HOST__RecursiveLock_Lock(&g_coinStateLock);

    if (COIN_HOST__Sleep_TryEnterIo())
    {
        if (!g_coinsFailed)
        {
            if (!g_patchedHome)
            {
                if (PLUGIN_coin_SetupCoins())
                {
                    g_patchedHome = true;
                    if (!g_coinAssetsReady)
                    {
                        if (R_SUCCEEDED(PLUGIN_coin_EnsurePackedAssets()))
                            g_coinAssetsReady = true;
                        else
                            g_coinAssetRetryTicks = 30u;
                    }
                }
                else if (g_coinsFailed)
                {
                    PLUGIN_coin_StopForIncompatibleHook();
                }
            }
            else
            {
                bool maintenanceDue = ++g_coinMaintenanceTicks >= 30u;
                if (maintenanceDue)
                    g_coinMaintenanceTicks = 0;
                if (!g_coinAssetsReady && g_coinAssetRetryTicks)
                {
                    g_coinAssetRetryTicks--;
                    if (R_SUCCEEDED(PLUGIN_coin_EnsurePackedAssets()))
                        g_coinAssetsReady = true;
                }
                PLUGIN_coin_HandleSettingsRequest();
                u32 sleepCycle = COIN_BLUR__SleepCycleCounter();
                bool woke = sleepCycle != g_coinLastSleepCycle;
                g_coinLastSleepCycle = sleepCycle;
                u32 catchupState = PLUGIN_coin_HomeCatchupState();
                bool workPending = coinsBin != g_lastCoins || PLUGIN_coin_PendingEarned() ||
                    g_coinSavePending || coinsEverSpent != g_lastEverSpent ||
                    g_coinExtendedDirty || PLUGIN_coin_RtcDayDecisionPending() ||
                    PLUGIN_coin_HomeAccountingChanged(g_coinAccountingSavedGeneration) ||
                    catchupState == 4u;
                u32 currentDay = 0;
                u32 currentStamp = 0;
                bool calendarSampled = false;
                if (maintenanceDue && !workPending)
                {
                    calendarSampled = true;
                    (void)PLUGIN_coin_ReadCalendarNow(&currentDay, &currentStamp);
                    workPending = PLUGIN_coin_HomeCounterExceedsToday(currentDay, currentStamp) ||
                        PLUGIN_coin_DayHistoryNeedsUpdate(currentDay);
                }

                if (workPending && !(catchupState >= 1u && catchupState <= 3u))
                {
                    PLUGIN_coin_HandleCoins();
                    calendarSampled = false;
                }
                if (maintenanceDue || woke)
                {
                    if (!calendarSampled)
                        (void)PLUGIN_coin_ReadCalendarNow(&currentDay, &currentStamp);
                    PLUGIN_coin_MaintainNativeSentinel(currentDay, currentStamp);
                }
            }
        }

        COIN_HOST__Sleep_LeaveIo();
    }

    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
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
            PLUGIN_coin_StringEquals(item->title, g_coinSetCoinsItem))
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
    if (!COIN_MENU__GetApiVersion)
        return false;

    u32 menuApiVersion = COIN_MENU__GetApiVersion();
    if (SYSPLUGIN_MENU_API_LOADER_REVISION(menuApiVersion) < SYSPLUGIN_MENU_LOADER_API_REVISION ||
        SYSPLUGIN_MENU_API_PUBLIC_REVISION(menuApiVersion) < SYSPLUGIN_MENU_PUBLIC_API_REVISION ||
        !COIN_BLUR__AddTickFunc || !COIN_BLUR__RemoveTickFunc ||
        !COIN_BLUR__AddFeatureItem ||
        !COIN_BLUR__DrawFeatureFrame || !COIN_MENU__AddItem ||
        !COIN_MENU__RemoveItem || !COIN_MENU__OpenPluginFile || !COIN_MENU__UnpackLz10File ||
        !COIN_MENU__ExtractRawFile || !COIN_MENU__ClosePluginFile || !COIN_MENU__LoadData ||
        !COIN_MENU__SaveData || !COIN_HOST__dateTimeToString)
    {
        return false;
    }

    COIN_HOST__RecursiveLock_Init(&g_coinStateLock);
    g_coinLastSleepCycle = COIN_BLUR__SleepCycleCounter();
    PLUGIN_coin_LoadMenuSettings();

    if (!COIN_BLUR__AddTickFunc(PLUGIN_coin_OnBlurTick, 1000000000LL))
        return false;

    if (!COIN_BLUR__AddFeatureItem(
            &g_coinBlurFeatureRegistration,
            COIN_PLUGIN_ID,
            g_coinDebugTitle,
            PLUGIN_coin_OpenDebugSerialized))
    {
        COIN_BLUR__RemoveTickFunc(PLUGIN_coin_OnBlurTick);
        return false;
    }

    if (COIN_MENU__AddItem(
            &g_coinMenuRegistration,
            COIN_PLUGIN_ID,
            g_coinMenuTitle,
            PLUGIN_coin_OpenPlayCoinzMenuSerialized,
            RGB565(31, 63, 20)))
    {
        g_coinMenuRegistered = true;
        PLUGIN_coin_RemoveBuiltInMenuItem();
    }

    return true;
}
