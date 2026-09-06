// build-only stub makes the marker pass notice this file when compiled alone
#if !defined(PLAYCOIN_SECRETS_CHECKS) && !defined(PLAYCOIN_SECRETS_DATA) && !defined(PLAYCOIN_SECRETS_NEWS)
__attribute__((used)) static void playcoin_secrets_manifest(void) { return; }
#endif

// achievements, notifications and other secret-ish stuff
#if defined(PLAYCOIN_SECRETS_CHECKS)
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
#elif defined(PLAYCOIN_SECRETS_DATA)
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

// unpack only the secret assets we actually need
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
#elif defined(PLAYCOIN_SECRETS_NEWS)
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

        // old USA/current EUR share this LED bank, address just moves
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

    // restore the exact LED slot we patched
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
            // NEWS died, so its patch died with it
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
    // branch it out so GCC doesnt invent a stray pointer table
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
    // NEWS epoch starts in 2000, osGetTime starts in 1900
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
    // dump NEWS through news:s, not its system save archive
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

    // exact title + Coin pid catches current and old Coin notifications
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
    // raw delete avoids the zombie slots left by NEWS's public header setter
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
        // new secret gets a fresh five-second send window
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

        // pending blocks duplicates until NEWS actually accepts it
        g_coinPendingAchievementMask &= ~bit;
    }

    g_coinHardDiagLastResult = R_FAILED(g_coinHardDiagWindowResult) ?
        g_coinHardDiagWindowResult : sendRc;
    g_coinHardDiagCompletedSerial++;

    g_coinHardDiagState = g_coinPendingAchievementMask ?
        COIN_DIAG_WAIT_TRIGGER : COIN_DIAG_WAIT_RESTORE;

    // restart the five-second window after IPC
    (void)COIN_BLUR__AddTickFunc(PLUGIN_coin_HardNotificationTick, COIN_NOTIFICATION_TICK_NS);
}

PLUGIN_CODE(coin) Result PLUGIN_coin_TriggerAchievement(u32 achievementIndex)
{
    if (achievementIndex >= COIN_ACHIEVEMENT_COUNT)
        return (Result)-26;

    Result rc = PLUGIN_coin_EnsurePackedAssets();
    if (R_FAILED(rc))
        return rc;

    // only decode the record being queued
    CoinAchievement achievement;
    rc = PLUGIN_coin_ReadAchievement(achievementIndex, &achievement);
    if (R_FAILED(rc))
        return rc;

    // existing NEWS item means it already landed, just commit the bit
    u32 existingNotifications = 0;
    rc = PLUGIN_coin_ScanAchievementNews(achievementIndex, false, &existingNotifications);
    if (R_FAILED(rc))
        return rc;
    if (existingNotifications)
        return PLUGIN_coin_RecordAchievementEarned(achievementIndex);

    // yellow now, send on the five-second callback
    Result yellowRc = PLUGIN_coin_BeginNewsLedWindow();
    (void)PLUGIN_coin_EnsureNotificationScratch();
    g_coinHardDiagWindowResult = yellowRc;

    u32 bit = 1u << achievementIndex;
    g_coinPendingAchievementMask |= bit;
    g_coinHardDiagState = COIN_DIAG_WAIT_TRIGGER;

    // add the temp callback once, otherwise just rearm it
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
#endif
