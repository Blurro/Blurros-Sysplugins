// build-only stub makes the marker pass notice this file when compiled alone
#if !defined(PLAYCOIN_HELPERS_EARLY) && !defined(PLAYCOIN_HELPERS_STATE) && !defined(PLAYCOIN_HELPERS_TEXT) && !defined(PLAYCOIN_HELPERS_STEPS) && !defined(PLAYCOIN_HELPERS_MENU_ITEMS)
__attribute__((used)) static void playcoin_helpers_manifest(void) { return; }
#endif

// source chunk: playcoin.c includes this so the old static state stays local
#if defined(PLAYCOIN_HELPERS_EARLY)
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

PLUGIN_CODE(coin) static void PLUGIN_coin_SyncAchievementSettingToExtendedData(void)
{
    u32 oldValue = g_coinExtendedData[COIN_EXT_TODAY_WORD];
    u32 newValue = g_coinAchievementsEnabled ?
        oldValue & ~COIN_EXT_ACHIEVEMENTS_DISABLED :
        oldValue | COIN_EXT_ACHIEVEMENTS_DISABLED;

    if (newValue != oldValue)
    {
        g_coinExtendedData[COIN_EXT_TODAY_WORD] = newValue;
        g_coinExtendedDirty = true;
    }
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

#define COIN_DAY_MS 86400000ULL
#define COIN_Y2K_DAY_ORDINAL 36525u

PLUGIN_CODE(coin) static bool PLUGIN_coin_ReadDayStepTotal(
    u32 dayOrdinal,
    u32 *outSteps
)
{
    if (!outSteps || dayOrdinal < COIN_Y2K_DAY_ORDINAL)
        return false;

    volatile u16 buckets[24];
    for (u32 i = 0; i < 24u; i++)
        buckets[i] = 0;

    Handle handle = 0;
    Result rc = COIN_HOST__srvGetServiceHandle(&handle, g_coinPtmU);
    if (R_FAILED(rc))
        return false;

    u64 queryMs = (u64)(dayOrdinal - COIN_Y2K_DAY_ORDINAL) * COIN_DAY_MS;
    u8 *tls;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    u32 *cmdbuf = (u32*)(tls + 0x80);
    cmdbuf[0] = 0x000B00C2u;
    cmdbuf[1] = 24u;
    cmdbuf[2] = (u32)queryMs;
    cmdbuf[3] = (u32)(queryMs >> 32);
    cmdbuf[4] = (24u << 4) | 0xCu;
    cmdbuf[5] = (u32)buckets;

    rc = COIN_HOST__svcSendSyncRequest(handle);
    if (R_SUCCEEDED(rc))
        rc = (Result)cmdbuf[1];
    COIN_HOST__svcCloseHandle(handle);
    if (R_FAILED(rc))
        return false;

    u32 total = 0;
    for (u32 i = 0; i < 24u; i++)
        total += buckets[i];
    *outSteps = total;
    return true;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_ProgressiveCoinsForCompletedDay(u32 steps)
{
    u32 coins = 0;
    u32 cost = 100u;
    while (steps >= cost && coins < 0xFFFFu)
    {
        steps -= cost;
        coins++;
        if (coins >= 10u && cost <= 0xFFFFFFFCu)
            cost += 3u;
    }
    return coins;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_CoinsForCompletedDay(u32 steps)
{
    return g_coinProgressiveCostEnabled ?
        PLUGIN_coin_ProgressiveCoinsForCompletedDay(steps) : steps / 100u;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_ProgressiveStepsSpent(u32 coins)
{
    u32 spent = 0;
    u32 cost = 100u;
    for (u32 coin = 0; coin < coins; coin++)
    {
        if (spent > 0xFFFFFFFFu - cost)
            return 0xFFFFFFFFu;

        spent += cost;
        if (coin >= 9u && cost <= 0xFFFFFFFCu)
            cost += 3u;
    }
    return spent;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_PreparePreviousDayCatchup(
    u32 *completedBucketOut,
    u32 *walletMissingOut,
    u32 *trackedMissingOut
)
{
    if (!completedBucketOut || !walletMissingOut || !trackedMissingOut)
        return COIN_PREVDAY_NONE;

    *completedBucketOut = 0;
    *walletMissingOut = 0;
    *trackedMissingOut = 0;
    u32 currentStamp = PLUGIN_coin_CurrentCalendarStamp();
    if (!PLUGIN_coin_RtcExactNextDayPending() || !currentStamp ||
        PLUGIN_coin_RtcDecisionCalendarStamp() != currentStamp)
    {
        return COIN_PREVDAY_NONE;
    }

    u32 currentDay = PLUGIN_coin_CurrentCalendarDay();
    u32 dayState = g_coinExtendedData[COIN_EXT_LAST_DAY_WORD];
    u32 savedDay = dayState & COIN_EXT_DAY_VALUE_MASK;
    if (!currentDay || !savedDay ||
        (dayState & COIN_EXT_DAY_REBASE_PRESERVE) ||
        savedDay == COIN_EXT_DAY_VALUE_MASK || currentDay != savedDay + 1u)
    {
        return COIN_PREVDAY_NONE;
    }

    u32 previousDaySteps = 0;
    if (!PLUGIN_coin_ReadDayStepTotal(savedDay, &previousDaySteps))
        return COIN_PREVDAY_RETRY;

    u32 walletTarget = PLUGIN_coin_CoinsForCompletedDay(previousDaySteps);
    u32 progressiveTarget =
        PLUGIN_coin_ProgressiveCoinsForCompletedDay(previousDaySteps);
    u32 progressiveClaimed = PLUGIN_coin_GetTodayWalked();
    u32 walletClaimed = progressiveClaimed;
    if (!g_coinProgressiveCostEnabled)
    {
        u32 nextCost = progressiveClaimed < 10u ?
            100u : 100u + 3u * (progressiveClaimed - 9u);
        u32 remainder = PLUGIN_coin_RtcPreviousProgressiveRemainder();
        u32 spent = PLUGIN_coin_ProgressiveStepsSpent(progressiveClaimed);

        // dont award flat coins if the current state looks wrong
        if (spent == 0xFFFFFFFFu || remainder >= nextCost ||
            spent > 0xFFFFFFFFu - remainder)
        {
            walletClaimed = walletTarget;
            progressiveClaimed = progressiveTarget;
        }
        else
        {
            walletClaimed = (spent + remainder) / 100u;
        }
    }

    // the rolling buckets always keep the +3 progression shadow
    *completedBucketOut = progressiveTarget;
    *walletMissingOut = walletTarget > walletClaimed ?
        walletTarget - walletClaimed : 0;
    *trackedMissingOut = progressiveTarget > progressiveClaimed ?
        progressiveTarget - progressiveClaimed : 0;
    return COIN_PREVDAY_READY;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_WriteVanillaCoinBalance(u16 amount)
{
    FS_Path pathData;
    pathData.type = PATH_BINARY;
    pathData.size = sizeof(g_gameCoinArchivePath);
    pathData.data = g_gameCoinArchivePath;

    FS_Archive archive;
    Result rc = COIN_HOST__FSUSER_OpenArchive(
        &archive,
        ARCHIVE_SHARED_EXTDATA,
        pathData
    );
    if (R_FAILED(rc))
        return false;

    Handle file = 0;
    rc = COIN_HOST__FSUSER_OpenFile(
        &file,
        archive,
        COIN_HOST__fsMakePath(PATH_ASCII, g_gameCoinPath),
        FS_OPEN_WRITE,
        0
    );
    if (R_SUCCEEDED(rc))
    {
        rc = COIN_HOST__FSFILE_Write(
            file,
            NULL,
            4,
            &amount,
            sizeof(amount),
            FS_WRITE_FLUSH
        );
        Result closeRc = COIN_HOST__FSFILE_Close(file);
        if (R_SUCCEEDED(rc) && R_FAILED(closeRc))
            rc = closeRc;
    }

    Result archiveCloseRc = COIN_HOST__FSUSER_CloseArchive(archive);
    if (R_SUCCEEDED(rc) && R_FAILED(archiveCloseRc))
        rc = archiveCloseRc;
    return R_SUCCEEDED(rc);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_ApplyPreviousDayCatchup(
    u32 walletMissing,
    u32 trackedMissing
)
{
    u32 room = coinsBin < 30000u ? 30000u - coinsBin : 0u;
    u32 walletCredited = walletMissing < room ? walletMissing : room;
    if (!walletCredited && !trackedMissing)
        return true;

    u32 vanilla = (u32)g_coinDat + walletCredited;
    u16 newVanilla = (u16)(vanilla > 300u ? 300u : vanilla);
    if (newVanilla != g_coinDat && !PLUGIN_coin_WriteVanillaCoinBalance(newVanilla))
        return false;

    coinsBin += walletCredited;
    g_coinDat = newVanilla;
    coinsTrue = PLUGIN_coin_SaturatingAdd(coinsTrue, trackedMissing);
    coinsRec = PLUGIN_coin_SaturatingAdd(coinsRec, trackedMissing);
    PLUGIN_coin_ClampTrackedAccounting();

    g_coinExtendedDirty = true;
    if (trackedMissing)
        g_coinEarnedEventPending = true;
    return true;
}

// casino hands us finalized whole-coin values
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
    COIN_HOST__RecursiveLock_Lock(&g_coinStateLock);

    if (g_coinsFailed || !g_patchedHome || destroyedFeeCoins > depositedCoins)
        goto rejected;

    Handle homeProcess = 0;
    if (!PLUGIN_coin_LockHomeState(&homeProcess))
        goto rejected;

    // only the walking-backed overlap counts toward tracked spending/progress
    u32 trackedCoins = depositedCoins < coinsRec ? depositedCoins : coinsRec;

    // apply the 5% fee only to the legit slice, whole coins only
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
    COIN_HOST__svcFlushEntireDataCache();
    bool accepted = PLUGIN_coin_UnlockHomeState(homeProcess);
    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
    return accepted;

rejected:
    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
    return false;
}

// returns how many whole coins actually qualified for secrets
PLUGIN_CODE(coin) u32 PLUGIN_coin_RecordBlackjackQualified(u32 qualifiedCoins)
{
    COIN_HOST__RecursiveLock_Lock(&g_coinStateLock);

    if (g_coinsFailed || !g_patchedHome || !qualifiedCoins)
        goto rejected;

    u32 deposited = PLUGIN_coin_GetBlackjackDepositedCounter();
    u32 qualified = PLUGIN_coin_GetBlackjackQualifiedCounter();
    u32 remaining = deposited - qualified;
    u32 credited = qualifiedCoins < remaining ? qualifiedCoins : remaining;
    if (!credited)
        goto rejected;

    PLUGIN_coin_SetBlackjackCounters(deposited, qualified + credited);
    g_coinExtendedDirty = true;
    g_coinGambleEvent = true;
    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
    return credited;

rejected:
    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
    return 0;
}

PLUGIN_CODE(coin) bool PLUGIN_coin_RecordBlackjackWithdrawal(
    u32 receivedCoins,
    u32 netSurplusCoins
)
{
    COIN_HOST__RecursiveLock_Lock(&g_coinStateLock);

    if (g_coinsFailed || !g_patchedHome || netSurplusCoins > receivedCoins)
        goto rejected;

    Handle homeProcess = 0;
    if (!PLUGIN_coin_LockHomeState(&homeProcess))
        goto rejected;

    bool accepted = false;
    u32 newSurplus = PLUGIN_coin_SaturatingAdd(
        PLUGIN_coin_GetBlackjackSurplusCounter(),
        netSurplusCoins
    );
    u32 capacity = PLUGIN_coin_SaturatingAdd(coinsTrue, newSurplus);
    if (coinsEverSpent > capacity)
        goto done;

    u32 available = capacity - coinsEverSpent;
    if (coinsRec > available || receivedCoins > available - coinsRec ||
        coinsRec > 30000u || receivedCoins > 30000u - coinsRec)
    {
        goto done;
    }

    g_coinExtendedData[COIN_EXT_BLACKJACK_SURPLUS_WORD] = newSurplus;
    coinsRec += receivedCoins;
    g_coinExtendedDirty = true;
    g_coinBalanceEvent = true;
    COIN_HOST__svcFlushEntireDataCache();
    accepted = true;

done:
    if (!PLUGIN_coin_UnlockHomeState(homeProcess))
        accepted = false;
    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
    return accepted;

rejected:
    COIN_HOST__RecursiveLock_Unlock(&g_coinStateLock);
    return false;
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

    // one-based days leave zero free for uninitialized state
    u32 ordinal = 1u;
    for (u32 y = 1900u; y < year; y++)
        ordinal += PLUGIN_coin_IsLeapYear(y) ? 366u : 365u;

    ordinal += g_coinMonthStart[month - 1u] + day - 1u;
    if (month > 2u && PLUGIN_coin_IsLeapYear(year))
        ordinal++;
    return ordinal;
}

PLUGIN_CODE(coin) static u32 PLUGIN_coin_CurrentCalendarStamp(void)
{
    char date[20];
    if (COIN_HOST__dateTimeToString(date, COIN_HOST__osGetTime(), false) < 10)
        return 0;

    if (date[4] != '-' || date[7] != '-')
        return 0;
    for (u32 i = 0; i < 10u; i++)
    {
        if (i == 4u || i == 7u)
            continue;
        if (date[i] < '0' || date[i] > '9')
            return 0;
    }

    u32 year = (u32)(date[0] - '0') * 1000u +
        (u32)(date[1] - '0') * 100u +
        (u32)(date[2] - '0') * 10u +
        (u32)(date[3] - '0');
    u32 month = (u32)(date[5] - '0') * 10u + (u32)(date[6] - '0');
    u32 day = (u32)(date[8] - '0') * 10u + (u32)(date[9] - '0');
    if (year < 1900u || month < 1u || month > 12u || day < 1u || day > 31u)
        return 0;

    return year | (month << 16) | (day << 24);
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_UpdateDayHistory(void)
{
    u32 currentDay = PLUGIN_coin_CurrentCalendarDay();
    if (!currentDay)
        return false;

    u32 dayState = g_coinExtendedData[COIN_EXT_LAST_DAY_WORD];
    u32 lastDay = dayState & COIN_EXT_DAY_VALUE_MASK;
    if (!lastDay)
    {
        g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] = currentDay;
        PLUGIN_coin_SetTodayWalked(0);
        g_coinExtendedDirty = true;
        return true;
    }

    if (currentDay == lastDay)
        return false;

    // only HandleCoins may authorize an ordinary one-day transition
    if (!(dayState & COIN_EXT_DAY_REBASE_PRESERVE) &&
        lastDay != COIN_EXT_DAY_VALUE_MASK && currentDay == lastDay + 1u &&
        !g_coinDayTransitionAuthorized)
    {
        return false;
    }

    // clock went backwards, just move the bucket anchor
    if (currentDay < lastDay)
    {
        g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] =
            currentDay | COIN_EXT_DAY_REBASE_PRESERVE;
        g_coinExtendedDirty = true;
        return true;
    }

    u32 delta = currentDay - lastDay;

    // huge forward jump is probably a clock correction
    if (delta > 300u)
    {
        g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] =
            currentDay | COIN_EXT_DAY_REBASE_PRESERVE;
        g_coinExtendedDirty = true;
        return true;
    }

    // normal forward time ages the rolling week
    if (delta >= 7u)
    {
        for (u32 i = 0; i < COIN_DAY_HISTORY_COUNT; i++)
            PLUGIN_coin_SetHistoryDay(i, 0);
        PLUGIN_coin_SetTodayWalked(0);
    }
    else
    {
        u16 oldToday = PLUGIN_coin_GetTodayWalked();
        for (s32 i = (s32)COIN_DAY_HISTORY_COUNT - 1; i >= 0; i--)
        {
            if ((u32)i >= delta)
                PLUGIN_coin_SetHistoryDay((u32)i, PLUGIN_coin_GetHistoryDay((u32)i - delta));
            else if ((u32)i + 1u == delta)
                PLUGIN_coin_SetHistoryDay((u32)i, oldToday);
            else
                PLUGIN_coin_SetHistoryDay((u32)i, 0);
        }
        PLUGIN_coin_SetTodayWalked(0);
    }

    // normal rotation lets HOME clamp Today again
    g_coinExtendedData[COIN_EXT_LAST_DAY_WORD] = currentDay;
    g_coinExtendedDirty = true;
    return true;
}

PLUGIN_CODE(coin) static bool PLUGIN_coin_DayHistoryNeedsUpdate(void)
{
    u32 currentDay = PLUGIN_coin_CurrentCalendarDay();
    u32 dayState = g_coinExtendedData[COIN_EXT_LAST_DAY_WORD];
    u32 savedDay = dayState & COIN_EXT_DAY_VALUE_MASK;
    if (!currentDay || (savedDay && currentDay == savedDay))
        return false;

    if (savedDay && !(dayState & COIN_EXT_DAY_REBASE_PRESERVE) &&
        savedDay != COIN_EXT_DAY_VALUE_MASK && currentDay == savedDay + 1u)
    {
        return PLUGIN_coin_RtcDayDecisionPending();
    }

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
#elif defined(PLAYCOIN_HELPERS_STATE)
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
        (data[COIN_EXT_TODAY_WORD] &
            ~(0x0000FFFFu | COIN_EXT_ACHIEVEMENTS_DISABLED)) == 0;
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
#elif defined(PLAYCOIN_HELPERS_TEXT)
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
#elif defined(PLAYCOIN_HELPERS_STEPS)
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
#elif defined(PLAYCOIN_HELPERS_MENU_ITEMS)
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
#endif
