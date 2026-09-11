// build-only stub makes the marker pass notice this file when compiled alone
#if !defined(PLAYCOIN_MENUS_EDITOR) && !defined(PLAYCOIN_MENUS_MAIN) && !defined(PLAYCOIN_MENUS_DEBUG_AND_ACHIEVEMENTS)
__attribute__((used)) static void playcoin_menus_manifest(void) { return; }
#endif

// PlayCoinz pages and debug UI
#if defined(PLAYCOIN_MENUS_EDITOR)
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

    if (!g_coinProgressiveCostEnabled)
    {
        COIN_HOST__Draw_DrawString(
            20,
            180,
            COLOR_GRAY,
            g_progressiveDisabledEditorText
        );
    }
    else if (!g_coinAchievementsEnabled)
    {
        COIN_HOST__Draw_DrawString(
            20,
            180,
            COLOR_GRAY,
            g_achievementsDisabledEditorText
        );
    }
    else if (playCoins != recommendedDisplay)
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

    if (g_coinAchievementsEnabled && playCoins > trueCoinDisplay)
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
#elif defined(PLAYCOIN_MENUS_MAIN)
PLUGIN_CODE(coin) void PLUGIN_coin_EditPlayCoins(void)
{
    COIN_HOST__RecursiveLock_Lock(&g_coinStateLock);

    // apply the edited balance
    PLUGIN_coin_UpdatePlayCoins();

    u16 playCoins = coinsBin > coinsSpent ? (u16)(coinsBin - coinsSpent) : 0;
    Result res = 0;
    u32 pendingEarned = PLUGIN_coin_PendingEarned();
    u32 trueCoinDisplay = coinsTrue + pendingEarned; // coinstrue (aka coins ever earned) doesnt decrease
    u32 recommendedDisplay = coinsRec + pendingEarned > coinsSpent ?
        coinsRec + pendingEarned - coinsSpent : 0;
    bool previousWarning = g_coinAchievementsEnabled && playCoins > trueCoinDisplay;
    bool previousRecommended = g_coinAchievementsEnabled &&
        g_coinProgressiveCostEnabled &&
        playCoins != recommendedDisplay;
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
            break;
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
            else if ((pressed & KEY_Y) && g_coinAchievementsEnabled &&
                     g_coinProgressiveCostEnabled &&
                     playCoins != recommendedDisplay)
            {
                playCoins = (u16)recommendedDisplay;
                updated = true;
            }

            if (updated)
            {
                bool currentWarning = g_coinAchievementsEnabled &&
                    playCoins > trueCoinDisplay;
                bool currentRecommended = g_coinAchievementsEnabled &&
                    g_coinProgressiveCostEnabled &&
                    playCoins != recommendedDisplay;

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
            // the notification is the success message, only show async errors here
            PLUGIN_coin_DrawEditor(
                playCoins,
                trueCoinDisplay,
                recommendedDisplay,
                R_FAILED(res),
                res
            );
        }
    } while (!COIN_HOST__menuShouldExit);

    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
}
#elif defined(PLAYCOIN_MENUS_DEBUG_AND_ACHIEVEMENTS)
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
    if (!g_coinProgressiveCostEnabled)
        return;

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
        COIN_HOST__Draw_DrawFormattedString(
            20,
            185,
            COLOR_WHITE,
            g_coinProgressiveCostEnabled ? g_coinTodayFmt : g_coinProgressiveTodayFmt,
            d.coinsToday
        );
        if (g_coinProgressiveCostEnabled)
        {
            COIN_HOST__Draw_DrawFormattedString(20, 198, COLOR_YELLOW, g_coinNextCoinFmt, d.coinsToday + 1u, nextCost);
            COIN_HOST__Draw_DrawFormattedString(20, 211, COLOR_YELLOW, g_coinHistoryNeededFmt, remaining);
        }
    }

    if (g_coinProgressiveCostEnabled)
    {
        COIN_HOST__Draw_DrawString(COIN_DEBUG_BUCKET_X, 36u, COLOR_GRAY, g_coinBucketGroup);
        PLUGIN_coin_DrawDebugTimestampNoLock();
        PLUGIN_coin_DrawDebugBucketValuesNoLock(false);
    }

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
    return g_coinAchievementsEnabled &&
        R_SUCCEEDED(PLUGIN_coin_ReadAchievementEarnedMask(&earnedMask)) &&
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
    if (g_coinAchievementsEnabled)
        COIN_HOST__Draw_DrawString(20, 220, COIN_FRAME_TITLE_COLOR, g_coinArtworkCredit);
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

    // keep one row of context above the selected secret when possible
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
    // erase the old cursor before redrawing the row
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

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawAchievementConfirm(
    u32 selected,
    bool disablingAchievements
)
{
    u32 noY = disablingAchievements ? 173u : 55u;
    u32 yesY = disablingAchievements ? 188u : 70u;

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinConfirmTitle);
    if (disablingAchievements)
    {
        COIN_HOST__Draw_DrawString(
            20,
            42,
            COLOR_WHITE,
            g_coinAchievementsDisableExplain
        );
    }
    PLUGIN_coin_DrawConfirmItem(noY, selected == 0u, g_coinConfirmNo);
    PLUGIN_coin_DrawConfirmItem(yesY, selected == 1u, g_coinConfirmYes);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RedrawAchievementConfirmSelection(
    u32 oldSelected,
    u32 selected,
    bool disablingAchievements
)
{
    u32 noY = disablingAchievements ? 173u : 55u;
    u32 yesY = disablingAchievements ? 188u : 70u;
    u32 oldY = oldSelected == 0u ? noY : yesY;
    u32 newY = selected == 0u ? noY : yesY;
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

PLUGIN_CODE(coin) static bool PLUGIN_coin_ConfirmAchievementAction(
    bool disablingAchievements
)
{
    u32 selected = 0;
    PLUGIN_coin_DrawAchievementConfirm(selected, disablingAchievements);

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return false;
        if (pressed & (KEY_DUP | KEY_DDOWN))
        {
            u32 oldSelected = selected;
            selected = selected ? 0u : 1u;
            PLUGIN_coin_RedrawAchievementConfirmSelection(
                oldSelected,
                selected,
                disablingAchievements
            );
        }
        else if (pressed & KEY_A)
        {
            return selected == 1u;
        }
    } while (!COIN_HOST__menuShouldExit);

    return false;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawProgressiveCostConfirm(u32 selected)
{
    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinConfirmTitle);
    COIN_HOST__Draw_DrawString(20, 42, COLOR_WHITE, g_coinProgressiveCostExplain);
    COIN_HOST__Draw_DrawString(20, 117, COLOR_ORANGE, g_coinProgressiveCostWarning);
    PLUGIN_coin_DrawConfirmItem(173u, selected == 0u, g_coinConfirmNo);
    PLUGIN_coin_DrawConfirmItem(188u, selected == 1u, g_coinConfirmYes);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RedrawProgressiveCostConfirmSelection(
    u32 oldSelected,
    u32 selected
)
{
    u32 oldY = oldSelected == 0u ? 173u : 188u;
    u32 newY = selected == 0u ? 173u : 188u;
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

PLUGIN_CODE(coin) static bool PLUGIN_coin_ConfirmProgressiveCostAction(void)
{
    u32 selected = 0;
    PLUGIN_coin_DrawProgressiveCostConfirm(selected);

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return false;
        if (pressed & (KEY_DUP | KEY_DDOWN))
        {
            u32 oldSelected = selected;
            selected = selected ? 0u : 1u;
            PLUGIN_coin_RedrawProgressiveCostConfirmSelection(oldSelected, selected);
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
            if (PLUGIN_coin_ConfirmAchievementAction(false))
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

PLUGIN_CODE(coin) static u32 PLUGIN_coin_OptionsY(u32 selected)
{
    return 45u + selected * 15u;
}

PLUGIN_CODE(coin) static const char *PLUGIN_coin_OptionTitle(u32 selected)
{
    if (selected == 0u)
    {
        return g_coinAchievementsEnabled ? g_coinDisableAchievementsItem :
            g_coinEnableAchievementsItem;
    }

    return g_coinProgressiveCostEnabled ? g_coinDisableProgressiveCostItem :
        g_coinEnableProgressiveCostItem;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_OptionColor(u32 selected)
{
    if (selected == 0u)
        return g_coinAchievementsEnabled ? COLOR_WHITE : COLOR_GREEN;
    return g_coinProgressiveCostEnabled ? COLOR_WHITE : COLOR_GREEN;
}

PLUGIN_CODE(coin) static void PLUGIN_coin_DrawOptions(u32 selected)
{
    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_ClearFramebuffer();
    PLUGIN_coin_DrawFrame(g_coinOptionsPageTitle);
    for (u32 i = 0; i < 2u; i++)
    {
        PLUGIN_coin_DrawSimpleMenuItem(
            PLUGIN_coin_OptionsY(i),
            selected == i,
            PLUGIN_coin_OptionTitle(i),
            PLUGIN_coin_OptionColor(i)
        );
    }
    COIN_HOST__Draw_DrawString(20, 120, COLOR_GRAY, g_coinBackShort);
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RedrawOptionsSelection(
    u32 oldSelected,
    u32 selected
)
{
    u32 oldY = PLUGIN_coin_OptionsY(oldSelected);
    u32 newY = PLUGIN_coin_OptionsY(selected);

    COIN_HOST__Draw_Lock();
    COIN_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_coinMenuClearRow);
    COIN_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_coinMenuClearRow);
    PLUGIN_coin_DrawSimpleMenuItem(
        oldY,
        false,
        PLUGIN_coin_OptionTitle(oldSelected),
        PLUGIN_coin_OptionColor(oldSelected)
    );
    PLUGIN_coin_DrawSimpleMenuItem(
        newY,
        true,
        PLUGIN_coin_OptionTitle(selected),
        PLUGIN_coin_OptionColor(selected)
    );
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_RedrawOptionValues(
    bool achievementsChanged,
    bool progressiveChanged,
    u32 selected
)
{
    COIN_HOST__Draw_Lock();
    if (achievementsChanged)
    {
        u32 y = PLUGIN_coin_OptionsY(0u);
        COIN_HOST__Draw_DrawString(10, y, COLOR_BLACK, g_coinMenuClearRow);
        PLUGIN_coin_DrawSimpleMenuItem(
            y,
            selected == 0u,
            PLUGIN_coin_OptionTitle(0u),
            PLUGIN_coin_OptionColor(0u)
        );
    }
    if (progressiveChanged)
    {
        u32 y = PLUGIN_coin_OptionsY(1u);
        COIN_HOST__Draw_DrawString(10, y, COLOR_BLACK, g_coinMenuClearRow);
        PLUGIN_coin_DrawSimpleMenuItem(
            y,
            selected == 1u,
            PLUGIN_coin_OptionTitle(1u),
            PLUGIN_coin_OptionColor(1u)
        );
    }
    COIN_HOST__Draw_FlushFramebuffer();
    COIN_HOST__Draw_Unlock();
}

PLUGIN_CODE(coin) static void PLUGIN_coin_OpenOptions(void)
{
    u32 selected = 0;
    PLUGIN_coin_DrawOptions(selected);

    do
    {
        u32 pressed = COIN_HOST__waitInputWithTimeout(50);
        if (pressed & KEY_B)
            break;
        if (pressed & (KEY_DUP | KEY_DDOWN))
        {
            u32 oldSelected = selected;
            selected = selected ? 0u : 1u;
            PLUGIN_coin_RedrawOptionsSelection(oldSelected, selected);
        }
        else if (pressed & KEY_A)
        {
            if (selected == 0u)
            {
                if (g_coinAchievementsEnabled)
                {
                    if (PLUGIN_coin_ConfirmAchievementAction(true))
                        g_coinAchievementsEnabled = false;
                    PLUGIN_coin_DrawOptions(selected);
                }
                else
                {
                    bool progressiveChanged = !g_coinProgressiveCostEnabled;
                    if (PLUGIN_coin_SetProgressiveCostEnabled(true))
                    {
                        g_coinAchievementsEnabled = true;
                        PLUGIN_coin_RedrawOptionValues(true, progressiveChanged, selected);
                    }
                    else
                    {
                        PLUGIN_coin_DrawOptions(selected);
                    }
                }
            }
            else
            {
                if (g_coinProgressiveCostEnabled)
                {
                    if (PLUGIN_coin_ConfirmProgressiveCostAction())
                    {
                        if (PLUGIN_coin_SetProgressiveCostEnabled(false))
                            g_coinAchievementsEnabled = false;
                    }
                    PLUGIN_coin_DrawOptions(selected);
                }
                else
                {
                    if (PLUGIN_coin_SetProgressiveCostEnabled(true))
                        PLUGIN_coin_RedrawOptionValues(false, true, selected);
                    else
                        PLUGIN_coin_DrawOptions(selected);
                }
            }
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
#endif
