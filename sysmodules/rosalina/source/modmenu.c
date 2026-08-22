#include <3ds.h>
#include "memory.h"
#include "draw.h"
#include "menu.h"
#include "menus.h"
#include "menus/miscellaneous.h"

#ifdef __INTELLISENSE__
#define __attribute__(x)
#endif

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define MENU_PLUGIN_MAGIC       0x24584E33u
#define LOADER_PLUGIN_MAGIC     0x25584E33u
#define MENU_PLUGIN_ID          0x554E454Du
#define MENU_HEADER_SIZE        0x2Cu
#define MENU_MAX_HOST_ITEMS     24u
#define MENU_VISIBLE_ITEMS      16u
#define MENU_ITEM_SPACING_X     6u
#define MENU_ITEM_SPACING_Y     11u
#define MENU_ITEM_TOP_Y         45u
#define MENU_TOP_DOTS_Y         34u
#define MENU_FRAME_COLOR        RGB565(2, 19, 31)
#define MENU_DATA_COPY_CHUNK    0x400u

typedef struct PluginMenuRegistration
{
    u32 pluginId;
    const char *title;
    void (*callback)(void);
    u32 color;
    struct PluginMenuRegistration *next;
} PluginMenuRegistration;

typedef struct __attribute__((packed))
{
    u32 magic;
    u32 pluginId;
    u32 codeSize;
    u32 dataSize;
    u32 bssSize;
    u32 fastRelocSize;
    u32 repairSize;
    u32 ownAbiLo;
    u32 ownAbiHi;
    u32 expectedEnvLo;
    u32 expectedEnvHi;
} PluginMenu3nxHeader;

typedef struct
{
    u32 expectedEnvLo;
    u32 expectedEnvHi;
} PluginMenuSeenState;

typedef struct __attribute__((packed))
{
    u32 count;
} PluginMenuDataHeader;

typedef struct __attribute__((packed))
{
    u32 pluginId;
    u32 offset;
    u32 size;
} PluginMenuDataEntry;

extern bool PLUGIN_MENU_InstallDrawStringHook(void);

PLUGIN_DATA(MENU) void *pluginTable_MENU[] = {
    (void*)svcSleepThread,
    (void*)FSUSER_OpenArchive,
    (void*)FSUSER_CloseArchive,
    (void*)FSUSER_OpenDirectory,
    (void*)FSDIR_Read,
    (void*)FSDIR_Close,
    (void*)FSUSER_OpenFile,
    (void*)FSFILE_Read,
    (void*)FSFILE_Write,
    (void*)FSFILE_SetSize,
    (void*)FSFILE_Close,
    (void*)fsMakePath,
    (void*)Draw_Lock,
    (void*)Draw_Unlock,
    (void*)Draw_ClearFramebuffer,
    (void*)Draw_DrawString,
    (void*)Draw_DrawCharacter,
    (void*)Draw_FlushFramebuffer,
    (void*)waitInput,
    (void*)&menuShouldExit,
    (void*)&rosalinaMenu,
    (void*)&miscellaneousMenu,
    (void*)svcMapProcessMemoryEx,
    (void*)svcUnmapProcessMemoryEx,
    (void*)svcQueryMemory,
    (void*)svcFlushEntireDataCache,
    (void*)svcInvalidateEntireInstructionCache,
    (void*)FSFILE_GetSize,
    (void*)FSUSER_DeleteFile,
};

#define MENU_HOST__svcSleepThread            ((void(*)(s64))pluginTable_MENU[0])
#define MENU_HOST__FSUSER_OpenArchive        ((Result(*)(FS_Archive*,FS_ArchiveID,FS_Path))pluginTable_MENU[1])
#define MENU_HOST__FSUSER_CloseArchive       ((Result(*)(FS_Archive))pluginTable_MENU[2])
#define MENU_HOST__FSUSER_OpenDirectory      ((Result(*)(Handle*,FS_Archive,FS_Path))pluginTable_MENU[3])
#define MENU_HOST__FSDIR_Read                ((Result(*)(Handle,u32*,u32,FS_DirectoryEntry*))pluginTable_MENU[4])
#define MENU_HOST__FSDIR_Close               ((Result(*)(Handle))pluginTable_MENU[5])
#define MENU_HOST__FSUSER_OpenFile           ((Result(*)(Handle*,FS_Archive,FS_Path,u32,u32))pluginTable_MENU[6])
#define MENU_HOST__FSFILE_Read               ((Result(*)(Handle,u32*,u64,void*,u32))pluginTable_MENU[7])
#define MENU_HOST__FSFILE_Write              ((Result(*)(Handle,u32*,u64,const void*,u32,u32))pluginTable_MENU[8])
#define MENU_HOST__FSFILE_SetSize            ((Result(*)(Handle,u64))pluginTable_MENU[9])
#define MENU_HOST__FSFILE_Close              ((Result(*)(Handle))pluginTable_MENU[10])
#define MENU_HOST__fsMakePath                ((FS_Path(*)(FS_PathType,const void*))pluginTable_MENU[11])
#define MENU_HOST__Draw_Lock                 ((void(*)(void))pluginTable_MENU[12])
#define MENU_HOST__Draw_Unlock               ((void(*)(void))pluginTable_MENU[13])
#define MENU_HOST__Draw_ClearFramebuffer     ((void(*)(void))pluginTable_MENU[14])
#define MENU_HOST__Draw_DrawString           ((u32(*)(u32,u32,u32,const char*))pluginTable_MENU[15])
#define MENU_HOST__Draw_DrawCharacter        ((void(*)(u32,u32,u32,char))pluginTable_MENU[16])
#define MENU_HOST__Draw_FlushFramebuffer     ((void(*)(void))pluginTable_MENU[17])
#define MENU_HOST__waitInput                 ((u32(*)(void))pluginTable_MENU[18])
#define MENU_HOST__menuShouldExit            (*(volatile bool*)pluginTable_MENU[19])
#define MENU_HOST__rosalinaMenu              ((Menu*)pluginTable_MENU[20])
#define MENU_HOST__miscellaneousMenu         ((Menu*)pluginTable_MENU[21])
#define MENU_HOST__FSFILE_GetSize            ((Result(*)(Handle,u64*))pluginTable_MENU[27])
#define MENU_HOST__FSUSER_DeleteFile         ((Result(*)(FS_Archive,FS_Path))pluginTable_MENU[28])

PLUGIN_RODATA(MENU) const char g_MENUEntryTitle[] = "Sysplugin Menu";
PLUGIN_RODATA(MENU) const char g_MENUUnreadText[] = "(!)";
PLUGIN_RODATA(MENU) static const char g_MENUPluginsPath[] = "/luma/plugins";
PLUGIN_RODATA(MENU) static const char g_MENUStatePath[] = "/luma/modmenu.dat";
PLUGIN_RODATA(MENU) static const char g_MENUTempPath[] = "/luma/modmenu.tmp";
PLUGIN_RODATA(MENU) static const char g_MENUEmptyPath[] = "";
PLUGIN_RODATA(MENU) static const char g_MENUManageTitle[] = "Manage Sysplugins...";
PLUGIN_RODATA(MENU) static const char g_MENUManageEmptyText[] = "No management actions yet.";
PLUGIN_RODATA(MENU) static const char g_MENUDots[] = "...";
PLUGIN_RODATA(MENU) static const char g_MENUPlus[] = "+";
PLUGIN_RODATA(MENU) static const char g_MENUPipe[] = "|";
PLUGIN_RODATA(MENU) static const char g_MENUSelectedLeft[] = ">>";
PLUGIN_RODATA(MENU) static const char g_MENUSelectedRight[] = "<<        ";
PLUGIN_RODATA(MENU) static const char g_MENUUnselected[] = " *";
PLUGIN_RODATA(MENU) static const char g_MENUClearRow[] =
    "                                                  ";

PLUGIN_DATA(MENU) bool g_MENUUnread = true;
PLUGIN_BSS(MENU) bool g_MENUInsideMenu;
PLUGIN_BSS(MENU) static bool g_MENUHasExpectedEnv;
PLUGIN_BSS(MENU) u64 PLUGIN_MENU_expectedEnv;
PLUGIN_BSS(MENU) static volatile s32 g_MENURegistryLock;
PLUGIN_BSS(MENU) static volatile s32 g_MENUDataLock;
PLUGIN_BSS(MENU) static PluginMenuRegistration *g_MENUFirstItem;
PLUGIN_BSS(MENU) static PluginMenuRegistration *g_MENULastItem;
PLUGIN_BSS(MENU) static u32 g_MENUItemCount;
PLUGIN_BSS(MENU) static u32 g_MENURegistryGeneration;
PLUGIN_BSS(MENU) static PluginMenuRegistration g_MENUManageItem;
PLUGIN_BSS(MENU) static FS_DirectoryEntry g_MENUScanEntry;
PLUGIN_BSS(MENU) static char g_MENUScanName[256];
PLUGIN_BSS(MENU) static char g_MENUScanPath[272];
PLUGIN_BSS(MENU) static char g_MENUBestName[256];
PLUGIN_BSS(MENU) static bool g_MENURootInserted;
PLUGIN_BSS(MENU) static u32 g_MENURootIndex;
PLUGIN_BSS(MENU) static u32 g_MENURootOriginalCount;
PLUGIN_BSS(MENU) static u8 g_MENUDataScratch[MENU_DATA_COPY_CHUNK];

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LockWord(volatile s32 *word)
{
    s32 *lock = (s32*)word;

    for (;;)
    {
        if (__ldrex(lock) == 0)
        {
            if (!__strex(lock, 1))
            {
                __dmb();
                return;
            }
        }
        else
        {
            __clrex();
        }

        MENU_HOST__svcSleepThread(1000);
    }
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnlockWord(volatile s32 *word)
{
    __dmb();
    *word = 0;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LockRegistry(void)
{
    PLUGIN_MENU_LockWord(&g_MENURegistryLock);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnlockRegistry(void)
{
    PLUGIN_MENU_UnlockWord(&g_MENURegistryLock);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LockData(void)
{
    PLUGIN_MENU_LockWord(&g_MENUDataLock);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_UnlockData(void)
{
    PLUGIN_MENU_UnlockWord(&g_MENUDataLock);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_AppendInternal(
    PluginMenuRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void),
    u32 color
)
{
    if (!item || !title || !callback)
        return false;

    PLUGIN_MENU_LockRegistry();
    if (g_MENUItemCount == 0xFFFFFFFFu)
    {
        PLUGIN_MENU_UnlockRegistry();
        return false;
    }

    item->pluginId = pluginId;
    item->title = title;
    item->callback = callback;
    item->color = color;
    item->next = NULL;

    if (g_MENULastItem)
        g_MENULastItem->next = item;
    else
        g_MENUFirstItem = item;

    g_MENULastItem = item;
    g_MENUItemCount++;
    g_MENURegistryGeneration++;
    PLUGIN_MENU_UnlockRegistry();
    return true;
}

// The caller owns this record and must keep it mapped until RemoveItem returns.
PLUGIN_CODE(MENU) bool PLUGIN_MENU_AddItem(
    PluginMenuRegistration *item,
    u32 pluginId,
    const char *title,
    void (*callback)(void),
    u32 color
)
{
    if (!item || !pluginId || !title || !callback)
        return false;

    PLUGIN_MENU_LockRegistry();

    for (PluginMenuRegistration *current = g_MENUFirstItem; current; current = current->next)
    {
        if (current == item)
        {
            current->pluginId = pluginId;
            current->title = title;
            current->callback = callback;
            current->color = color;
            g_MENURegistryGeneration++;
            PLUGIN_MENU_UnlockRegistry();
            return true;
        }
    }

    if (g_MENUItemCount == 0xFFFFFFFFu)
    {
        PLUGIN_MENU_UnlockRegistry();
        return false;
    }

    item->pluginId = pluginId;
    item->title = title;
    item->callback = callback;
    item->color = color;
    item->next = NULL;

    if (g_MENULastItem)
        g_MENULastItem->next = item;
    else
        g_MENUFirstItem = item;

    g_MENULastItem = item;
    g_MENUItemCount++;
    g_MENURegistryGeneration++;
    PLUGIN_MENU_UnlockRegistry();
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_RemoveItem(PluginMenuRegistration *item)
{
    if (!item)
        return false;

    if (item == &g_MENUManageItem)
        return false;

    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *previous = NULL;
    PluginMenuRegistration *current = g_MENUFirstItem;

    while (current && current != item)
    {
        previous = current;
        current = current->next;
    }

    if (!current)
    {
        PLUGIN_MENU_UnlockRegistry();
        return false;
    }

    if (previous)
        previous->next = current->next;
    else
        g_MENUFirstItem = current->next;

    if (g_MENULastItem == current)
        g_MENULastItem = previous;

    current->pluginId = 0;
    current->title = NULL;
    current->callback = NULL;
    current->color = 0;
    current->next = NULL;
    g_MENUItemCount--;
    g_MENURegistryGeneration++;
    PLUGIN_MENU_UnlockRegistry();
    return true;
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_StringLength(const char *text)
{
    const volatile char *p = text;
    u32 length = 0;

    while (p[length])
        length++;

    return length;
}

PLUGIN_CODE(MENU) static s32 PLUGIN_MENU_StringCompare(const char *a, const char *b)
{
    const volatile char *left = a;
    const volatile char *right = b;

    while (*left && *left == *right)
    {
        left++;
        right++;
    }

    return (s32)(u8)*left - (s32)(u8)*right;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_CopyString(char *dst, const char *src, u32 size)
{
    volatile char *out = dst;
    const volatile char *in = src;
    u32 i = 0;

    while (i + 1u < size && in[i])
    {
        out[i] = in[i];
        i++;
    }

    out[i] = 0;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_Add32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;

    if (value < a)
        return false;

    *out = value;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadExact(
    Handle file,
    u64 offset,
    void *buffer,
    u32 size
)
{
    u32 read = 0;
    return !size ||
        (R_SUCCEEDED(MENU_HOST__FSFILE_Read(file, &read, offset, buffer, size)) && read == size);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_WriteExact(
    Handle file,
    u64 offset,
    const void *buffer,
    u32 size
)
{
    u32 written = 0;
    return !size ||
        (R_SUCCEEDED(MENU_HOST__FSFILE_Write(file, &written, offset, buffer, size, FS_WRITE_FLUSH)) &&
         written == size);
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadBufferRange(
    Handle file,
    u64 offset,
    void *buffer,
    u32 size
)
{
    u8 *out = (u8*)buffer;

    while (size)
    {
        u32 chunk = size < MENU_DATA_COPY_CHUNK ? size : MENU_DATA_COPY_CHUNK;
        if (!PLUGIN_MENU_ReadExact(file, offset, out, chunk))
            return false;

        offset += chunk;
        out += chunk;
        size -= chunk;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_WriteBufferRange(
    Handle file,
    u64 offset,
    const void *buffer,
    u32 size
)
{
    const u8 *in = (const u8*)buffer;

    while (size)
    {
        u32 chunk = size < MENU_DATA_COPY_CHUNK ? size : MENU_DATA_COPY_CHUNK;
        if (!PLUGIN_MENU_WriteExact(file, offset, in, chunk))
            return false;

        offset += chunk;
        in += chunk;
        size -= chunk;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadDataEntry(
    Handle file,
    u32 index,
    PluginMenuDataEntry *entry
)
{
    u32 offset;
    u32 scaled;

    if (index > 0xFFFFFFFFu / sizeof(*entry))
        return false;

    scaled = index * sizeof(*entry);
    if (!PLUGIN_MENU_Add32(sizeof(PluginMenuDataHeader), scaled, &offset))
        return false;

    return PLUGIN_MENU_ReadExact(file, offset, entry, sizeof(*entry));
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ValidateDataFile(
    Handle file,
    PluginMenuDataHeader *headerOut,
    u32 *fileSizeOut
)
{
    u64 fileSize64;
    PluginMenuDataHeader header;
    u32 tableBytes;
    u32 expectedOffset;
    u32 previousId = 0;

    if (R_FAILED(MENU_HOST__FSFILE_GetSize(file, &fileSize64)) ||
        fileSize64 > 0xFFFFFFFFu ||
        fileSize64 < sizeof(header) ||
        !PLUGIN_MENU_ReadExact(file, 0, &header, sizeof(header)) ||
        header.count > (0xFFFFFFFFu - sizeof(header)) / sizeof(PluginMenuDataEntry))
    {
        return false;
    }

    tableBytes = header.count * sizeof(PluginMenuDataEntry);
    if (!PLUGIN_MENU_Add32(sizeof(header), tableBytes, &expectedOffset) ||
        expectedOffset > (u32)fileSize64)
    {
        return false;
    }

    for (u32 i = 0; i < header.count; i++)
    {
        PluginMenuDataEntry entry;
        u32 blockEnd;
        u32 blockId;

        if (!PLUGIN_MENU_ReadDataEntry(file, i, &entry) ||
            !entry.pluginId ||
            (i && entry.pluginId <= previousId) ||
            entry.offset != expectedOffset ||
            !PLUGIN_MENU_Add32(entry.offset, sizeof(u32), &blockEnd) ||
            !PLUGIN_MENU_Add32(blockEnd, entry.size, &blockEnd) ||
            blockEnd > (u32)fileSize64 ||
            !PLUGIN_MENU_ReadExact(file, entry.offset, &blockId, sizeof(blockId)) ||
            blockId != entry.pluginId)
        {
            return false;
        }

        previousId = entry.pluginId;
        expectedOffset = blockEnd;
    }

    if (expectedOffset != (u32)fileSize64)
        return false;

    if (headerOut)
        *headerOut = header;
    if (fileSizeOut)
        *fileSizeOut = (u32)fileSize64;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_FindDataEntry(
    Handle file,
    const PluginMenuDataHeader *header,
    u32 pluginId,
    PluginMenuDataEntry *entryOut
)
{
    for (u32 i = 0; i < header->count; i++)
    {
        PluginMenuDataEntry entry;
        if (!PLUGIN_MENU_ReadDataEntry(file, i, &entry))
            return false;

        if (entry.pluginId == pluginId)
        {
            if (entryOut)
                *entryOut = entry;
            return true;
        }

        if (entry.pluginId > pluginId)
            break;
    }

    return false;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_GetDataSize(u32 pluginId, u32 *sizeOut)
{
    if (!pluginId || !sizeOut)
        return false;

    bool found = false;
    FS_Archive archive;
    Handle file;

    PLUGIN_MENU_LockData();

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenFile(
                &file,
                archive,
                MENU_HOST__fsMakePath(PATH_ASCII, g_MENUStatePath),
                FS_OPEN_READ,
                0)))
        {
            PluginMenuDataHeader header;
            PluginMenuDataEntry entry;
            if (PLUGIN_MENU_ValidateDataFile(file, &header, NULL) &&
                PLUGIN_MENU_FindDataEntry(file, &header, pluginId, &entry))
            {
                *sizeOut = entry.size;
                found = true;
            }

            MENU_HOST__FSFILE_Close(file);
        }

        MENU_HOST__FSUSER_CloseArchive(archive);
    }

    PLUGIN_MENU_UnlockData();
    return found;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_LoadData(u32 pluginId, void *data, u32 size)
{
    if (!pluginId || (size && !data))
        return false;

    bool loaded = false;
    FS_Archive archive;
    Handle file;

    PLUGIN_MENU_LockData();

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenFile(
                &file,
                archive,
                MENU_HOST__fsMakePath(PATH_ASCII, g_MENUStatePath),
                FS_OPEN_READ,
                0)))
        {
            PluginMenuDataHeader header;
            PluginMenuDataEntry entry;
            if (PLUGIN_MENU_ValidateDataFile(file, &header, NULL) &&
                PLUGIN_MENU_FindDataEntry(file, &header, pluginId, &entry) &&
                entry.size == size)
            {
                loaded = PLUGIN_MENU_ReadBufferRange(
                    file,
                    (u64)entry.offset + sizeof(u32),
                    data,
                    size
                );
            }

            MENU_HOST__FSFILE_Close(file);
        }

        MENU_HOST__FSUSER_CloseArchive(archive);
    }

    PLUGIN_MENU_UnlockData();
    return loaded;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_CopyDataRange(
    Handle src,
    u64 srcOffset,
    Handle dst,
    u64 dstOffset,
    u32 size
)
{
    while (size)
    {
        u32 chunk = size < MENU_DATA_COPY_CHUNK ? size : MENU_DATA_COPY_CHUNK;
        if (!PLUGIN_MENU_ReadExact(src, srcOffset, g_MENUDataScratch, chunk) ||
            !PLUGIN_MENU_WriteExact(dst, dstOffset, g_MENUDataScratch, chunk))
        {
            return false;
        }

        srcOffset += chunk;
        dstOffset += chunk;
        size -= chunk;
    }

    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_WriteDataRecord(
    Handle newFile,
    Handle oldFile,
    bool copyOld,
    const PluginMenuDataEntry *oldEntry,
    u32 tableIndex,
    u32 *dataOffset,
    u32 pluginId,
    const void *data,
    u32 size
)
{
    PluginMenuDataEntry entry;
    u32 tableOffset;
    u32 scaled;
    u32 payloadOffset;
    u32 end;

    if (tableIndex > 0xFFFFFFFFu / sizeof(entry))
        return false;

    scaled = tableIndex * sizeof(entry);
    if (!PLUGIN_MENU_Add32(sizeof(PluginMenuDataHeader), scaled, &tableOffset) ||
        !PLUGIN_MENU_Add32(*dataOffset, sizeof(u32), &payloadOffset) ||
        !PLUGIN_MENU_Add32(payloadOffset, size, &end))
    {
        return false;
    }

    entry.pluginId = pluginId;
    entry.offset = *dataOffset;
    entry.size = size;

    if (!PLUGIN_MENU_WriteExact(newFile, tableOffset, &entry, sizeof(entry)) ||
        !PLUGIN_MENU_WriteExact(newFile, entry.offset, &pluginId, sizeof(pluginId)))
    {
        return false;
    }

    if (copyOld)
    {
        if (!oldEntry || oldEntry->size != size ||
            !PLUGIN_MENU_CopyDataRange(
                oldFile,
                (u64)oldEntry->offset + sizeof(u32),
                newFile,
                payloadOffset,
                size))
        {
            return false;
        }
    }
    else if (!PLUGIN_MENU_WriteBufferRange(newFile, payloadOffset, data, size))
    {
        return false;
    }

    *dataOffset = end;
    return true;
}

PLUGIN_CODE(MENU) bool PLUGIN_MENU_SaveData(u32 pluginId, const void *data, u32 size)
{
    if (!pluginId || (size && !data))
        return false;

    bool success = false;
    bool oldOpen = false;
    FS_Archive archive;
    Handle oldFile = 0;
    Handle tempFile = 0;
    Handle stateFile = 0;
    PluginMenuDataHeader oldHeader;
    u32 oldCount = 0;
    u32 preservedBytes = 0;
    bool replacing = false;

    PLUGIN_MENU_LockData();

    if (R_FAILED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        goto done;
    }

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenFile(
            &oldFile,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUStatePath),
            FS_OPEN_READ,
            0)))
    {
        oldOpen = true;
        if (PLUGIN_MENU_ValidateDataFile(oldFile, &oldHeader, NULL))
        {
            oldCount = oldHeader.count;
            for (u32 i = 0; i < oldCount; i++)
            {
                PluginMenuDataEntry entry;
                u32 blockSize;

                if (!PLUGIN_MENU_ReadDataEntry(oldFile, i, &entry) ||
                    !PLUGIN_MENU_Add32(sizeof(u32), entry.size, &blockSize))
                {
                    oldCount = 0;
                    preservedBytes = 0;
                    replacing = false;
                    break;
                }

                if (entry.pluginId == pluginId)
                    replacing = true;
                else if (!PLUGIN_MENU_Add32(preservedBytes, blockSize, &preservedBytes))
                {
                    oldCount = 0;
                    preservedBytes = 0;
                    replacing = false;
                    break;
                }
            }
        }
    }

    u32 newCount = oldCount + (replacing ? 0u : 1u);
    u32 tableBytes;
    u32 tableEnd;
    u32 targetBytes;
    u32 finalSize;

    if (newCount < oldCount ||
        newCount > (0xFFFFFFFFu - sizeof(PluginMenuDataHeader)) / sizeof(PluginMenuDataEntry) ||
        !PLUGIN_MENU_Add32(sizeof(u32), size, &targetBytes))
    {
        goto close_archive;
    }

    tableBytes = newCount * sizeof(PluginMenuDataEntry);
    if (!PLUGIN_MENU_Add32(sizeof(PluginMenuDataHeader), tableBytes, &tableEnd) ||
        !PLUGIN_MENU_Add32(tableEnd, preservedBytes, &finalSize) ||
        !PLUGIN_MENU_Add32(finalSize, targetBytes, &finalSize))
    {
        goto close_archive;
    }

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &tempFile,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUTempPath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
            0)) ||
        R_FAILED(MENU_HOST__FSFILE_SetSize(tempFile, finalSize)))
    {
        goto close_archive;
    }

    PluginMenuDataHeader newHeader;
    newHeader.count = newCount;

    if (!PLUGIN_MENU_WriteExact(tempFile, 0, &newHeader, sizeof(newHeader)))
        goto close_temp;

    u32 tableIndex = 0;
    u32 dataOffset = tableEnd;
    bool targetWritten = false;

    for (u32 i = 0; i < oldCount; i++)
    {
        PluginMenuDataEntry entry;
        if (!PLUGIN_MENU_ReadDataEntry(oldFile, i, &entry))
            goto close_temp;

        if (!targetWritten && pluginId < entry.pluginId)
        {
            if (!PLUGIN_MENU_WriteDataRecord(
                    tempFile,
                    oldFile,
                    false,
                    NULL,
                    tableIndex++,
                    &dataOffset,
                    pluginId,
                    data,
                    size))
            {
                goto close_temp;
            }
            targetWritten = true;
        }

        if (entry.pluginId == pluginId)
        {
            if (!PLUGIN_MENU_WriteDataRecord(
                    tempFile,
                    oldFile,
                    false,
                    NULL,
                    tableIndex++,
                    &dataOffset,
                    pluginId,
                    data,
                    size))
            {
                goto close_temp;
            }
            targetWritten = true;
        }
        else if (!PLUGIN_MENU_WriteDataRecord(
                    tempFile,
                    oldFile,
                    true,
                    &entry,
                    tableIndex++,
                    &dataOffset,
                    entry.pluginId,
                    NULL,
                    entry.size))
        {
            goto close_temp;
        }
    }

    if (!targetWritten &&
        !PLUGIN_MENU_WriteDataRecord(
            tempFile,
            oldFile,
            false,
            NULL,
            tableIndex++,
            &dataOffset,
            pluginId,
            data,
            size))
    {
        goto close_temp;
    }

    if (tableIndex != newCount || dataOffset != finalSize)
        goto close_temp;

    if (oldOpen)
    {
        MENU_HOST__FSFILE_Close(oldFile);
        oldOpen = false;
    }

    if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
            &stateFile,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUStatePath),
            FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_CREATE,
            0)) ||
        R_FAILED(MENU_HOST__FSFILE_SetSize(stateFile, finalSize)) ||
        !PLUGIN_MENU_CopyDataRange(tempFile, 0, stateFile, 0, finalSize))
    {
        goto close_state;
    }

    success = true;

close_state:
    if (stateFile)
        MENU_HOST__FSFILE_Close(stateFile);
close_temp:
    if (tempFile)
        MENU_HOST__FSFILE_Close(tempFile);
    (void)MENU_HOST__FSUSER_DeleteFile(
        archive,
        MENU_HOST__fsMakePath(PATH_ASCII, g_MENUTempPath)
    );
close_archive:
    if (oldOpen)
        MENU_HOST__FSFILE_Close(oldFile);
    MENU_HOST__FSUSER_CloseArchive(archive);
done:
    PLUGIN_MENU_UnlockData();
    return success;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ReadHeader(
    Handle file,
    u64 fileSize,
    u32 offset,
    PluginMenu3nxHeader *header,
    u32 *nextOffset
)
{
    u32 end;

    if (!PLUGIN_MENU_ReadExact(file, offset, header, sizeof(*header)) ||
        !PLUGIN_MENU_Add32(offset, MENU_HEADER_SIZE, &end) ||
        !PLUGIN_MENU_Add32(end, header->fastRelocSize, &end) ||
        !PLUGIN_MENU_Add32(end, header->codeSize, &end) ||
        !PLUGIN_MENU_Add32(end, header->dataSize, &end) ||
        !PLUGIN_MENU_Add32(end, header->repairSize, &end) ||
        end > 0xFFFFFFF0u || end > fileSize)
    {
        return false;
    }

    *nextOffset = (end + 0xFu) & ~0xFu;
    return *nextOffset > offset && *nextOffset <= fileSize + 0xFu;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_ParsePriority(
    char *name,
    u32 length,
    u32 *priority
)
{
    if (length < 7u ||
        name[length - 4u] != '.' ||
        name[length - 3u] != '3' ||
        name[length - 2u] != 'n' ||
        name[length - 1u] != 'x')
    {
        return false;
    }

    char *extension = &name[length - 4u];
    char *priorityDot = extension - 1;

    while (priorityDot > name && *priorityDot != '.')
        priorityDot--;

    if (*priorityDot != '.' || priorityDot + 1 == extension)
        return false;

    u32 value = 0;
    for (char *character = priorityDot + 1; character < extension; character++)
    {
        if (*character < '0' || *character > '9')
            return false;

        u32 digit = (u32)(*character - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u)
            return false;

        value = value * 10u + digit;
    }

    *priority = value;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_MakePluginPath(const char *name)
{
    u32 prefixLength = PLUGIN_MENU_StringLength(g_MENUPluginsPath);
    u32 nameLength = PLUGIN_MENU_StringLength(name);

    if (prefixLength + nameLength + 2u > sizeof(g_MENUScanPath))
        return false;

    for (u32 i = 0; i < prefixLength; i++)
        g_MENUScanPath[i] = g_MENUPluginsPath[i];

    g_MENUScanPath[prefixLength] = '/';
    for (u32 i = 0; i < nameLength; i++)
        g_MENUScanPath[prefixLength + 1u + i] = name[i];
    g_MENUScanPath[prefixLength + 1u + nameLength] = 0;
    return true;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_IsEarlier(
    bool found,
    u32 priority,
    const char *name,
    u32 offset,
    u32 bestPriority,
    u32 bestOffset
)
{
    if (!found || priority != bestPriority)
        return !found || priority < bestPriority;

    s32 comparison = PLUGIN_MENU_StringCompare(name, g_MENUBestName);
    if (comparison)
        return comparison < 0;

    return offset < bestOffset;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_FindExpectedEnvironment(FS_Archive archive)
{
    Handle directory;
    bool found = false;
    u32 bestPriority = 0;
    u32 bestOffset = 0;
    u32 bestEnvLo = 0;
    u32 bestEnvHi = 0;

    if (R_FAILED(MENU_HOST__FSUSER_OpenDirectory(
            &directory,
            archive,
            MENU_HOST__fsMakePath(PATH_ASCII, g_MENUPluginsPath))))
    {
        return false;
    }

    for (;;)
    {
        u32 entriesRead = 0;
        if (R_FAILED(MENU_HOST__FSDIR_Read(directory, &entriesRead, 1, &g_MENUScanEntry)) ||
            !entriesRead)
        {
            break;
        }

        u32 length = 0;
        while (length + 1u < sizeof(g_MENUScanName) && g_MENUScanEntry.name[length])
        {
            g_MENUScanName[length] = (char)g_MENUScanEntry.name[length];
            length++;
        }
        g_MENUScanName[length] = 0;

        u32 priority;
        if (!PLUGIN_MENU_ParsePriority(g_MENUScanName, length, &priority) ||
            !PLUGIN_MENU_MakePluginPath(g_MENUScanName))
        {
            continue;
        }

        Handle file;
        if (R_FAILED(MENU_HOST__FSUSER_OpenFile(
                &file,
                archive,
                MENU_HOST__fsMakePath(PATH_ASCII, g_MENUScanPath),
                FS_OPEN_READ,
                0)))
        {
            continue;
        }

        u32 offset = 0;
        for (;;)
        {
            PluginMenu3nxHeader header;
            u32 nextOffset;

            if (!PLUGIN_MENU_ReadHeader(
                    file,
                    g_MENUScanEntry.fileSize,
                    offset,
                    &header,
                    &nextOffset) ||
                (header.magic != MENU_PLUGIN_MAGIC && header.magic != LOADER_PLUGIN_MAGIC))
            {
                break;
            }

            if (header.magic == MENU_PLUGIN_MAGIC &&
                header.pluginId == MENU_PLUGIN_ID &&
                PLUGIN_MENU_IsEarlier(
                    found,
                    priority,
                    g_MENUScanName,
                    offset,
                    bestPriority,
                    bestOffset))
            {
                found = true;
                bestPriority = priority;
                bestOffset = offset;
                bestEnvLo = header.expectedEnvLo & ~1u;
                bestEnvHi = header.expectedEnvHi;
                PLUGIN_MENU_CopyString(g_MENUBestName, g_MENUScanName, sizeof(g_MENUBestName));
            }

            offset = nextOffset;
        }

        MENU_HOST__FSFILE_Close(file);
    }

    MENU_HOST__FSDIR_Close(directory);

    if (!found || (!bestEnvLo && !bestEnvHi))
        return false;

    PLUGIN_MENU_expectedEnv = ((u64)bestEnvHi << 32) | bestEnvLo;
    g_MENUHasExpectedEnv = true;
    return true;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_LoadSeenState(void)
{
    FS_Archive archive;
    bool foundEnvironment = false;

    g_MENUUnread = true;
    g_MENUHasExpectedEnv = false;
    PLUGIN_MENU_expectedEnv = 0;

    if (R_SUCCEEDED(MENU_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            MENU_HOST__fsMakePath(PATH_EMPTY, g_MENUEmptyPath))))
    {
        foundEnvironment = PLUGIN_MENU_FindExpectedEnvironment(archive);
        MENU_HOST__FSUSER_CloseArchive(archive);
    }

    if (foundEnvironment)
    {
        PluginMenuSeenState state;
        if (PLUGIN_MENU_LoadData(MENU_PLUGIN_ID, &state, sizeof(state)) &&
            state.expectedEnvLo == (u32)PLUGIN_MENU_expectedEnv &&
            state.expectedEnvHi == (u32)(PLUGIN_MENU_expectedEnv >> 32))
        {
            g_MENUUnread = false;
        }
    }
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_SaveSeenState(void)
{
    if (!g_MENUHasExpectedEnv)
        return false;

    PluginMenuSeenState state;
    state.expectedEnvLo = (u32)PLUGIN_MENU_expectedEnv;
    state.expectedEnvHi = (u32)(PLUGIN_MENU_expectedEnv >> 32);
    return PLUGIN_MENU_SaveData(MENU_PLUGIN_ID, &state, sizeof(state));
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_GetRegistryState(
    u32 *count,
    u32 *generation
)
{
    PLUGIN_MENU_LockRegistry();
    *count = g_MENUItemCount;
    *generation = g_MENURegistryGeneration;
    PLUGIN_MENU_UnlockRegistry();
}

PLUGIN_CODE(MENU) static u32 PLUGIN_MENU_SnapshotItems(
    u32 first,
    const char **titles,
    u32 *colors
)
{
    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *item = g_MENUFirstItem;
    for (u32 i = 0; item && i < first; i++)
        item = item->next;

    u32 count = 0;
    while (item && count < MENU_VISIBLE_ITEMS)
    {
        titles[count] = item->title;
        colors[count] = item->color;
        count++;
        item = item->next;
    }

    PLUGIN_MENU_UnlockRegistry();
    return count;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_GetPresentation(
    u32 index,
    const char **title,
    u32 *color
)
{
    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *item = g_MENUFirstItem;
    for (u32 i = 0; item && i < index; i++)
        item = item->next;

    if (item)
    {
        *title = item->title;
        *color = item->color;
    }

    PLUGIN_MENU_UnlockRegistry();
    return item != NULL;
}

PLUGIN_CODE(MENU) static void (*PLUGIN_MENU_GetCallback(u32 index))(void)
{
    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *item = g_MENUFirstItem;
    for (u32 i = 0; item && i < index; i++)
        item = item->next;

    void (*callback)(void) = item ? item->callback : NULL;
    PLUGIN_MENU_UnlockRegistry();
    return callback;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_DrawFrame(const char *title)
{
    MENU_HOST__Draw_DrawString(10, 8, MENU_FRAME_COLOR, g_MENUPlus);
    for (u32 i = 0; i < 35u; i++)
        MENU_HOST__Draw_DrawCharacter(16u + i * MENU_ITEM_SPACING_X, 8, MENU_FRAME_COLOR, '-');
    MENU_HOST__Draw_DrawString(222, 8, MENU_FRAME_COLOR, g_MENUPlus);
    MENU_HOST__Draw_DrawString(10, 16, MENU_FRAME_COLOR, g_MENUPipe);
    MENU_HOST__Draw_DrawString(222, 16, MENU_FRAME_COLOR, g_MENUPipe);
    MENU_HOST__Draw_DrawString(10, 24, MENU_FRAME_COLOR, g_MENUPlus);
    for (u32 i = 0; i < 35u; i++)
        MENU_HOST__Draw_DrawCharacter(16u + i * MENU_ITEM_SPACING_X, 24, MENU_FRAME_COLOR, '-');
    MENU_HOST__Draw_DrawString(222, 24, MENU_FRAME_COLOR, g_MENUPlus);
    MENU_HOST__Draw_DrawString(20, 16, COLOR_WHITE, title);
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_DrawItem(
    u32 y,
    bool selected,
    const char *title,
    u32 color
)
{
    if (selected)
    {
        MENU_HOST__Draw_DrawString(15, y, COLOR_ORANGE, g_MENUSelectedLeft);
        MENU_HOST__Draw_DrawString(35, y, COLOR_CYAN, title);
        MENU_HOST__Draw_DrawString(250, y, COLOR_ORANGE, g_MENUSelectedRight);
    }
    else
    {
        MENU_HOST__Draw_DrawString(15, y, COLOR_GRAY, g_MENUUnselected);
        MENU_HOST__Draw_DrawString(35, y, color, title);
    }
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_Draw(u32 first, u32 selected, u32 total)
{
    const char *titles[MENU_VISIBLE_ITEMS];
    u32 colors[MENU_VISIBLE_ITEMS];
    u32 shown = PLUGIN_MENU_SnapshotItems(first, titles, colors);

    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    PLUGIN_MENU_DrawFrame(g_MENUEntryTitle);

    if (first)
        MENU_HOST__Draw_DrawString(35, MENU_TOP_DOTS_Y, COLOR_GRAY, g_MENUDots);

    for (u32 i = 0; i < shown; i++)
    {
        PLUGIN_MENU_DrawItem(
            MENU_ITEM_TOP_Y + i * MENU_ITEM_SPACING_Y,
            first + i == selected,
            titles[i],
            colors[i]
        );
    }

    if (first + shown < total)
    {
        MENU_HOST__Draw_DrawString(
            35,
            MENU_ITEM_TOP_Y + MENU_VISIBLE_ITEMS * MENU_ITEM_SPACING_Y,
            COLOR_GRAY,
            g_MENUDots
        );
    }

    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_RedrawSelection(
    u32 first,
    u32 oldSelected,
    u32 selected
)
{
    const char *oldTitle;
    const char *newTitle;
    u32 oldColor;
    u32 newColor;

    if (!PLUGIN_MENU_GetPresentation(oldSelected, &oldTitle, &oldColor) ||
        !PLUGIN_MENU_GetPresentation(selected, &newTitle, &newColor))
    {
        return;
    }

    u32 oldY = MENU_ITEM_TOP_Y + (oldSelected - first) * MENU_ITEM_SPACING_Y;
    u32 newY = MENU_ITEM_TOP_Y + (selected - first) * MENU_ITEM_SPACING_Y;

    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_DrawString(10, oldY, COLOR_BLACK, g_MENUClearRow);
    MENU_HOST__Draw_DrawString(10, newY, COLOR_BLACK, g_MENUClearRow);
    PLUGIN_MENU_DrawItem(oldY, false, oldTitle, oldColor);
    PLUGIN_MENU_DrawItem(newY, true, newTitle, newColor);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ClearForCallback(void)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_Manage(void)
{
    MENU_HOST__Draw_Lock();
    MENU_HOST__Draw_ClearFramebuffer();
    PLUGIN_MENU_DrawFrame(g_MENUManageTitle);
    MENU_HOST__Draw_DrawString(35, MENU_ITEM_TOP_Y, COLOR_GRAY, g_MENUManageEmptyText);
    MENU_HOST__Draw_FlushFramebuffer();
    MENU_HOST__Draw_Unlock();

    while (!MENU_HOST__menuShouldExit)
        if (MENU_HOST__waitInput() & KEY_B)
            break;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_Open(void)
{
    if (g_MENUUnread)
    {
        g_MENUUnread = false;
        (void)PLUGIN_MENU_SaveSeenState();
    }

    u32 selected = 0;
    u32 first = 0;
    u32 drawnCount = 0;
    u32 drawnGeneration = 0;
    bool fullRedraw = true;
    g_MENUInsideMenu = true;

    for (;;)
    {
        u32 count;
        u32 generation;
        PLUGIN_MENU_GetRegistryState(&count, &generation);
        if (!count || MENU_HOST__menuShouldExit)
            break;

        if (selected >= count)
            selected = count - 1u;

        if (count <= MENU_VISIBLE_ITEMS)
            first = 0;
        else
        {
            if (first > selected)
                first = selected;
            if (selected >= first + MENU_VISIBLE_ITEMS)
                first = selected - MENU_VISIBLE_ITEMS + 1u;
            if (first > count - MENU_VISIBLE_ITEMS)
                first = count - MENU_VISIBLE_ITEMS;
        }

        if (count != drawnCount || generation != drawnGeneration)
            fullRedraw = true;

        if (fullRedraw)
        {
            PLUGIN_MENU_Draw(first, selected, count);
            drawnCount = count;
            drawnGeneration = generation;
            fullRedraw = false;
        }

        u32 pressed = MENU_HOST__waitInput();

        if (MENU_HOST__menuShouldExit)
            break;

        if (pressed & KEY_A)
        {
            void (*callback)(void) = PLUGIN_MENU_GetCallback(selected);
            if (callback)
            {
                PLUGIN_MENU_ClearForCallback();
                callback();
                fullRedraw = true;
            }
        }
        else if (pressed & KEY_B)
        {
            break;
        }
        else if (pressed & KEY_DOWN)
        {
            u32 oldSelected = selected;
            u32 oldFirst = first;

            if (selected + 1u >= count)
            {
                selected = 0;
                first = 0;
            }
            else
            {
                selected++;
                if (selected >= first + MENU_VISIBLE_ITEMS)
                    first++;
            }

            if (first != oldFirst)
                fullRedraw = true;
            else if (selected != oldSelected)
                PLUGIN_MENU_RedrawSelection(first, oldSelected, selected);
        }
        else if (pressed & KEY_UP)
        {
            u32 oldSelected = selected;
            u32 oldFirst = first;

            if (!selected)
            {
                selected = count - 1u;
                first = count > MENU_VISIBLE_ITEMS ? count - MENU_VISIBLE_ITEMS : 0;
            }
            else
            {
                selected--;
                if (selected < first)
                    first--;
            }

            if (first != oldFirst)
                fullRedraw = true;
            else if (selected != oldSelected)
                PLUGIN_MENU_RedrawSelection(first, oldSelected, selected);
        }
    }

    g_MENUInsideMenu = false;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_CopyMenuItem(MenuItem *dst, const MenuItem *src)
{
    dst->title = src->title;
    dst->action_type = src->action_type;
    if (src->action_type == METHOD)
        dst->method = src->method;
    else
        dst->menu = src->menu;
    dst->visibility = src->visibility;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_InsertRootItem(void)
{
    Menu *root = MENU_HOST__rosalinaMenu;
    u32 count = 0;

    while (count < MENU_MAX_HOST_ITEMS && root->items[count].action_type != MENU_END)
        count++;

    if (count >= MENU_MAX_HOST_ITEMS - 1u)
        return false;

    u32 index = MENU_MAX_HOST_ITEMS;
    for (u32 i = 0; i < count; i++)
    {
        if (root->items[i].action_type == MENU &&
            root->items[i].menu == MENU_HOST__miscellaneousMenu)
        {
            index = i;
            break;
        }
    }

    if (index == MENU_MAX_HOST_ITEMS)
        return false;

    for (u32 i = count + 1u; i > index; i--)
        PLUGIN_MENU_CopyMenuItem(&root->items[i], &root->items[i - 1u]);

    root->items[index].title = g_MENUEntryTitle;
    root->items[index].action_type = METHOD;
    root->items[index].method = PLUGIN_MENU_Open;
    root->items[index].visibility = NULL;

    g_MENURootInserted = true;
    g_MENURootIndex = index;
    g_MENURootOriginalCount = count;
    return true;
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_RemoveRootItem(void)
{
    if (!g_MENURootInserted)
        return;

    Menu *root = MENU_HOST__rosalinaMenu;
    if (root->items[g_MENURootIndex].action_type == METHOD &&
        root->items[g_MENURootIndex].method == PLUGIN_MENU_Open)
    {
        for (u32 i = g_MENURootIndex; i <= g_MENURootOriginalCount; i++)
            PLUGIN_MENU_CopyMenuItem(&root->items[i], &root->items[i + 1u]);
    }

    g_MENURootInserted = false;
}

PLUGIN_CODE(MENU) static bool PLUGIN_MENU_SetupItems(void)
{
    return PLUGIN_MENU_AppendInternal(
        &g_MENUManageItem,
        MENU_PLUGIN_ID,
        g_MENUManageTitle,
        PLUGIN_MENU_Manage,
        COLOR_WHITE
    );
}

PLUGIN_CODE(MENU) static void PLUGIN_MENU_ResetRegistry(void)
{
    PLUGIN_MENU_LockRegistry();

    PluginMenuRegistration *item = g_MENUFirstItem;
    while (item)
    {
        PluginMenuRegistration *next = item->next;
        item->pluginId = 0;
        item->title = NULL;
        item->callback = NULL;
        item->color = 0;
        item->next = NULL;
        item = next;
    }

    g_MENUFirstItem = NULL;
    g_MENULastItem = NULL;
    g_MENUItemCount = 0;
    g_MENURegistryGeneration++;
    PLUGIN_MENU_UnlockRegistry();
}

PLUGIN_MAIN(MENU) bool PLUGIN_MENU_Main(void)
{
    if (!PLUGIN_MENU_SetupItems())
    {
        PLUGIN_MENU_ResetRegistry();
        return false;
    }

    PLUGIN_MENU_LoadSeenState();

    if (!PLUGIN_MENU_InsertRootItem())
    {
        PLUGIN_MENU_ResetRegistry();
        return false;
    }

    // Install last so every false return leaves no pointer into MENU.
    if (!PLUGIN_MENU_InstallDrawStringHook())
    {
        PLUGIN_MENU_RemoveRootItem();
        PLUGIN_MENU_ResetRegistry();
        return false;
    }

    return true;
}