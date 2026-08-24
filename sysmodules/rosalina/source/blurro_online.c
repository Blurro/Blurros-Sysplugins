#include <3ds.h>
#include "csvc.h"
#include "draw.h"
#include "menu.h"
#include "minisoc.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define BLUR_TRANSIENT_MAGIC 0x26584E33u
#define BLUR_HTTPS_ID 0x73707468u
#define BLUR_ONLINE_ID 0x6E6C6E6Fu
#define BLUR_TRANSIENT_HEADER_SIZE 0x2Cu
#define BLUR_ONLINE_API_VERSION 4u
#define BLUR_HTTPS_HOST_API_VERSION 1u
#define BLUR_HTTPS_API_VERSION 2u
#define BLUR_TRANSIENT_LOW 0x10000000u
#define BLUR_TRANSIENT_HIGH 0x14000000u
#define BLUR_TRANSIENT_MAX_FILE_SIZE 0x20000u
#define BLUR_TRANSIENT_MAX_RUNTIME_SIZE 0x30000u
#define BLUR_ONLINE_BORDER_COLOR RGB8_to_565(65, 105, 225)
#define BLUR_ONLINE_TITLE_COLOR COLOR_CYAN

#pragma pack(push, 1)
typedef struct
{
    u32 magic;
    u32 plgid;
    u32 codeSize;
    u32 dataSize;
    u32 bssSize;
    u32 fastRelocSize;
    u32 repairSize;
    u32 ownAbiLo;
    u32 ownAbiHi;
    u32 expectedEnvLo;
    u32 expectedEnvHi;
} BlurTransientHeader;
#pragma pack(pop)

typedef struct
{
    u32 version;
    Result (*FSUSER_OpenArchive)(FS_Archive *, FS_ArchiveID, FS_Path);
    Result (*FSUSER_CloseArchive)(FS_Archive);
    Result (*FSUSER_OpenFile)(Handle *, FS_Archive, FS_Path, u32, u32);
    Result (*FSFILE_Write)(Handle, u32 *, u64, const void *, u32, u32);
    Result (*FSFILE_SetSize)(Handle, u64);
    Result (*FSFILE_Close)(Handle);
    Result (*FSUSER_DeleteFile)(FS_Archive, FS_Path);
    FS_Path (*fsMakePath)(FS_PathType, const void *);
    Result (*svcControlMemoryUnsafe)(u32 *, u32, u32, MemOp, MemPerm);
    Result (*svcQueryMemory)(MemInfo *, PageInfo *, u32);
    Result (*srvGetServiceHandle)(Handle *, const char *);
    Result (*miniSocInit)(void);
    Result (*miniSocExit)(void);
    int (*socSocket)(int, int, int);
    int (*socConnect)(int, const struct sockaddr *, socklen_t);
    int (*socPoll)(struct pollfd *, nfds_t, int);
    ssize_t (*socSendto)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
    ssize_t (*socRecvfrom)(int, void *, size_t, int, struct sockaddr *, socklen_t *);
    int (*socClose)(int);
} BlurHttpsHostApi;

typedef struct
{
    u32 version;
    Result (*downloadToFile)(const char *url, const char *path, u32 maxSize);
    Result (*downloadToMemory)(const char *url, void *buffer, u32 bufferSize, u32 *actualSize);
} BlurHttpsApi;

#define BLUR_ONLINE_DIR_NAME_CAP 256u
#define BLUR_ONLINE_DIR_NAME_COMPLETE 1u

typedef struct
{
    char name[BLUR_ONLINE_DIR_NAME_CAP];
    u64 fileSize;
    u32 attributes;
    u32 flags;
} BlurOnlineDirEntry;

typedef Result (*BlurOnlineDirVisitor)(
    const BlurOnlineDirEntry *entry,
    void *context,
    bool *stop
);

typedef struct
{
    u32 version;
    void (*drawLock)(void);
    void (*drawUnlock)(void);
    void (*drawClear)(void);
    u32 (*drawString)(u32, u32, u32, const char *);
    void (*drawFlush)(void);
    u32 (*waitInputWithTimeout)(s32);
    volatile bool *menuShouldExit;
    Result (*downloadToFile)(const char *url, const char *path, u32 maxSize);
    Result (*downloadToMemory)(const char *url, void *buffer, u32 bufferSize, u32 *actualSize);
    Result (*getFileSize)(const char *path, u32 *size);
    Result (*readFile)(const char *path, u32 offset, void *buffer, u32 size, u32 *actualRead);
    Result (*writeFile)(const char *path, u32 offset, const void *buffer, u32 size, u32 *actualWritten);
    Result (*setFileSize)(const char *path, u32 size);
    Result (*deleteFile)(const char *path);
    Result (*renameFile)(const char *oldPath, const char *newPath);
    Result (*fileExists)(const char *path, bool *exists);
    Result (*enumerateDirectory)(
        const char *path,
        BlurOnlineDirVisitor visitor,
        void *context,
        u32 *entriesVisited
    );
} BlurOnlineApi;

typedef struct
{
    u32 base;
    u32 codeSize;
    u32 totalSize;
    bool codeProtected;
} BlurTransientImage;

PLUGIN_DATA(blur) static void *pluginTable_blur_online[] = {
    (void *)FSUSER_OpenArchive,
    (void *)FSUSER_CloseArchive,
    (void *)FSUSER_OpenFile,
    (void *)FSFILE_GetSize,
    (void *)FSFILE_Read,
    (void *)FSFILE_Close,
    (void *)fsMakePath,
    (void *)svcControlMemoryUnsafe,
    (void *)svcQueryMemory,
    (void *)svcFlushEntireDataCache,
    (void *)svcInvalidateEntireInstructionCache,
    (void *)Draw_Lock,
    (void *)Draw_Unlock,
    (void *)Draw_ClearFramebuffer,
    (void *)Draw_DrawString,
    (void *)Draw_FlushFramebuffer,
    (void *)waitInputWithTimeout,
    (void *)&menuShouldExit,
    (void *)FSFILE_Write,
    (void *)FSFILE_SetSize,
    (void *)FSUSER_DeleteFile,
    (void *)srvGetServiceHandle,
    (void *)miniSocInit,
    (void *)miniSocExit,
    (void *)socSocket,
    (void *)socConnect,
    (void *)socPoll,
    (void *)socSendto,
    (void *)socRecvfrom,
    (void *)socClose,
    (void *)FSUSER_RenameFile,
    (void *)FSUSER_OpenDirectory,
    (void *)FSDIR_Read,
    (void *)FSDIR_Close,
};

#define ONLINE_HOST__FSUSER_OpenArchive ((Result (*)(FS_Archive *, FS_ArchiveID, FS_Path))pluginTable_blur_online[0])
#define ONLINE_HOST__FSUSER_CloseArchive ((Result (*)(FS_Archive))pluginTable_blur_online[1])
#define ONLINE_HOST__FSUSER_OpenFile ((Result (*)(Handle *, FS_Archive, FS_Path, u32, u32))pluginTable_blur_online[2])
#define ONLINE_HOST__FSFILE_GetSize ((Result (*)(Handle, u64 *))pluginTable_blur_online[3])
#define ONLINE_HOST__FSFILE_Read ((Result (*)(Handle, u32 *, u64, void *, u32))pluginTable_blur_online[4])
#define ONLINE_HOST__FSFILE_Close ((Result (*)(Handle))pluginTable_blur_online[5])
#define ONLINE_HOST__fsMakePath ((FS_Path (*)(FS_PathType, const void *))pluginTable_blur_online[6])
#define ONLINE_HOST__svcControlMemoryUnsafe ((Result (*)(u32 *, u32, u32, MemOp, MemPerm))pluginTable_blur_online[7])
#define ONLINE_HOST__svcQueryMemory ((Result (*)(MemInfo *, PageInfo *, u32))pluginTable_blur_online[8])
#define ONLINE_HOST__svcFlushEntireDataCache ((void (*)(void))pluginTable_blur_online[9])
#define ONLINE_HOST__svcInvalidateEntireInstructionCache ((void (*)(void))pluginTable_blur_online[10])
#define ONLINE_HOST__Draw_Lock ((void (*)(void))pluginTable_blur_online[11])
#define ONLINE_HOST__Draw_Unlock ((void (*)(void))pluginTable_blur_online[12])
#define ONLINE_HOST__Draw_ClearFramebuffer ((void (*)(void))pluginTable_blur_online[13])
#define ONLINE_HOST__Draw_DrawString ((u32 (*)(u32, u32, u32, const char *))pluginTable_blur_online[14])
#define ONLINE_HOST__Draw_FlushFramebuffer ((void (*)(void))pluginTable_blur_online[15])
#define ONLINE_HOST__waitInputWithTimeout ((u32 (*)(s32))pluginTable_blur_online[16])
#define ONLINE_HOST__menuShouldExit (*(volatile bool *)pluginTable_blur_online[17])
#define ONLINE_HOST__FSFILE_Write ((Result (*)(Handle, u32 *, u64, const void *, u32, u32))pluginTable_blur_online[18])
#define ONLINE_HOST__FSFILE_SetSize ((Result (*)(Handle, u64))pluginTable_blur_online[19])
#define ONLINE_HOST__FSUSER_DeleteFile ((Result (*)(FS_Archive, FS_Path))pluginTable_blur_online[20])
#define ONLINE_HOST__srvGetServiceHandle ((Result (*)(Handle *, const char *))pluginTable_blur_online[21])
#define ONLINE_HOST__miniSocInit ((Result (*)(void))pluginTable_blur_online[22])
#define ONLINE_HOST__miniSocExit ((Result (*)(void))pluginTable_blur_online[23])
#define ONLINE_HOST__socSocket ((int (*)(int, int, int))pluginTable_blur_online[24])
#define ONLINE_HOST__socConnect ((int (*)(int, const struct sockaddr *, socklen_t))pluginTable_blur_online[25])
#define ONLINE_HOST__socPoll ((int (*)(struct pollfd *, nfds_t, int))pluginTable_blur_online[26])
#define ONLINE_HOST__socSendto ((ssize_t (*)(int, const void *, size_t, int, const struct sockaddr *, socklen_t))pluginTable_blur_online[27])
#define ONLINE_HOST__socRecvfrom ((ssize_t (*)(int, void *, size_t, int, struct sockaddr *, socklen_t *))pluginTable_blur_online[28])
#define ONLINE_HOST__socClose ((int (*)(int))pluginTable_blur_online[29])
#define ONLINE_HOST__FSUSER_RenameFile ((Result (*)(FS_Archive, FS_Path, FS_Archive, FS_Path))pluginTable_blur_online[30])
#define ONLINE_HOST__FSUSER_OpenDirectory ((Result (*)(Handle *, FS_Archive, FS_Path))pluginTable_blur_online[31])
#define ONLINE_HOST__FSDIR_Read ((Result (*)(Handle, u32 *, u32, FS_DirectoryEntry *))pluginTable_blur_online[32])
#define ONLINE_HOST__FSDIR_Close ((Result (*)(Handle))pluginTable_blur_online[33])

PLUGIN_RODATA(blur) static const char g_blurHttpsPath[] = "/luma/plugins/httpslib.3on";
PLUGIN_RODATA(blur) static const char g_blurOnlineFilename[] = "onlineblurrotemporary.3on";
PLUGIN_RODATA(blur) static const char g_blurOnlinePathPrefix[] = "/luma/plugins/";
PLUGIN_RODATA(blur) static const char g_blurOnlineUrlPrefix[] = "https://blurro.github.io/sysplugins/online_v1/";
PLUGIN_RODATA(blur) static const char g_blurOnlineEmptyPath[] = "";
PLUGIN_RODATA(blur) static const char g_blurOnlineErrorTitle[] = "Online Menu";
PLUGIN_RODATA(blur) static const char g_blurOnlineErrorText[] = "Could not load Online Menu";
PLUGIN_RODATA(blur) static const char g_blurOnlineWaitingText[] = "Waiting...";
PLUGIN_RODATA(blur) static const char g_blurOnlineBackText[] = "press B to go back";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageBuildPath[] = "stage: build online path";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageOpenHttps[] = "stage: open httpslib.3on";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageInitHttps[] = "stage: init httpslib.3on";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageDownload[] = "stage: HTTPS download";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageDelete[] = "stage: delete temp 3on";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageOpenArchive[] = "stage: open SD archive";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageOpenFile[] = "stage: open 3on file";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageValidate[] = "stage: validate 3on";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageFindMemory[] = "stage: find free memory";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageAllocate[] = "stage: allocate memory";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageRead[] = "stage: read payload";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageRelocate[] = "stage: self relocate";
PLUGIN_RODATA(blur) static const char g_blurOnlineStageProtect[] = "stage: protect RX";
PLUGIN_RODATA(blur) static const char g_blurOnlineResultPrefix[] = "result: 0x";
PLUGIN_RODATA(blur) static const char g_blurOnlineHexDigits[] = "0123456789ABCDEF";
PLUGIN_RODATA(blur) static const char g_blurOnlinePlus[] = "+";
PLUGIN_RODATA(blur) static const char g_blurOnlinePipe[] = "|";
PLUGIN_RODATA(blur) static const char g_blurOnlineRail[] = "----------------------";
PLUGIN_DATA(blur) static const char *g_blurOnlineLastStage = g_blurOnlineStageValidate;
PLUGIN_DATA(blur) static Result g_blurOnlineLastResult = 0;
PLUGIN_DATA(blur) static Result (*g_blurHttpsDownload)(const char *, const char *, u32) = NULL;
PLUGIN_DATA(blur) static Result (*g_blurHttpsDownloadMemory)(const char *, void *, u32, u32 *) = NULL;
PLUGIN_BSS(blur) static BlurHttpsHostApi g_blurHttpsHostApi;
PLUGIN_BSS(blur) static char g_blurOnlineResultHex[9];
PLUGIN_BSS(blur) static char g_blurOnlinePath[64];
PLUGIN_BSS(blur) static char g_blurOnlineUrl[128];

extern Result PLUGIN_blur_OnlineSvcGetProcessId(u32 *processId, Handle process);
extern Result PLUGIN_blur_OnlineSvcOpenProcess(Handle *process, u32 processId);
extern Result PLUGIN_blur_OnlineSvcControlProcessMemory(
    Handle process,
    u32 addr0,
    u32 addr1,
    u32 size,
    u32 operation,
    u32 permission
);
extern Result PLUGIN_blur_OnlineSvcCloseHandle(Handle handle);

__asm__(
    ".arm\n"
    ".section .plugin_blur, \"ax\", %progbits\n"
    ".balign 4\n"
    ".global PLUGIN_blur_OnlineSvcGetProcessId\n"
    ".type PLUGIN_blur_OnlineSvcGetProcessId, %function\n"
    "PLUGIN_blur_OnlineSvcGetProcessId:\n"
    "push {r0}\n"
    "mov r0, r1\n"
    "svc 0x35\n"
    "pop {r2}\n"
    "str r1, [r2]\n"
    "bx lr\n"
    ".global PLUGIN_blur_OnlineSvcOpenProcess\n"
    ".type PLUGIN_blur_OnlineSvcOpenProcess, %function\n"
    "PLUGIN_blur_OnlineSvcOpenProcess:\n"
    "push {r0}\n"
    "mov r0, r1\n"
    "svc 0x33\n"
    "pop {r2}\n"
    "str r1, [r2]\n"
    "bx lr\n"
    ".global PLUGIN_blur_OnlineSvcControlProcessMemory\n"
    ".type PLUGIN_blur_OnlineSvcControlProcessMemory, %function\n"
    "PLUGIN_blur_OnlineSvcControlProcessMemory:\n"
    "push {r4, r5}\n"
    "ldr r4, [sp, #8]\n"
    "ldr r5, [sp, #12]\n"
    "svc 0x70\n"
    "pop {r4, r5}\n"
    "bx lr\n"
    ".global PLUGIN_blur_OnlineSvcCloseHandle\n"
    ".type PLUGIN_blur_OnlineSvcCloseHandle, %function\n"
    "PLUGIN_blur_OnlineSvcCloseHandle:\n"
    "svc 0x23\n"
    "bx lr\n"
);

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineProtect(u32 address, u32 size, MemPerm permission)
{
    u32 processId = 0;
    Handle process = 0;
    Result result = PLUGIN_blur_OnlineSvcGetProcessId(&processId, CUR_PROCESS_HANDLE);

    if (R_FAILED(result))
        return result;

    result = PLUGIN_blur_OnlineSvcOpenProcess(&process, processId);
    if (R_FAILED(result))
        return result;

    result = PLUGIN_blur_OnlineSvcControlProcessMemory(
        process,
        address,
        0,
        size,
        MEMOP_PROT,
        permission
    );
    (void)PLUGIN_blur_OnlineSvcCloseHandle(process);
    return result;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineJoin(
    char *out,
    u32 outSize,
    const char *prefix,
    const char *filename
)
{
    u32 pos = 0;

    while (*(volatile const char *)prefix)
    {
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = *(volatile const char *)prefix++;
    }
    while (*(volatile const char *)filename)
    {
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = *(volatile const char *)filename++;
    }
    out[pos] = 0;
    return true;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineAdd32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;
    if (value < a)
        return false;
    *out = value;
    return true;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineMul32(u32 a, u32 b, u32 *out)
{
    if (a && b > 0xFFFFFFFFu / a)
        return false;
    *out = a * b;
    return true;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineAlignPage(u32 value, u32 *out)
{
    if (value > 0xFFFFF000u)
        return false;
    *out = (value + 0xFFFu) & ~0xFFFu;
    return true;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineReadExact(
    Handle file,
    u32 offset,
    void *buffer,
    u32 size
)
{
    u32 read = 0;
    return R_SUCCEEDED(ONLINE_HOST__FSFILE_Read(file, &read, offset, buffer, size)) && read == size;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineFindFreeRange(u32 size, u32 *outBase)
{
    u32 scan = BLUR_TRANSIENT_LOW;

    while (scan < BLUR_TRANSIENT_HIGH)
    {
        MemInfo info;
        PageInfo page;
        u32 next;

        if (R_FAILED(ONLINE_HOST__svcQueryMemory(&info, &page, scan)))
            return false;

        if (info.state == MEMSTATE_FREE)
        {
            u32 base = info.base_addr;
            u32 end;
            if (base < BLUR_TRANSIENT_LOW)
                base = BLUR_TRANSIENT_LOW;
            if (base <= 0xFFFFFFFFu - size &&
                PLUGIN_blur_OnlineAdd32(base, size, &end) &&
                end <= BLUR_TRANSIENT_HIGH &&
                end <= info.base_addr + info.size)
            {
                *outBase = base;
                return true;
            }
        }

        next = info.base_addr + info.size;
        if (next <= scan)
            return false;
        scan = next;
    }
    return false;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineValidateRepair(
    Handle file,
    u32 repairOffset,
    u32 repairSize
)
{
    u32 exportCount;
    u32 exportBytes;

    if (repairSize < 4u || !PLUGIN_blur_OnlineReadExact(file, repairOffset, &exportCount, 4u))
        return false;
    if (!PLUGIN_blur_OnlineMul32(exportCount, 12u, &exportBytes))
        return false;
    return exportBytes == repairSize - 4u;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineApplyRelocs(
    Handle file,
    u32 relocOffset,
    u32 relocSize,
    u32 expectedId,
    u32 runtimeBase,
    u32 runtimeSize
)
{
    u32 cursor = 0;

    while (cursor < relocSize)
    {
        u32 group[2];
        u32 pairBytes;

        if (relocSize - cursor < 8u ||
            !PLUGIN_blur_OnlineReadExact(file, relocOffset + cursor, group, 8u) ||
            group[0] != expectedId ||
            !PLUGIN_blur_OnlineMul32(group[1], 8u, &pairBytes) ||
            pairBytes > relocSize - cursor - 8u)
        {
            return false;
        }

        cursor += 8u;
        for (u32 i = 0; i < group[1]; i++)
        {
            u32 pair[2];

            if (!PLUGIN_blur_OnlineReadExact(file, relocOffset + cursor, pair, 8u) ||
                (pair[0] & 3u) ||
                pair[0] > runtimeSize - 4u ||
                pair[1] >= runtimeSize)
            {
                return false;
            }

            *(volatile u32 *)(runtimeBase + pair[0]) = runtimeBase + pair[1];
            cursor += 8u;
        }
    }

    return cursor == relocSize;
}

PLUGIN_CODE(blur) static void PLUGIN_blur_OnlineFree(BlurTransientImage *image)
{
    u32 out;

    if (!image || !image->base)
        return;

    if (image->codeProtected && image->codeSize)
        (void)PLUGIN_blur_OnlineProtect(image->base, image->codeSize, MEMPERM_READWRITE);

    (void)ONLINE_HOST__svcControlMemoryUnsafe(
        &out,
        image->base,
        image->totalSize,
        MEMOP_FREE | MEMOP_REGION_SYSTEM,
        MEMPERM_DONTCARE
    );
    image->base = 0;
    image->codeSize = 0;
    image->totalSize = 0;
    image->codeProtected = false;
}

PLUGIN_CODE(blur) static void PLUGIN_blur_OnlineSetFailure(const char *stage, Result result)
{
    u32 value = (u32)result;

    g_blurOnlineLastStage = stage;
    g_blurOnlineLastResult = result;
    for (u32 i = 0; i < 8; i++)
        g_blurOnlineResultHex[i] = g_blurOnlineHexDigits[(value >> ((7u - i) * 4u)) & 0xFu];
    g_blurOnlineResultHex[8] = 0;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_LoadTransient(
    const char *path,
    u32 expectedId,
    const char *openStage,
    BlurTransientImage *image
)
{
    FS_Archive archive = 0;
    Handle file = 0;
    BlurTransientHeader header;
    u64 fileSize64 = 0;
    u32 fileEnd;
    u32 codeOffset;
    u32 dataOffset;
    u32 repairOffset;
    u32 codeSize;
    u32 dataAndBssSize;
    u32 dataSize;
    u32 totalSize;
    u32 base = 0;
    u32 allocated = 0;
    u32 dataAddress;
    bool archiveOpen = false;
    bool fileOpen = false;
    bool memoryAllocated = false;

    if (!image)
        return false;
    image->base = 0;
    image->codeSize = 0;
    image->totalSize = 0;
    image->codeProtected = false;

    {
        Result result = ONLINE_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            ONLINE_HOST__fsMakePath(PATH_EMPTY, g_blurOnlineEmptyPath));
        if (R_FAILED(result))
        {
            PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageOpenArchive, result);
            goto fail;
        }
    }
    archiveOpen = true;

    {
        Result result = ONLINE_HOST__FSUSER_OpenFile(
            &file,
            archive,
            ONLINE_HOST__fsMakePath(PATH_ASCII, path),
            FS_OPEN_READ,
            0);
        if (R_FAILED(result))
        {
            PLUGIN_blur_OnlineSetFailure(openStage, result);
            goto fail;
        }
    }
    fileOpen = true;

    if (R_FAILED(ONLINE_HOST__FSFILE_GetSize(file, &fileSize64)) ||
        fileSize64 < BLUR_TRANSIENT_HEADER_SIZE ||
        fileSize64 > BLUR_TRANSIENT_MAX_FILE_SIZE ||
        !PLUGIN_blur_OnlineReadExact(file, 0, &header, sizeof(header)) ||
        header.magic != BLUR_TRANSIENT_MAGIC ||
        header.plgid != expectedId ||
        !header.codeSize ||
        !PLUGIN_blur_OnlineAdd32(BLUR_TRANSIENT_HEADER_SIZE, header.fastRelocSize, &codeOffset) ||
        !PLUGIN_blur_OnlineAdd32(codeOffset, header.codeSize, &dataOffset) ||
        !PLUGIN_blur_OnlineAdd32(dataOffset, header.dataSize, &repairOffset) ||
        !PLUGIN_blur_OnlineAdd32(repairOffset, header.repairSize, &fileEnd) ||
        fileEnd > (u32)fileSize64 ||
        !PLUGIN_blur_OnlineAlignPage(header.codeSize, &codeSize) ||
        !PLUGIN_blur_OnlineAdd32(header.dataSize, header.bssSize, &dataAndBssSize) ||
        !PLUGIN_blur_OnlineAlignPage(dataAndBssSize, &dataSize) ||
        !PLUGIN_blur_OnlineAdd32(codeSize, dataSize, &totalSize) ||
        !totalSize ||
        totalSize > BLUR_TRANSIENT_MAX_RUNTIME_SIZE ||
        !PLUGIN_blur_OnlineValidateRepair(file, repairOffset, header.repairSize))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageValidate, (Result)0xD8A0A046u);
        goto fail;
    }

    if (!PLUGIN_blur_OnlineFindFreeRange(totalSize, &base))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageFindMemory, (Result)0xD8A0A047u);
        goto fail;
    }

    {
        Result result = ONLINE_HOST__svcControlMemoryUnsafe(
            &allocated,
            base,
            totalSize,
            MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
            MEMPERM_READWRITE);
        if (R_FAILED(result) || !allocated)
        {
            PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageAllocate, R_FAILED(result) ? result : (Result)0xD8A0A047u);
            goto fail;
        }
    }
    base = allocated;
    memoryAllocated = true;
    dataAddress = base + codeSize;

    if (!PLUGIN_blur_OnlineReadExact(file, codeOffset, (void *)base, header.codeSize) ||
        (header.dataSize && !PLUGIN_blur_OnlineReadExact(file, dataOffset, (void *)dataAddress, header.dataSize)))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageRead, (Result)0xD8A0A048u);
        goto fail;
    }

    for (u32 i = 0; i < header.bssSize; i++)
        *(volatile u8 *)(dataAddress + header.dataSize + i) = 0;

    if (!PLUGIN_blur_OnlineApplyRelocs(
            file,
            BLUR_TRANSIENT_HEADER_SIZE,
            header.fastRelocSize,
            expectedId,
            base,
            totalSize))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageRelocate, (Result)0xD8A0A049u);
        goto fail;
    }

    ONLINE_HOST__FSFILE_Close(file);
    file = 0;
    fileOpen = false;
    ONLINE_HOST__FSUSER_CloseArchive(archive);
    archive = 0;
    archiveOpen = false;

    ONLINE_HOST__svcFlushEntireDataCache();
    {
        Result result = PLUGIN_blur_OnlineProtect(base, codeSize, MEMPERM_READEXECUTE);
        if (R_FAILED(result))
        {
            PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageProtect, result);
            goto fail;
        }
    }
    ONLINE_HOST__svcInvalidateEntireInstructionCache();

    image->base = base;
    image->codeSize = codeSize;
    image->totalSize = totalSize;
    image->codeProtected = true;
    return true;

fail:
    if (fileOpen)
        ONLINE_HOST__FSFILE_Close(file);
    if (archiveOpen)
        ONLINE_HOST__FSUSER_CloseArchive(archive);
    if (memoryAllocated)
    {
        BlurTransientImage temporary = { base, codeSize, totalSize, false };
        PLUGIN_blur_OnlineFree(&temporary);
    }
    return false;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineDownloadToFile(
    const char *url,
    const char *path,
    u32 maxSize
)
{
    if (!g_blurHttpsDownload)
        return (Result)0xD8A0A068u;
    return g_blurHttpsDownload(url, path, maxSize);
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineDownloadToMemory(
    const char *url,
    void *buffer,
    u32 bufferSize,
    u32 *actualSize
)
{
    if (!g_blurHttpsDownloadMemory)
        return (Result)0xD8A0A068u;
    return g_blurHttpsDownloadMemory(url, buffer, bufferSize, actualSize);
}

#define BLUR_ONLINE_FS_MAX_PATH 271u
#define BLUR_ONLINE_FS_BAD_ARG ((Result)0xD8A0A070u)
#define BLUR_ONLINE_FS_TOO_LARGE ((Result)0xD8A0A071u)

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineValidFsPath(const char *path)
{
    if (!path || path[0] != '/')
        return false;

    for (u32 i = 1; i <= BLUR_ONLINE_FS_MAX_PATH; i++)
    {
        if (!path[i])
            return true;
    }
    return false;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineOpenSdArchive(FS_Archive *archive)
{
    if (!archive)
        return BLUR_ONLINE_FS_BAD_ARG;
    *archive = 0;
    return ONLINE_HOST__FSUSER_OpenArchive(
        archive,
        ARCHIVE_SDMC,
        ONLINE_HOST__fsMakePath(PATH_EMPTY, g_blurOnlineEmptyPath));
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineGetFileSize(const char *path, u32 *size)
{
    FS_Archive archive = 0;
    Handle file = 0;
    u64 fileSize = 0;
    Result result;

    if (!size || !PLUGIN_blur_OnlineValidFsPath(path))
        return BLUR_ONLINE_FS_BAD_ARG;
    *size = 0;

    result = PLUGIN_blur_OnlineOpenSdArchive(&archive);
    if (R_FAILED(result))
        return result;

    result = ONLINE_HOST__FSUSER_OpenFile(
        &file,
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, path),
        FS_OPEN_READ,
        0);
    if (R_SUCCEEDED(result))
    {
        result = ONLINE_HOST__FSFILE_GetSize(file, &fileSize);
        ONLINE_HOST__FSFILE_Close(file);
        if (R_SUCCEEDED(result))
        {
            if (fileSize > 0xFFFFFFFFu)
                result = BLUR_ONLINE_FS_TOO_LARGE;
            else
                *size = (u32)fileSize;
        }
    }

    ONLINE_HOST__FSUSER_CloseArchive(archive);
    return result;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineReadFile(
    const char *path,
    u32 offset,
    void *buffer,
    u32 size,
    u32 *actualRead
)
{
    FS_Archive archive = 0;
    Handle file = 0;
    Result result;

    if (!actualRead || (!buffer && size) || !PLUGIN_blur_OnlineValidFsPath(path))
        return BLUR_ONLINE_FS_BAD_ARG;
    if (size > 0xFFFFFFFFu - offset)
        return BLUR_ONLINE_FS_TOO_LARGE;
    *actualRead = 0;
    if (!size)
        return 0;

    result = PLUGIN_blur_OnlineOpenSdArchive(&archive);
    if (R_FAILED(result))
        return result;

    result = ONLINE_HOST__FSUSER_OpenFile(
        &file,
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, path),
        FS_OPEN_READ,
        0);
    if (R_SUCCEEDED(result))
    {
        result = ONLINE_HOST__FSFILE_Read(file, actualRead, offset, buffer, size);
        ONLINE_HOST__FSFILE_Close(file);
    }

    ONLINE_HOST__FSUSER_CloseArchive(archive);
    return result;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineWriteFile(
    const char *path,
    u32 offset,
    const void *buffer,
    u32 size,
    u32 *actualWritten
)
{
    FS_Archive archive = 0;
    Handle file = 0;
    Result result;

    if (!actualWritten || (!buffer && size) || !PLUGIN_blur_OnlineValidFsPath(path))
        return BLUR_ONLINE_FS_BAD_ARG;
    if (size > 0xFFFFFFFFu - offset)
        return BLUR_ONLINE_FS_TOO_LARGE;
    *actualWritten = 0;
    if (!size)
        return 0;

    result = PLUGIN_blur_OnlineOpenSdArchive(&archive);
    if (R_FAILED(result))
        return result;

    result = ONLINE_HOST__FSUSER_OpenFile(
        &file,
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, path),
        FS_OPEN_WRITE | FS_OPEN_CREATE,
        0);
    if (R_SUCCEEDED(result))
    {
        result = ONLINE_HOST__FSFILE_Write(file, actualWritten, offset, buffer, size, FS_WRITE_FLUSH);
        ONLINE_HOST__FSFILE_Close(file);
    }

    ONLINE_HOST__FSUSER_CloseArchive(archive);
    return result;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineSetFileSize(const char *path, u32 size)
{
    FS_Archive archive = 0;
    Handle file = 0;
    Result result;

    if (!PLUGIN_blur_OnlineValidFsPath(path))
        return BLUR_ONLINE_FS_BAD_ARG;

    result = PLUGIN_blur_OnlineOpenSdArchive(&archive);
    if (R_FAILED(result))
        return result;

    result = ONLINE_HOST__FSUSER_OpenFile(
        &file,
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, path),
        FS_OPEN_WRITE | FS_OPEN_CREATE,
        0);
    if (R_SUCCEEDED(result))
    {
        result = ONLINE_HOST__FSFILE_SetSize(file, size);
        ONLINE_HOST__FSFILE_Close(file);
    }

    ONLINE_HOST__FSUSER_CloseArchive(archive);
    return result;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineDeleteFile(const char *path)
{
    FS_Archive archive = 0;
    Result result;

    if (!PLUGIN_blur_OnlineValidFsPath(path))
        return BLUR_ONLINE_FS_BAD_ARG;

    result = PLUGIN_blur_OnlineOpenSdArchive(&archive);
    if (R_FAILED(result))
        return result;

    result = ONLINE_HOST__FSUSER_DeleteFile(
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, path));
    ONLINE_HOST__FSUSER_CloseArchive(archive);
    return result;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineRenameFile(const char *oldPath, const char *newPath)
{
    FS_Archive archive = 0;
    Result result;

    if (!PLUGIN_blur_OnlineValidFsPath(oldPath) || !PLUGIN_blur_OnlineValidFsPath(newPath))
        return BLUR_ONLINE_FS_BAD_ARG;

    result = PLUGIN_blur_OnlineOpenSdArchive(&archive);
    if (R_FAILED(result))
        return result;

    result = ONLINE_HOST__FSUSER_RenameFile(
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, oldPath),
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, newPath));
    ONLINE_HOST__FSUSER_CloseArchive(archive);
    return result;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineFileExists(const char *path, bool *exists)
{
    FS_Archive archive = 0;
    Handle file = 0;
    Result result;

    if (!exists || !PLUGIN_blur_OnlineValidFsPath(path))
        return BLUR_ONLINE_FS_BAD_ARG;
    *exists = false;

    result = PLUGIN_blur_OnlineOpenSdArchive(&archive);
    if (R_FAILED(result))
        return result;

    result = ONLINE_HOST__FSUSER_OpenFile(
        &file,
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, path),
        FS_OPEN_READ,
        0);
    if (R_SUCCEEDED(result))
    {
        *exists = true;
        ONLINE_HOST__FSFILE_Close(file);
        result = 0;
    }
    else if (R_DESCRIPTION(result) == RD_NOT_FOUND)
    {
        result = 0;
    }

    ONLINE_HOST__FSUSER_CloseArchive(archive);
    return result;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineAppendUtf8(
    char *output,
    u32 capacity,
    u32 *length,
    u32 codepoint
)
{
    u8 bytes[4];
    u32 count;

    if (codepoint <= 0x7Fu)
    {
        bytes[0] = (u8)codepoint;
        count = 1;
    }
    else if (codepoint <= 0x7FFu)
    {
        bytes[0] = (u8)(0xC0u | (codepoint >> 6));
        bytes[1] = (u8)(0x80u | (codepoint & 0x3Fu));
        count = 2;
    }
    else if (codepoint <= 0xFFFFu)
    {
        bytes[0] = (u8)(0xE0u | (codepoint >> 12));
        bytes[1] = (u8)(0x80u | ((codepoint >> 6) & 0x3Fu));
        bytes[2] = (u8)(0x80u | (codepoint & 0x3Fu));
        count = 3;
    }
    else
    {
        bytes[0] = (u8)(0xF0u | (codepoint >> 18));
        bytes[1] = (u8)(0x80u | ((codepoint >> 12) & 0x3Fu));
        bytes[2] = (u8)(0x80u | ((codepoint >> 6) & 0x3Fu));
        bytes[3] = (u8)(0x80u | (codepoint & 0x3Fu));
        count = 4;
    }

    if (!output || !length || !capacity || *length + count >= capacity)
        return false;

    for (u32 i = 0; i < count; i++)
        output[(*length)++] = (char)bytes[i];
    output[*length] = 0;
    return true;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineDirectoryNameToUtf8(
    char *output,
    u32 capacity,
    const u16 *input
)
{
    u32 length = 0;

    if (!output || !capacity || !input)
        return false;
    output[0] = 0;

    for (u32 i = 0; i < 0x106u; i++)
    {
        u32 codepoint = input[i];
        if (!codepoint)
            return true;

        if (codepoint >= 0xD800u && codepoint <= 0xDBFFu)
        {
            u32 low = i + 1u < 0x106u ? input[i + 1u] : 0;
            if (low < 0xDC00u || low > 0xDFFFu)
                codepoint = 0xFFFDu;
            else
            {
                codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
                i++;
            }
        }
        else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu)
        {
            codepoint = 0xFFFDu;
        }

        if (!PLUGIN_blur_OnlineAppendUtf8(output, capacity, &length, codepoint))
            return false;
    }

    return false;
}

PLUGIN_CODE(blur) static Result PLUGIN_blur_OnlineEnumerateDirectory(
    const char *path,
    BlurOnlineDirVisitor visitor,
    void *context,
    u32 *entriesVisited
)
{
    FS_Archive archive = 0;
    Handle directory = 0;
    FS_DirectoryEntry rawEntry;
    BlurOnlineDirEntry entry;
    Result result;
    bool directoryOpen = false;

    if (!visitor || !PLUGIN_blur_OnlineValidFsPath(path))
        return BLUR_ONLINE_FS_BAD_ARG;
    if (entriesVisited)
        *entriesVisited = 0;

    result = PLUGIN_blur_OnlineOpenSdArchive(&archive);
    if (R_FAILED(result))
        return result;

    result = ONLINE_HOST__FSUSER_OpenDirectory(
        &directory,
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, path));
    if (R_FAILED(result))
        goto finish;
    directoryOpen = true;

    for (;;)
    {
        u32 count = 0;
        bool stop = false;

        result = ONLINE_HOST__FSDIR_Read(directory, &count, 1, &rawEntry);
        if (R_FAILED(result) || !count)
            break;

        entry.fileSize = rawEntry.fileSize;
        entry.attributes = rawEntry.attributes;
        entry.flags = 0;
        if (PLUGIN_blur_OnlineDirectoryNameToUtf8(entry.name, sizeof(entry.name), rawEntry.name))
            entry.flags |= BLUR_ONLINE_DIR_NAME_COMPLETE;

        if (entriesVisited)
            (*entriesVisited)++;
        result = visitor(&entry, context, &stop);
        if (R_FAILED(result) || stop)
            break;
    }

finish:
    if (directoryOpen)
    {
        Result closeResult = ONLINE_HOST__FSDIR_Close(directory);
        if (R_SUCCEEDED(result) && R_FAILED(closeResult))
            result = closeResult;
    }
    {
        Result closeResult = ONLINE_HOST__FSUSER_CloseArchive(archive);
        if (R_SUCCEEDED(result) && R_FAILED(closeResult))
            result = closeResult;
    }
    return result;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_LoadHttpsLibrary(BlurTransientImage *image)
{
    BlurHttpsApi httpsApi;
    bool initialized;

    if (!PLUGIN_blur_LoadTransient(g_blurHttpsPath, BLUR_HTTPS_ID, g_blurOnlineStageOpenHttps, image))
        return false;

    g_blurHttpsHostApi.version = BLUR_HTTPS_HOST_API_VERSION;
    g_blurHttpsHostApi.FSUSER_OpenArchive = ONLINE_HOST__FSUSER_OpenArchive;
    g_blurHttpsHostApi.FSUSER_CloseArchive = ONLINE_HOST__FSUSER_CloseArchive;
    g_blurHttpsHostApi.FSUSER_OpenFile = ONLINE_HOST__FSUSER_OpenFile;
    g_blurHttpsHostApi.FSFILE_Write = ONLINE_HOST__FSFILE_Write;
    g_blurHttpsHostApi.FSFILE_SetSize = ONLINE_HOST__FSFILE_SetSize;
    g_blurHttpsHostApi.FSFILE_Close = ONLINE_HOST__FSFILE_Close;
    g_blurHttpsHostApi.FSUSER_DeleteFile = ONLINE_HOST__FSUSER_DeleteFile;
    g_blurHttpsHostApi.fsMakePath = ONLINE_HOST__fsMakePath;
    g_blurHttpsHostApi.svcControlMemoryUnsafe = ONLINE_HOST__svcControlMemoryUnsafe;
    g_blurHttpsHostApi.svcQueryMemory = ONLINE_HOST__svcQueryMemory;
    g_blurHttpsHostApi.srvGetServiceHandle = ONLINE_HOST__srvGetServiceHandle;
    g_blurHttpsHostApi.miniSocInit = ONLINE_HOST__miniSocInit;
    g_blurHttpsHostApi.miniSocExit = ONLINE_HOST__miniSocExit;
    g_blurHttpsHostApi.socSocket = ONLINE_HOST__socSocket;
    g_blurHttpsHostApi.socConnect = ONLINE_HOST__socConnect;
    g_blurHttpsHostApi.socPoll = ONLINE_HOST__socPoll;
    g_blurHttpsHostApi.socSendto = ONLINE_HOST__socSendto;
    g_blurHttpsHostApi.socRecvfrom = ONLINE_HOST__socRecvfrom;
    g_blurHttpsHostApi.socClose = ONLINE_HOST__socClose;

    httpsApi.version = 0;
    httpsApi.downloadToFile = NULL;
    httpsApi.downloadToMemory = NULL;
    initialized = ((bool (*)(const BlurHttpsHostApi *, BlurHttpsApi *))image->base)(&g_blurHttpsHostApi, &httpsApi);
    if (!initialized || httpsApi.version != BLUR_HTTPS_API_VERSION ||
        !httpsApi.downloadToFile || !httpsApi.downloadToMemory)
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageInitHttps, (Result)0xD8A0A069u);
        PLUGIN_blur_OnlineFree(image);
        return false;
    }

    g_blurHttpsDownload = httpsApi.downloadToFile;
    g_blurHttpsDownloadMemory = httpsApi.downloadToMemory;
    return true;
}

PLUGIN_CODE(blur) static void PLUGIN_blur_UnloadHttpsLibrary(BlurTransientImage *image)
{
    g_blurHttpsDownload = NULL;
    g_blurHttpsDownloadMemory = NULL;
    PLUGIN_blur_OnlineFree(image);
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_DownloadOnlineMenu(void)
{
    Result result;

    if (!PLUGIN_blur_OnlineJoin(
            g_blurOnlinePath,
            sizeof(g_blurOnlinePath),
            g_blurOnlinePathPrefix,
            g_blurOnlineFilename) ||
        !PLUGIN_blur_OnlineJoin(
            g_blurOnlineUrl,
            sizeof(g_blurOnlineUrl),
            g_blurOnlineUrlPrefix,
            g_blurOnlineFilename))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageBuildPath, (Result)0xD8A0A04Bu);
        return false;
    }

    result = PLUGIN_blur_OnlineDownloadToFile(
        g_blurOnlineUrl,
        g_blurOnlinePath,
        BLUR_TRANSIENT_MAX_FILE_SIZE);
    if (R_FAILED(result))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageDownload, result);
        return false;
    }
    return true;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_DeleteOnlineTemp(void)
{
    FS_Archive archive = 0;
    Result result = ONLINE_HOST__FSUSER_OpenArchive(
        &archive,
        ARCHIVE_SDMC,
        ONLINE_HOST__fsMakePath(PATH_EMPTY, g_blurOnlineEmptyPath));

    if (R_FAILED(result))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageDelete, result);
        return false;
    }

    result = ONLINE_HOST__FSUSER_DeleteFile(
        archive,
        ONLINE_HOST__fsMakePath(PATH_ASCII, g_blurOnlinePath));
    ONLINE_HOST__FSUSER_CloseArchive(archive);

    if (R_FAILED(result))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageDelete, result);
        return false;
    }
    return true;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_RunOnlineMenu(void)
{
    BlurTransientImage image;
    BlurOnlineApi api;
    bool ok = false;

    if (!PLUGIN_blur_LoadTransient(g_blurOnlinePath, BLUR_ONLINE_ID, g_blurOnlineStageOpenFile, &image))
        return false;

    api.version = BLUR_ONLINE_API_VERSION;
    api.drawLock = ONLINE_HOST__Draw_Lock;
    api.drawUnlock = ONLINE_HOST__Draw_Unlock;
    api.drawClear = ONLINE_HOST__Draw_ClearFramebuffer;
    api.drawString = ONLINE_HOST__Draw_DrawString;
    api.drawFlush = ONLINE_HOST__Draw_FlushFramebuffer;
    api.waitInputWithTimeout = ONLINE_HOST__waitInputWithTimeout;
    api.menuShouldExit = (volatile bool *)pluginTable_blur_online[17];
    api.downloadToFile = PLUGIN_blur_OnlineDownloadToFile;
    api.downloadToMemory = PLUGIN_blur_OnlineDownloadToMemory;
    api.getFileSize = PLUGIN_blur_OnlineGetFileSize;
    api.readFile = PLUGIN_blur_OnlineReadFile;
    api.writeFile = PLUGIN_blur_OnlineWriteFile;
    api.setFileSize = PLUGIN_blur_OnlineSetFileSize;
    api.deleteFile = PLUGIN_blur_OnlineDeleteFile;
    api.renameFile = PLUGIN_blur_OnlineRenameFile;
    api.fileExists = PLUGIN_blur_OnlineFileExists;
    api.enumerateDirectory = PLUGIN_blur_OnlineEnumerateDirectory;

    ((void (*)(const BlurOnlineApi *))image.base)(&api);
    ok = true;
    PLUGIN_blur_OnlineFree(&image);
    return ok;
}

PLUGIN_CODE(blur) static void PLUGIN_blur_DrawOnlineFrame(void)
{
    ONLINE_HOST__Draw_DrawString(10, 8, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePlus);
    ONLINE_HOST__Draw_DrawString(16, 8, BLUR_ONLINE_BORDER_COLOR, g_blurOnlineRail);
    ONLINE_HOST__Draw_DrawString(148, 8, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePlus);
    ONLINE_HOST__Draw_DrawString(10, 16, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePipe);
    ONLINE_HOST__Draw_DrawString(148, 16, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePipe);
    ONLINE_HOST__Draw_DrawString(10, 24, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePlus);
    ONLINE_HOST__Draw_DrawString(16, 24, BLUR_ONLINE_BORDER_COLOR, g_blurOnlineRail);
    ONLINE_HOST__Draw_DrawString(148, 24, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePlus);
    ONLINE_HOST__Draw_DrawString(20, 16, BLUR_ONLINE_TITLE_COLOR, g_blurOnlineErrorTitle);
}

PLUGIN_CODE(blur) static void PLUGIN_blur_DrawOnlineWaiting(void)
{
    ONLINE_HOST__Draw_Lock();
    ONLINE_HOST__Draw_ClearFramebuffer();
    PLUGIN_blur_DrawOnlineFrame();
    ONLINE_HOST__Draw_DrawString(20, 55, COLOR_WHITE, g_blurOnlineWaitingText);
    ONLINE_HOST__Draw_FlushFramebuffer();
    ONLINE_HOST__Draw_Unlock();
}

PLUGIN_CODE(blur) static void PLUGIN_blur_DrawOnlineError(void)
{
    ONLINE_HOST__Draw_Lock();
    ONLINE_HOST__Draw_ClearFramebuffer();
    PLUGIN_blur_DrawOnlineFrame();
    ONLINE_HOST__Draw_DrawString(20, 45, COLOR_RED, g_blurOnlineErrorText);
    ONLINE_HOST__Draw_DrawString(20, 65, COLOR_WHITE, g_blurOnlineLastStage);
    ONLINE_HOST__Draw_DrawString(20, 85, COLOR_WHITE, g_blurOnlineResultPrefix);
    ONLINE_HOST__Draw_DrawString(80, 85, COLOR_WHITE, g_blurOnlineResultHex);
    ONLINE_HOST__Draw_DrawString(20, 120, COLOR_GRAY, g_blurOnlineBackText);
    ONLINE_HOST__Draw_FlushFramebuffer();
    ONLINE_HOST__Draw_Unlock();

    do
    {
        if (ONLINE_HOST__waitInputWithTimeout(50) & KEY_B)
            return;
    } while (!ONLINE_HOST__menuShouldExit);
}

PLUGIN_CODE(blur) void PLUGIN_blur_OpenOnlineMenu(void)
{
    BlurTransientImage httpsImage;
    bool downloaded = false;
    bool ran = false;
    bool deleted = true;

    PLUGIN_blur_DrawOnlineWaiting();

    if (!PLUGIN_blur_LoadHttpsLibrary(&httpsImage))
    {
        PLUGIN_blur_DrawOnlineError();
        return;
    }

    downloaded = PLUGIN_blur_DownloadOnlineMenu();
    if (downloaded)
        ran = PLUGIN_blur_RunOnlineMenu();
    if (downloaded)
        deleted = PLUGIN_blur_DeleteOnlineTemp();

    PLUGIN_blur_UnloadHttpsLibrary(&httpsImage);

    if (!downloaded || !ran || !deleted)
        PLUGIN_blur_DrawOnlineError();
}

PLUGIN_RODATA(onln) static const char g_onlineTitle[] = "Online Menu";
PLUGIN_RODATA(onln) static const char g_onlineUtcItem[] = "UTC Fetch Menu";
PLUGIN_RODATA(onln) static const char g_onlineUpdateItem[] = "Check Sysplugin Updates...";
PLUGIN_RODATA(onln) static const char g_onlineCursor[] = ">";
PLUGIN_RODATA(onln) static const char g_onlineUtcTitle[] = "UTC Fetch Menu";
PLUGIN_RODATA(onln) static const char g_onlinePrompt[] = "A: refresh live UTC";
PLUGIN_RODATA(onln) static const char g_onlineReady[] = "Press A to fetch current UTC";
PLUGIN_RODATA(onln) static const char g_onlineFetching[] = "Fetching live HTTPS time...";
PLUGIN_RODATA(onln) static const char g_onlineSuccess[] = "Live HTTPS response:";
PLUGIN_RODATA(onln) static const char g_onlineFailure[] = "HTTPS fetch failed";
PLUGIN_RODATA(onln) static const char g_onlineParseFailure[] = "Could not parse utc_iso";
PLUGIN_RODATA(onln) static const char g_onlineResultPrefix[] = "result: 0x";
PLUGIN_RODATA(onln) static const char g_onlineHexDigits[] = "0123456789ABCDEF";
PLUGIN_RODATA(onln) static const char g_onlineBack[] = "B: go back";
PLUGIN_RODATA(onln) static const char g_onlineTimeUrl[] = "https://utctime.app/api/now";
PLUGIN_RODATA(onln) static const char g_onlineUtcKey[] = "\"utc_iso\"";
PLUGIN_RODATA(onln) static const char g_onlinePlus[] = "+";
PLUGIN_RODATA(onln) static const char g_onlinePipe[] = "|";
PLUGIN_RODATA(onln) static const char g_onlineRail[] = "----------------------";

PLUGIN_RODATA(onln) static const char g_updateTitle[] = "Available Sysplugin Updates";
PLUGIN_RODATA(onln) static const char g_updateManifestUrl[] = "https://blurro.github.io/sysplugins/online_v1/plgupdates.txt";
PLUGIN_RODATA(onln) static const char g_updatePluginsDir[] = "/luma/plugins";
PLUGIN_RODATA(onln) static const char g_updatePluginsPrefix[] = "/luma/plugins/";
PLUGIN_RODATA(onln) static const char g_updateManifestPath[] = "/luma/plugins/.plgupdates.tmp";
PLUGIN_RODATA(onln) static const char g_updateDownloadPath[] = "/luma/plugins/.plgupdate.download";
PLUGIN_RODATA(onln) static const char g_updateRebuildPath[] = "/luma/plugins/.plgupdate.rebuild";
PLUGIN_RODATA(onln) static const char g_updateBackupPath[] = "/luma/plugins/.plgupdate.backup";
PLUGIN_RODATA(onln) static const char g_updateJournalPath[] = "/luma/plugins/.plgupdate.path";
PLUGIN_RODATA(onln) static const char g_updateScanning[] = "Scanning selected sysplugins...";
PLUGIN_RODATA(onln) static const char g_updateFetching[] = "Fetching plgupdates.txt...";
PLUGIN_RODATA(onln) static const char g_updateUpdating[] = "Installing:";
PLUGIN_RODATA(onln) static const char g_updateInstalled[] = "Update installed";
PLUGIN_RODATA(onln) static const char g_updateAllDone[] = "Install All complete";
PLUGIN_RODATA(onln) static const char g_updateScanFailed[] = "Plugin scan failed";
PLUGIN_RODATA(onln) static const char g_updateManifestFailed[] = "Manifest fetch/parse failed";
PLUGIN_RODATA(onln) static const char g_updateRecovering[] = "Checking interrupted update...";
PLUGIN_RODATA(onln) static const char g_updateRecoverFailed[] = "Recovery of old update failed";
PLUGIN_RODATA(onln) static const char g_updateUpdatedLabel[] = "Updated: ";
PLUGIN_RODATA(onln) static const char g_updateFailedLabel[] = "Failed: ";
PLUGIN_RODATA(onln) static const char g_updateRestart[] = "Reboot to load installed updates";
PLUGIN_RODATA(onln) static const char g_updatePressBack[] = "B: go back";
PLUGIN_RODATA(onln) static const char g_updateShowAll[] = "Y: Show all";
PLUGIN_RODATA(onln) static const char g_updateShowUpdates[] = "Y: Show only updates";
PLUGIN_RODATA(onln) static const char g_updateSlash[] = "/";
PLUGIN_RODATA(onln) static const char g_updateNoUpdates[] = "No updates available";
PLUGIN_RODATA(onln) static const char g_updateAll[] = "Install All";
PLUGIN_RODATA(onln) static const char g_updateLoaderName[] = "Loader";
PLUGIN_RODATA(onln) static const char g_updateRosalinaName[] = "Rosalina";
PLUGIN_RODATA(onln) static const char g_updateOpenModule[] = " (";
PLUGIN_RODATA(onln) static const char g_updateOpenLoaderModule[] = "   (";
PLUGIN_RODATA(onln) static const char g_updateCloseModule[] = "): ";
PLUGIN_RODATA(onln) static const char g_updateCloseOnly[] = ")";
PLUGIN_RODATA(onln) static const char g_updateNullVersion[] = "null";
PLUGIN_RODATA(onln) static const char g_updateVersionPrefix[] = "v";
PLUGIN_RODATA(onln) static const char g_updateArrow[] = " -> ";
PLUGIN_RODATA(onln) static const char g_updateDots[] = "...";
PLUGIN_RODATA(onln) static const char g_updateClearRow[] =
    "                                                  ";
PLUGIN_RODATA(onln) static const char g_updateWideRail[] = "------------------------------";
PLUGIN_RODATA(onln) static const u32 g_updateDecimalPowers[] = {
    1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
    10000u, 1000u, 100u, 10u, 1u
};

#define ONLN_3NX_HEADER_SIZE 0x30u
#define ONLN_VERSION_MAGIC 0x56584E33u
#define ONLN_ROSALINA_MAGIC 0x24584E33u
#define ONLN_LOADER_MAGIC 0x25584E33u
#define ONLN_MAX_SELECTED 31u
#define ONLN_UPDATE_PATH_CAP 272u
#define ONLN_UPDATE_IO_SIZE 0x1000u
#define ONLN_UPDATE_URL_CAP 2048u
#define ONLN_UPDATE_MAX_DOWNLOAD 0x20000u
#define ONLN_UPDATE_MANIFEST_MAX 0x20000u
#define ONLN_UPDATE_VISIBLE_ITEMS 14u
#define ONLN_UPDATE_ITEM_TOP_Y 45u
#define ONLN_UPDATE_ITEM_SPACING_Y 11u
#define ONLN_UPDATE_TOP_DOTS_Y 34u
#define ONLN_UPDATE_PROMPT_Y 221u
#define ONLN_UPDATE_NEWER_COLOR RGB565(20, 63, 21)
#define ONLN_UPDATE_OLDER_COLOR RGB565(31, 36, 18)
#define ONLN_UPDATE_BAD_FORMAT ((Result)0xD8A0A080u)
#define ONLN_UPDATE_BAD_FILE ((Result)0xD8A0A081u)
#define ONLN_UPDATE_COPY_FAILED ((Result)0xD8A0A083u)
#define ONLN_UPDATE_SWAP_FAILED ((Result)0xD8A0A084u)
#define ONLN_UPDATE_RECOVERY_FAILED ((Result)0xD8A0A085u)

#pragma pack(push, 1)
typedef struct
{
    u32 magic;
    u32 plgid;
    u32 codeSize;
    u32 dataSize;
    u32 bssSize;
    u32 fastRelocSize;
    u32 repairSize;
    u32 ownAbiLo;
    u32 ownAbiHi;
    u32 expectedEnvLo;
    u32 expectedEnvHi;
    u32 metadataSize;
} Onln3nxHeader;
#pragma pack(pop)

typedef struct
{
    char path[ONLN_UPDATE_PATH_CAP];
    u32 priority;
    u32 fileOffset;
    u32 entryEnd;
    u32 plgid;
    u32 magic;
    u32 localVersion;
    u32 remoteVersion;
    u32 urlOffset;
    u32 urlLength;
    bool hasLocalVersion;
    bool hasRemote;
} OnlnSelectedPlugin;

typedef struct
{
    const BlurOnlineApi *api;
} OnlnScanContext;

typedef struct
{
    const char *afterName;
    bool found;
} OnlnInstallScanContext;

PLUGIN_BSS(onln) static char g_onlineResultHex[9];
PLUGIN_BSS(onln) static char g_onlineTimeJson[512];
PLUGIN_BSS(onln) static char g_onlineUtc[25];
PLUGIN_BSS(onln) static OnlnSelectedPlugin g_updateLoader[ONLN_MAX_SELECTED];
PLUGIN_BSS(onln) static OnlnSelectedPlugin g_updateRosalina[ONLN_MAX_SELECTED];
PLUGIN_BSS(onln) static u32 g_updateLoaderCount;
PLUGIN_BSS(onln) static u32 g_updateRosalinaCount;
PLUGIN_BSS(onln) static u8 g_updateIo[ONLN_UPDATE_IO_SIZE];
PLUGIN_BSS(onln) static char g_updateUrl[ONLN_UPDATE_URL_CAP];
PLUGIN_BSS(onln) static char g_updateNumber[4];
PLUGIN_BSS(onln) static char g_updateId[5];
PLUGIN_BSS(onln) static char g_updateJournalRead[ONLN_UPDATE_PATH_CAP];
PLUGIN_BSS(onln) static char g_updateInstallNextName[BLUR_ONLINE_DIR_NAME_CAP];
PLUGIN_BSS(onln) static char g_updateInstallAfterName[BLUR_ONLINE_DIR_NAME_CAP];

PLUGIN_CODE(onln) static void PLUGIN_onln_FormatResult(Result result)
{
    u32 value = (u32)result;
    for (u32 i = 0; i < 8; i++)
        g_onlineResultHex[i] = g_onlineHexDigits[(value >> ((7u - i) * 4u)) & 0xFu];
    g_onlineResultHex[8] = 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParseUtc(const char *json, u32 size)
{
    u32 keySize = 9u;

    for (u32 i = 0; i + keySize < size; i++)
    {
        bool match = true;
        for (u32 j = 0; j < keySize; j++)
        {
            if (json[i + j] != g_onlineUtcKey[j])
            {
                match = false;
                break;
            }
        }
        if (!match)
            continue;

        u32 pos = i + keySize;
        while (pos < size && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':'))
            pos++;
        if (pos >= size || json[pos] != '"')
            return false;
        pos++;

        u32 out = 0;
        while (pos < size && json[pos] != '"' && out + 1u < sizeof(g_onlineUtc))
            g_onlineUtc[out++] = json[pos++];
        if (pos >= size || json[pos] != '"' || !out)
            return false;
        g_onlineUtc[out] = 0;
        return true;
    }
    return false;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawFrame(const BlurOnlineApi *api, const char *title)
{
    api->drawString(10, 8, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 8, BLUR_ONLINE_BORDER_COLOR, g_onlineRail);
    api->drawString(148, 8, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(10, 16, BLUR_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(148, 16, BLUR_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(10, 24, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 24, BLUR_ONLINE_BORDER_COLOR, g_onlineRail);
    api->drawString(148, 24, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(20, 16, BLUR_ONLINE_TITLE_COLOR, title);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawRoot(const BlurOnlineApi *api, u32 selected)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawFrame(api, g_onlineTitle);
    if (selected == 0)
        api->drawString(12, 48, COLOR_CYAN, g_onlineCursor);
    api->drawString(24, 48, selected == 0 ? COLOR_CYAN : COLOR_WHITE, g_onlineUtcItem);
    if (selected == 1)
        api->drawString(12, 68, COLOR_CYAN, g_onlineCursor);
    api->drawString(24, 68, selected == 1 ? COLOR_CYAN : COLOR_WHITE, g_onlineUpdateItem);
    api->drawString(20, 120, COLOR_GRAY, g_onlineBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUtc(const BlurOnlineApi *api, s32 state, Result result)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawFrame(api, g_onlineUtcTitle);
    api->drawString(20, 45, COLOR_WHITE, g_onlinePrompt);

    if (state == 0)
        api->drawString(20, 70, COLOR_GRAY, g_onlineReady);
    else if (state == 1)
        api->drawString(20, 70, COLOR_GRAY, g_onlineFetching);
    else if (state == 2)
    {
        api->drawString(20, 70, COLOR_GREEN, g_onlineSuccess);
        api->drawString(20, 90, COLOR_WHITE, g_onlineUtc);
    }
    else if (state == -2)
        api->drawString(20, 70, COLOR_RED, g_onlineParseFailure);
    else
    {
        PLUGIN_onln_FormatResult(result);
        api->drawString(20, 70, COLOR_RED, g_onlineFailure);
        api->drawString(20, 90, COLOR_WHITE, g_onlineResultPrefix);
        api->drawString(80, 90, COLOR_WHITE, g_onlineResultHex);
    }

    api->drawString(20, 120, COLOR_GRAY, g_onlineBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunUtc(const BlurOnlineApi *api)
{
    PLUGIN_onln_DrawUtc(api, 0, 0);
    do
    {
        u32 pressed = api->waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return;
        if (pressed & KEY_A)
        {
            u32 actualSize = 0;
            Result result;

            PLUGIN_onln_DrawUtc(api, 1, 0);
            result = api->downloadToMemory(
                g_onlineTimeUrl,
                g_onlineTimeJson,
                sizeof(g_onlineTimeJson) - 1u,
                &actualSize);

            if (R_FAILED(result))
            {
                PLUGIN_onln_DrawUtc(api, -1, result);
                continue;
            }

            if (actualSize >= sizeof(g_onlineTimeJson))
            {
                PLUGIN_onln_DrawUtc(api, -2, 0);
                continue;
            }
            g_onlineTimeJson[actualSize] = 0;
            if (!PLUGIN_onln_ParseUtc(g_onlineTimeJson, actualSize))
            {
                PLUGIN_onln_DrawUtc(api, -2, 0);
                continue;
            }
            PLUGIN_onln_DrawUtc(api, 2, 0);
        }
    } while (!*api->menuShouldExit);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Add32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;
    if (value < a)
        return false;
    *out = value;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Align16(u32 value, u32 *out)
{
    if (value > 0xFFFFFFF0u)
        return false;
    *out = (value + 0xFu) & ~0xFu;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ReadExact(
    const BlurOnlineApi *api,
    const char *path,
    u32 offset,
    void *buffer,
    u32 size)
{
    u32 done = 0;
    u8 *out = (u8 *)buffer;

    while (done < size)
    {
        u32 got = 0;
        u32 currentOffset;
        Result result;
        if (!PLUGIN_onln_Add32(offset, done, &currentOffset))
            return false;
        result = api->readFile(path, currentOffset, out + done, size - done, &got);
        if (R_FAILED(result) || !got || got > size - done)
            return false;
        done += got;
    }
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_WriteExact(
    const BlurOnlineApi *api,
    const char *path,
    u32 offset,
    const void *buffer,
    u32 size)
{
    u32 done = 0;
    const u8 *input = (const u8 *)buffer;

    while (done < size)
    {
        u32 wrote = 0;
        u32 currentOffset;
        Result result;
        if (!PLUGIN_onln_Add32(offset, done, &currentOffset))
            return false;
        result = api->writeFile(path, currentOffset, input + done, size - done, &wrote);
        if (R_FAILED(result) || !wrote || wrote > size - done)
            return false;
        done += wrote;
    }
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Read3nxHeader(
    const BlurOnlineApi *api,
    const char *path,
    u32 fileSize,
    u32 fileOffset,
    Onln3nxHeader *header,
    u32 *metadataOffset,
    u32 *nextOffset)
{
    u32 end;

    if (!header || !metadataOffset || !nextOffset ||
        fileOffset > fileSize || fileSize - fileOffset < ONLN_3NX_HEADER_SIZE ||
        !PLUGIN_onln_ReadExact(api, path, fileOffset, header, sizeof(*header)) ||
        !PLUGIN_onln_Add32(fileOffset, ONLN_3NX_HEADER_SIZE, &end) ||
        !PLUGIN_onln_Add32(end, header->fastRelocSize, &end) ||
        !PLUGIN_onln_Add32(end, header->codeSize, &end) ||
        !PLUGIN_onln_Add32(end, header->dataSize, &end) ||
        !PLUGIN_onln_Add32(end, header->repairSize, &end) ||
        !PLUGIN_onln_Align16(end, metadataOffset) ||
        !PLUGIN_onln_Add32(*metadataOffset, header->metadataSize, nextOffset) ||
        *nextOffset <= fileOffset)
    {
        return false;
    }
    return true;
}

PLUGIN_CODE(onln) static s32 PLUGIN_onln_StringCompare(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (s32)(u8)*a - (s32)(u8)*b;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_EntryEarlier(
    const OnlnSelectedPlugin *current,
    const char *path,
    u32 priority,
    u32 fileOffset)
{
    const char *name = path;
    const char *currentName = current->path;
    s32 comparison;

    while (*name)
        name++;
    while (name > path && name[-1] != '/')
        name--;
    while (*currentName)
        currentName++;
    while (currentName > current->path && currentName[-1] != '/')
        currentName--;

    if (priority != current->priority)
        return priority < current->priority;
    comparison = PLUGIN_onln_StringCompare(name, currentName);
    if (comparison != 0)
        return comparison < 0;
    return fileOffset < current->fileOffset;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_CopyString(char *out, u32 outSize, const char *text)
{
    u32 i = 0;
    if (!out || !outSize || !text)
        return false;
    while (text[i])
    {
        if (i + 1u >= outSize)
            return false;
        out[i] = text[i];
        i++;
    }
    out[i] = 0;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_MakePluginPath(char *out, u32 outSize, const char *name)
{
    u32 pos = 0;
    const char *prefix = g_updatePluginsPrefix;
    if (!out || !name)
        return false;
    while (*prefix)
    {
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = *prefix++;
    }
    while (*name)
    {
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = *name++;
    }
    out[pos] = 0;
    return true;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_CopySelected(
    OnlnSelectedPlugin *destination,
    const OnlnSelectedPlugin *source)
{
    u32 i = 0;
    while (i < sizeof(destination->path))
    {
        destination->path[i] = source->path[i];
        if (!source->path[i])
        {
            i++;
            while (i < sizeof(destination->path))
                destination->path[i++] = 0;
            break;
        }
        i++;
    }
    destination->priority = source->priority;
    destination->fileOffset = source->fileOffset;
    destination->entryEnd = source->entryEnd;
    destination->plgid = source->plgid;
    destination->magic = source->magic;
    destination->localVersion = source->localVersion;
    destination->remoteVersion = source->remoteVersion;
    destination->urlOffset = source->urlOffset;
    destination->urlLength = source->urlLength;
    destination->hasLocalVersion = source->hasLocalVersion;
    destination->hasRemote = source->hasRemote;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_AddSelected(
    OnlnSelectedPlugin *plugins,
    u32 *count,
    const char *path,
    u32 priority,
    u32 fileOffset,
    u32 entryEnd,
    const Onln3nxHeader *header,
    bool hasVersion,
    u32 version)
{
    u32 pos;
    OnlnSelectedPlugin incoming;

    if (!plugins || !count || !path || !header)
        return;

    if (!PLUGIN_onln_CopyString(incoming.path, sizeof(incoming.path), path))
        return;
    incoming.priority = priority;
    incoming.fileOffset = fileOffset;
    incoming.entryEnd = entryEnd;
    incoming.plgid = header->plgid;
    incoming.magic = header->magic;
    incoming.localVersion = version;
    incoming.remoteVersion = 0;
    incoming.urlOffset = 0;
    incoming.urlLength = 0;
    incoming.hasLocalVersion = hasVersion;
    incoming.hasRemote = false;

    for (pos = 0; pos < *count; pos++)
    {
        if (plugins[pos].plgid != header->plgid)
            continue;
        if (!PLUGIN_onln_EntryEarlier(&plugins[pos], path, priority, fileOffset))
            return;
        break;
    }

    if (pos == *count)
    {
        if (*count < ONLN_MAX_SELECTED)
            pos = (*count)++;
        else
        {
            pos = ONLN_MAX_SELECTED - 1u;
            if (!PLUGIN_onln_EntryEarlier(&plugins[pos], path, priority, fileOffset))
                return;
        }
    }

    while (pos > 0 && PLUGIN_onln_EntryEarlier(&plugins[pos - 1u], path, priority, fileOffset))
    {
        PLUGIN_onln_CopySelected(&plugins[pos], &plugins[pos - 1u]);
        pos--;
    }
    PLUGIN_onln_CopySelected(&plugins[pos], &incoming);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParsePriority(const char *name, u32 *priority)
{
    u32 length = 0;
    const char *extension;
    const char *priorityDot;
    u32 value = 0;

    if (!name || !priority)
        return false;
    while (name[length])
    {
        if (length + 1u >= BLUR_ONLINE_DIR_NAME_CAP)
            return false;
        length++;
    }
    if (length < 7u || name[length - 4u] != '.' || name[length - 3u] != '3' ||
        name[length - 2u] != 'n' || name[length - 1u] != 'x')
        return false;

    extension = &name[length - 4u];
    priorityDot = extension - 1;
    while (priorityDot > name && *priorityDot != '.')
        priorityDot--;
    if (*priorityDot != '.' || priorityDot + 1 == extension)
        return false;

    for (const char *character = priorityDot + 1; character < extension; character++)
    {
        u32 digit;
        if (*character < '0' || *character > '9')
            return false;
        digit = (u32)(*character - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    *priority = value;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_IsInstallCandidateName(const char *name)
{
    u32 length = 0;
    const char *extension;
    const char *priorityDot;

    if (!name)
        return false;
    while (name[length])
    {
        if (length + 1u >= BLUR_ONLINE_DIR_NAME_CAP)
            return false;
        length++;
    }

    if (length >= 7u &&
        name[length - 4u] == '.' && name[length - 3u] == '3' &&
        name[length - 2u] == 'n' && name[length - 1u] == 'x')
    {
        extension = &name[length - 4u];
    }
    else if (length >= 9u &&
             name[length - 6u] == '.' && name[length - 5u] == '3' &&
             name[length - 4u] == 'n' && name[length - 3u] == 'x' &&
             name[length - 2u] == '.' && name[length - 1u] == 'd')
    {
        extension = &name[length - 6u];
    }
    else
    {
        return false;
    }

    priorityDot = extension - 1;
    while (priorityDot > name && *priorityDot != '.')
        priorityDot--;
    if (*priorityDot != '.' || priorityDot + 1 == extension)
        return false;

    for (const char *character = priorityDot + 1; character < extension; character++)
    {
        if (*character < '0' || *character > '9')
            return false;
    }
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ReadVersion(
    const BlurOnlineApi *api,
    const char *path,
    const Onln3nxHeader *header,
    u32 metadataOffset,
    u32 *version)
{
    u8 bytes[8];
    u32 magic;

    if (!header || !version || header->metadataSize < 8u ||
        !PLUGIN_onln_ReadExact(api, path, metadataOffset, bytes, sizeof(bytes)))
        return false;

    magic = (u32)bytes[0] |
            ((u32)bytes[1] << 8) |
            ((u32)bytes[2] << 16) |
            ((u32)bytes[3] << 24);
    if (magic != ONLN_VERSION_MAGIC)
        return false;

    *version = (u32)bytes[4] |
               ((u32)bytes[5] << 8) |
               ((u32)bytes[6] << 16) |
               ((u32)bytes[7] << 24);
    return true;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanOneFile(
    const BlurOnlineApi *api,
    const char *name,
    u32 priority)
{
    char path[ONLN_UPDATE_PATH_CAP];
    u32 fileSize = 0;
    u32 fileOffset = 0;
    Result result;

    if (!PLUGIN_onln_MakePluginPath(path, sizeof(path), name))
        return 0;
    result = api->getFileSize(path, &fileSize);
    if (R_FAILED(result))
        return 0;

    while (fileOffset < fileSize)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        u32 version = 0;
        bool hasVersion;

        if (!PLUGIN_onln_Read3nxHeader(api, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) ||
            nextOffset > fileSize)
            break;
        if (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC)
            break;

        hasVersion = PLUGIN_onln_ReadVersion(api, path, &header, metadataOffset, &version);
        if (header.magic == ONLN_LOADER_MAGIC)
            PLUGIN_onln_AddSelected(g_updateLoader, &g_updateLoaderCount, path, priority, fileOffset, nextOffset, &header, hasVersion, version);
        else
            PLUGIN_onln_AddSelected(g_updateRosalina, &g_updateRosalinaCount, path, priority, fileOffset, nextOffset, &header, hasVersion, version);

        fileOffset = nextOffset;
    }
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanVisitor(
    const BlurOnlineDirEntry *entry,
    void *context,
    bool *stop)
{
    OnlnScanContext *scan = (OnlnScanContext *)context;
    u32 priority;
    (void)stop;

    if (!entry || !scan || !(entry->flags & BLUR_ONLINE_DIR_NAME_COMPLETE) ||
        (entry->attributes & FS_ATTRIBUTE_DIRECTORY) ||
        !PLUGIN_onln_ParsePriority(entry->name, &priority))
    {
        return 0;
    }
    return PLUGIN_onln_ScanOneFile(scan->api, entry->name, priority);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ScanSelected(const BlurOnlineApi *api)
{
    OnlnScanContext context;
    u32 visited = 0;
    g_updateLoaderCount = 0;
    g_updateRosalinaCount = 0;
    context.api = api;
    return api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_ScanVisitor, &context, &visited);
}

PLUGIN_CODE(onln) static OnlnSelectedPlugin *PLUGIN_onln_FindSelected(u32 magic, u32 plgid)
{
    OnlnSelectedPlugin *plugins = magic == ONLN_LOADER_MAGIC ? g_updateLoader : g_updateRosalina;
    u32 count = magic == ONLN_LOADER_MAGIC ? g_updateLoaderCount : g_updateRosalinaCount;
    for (u32 i = 0; i < count; i++)
    {
        if (plugins[i].plgid == plgid)
            return &plugins[i];
    }
    return NULL;
}

PLUGIN_CODE(onln) static s32 PLUGIN_onln_HexDigit(char value)
{
    if (value >= '0' && value <= '9')
        return (s32)(value - '0');
    if (value >= 'A' && value <= 'F')
        return (s32)(value - 'A' + 10);
    if (value >= 'a' && value <= 'f')
        return (s32)(value - 'a' + 10);
    return -1;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_ParseManifestPrefix(
    const char *prefix,
    u32 length,
    u32 *plgid,
    u32 *magic,
    u32 *version)
{
    u32 parsed = 0;

    if (!prefix || length < 13u || !plgid || !magic || !version)
        return false;

    for (u32 i = 0; i < 4u; i++)
    {
        char c = prefix[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_'))
        {
            return false;
        }
    }

    *plgid = (u32)(u8)prefix[0] |
             ((u32)(u8)prefix[1] << 8) |
             ((u32)(u8)prefix[2] << 16) |
             ((u32)(u8)prefix[3] << 24);

    for (u32 i = 4u; i < 12u; i++)
    {
        s32 digit = PLUGIN_onln_HexDigit(prefix[i]);
        if (digit < 0)
            return false;
        parsed = (parsed << 4) | (u32)digit;
    }

    if (prefix[12] == ':')
        *magic = ONLN_LOADER_MAGIC;
    else if (prefix[12] == ';')
        *magic = ONLN_ROSALINA_MAGIC;
    else
        return false;

    *version = parsed;
    return true;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ApplyManifestLine(
    const char *prefix,
    u32 prefixLength,
    u32 lineStart,
    u32 lineLength,
    char lastCharacter)
{
    u32 plgid;
    u32 magic;
    u32 version;
    u32 effectiveLength = lineLength;
    OnlnSelectedPlugin *selected;

    if (effectiveLength && lastCharacter == '\r')
        effectiveLength--;
    if (!effectiveLength)
        return 0;
    if (effectiveLength <= 13u ||
        prefixLength < 13u ||
        !PLUGIN_onln_ParseManifestPrefix(prefix, prefixLength, &plgid, &magic, &version))
    {
        return ONLN_UPDATE_BAD_FORMAT;
    }

    selected = PLUGIN_onln_FindSelected(magic, plgid);
    if (!selected)
        return 0;
    if (selected->hasRemote || effectiveLength - 13u >= ONLN_UPDATE_URL_CAP)
        return ONLN_UPDATE_BAD_FORMAT;

    selected->remoteVersion = version;
    selected->urlOffset = lineStart + 13u;
    selected->urlLength = effectiveLength - 13u;
    selected->hasRemote = true;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ParseManifest(const BlurOnlineApi *api)
{
    u32 fileSize = 0;
    u32 fileOffset = 0;
    u32 lineStart = 0;
    u32 lineLength = 0;
    u32 prefixLength = 0;
    char prefix[13];
    char lastCharacter = 0;
    Result result = api->getFileSize(g_updateManifestPath, &fileSize);

    if (R_FAILED(result) || !fileSize || fileSize > ONLN_UPDATE_MANIFEST_MAX)
        return R_FAILED(result) ? result : ONLN_UPDATE_BAD_FORMAT;

    while (fileOffset < fileSize)
    {
        u32 want = fileSize - fileOffset;
        u32 got = 0;

        if (want > sizeof(g_updateIo))
            want = sizeof(g_updateIo);

        result = api->readFile(g_updateManifestPath, fileOffset, g_updateIo, want, &got);
        if (R_FAILED(result) || !got || got > want)
            return R_FAILED(result) ? result : ONLN_UPDATE_BAD_FORMAT;

        for (u32 i = 0; i < got; i++)
        {
            char c = (char)g_updateIo[i];

            if (c == '\n')
            {
                result = PLUGIN_onln_ApplyManifestLine(
                    prefix,
                    prefixLength,
                    lineStart,
                    lineLength,
                    lastCharacter
                );
                if (R_FAILED(result))
                    return result;

                lineStart = fileOffset + i + 1u;
                lineLength = 0;
                prefixLength = 0;
                lastCharacter = 0;
                continue;
            }

            if (prefixLength < sizeof(prefix))
                prefix[prefixLength++] = c;

            lineLength++;
            lastCharacter = c;

            if (lineLength > 13u + ONLN_UPDATE_URL_CAP)
                return ONLN_UPDATE_BAD_FORMAT;
        }

        fileOffset += got;
    }

    if (lineLength)
        return PLUGIN_onln_ApplyManifestLine(prefix, prefixLength, lineStart, lineLength, lastCharacter);

    return 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_FileExists(const BlurOnlineApi *api, const char *path)
{
    bool exists = false;
    return R_SUCCEEDED(api->fileExists(path, &exists)) && exists;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DeleteIfExists(const BlurOnlineApi *api, const char *path)
{
    bool exists = false;
    if (R_SUCCEEDED(api->fileExists(path, &exists)) && exists)
        (void)api->deleteFile(path);
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_WriteJournal(const BlurOnlineApi *api, const char *path)
{
    u32 length = 0;
    while (path[length])
    {
        if (length + 1u >= sizeof(g_updateJournalRead))
            return ONLN_UPDATE_BAD_FORMAT;
        length++;
    }
    PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
    if (!PLUGIN_onln_WriteExact(api, g_updateJournalPath, 0, path, length) ||
        R_FAILED(api->setFileSize(g_updateJournalPath, length)))
        return ONLN_UPDATE_SWAP_FAILED;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_RecoverInterruptedUpdate(const BlurOnlineApi *api)
{
    bool backup = PLUGIN_onln_FileExists(api, g_updateBackupPath);
    bool journal = PLUGIN_onln_FileExists(api, g_updateJournalPath);

    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);

    if (!backup && !journal)
        return 0;
    if (!journal)
        return ONLN_UPDATE_RECOVERY_FAILED;

    {
        u32 size = 0;
        if (R_FAILED(api->getFileSize(g_updateJournalPath, &size)) || !size || size >= sizeof(g_updateJournalRead) ||
            !PLUGIN_onln_ReadExact(api, g_updateJournalPath, 0, g_updateJournalRead, size))
            return ONLN_UPDATE_RECOVERY_FAILED;
        g_updateJournalRead[size] = 0;
    }

    if (backup)
    {
        bool targetExists = PLUGIN_onln_FileExists(api, g_updateJournalRead);
        if (!targetExists)
        {
            if (R_FAILED(api->renameFile(g_updateBackupPath, g_updateJournalRead)))
                return ONLN_UPDATE_RECOVERY_FAILED;
        }
        else
        {
            if (R_FAILED(api->deleteFile(g_updateBackupPath)))
                return ONLN_UPDATE_RECOVERY_FAILED;
        }
    }
    PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_CopyRange(
    const BlurOnlineApi *api,
    const char *source,
    u32 sourceOffset,
    const char *destination,
    u32 destinationOffset,
    u32 size)
{
    u32 done = 0;
    while (done < size)
    {
        u32 chunk = size - done;
        u32 srcAt;
        u32 dstAt;
        if (chunk > sizeof(g_updateIo))
            chunk = sizeof(g_updateIo);
        if (!PLUGIN_onln_Add32(sourceOffset, done, &srcAt) ||
            !PLUGIN_onln_Add32(destinationOffset, done, &dstAt) ||
            !PLUGIN_onln_ReadExact(api, source, srcAt, g_updateIo, chunk) ||
            !PLUGIN_onln_WriteExact(api, destination, dstAt, g_updateIo, chunk))
            return ONLN_UPDATE_COPY_FAILED;
        done += chunk;
    }
    return 0;
}

static Result PLUGIN_onln_SwapRebuild(
    const BlurOnlineApi *api,
    const char *target);

PLUGIN_CODE(onln) static Result PLUGIN_onln_InstallNextVisitor(
    const BlurOnlineDirEntry *entry,
    void *context,
    bool *stop)
{
    OnlnInstallScanContext *scan = (OnlnInstallScanContext *)context;
    (void)stop;

    if (!entry || !scan || !(entry->flags & BLUR_ONLINE_DIR_NAME_COMPLETE) ||
        (entry->attributes & FS_ATTRIBUTE_DIRECTORY) ||
        !PLUGIN_onln_IsInstallCandidateName(entry->name) ||
        (scan->afterName && scan->afterName[0] && PLUGIN_onln_StringCompare(entry->name, scan->afterName) <= 0))
    {
        return 0;
    }

    if (!scan->found || PLUGIN_onln_StringCompare(entry->name, g_updateInstallNextName) < 0)
    {
        if (!PLUGIN_onln_CopyString(g_updateInstallNextName, sizeof(g_updateInstallNextName), entry->name))
            return ONLN_UPDATE_BAD_FORMAT;
        scan->found = true;
    }
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_FindNextInstallFile(
    const BlurOnlineApi *api,
    bool *found)
{
    OnlnInstallScanContext context;
    u32 visited = 0;
    Result result;

    if (!found)
        return ONLN_UPDATE_BAD_FORMAT;
    g_updateInstallNextName[0] = 0;
    context.afterName = g_updateInstallAfterName;
    context.found = false;
    result = api->enumerateDirectory(g_updatePluginsDir, PLUGIN_onln_InstallNextVisitor, &context, &visited);
    if (R_FAILED(result))
        return result;
    *found = context.found;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ReplaceMatchesInFile(
    const BlurOnlineApi *api,
    const char *name,
    u32 targetMagic,
    u32 targetPlgid,
    u32 downloadSize,
    u32 *updated)
{
    char path[ONLN_UPDATE_PATH_CAP];
    u32 fileSize = 0;
    u32 fileOffset = 0;
    u32 outputOffset = 0;
    u32 matches = 0;
    Result result;

    if (!updated || !PLUGIN_onln_MakePluginPath(path, sizeof(path), name))
        return ONLN_UPDATE_BAD_FORMAT;
    result = api->getFileSize(path, &fileSize);
    if (R_FAILED(result))
        return result;

    /* First pass: require the whole physical file/stack to parse cleanly before replacing anything. */
    while (fileOffset < fileSize)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        if (!PLUGIN_onln_Read3nxHeader(api, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) ||
            nextOffset > fileSize ||
            (header.magic != ONLN_ROSALINA_MAGIC && header.magic != ONLN_LOADER_MAGIC))
        {
            return 0; /* Not a fully valid 3NX/stack: leave it untouched. */
        }
        if (header.magic == targetMagic && header.plgid == targetPlgid)
            matches++;
        fileOffset = nextOffset;
    }
    if (!matches)
        return 0;
    if (*updated > 0xFFFFFFFFu - matches)
        return ONLN_UPDATE_BAD_FORMAT;

    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    result = api->setFileSize(g_updateRebuildPath, 0);
    if (R_FAILED(result))
        return result;

    fileOffset = 0;
    while (fileOffset < fileSize)
    {
        Onln3nxHeader header;
        u32 metadataOffset;
        u32 nextOffset;
        u32 chunkSize;
        u32 nextOutput;

        if (!PLUGIN_onln_Read3nxHeader(api, path, fileSize, fileOffset, &header, &metadataOffset, &nextOffset) ||
            nextOffset > fileSize)
        {
            result = ONLN_UPDATE_BAD_FILE;
            goto finish;
        }

        if (header.magic == targetMagic && header.plgid == targetPlgid)
        {
            chunkSize = downloadSize;
            if (!PLUGIN_onln_Add32(outputOffset, chunkSize, &nextOutput))
            {
                result = ONLN_UPDATE_BAD_FILE;
                goto finish;
            }
            result = PLUGIN_onln_CopyRange(api, g_updateDownloadPath, 0, g_updateRebuildPath, outputOffset, chunkSize);
        }
        else
        {
            chunkSize = nextOffset - fileOffset;
            if (!PLUGIN_onln_Add32(outputOffset, chunkSize, &nextOutput))
            {
                result = ONLN_UPDATE_BAD_FILE;
                goto finish;
            }
            result = PLUGIN_onln_CopyRange(api, path, fileOffset, g_updateRebuildPath, outputOffset, chunkSize);
        }
        if (R_FAILED(result))
            goto finish;
        outputOffset = nextOutput;
        fileOffset = nextOffset;
    }

    result = api->setFileSize(g_updateRebuildPath, outputOffset);
    if (R_FAILED(result))
        goto finish;
    result = PLUGIN_onln_SwapRebuild(api, path);
    if (R_FAILED(result))
        goto finish;

    *updated += matches;

finish:
    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    return result;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ReplaceAllOccurrences(
    const BlurOnlineApi *api,
    const OnlnSelectedPlugin *selected,
    u32 downloadSize)
{
    u32 updated = 0;
    Result result;

    g_updateInstallAfterName[0] = 0;
    for (;;)
    {
        bool found = false;
        result = PLUGIN_onln_FindNextInstallFile(api, &found);
        if (R_FAILED(result))
            return result;
        if (!found)
            break;
        if (!PLUGIN_onln_CopyString(g_updateInstallAfterName, sizeof(g_updateInstallAfterName), g_updateInstallNextName))
            return ONLN_UPDATE_BAD_FORMAT;

        result = PLUGIN_onln_ReplaceMatchesInFile(
            api,
            g_updateInstallNextName,
            selected->magic,
            selected->plgid,
            downloadSize,
            &updated);
        if (R_FAILED(result))
            return result;
    }

    return updated ? 0 : ONLN_UPDATE_BAD_FILE;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ValidateDownloaded(
    const BlurOnlineApi *api,
    const OnlnSelectedPlugin *selected,
    u32 manifestVersion,
    u32 *downloadSize)
{
    Onln3nxHeader header;
    u32 metadataOffset;
    u32 versionOffset;
    u32 nextOffset;
    u32 size = 0;
    u8 magicBytes[4];
    u8 versionBytes[4];
    u32 magic;
    Result result = api->getFileSize(g_updateDownloadPath, &size);

    if (R_FAILED(result))
        return result;
    if (!size || !PLUGIN_onln_Read3nxHeader(api, g_updateDownloadPath, size, 0, &header, &metadataOffset, &nextOffset) ||
        nextOffset != size || header.magic != selected->magic || header.plgid != selected->plgid ||
        header.metadataSize < 8u ||
        !PLUGIN_onln_ReadExact(api, g_updateDownloadPath, metadataOffset, magicBytes, sizeof(magicBytes)))
        return ONLN_UPDATE_BAD_FILE;

    magic = (u32)magicBytes[0] |
            ((u32)magicBytes[1] << 8) |
            ((u32)magicBytes[2] << 16) |
            ((u32)magicBytes[3] << 24);
    if (magic != ONLN_VERSION_MAGIC || !PLUGIN_onln_Add32(metadataOffset, 4u, &versionOffset))
        return ONLN_UPDATE_BAD_FILE;

    versionBytes[0] = (u8)(manifestVersion & 0xFFu);
    versionBytes[1] = (u8)((manifestVersion >> 8) & 0xFFu);
    versionBytes[2] = (u8)((manifestVersion >> 16) & 0xFFu);
    versionBytes[3] = (u8)((manifestVersion >> 24) & 0xFFu);
    if (!PLUGIN_onln_WriteExact(api, g_updateDownloadPath, versionOffset, versionBytes, sizeof(versionBytes)))
        return ONLN_UPDATE_COPY_FAILED;

    *downloadSize = size;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_ReadManifestUrl(
    const BlurOnlineApi *api,
    const OnlnSelectedPlugin *selected)
{
    if (!selected->hasRemote || !selected->urlLength || selected->urlLength >= sizeof(g_updateUrl) ||
        !PLUGIN_onln_ReadExact(api, g_updateManifestPath, selected->urlOffset, g_updateUrl, selected->urlLength))
        return ONLN_UPDATE_BAD_FORMAT;
    g_updateUrl[selected->urlLength] = 0;
    if (selected->urlLength < 8u || g_updateUrl[0] != 'h' || g_updateUrl[1] != 't' ||
        g_updateUrl[2] != 't' || g_updateUrl[3] != 'p' || g_updateUrl[4] != 's' ||
        g_updateUrl[5] != ':' || g_updateUrl[6] != '/' || g_updateUrl[7] != '/')
        return ONLN_UPDATE_BAD_FORMAT;
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_SwapRebuild(
    const BlurOnlineApi *api,
    const char *target)
{
    Result result;
    PLUGIN_onln_DeleteIfExists(api, g_updateBackupPath);
    result = PLUGIN_onln_WriteJournal(api, target);
    if (R_FAILED(result))
        return result;

    result = api->renameFile(target, g_updateBackupPath);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
        return result;
    }

    result = api->renameFile(g_updateRebuildPath, target);
    if (R_FAILED(result))
    {
        Result restore = api->renameFile(g_updateBackupPath, target);
        if (R_SUCCEEDED(restore))
            PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
        return ONLN_UPDATE_SWAP_FAILED;
    }

    PLUGIN_onln_DeleteIfExists(api, g_updateBackupPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateJournalPath);
    return 0;
}

PLUGIN_CODE(onln) static Result PLUGIN_onln_InstallUpdate(
    const BlurOnlineApi *api,
    const OnlnSelectedPlugin *selected)
{
    u32 downloadSize;
    Result result;

    result = PLUGIN_onln_ReadManifestUrl(api, selected);
    if (R_FAILED(result))
        return result;

    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    result = api->downloadToFile(g_updateUrl, g_updateDownloadPath, ONLN_UPDATE_MAX_DOWNLOAD);
    if (R_FAILED(result))
        return result;

    result = PLUGIN_onln_ValidateDownloaded(api, selected, selected->remoteVersion, &downloadSize);
    if (R_FAILED(result))
        goto finish;

    result = PLUGIN_onln_ReplaceAllOccurrences(api, selected, downloadSize);

finish:
    PLUGIN_onln_DeleteIfExists(api, g_updateDownloadPath);
    PLUGIN_onln_DeleteIfExists(api, g_updateRebuildPath);
    return result;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_FormatCount(u32 value)
{
    u32 tens = 0;
    if (value > 99u)
        value = 99u;
    while (value >= 10u)
    {
        value -= 10u;
        tens++;
    }
    if (tens)
    {
        g_updateNumber[0] = (char)('0' + tens);
        g_updateNumber[1] = (char)('0' + value);
        g_updateNumber[2] = 0;
    }
    else
    {
        g_updateNumber[0] = (char)('0' + value);
        g_updateNumber[1] = 0;
    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_FormatId(u32 plgid)
{
    g_updateId[0] = (char)(plgid & 0xFFu);
    g_updateId[1] = (char)((plgid >> 8) & 0xFFu);
    g_updateId[2] = (char)((plgid >> 16) & 0xFFu);
    g_updateId[3] = (char)((plgid >> 24) & 0xFFu);
    g_updateId[4] = 0;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_AppendText(char *out, u32 outSize, u32 *position, const char *text)
{
    u32 pos;
    if (!out || !outSize || !position || !text)
        return false;
    pos = *position;
    while (*text)
    {
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = *text++;
    }
    out[pos] = 0;
    *position = pos;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_AppendId(char *out, u32 outSize, u32 *position, u32 plgid)
{
    u32 pos;
    if (!out || !outSize || !position)
        return false;
    pos = *position;
    if (pos + 5u > outSize)
        return false;
    out[pos++] = (char)(plgid & 0xFFu);
    out[pos++] = (char)((plgid >> 8) & 0xFFu);
    out[pos++] = (char)((plgid >> 16) & 0xFFu);
    out[pos++] = (char)((plgid >> 24) & 0xFFu);
    out[pos] = 0;
    *position = pos;
    return true;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_FormatVersionText(char *out, u32 outSize, u32 version)
{
    char digits[10];
    u32 digitCount = 0;
    bool started = false;
    u32 value = version;
    u32 pos = 0;

    for (u32 i = 0; i < 10u; i++)
    {
        u32 power = g_updateDecimalPowers[i];
        u32 digit = 0;
        while (value >= power)
        {
            value -= power;
            digit++;
        }
        if (digit || started || power <= 100u)
        {
            started = true;
            digits[digitCount++] = (char)('0' + digit);
        }
    }

    if (!PLUGIN_onln_AppendText(out, outSize, &pos, g_updateVersionPrefix))
        return false;
    for (u32 i = 0; i < digitCount; i++)
    {
        if (i + 2u == digitCount)
        {
            if (pos + 1u >= outSize)
                return false;
            out[pos++] = '.';
            out[pos] = 0;
        }
        if (pos + 1u >= outSize)
            return false;
        out[pos++] = digits[i];
        out[pos] = 0;
    }
    return true;
}

PLUGIN_CODE(onln) static const char *PLUGIN_onln_ModuleName(u32 magic)
{
    return magic == ONLN_LOADER_MAGIC ? g_updateLoaderName : g_updateRosalinaName;
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_BuildUpdateLine(
    const OnlnSelectedPlugin *plugin,
    char *out,
    u32 outSize)
{
    char localVersion[16];
    char remoteVersion[16];
    u32 pos = 0;

    if (!plugin || !out || !outSize)
        return false;
    out[0] = 0;

    if (!PLUGIN_onln_FormatVersionText(remoteVersion, sizeof(remoteVersion), plugin->remoteVersion))
        return false;
    if (plugin->hasLocalVersion &&
        !PLUGIN_onln_FormatVersionText(localVersion, sizeof(localVersion), plugin->localVersion))
        return false;

    return PLUGIN_onln_AppendId(out, outSize, &pos, plugin->plgid) &&
           PLUGIN_onln_AppendText(out, outSize, &pos,
                                  plugin->magic == ONLN_LOADER_MAGIC ? g_updateOpenLoaderModule : g_updateOpenModule) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, PLUGIN_onln_ModuleName(plugin->magic)) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, g_updateCloseModule) &&
           PLUGIN_onln_AppendText(out, outSize, &pos,
                                  plugin->hasLocalVersion ? localVersion : g_updateNullVersion) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, g_updateArrow) &&
           PLUGIN_onln_AppendText(out, outSize, &pos, remoteVersion);
}

PLUGIN_CODE(onln) static bool PLUGIN_onln_Listed(const OnlnSelectedPlugin *plugin, bool showAll)
{
    return plugin && plugin->hasRemote &&
           (showAll || !plugin->hasLocalVersion || plugin->remoteVersion > plugin->localVersion);
}

PLUGIN_CODE(onln) static u32 PLUGIN_onln_CountListed(bool showAll)
{
    u32 count = 0;
    for (u32 i = 0; i < g_updateLoaderCount; i++)
        if (PLUGIN_onln_Listed(&g_updateLoader[i], showAll))
            count++;
    for (u32 i = 0; i < g_updateRosalinaCount; i++)
        if (PLUGIN_onln_Listed(&g_updateRosalina[i], showAll))
            count++;
    return count;
}

PLUGIN_CODE(onln) static OnlnSelectedPlugin *PLUGIN_onln_GetListed(u32 index, bool showAll)
{
    for (u32 i = 0; i < g_updateLoaderCount; i++)
    {
        if (!PLUGIN_onln_Listed(&g_updateLoader[i], showAll))
            continue;
        if (!index)
            return &g_updateLoader[i];
        index--;
    }
    for (u32 i = 0; i < g_updateRosalinaCount; i++)
    {
        if (!PLUGIN_onln_Listed(&g_updateRosalina[i], showAll))
            continue;
        if (!index)
            return &g_updateRosalina[i];
        index--;
    }
    return NULL;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateFrame(const BlurOnlineApi *api)
{
    api->drawString(10, 8, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 8, BLUR_ONLINE_BORDER_COLOR, g_updateWideRail);
    api->drawString(196, 8, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(10, 16, BLUR_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(196, 16, BLUR_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(10, 24, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 24, BLUR_ONLINE_BORDER_COLOR, g_updateWideRail);
    api->drawString(196, 24, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(22, 16, BLUR_ONLINE_TITLE_COLOR, g_updateTitle);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateStatus(
    const BlurOnlineApi *api,
    const char *message,
    const OnlnSelectedPlugin *plugin,
    u32 index,
    u32 total,
    Result result)
{
    char line[48];
    u32 pos = 0;

    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawUpdateFrame(api);
    api->drawString(20, 46, COLOR_WHITE, message);
    if (plugin)
    {
        line[0] = 0;
        if (PLUGIN_onln_AppendId(line, sizeof(line), &pos, plugin->plgid) &&
            PLUGIN_onln_AppendText(line, sizeof(line), &pos,
                                   plugin->magic == ONLN_LOADER_MAGIC ? g_updateOpenLoaderModule : g_updateOpenModule) &&
            PLUGIN_onln_AppendText(line, sizeof(line), &pos, PLUGIN_onln_ModuleName(plugin->magic)) &&
            PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateCloseOnly))
            api->drawString(20, 68, COLOR_CYAN, line);

        if (total)
        {
            pos = 0;
            PLUGIN_onln_FormatCount(index);
            if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateNumber) &&
                PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateSlash))
            {
                PLUGIN_onln_FormatCount(total);
                if (PLUGIN_onln_AppendText(line, sizeof(line), &pos, g_updateNumber))
                    api->drawString(190, 68, COLOR_GRAY, line);
            }
        }
    }
    if (R_FAILED(result))
    {
        PLUGIN_onln_FormatResult(result);
        api->drawString(20, 92, COLOR_RED, g_onlineResultPrefix);
        api->drawString(80, 92, COLOR_RED, g_onlineResultHex);
    }
    api->drawString(20, 120, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static u16 PLUGIN_onln_UpdateItemColor(const OnlnSelectedPlugin *plugin)
{
    if (!plugin || !plugin->hasLocalVersion)
        return ONLN_UPDATE_NEWER_COLOR;
    if (plugin->remoteVersion > plugin->localVersion)
        return ONLN_UPDATE_NEWER_COLOR;
    if (plugin->remoteVersion < plugin->localVersion)
        return ONLN_UPDATE_OLDER_COLOR;
    return COLOR_WHITE;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateItem(
    const BlurOnlineApi *api,
    u32 itemIndex,
    u32 y,
    bool selected,
    bool showAll,
    u32 available,
    bool hasInstallAll,
    u32 installAllIndex)
{
    char line[64];
    const char *text = g_updateAll;
    u16 color = selected ? COLOR_CYAN : COLOR_WHITE;

    if (itemIndex < available)
    {
        OnlnSelectedPlugin *plugin = PLUGIN_onln_GetListed(itemIndex, showAll);
        if (!plugin || !PLUGIN_onln_BuildUpdateLine(plugin, line, sizeof(line)))
            return;
        text = line;
        color = PLUGIN_onln_UpdateItemColor(plugin);
    }
    else if (!hasInstallAll || itemIndex != installAllIndex)
        return;

    if (selected)
        api->drawString(12, y, COLOR_CYAN, g_onlineCursor);
    api->drawString(24, y, color, text);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateList(
    const BlurOnlineApi *api,
    u32 first,
    u32 selected,
    bool showAll)
{
    u32 available = PLUGIN_onln_CountListed(showAll);
    bool hasInstallAll = available > 1u;
    u32 installAllIndex = available;
    u32 total = available + (hasInstallAll ? 1u : 0u);
    u32 shown = total - first;

    if (shown > ONLN_UPDATE_VISIBLE_ITEMS)
        shown = ONLN_UPDATE_VISIBLE_ITEMS;

    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawUpdateFrame(api);

    if (!total)
        api->drawString(24, ONLN_UPDATE_ITEM_TOP_Y, COLOR_GRAY, g_updateNoUpdates);

    if (first)
        api->drawString(24, ONLN_UPDATE_TOP_DOTS_Y, COLOR_GRAY, g_updateDots);

    for (u32 i = 0; i < shown; i++)
        PLUGIN_onln_DrawUpdateItem(
            api,
            first + i,
            ONLN_UPDATE_ITEM_TOP_Y + i * ONLN_UPDATE_ITEM_SPACING_Y,
            first + i == selected,
            showAll,
            available,
            hasInstallAll,
            installAllIndex
        );

    if (first + shown < total)
        api->drawString(24,
                        ONLN_UPDATE_ITEM_TOP_Y + ONLN_UPDATE_VISIBLE_ITEMS * ONLN_UPDATE_ITEM_SPACING_Y,
                        COLOR_GRAY,
                        g_updateDots);

    api->drawString(24, ONLN_UPDATE_PROMPT_Y, COLOR_GRAY,
                    showAll ? g_updateShowUpdates : g_updateShowAll);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RedrawUpdateSelection(
    const BlurOnlineApi *api,
    u32 first,
    u32 oldSelected,
    u32 selected,
    bool showAll)
{
    u32 available = PLUGIN_onln_CountListed(showAll);
    bool hasInstallAll = available > 1u;
    u32 installAllIndex = available;
    u32 oldY = ONLN_UPDATE_ITEM_TOP_Y + (oldSelected - first) * ONLN_UPDATE_ITEM_SPACING_Y;
    u32 newY = ONLN_UPDATE_ITEM_TOP_Y + (selected - first) * ONLN_UPDATE_ITEM_SPACING_Y;

    api->drawLock();
    api->drawString(10, oldY, COLOR_BLACK, g_updateClearRow);
    api->drawString(10, newY, COLOR_BLACK, g_updateClearRow);
    PLUGIN_onln_DrawUpdateItem(
        api, oldSelected, oldY, false, showAll, available, hasInstallAll, installAllIndex
    );
    PLUGIN_onln_DrawUpdateItem(
        api, selected, newY, true, showAll, available, hasInstallAll, installAllIndex
    );
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_DrawUpdateAllSummary(
    const BlurOnlineApi *api,
    u32 updated,
    u32 failed)
{
    api->drawLock();
    api->drawClear();
    PLUGIN_onln_DrawUpdateFrame(api);
    api->drawString(20, 42, COLOR_GREEN, g_updateAllDone);
    api->drawString(20, 66, COLOR_WHITE, g_updateUpdatedLabel);
    PLUGIN_onln_FormatCount(updated);
    api->drawString(86, 66, COLOR_WHITE, g_updateNumber);
    api->drawString(20, 84, failed ? COLOR_RED : COLOR_WHITE, g_updateFailedLabel);
    PLUGIN_onln_FormatCount(failed);
    api->drawString(86, 84, failed ? COLOR_RED : COLOR_WHITE, g_updateNumber);
    if (updated)
        api->drawString(20, 106, COLOR_YELLOW, g_updateRestart);
    api->drawString(20, 128, COLOR_GRAY, g_updatePressBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_CODE(onln) static void PLUGIN_onln_WaitBack(const BlurOnlineApi *api)
{
    do
    {
        if (api->waitInputWithTimeout(50) & KEY_B)
            return;
    } while (!*api->menuShouldExit);
}

PLUGIN_CODE(onln) static void PLUGIN_onln_MarkInstalled(OnlnSelectedPlugin *plugin)
{
    if (!plugin)
        return;
    plugin->localVersion = plugin->remoteVersion;
    plugin->hasLocalVersion = true;
}

PLUGIN_CODE(onln) static void PLUGIN_onln_UpdateAll(
    const BlurOnlineApi *api,
    bool *installedAny,
    bool showAll)
{
    u32 total = PLUGIN_onln_CountListed(showAll);
    u32 position = 0;
    u32 updated = 0;
    u32 failed = 0;

    for (u32 pass = 0; pass < 2u && !*api->menuShouldExit; pass++)
    {
        OnlnSelectedPlugin *plugins = pass == 0 ? g_updateLoader : g_updateRosalina;
        u32 count = pass == 0 ? g_updateLoaderCount : g_updateRosalinaCount;

        for (u32 i = 0; i < count && !*api->menuShouldExit; i++)
        {
            OnlnSelectedPlugin *plugin = &plugins[i];
            Result result;
            if (!PLUGIN_onln_Listed(plugin, showAll))
                continue;

            position++;
            PLUGIN_onln_DrawUpdateStatus(api, g_updateUpdating, plugin, position, total, 0);
            result = PLUGIN_onln_InstallUpdate(api, plugin);
            if (R_FAILED(result))
            {
                failed++;
                continue;
            }

            PLUGIN_onln_MarkInstalled(plugin);
            updated++;
            if (installedAny)
                *installedAny = true;
        }
    }

    if (!*api->menuShouldExit)
    {
        PLUGIN_onln_DrawUpdateAllSummary(api, updated, failed);
        PLUGIN_onln_WaitBack(api);
    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunUpdateList(const BlurOnlineApi *api)
{
    u32 selected = 0;
    u32 first = 0;
    bool installedAny = false;
    bool showAll = false;
    bool redraw = true;

    for (;;)
    {
        u32 available = PLUGIN_onln_CountListed(showAll);
        bool hasInstallAll = available > 1u;
        u32 installAllIndex = available;
        u32 total = available + (hasInstallAll ? 1u : 0u);

        if (*api->menuShouldExit)
            return;

        if (!total)
        {
            selected = 0;
            first = 0;
        }
        else
        {
            if (selected >= total)
                selected = total - 1u;
            if (total <= ONLN_UPDATE_VISIBLE_ITEMS)
                first = 0;
            else
            {
                if (first > selected)
                    first = selected;
                if (selected >= first + ONLN_UPDATE_VISIBLE_ITEMS)
                    first = selected - ONLN_UPDATE_VISIBLE_ITEMS + 1u;
                if (first > total - ONLN_UPDATE_VISIBLE_ITEMS)
                    first = total - ONLN_UPDATE_VISIBLE_ITEMS;
            }
        }

        if (redraw)
        {
            PLUGIN_onln_DrawUpdateList(api, first, selected, showAll);
            redraw = false;
        }

        {
            u32 pressed = api->waitInputWithTimeout(50);
            if (pressed & KEY_B)
                return;
            if (pressed & KEY_Y)
            {
                showAll = !showAll;
                selected = 0;
                first = 0;
                redraw = true;
                continue;
            }
            if (!total)
                continue;
            if (pressed & KEY_DOWN)
            {
                u32 oldSelected = selected;
                u32 oldFirst = first;

                if (selected + 1u >= total)
                {
                    selected = 0;
                    first = 0;
                }
                else
                {
                    selected++;
                    if (selected >= first + ONLN_UPDATE_VISIBLE_ITEMS)
                        first++;
                }

                if (first != oldFirst)
                    redraw = true;
                else if (selected != oldSelected)
                    PLUGIN_onln_RedrawUpdateSelection(api, first, oldSelected, selected, showAll);
                continue;
            }
            if (pressed & KEY_UP)
            {
                u32 oldSelected = selected;
                u32 oldFirst = first;

                if (!selected)
                {
                    selected = total - 1u;
                    first = total > ONLN_UPDATE_VISIBLE_ITEMS ? total - ONLN_UPDATE_VISIBLE_ITEMS : 0u;
                }
                else
                {
                    selected--;
                    if (selected < first)
                        first--;
                }

                if (first != oldFirst)
                    redraw = true;
                else if (selected != oldSelected)
                    PLUGIN_onln_RedrawUpdateSelection(api, first, oldSelected, selected, showAll);
                continue;
            }
            if (pressed & KEY_A)
            {
                if (hasInstallAll && selected == installAllIndex)
                {
                    PLUGIN_onln_UpdateAll(api, &installedAny, showAll);
                    redraw = true;
                    continue;
                }
                else
                {
                    OnlnSelectedPlugin *plugin;
                    plugin = PLUGIN_onln_GetListed(selected, showAll);
                    Result result;
                    if (!plugin)
                        continue;

                    PLUGIN_onln_DrawUpdateStatus(api, g_updateUpdating, plugin, 0, 0, 0);
                    result = PLUGIN_onln_InstallUpdate(api, plugin);
                    if (R_FAILED(result))
                    {
                        PLUGIN_onln_DrawUpdateStatus(api, g_updateUpdating, plugin, 0, 0, result);
                        PLUGIN_onln_WaitBack(api);
                        redraw = true;
                        continue;
                    }

                    PLUGIN_onln_MarkInstalled(plugin);
                    installedAny = true;
                    PLUGIN_onln_DrawUpdateStatus(api, g_updateInstalled, plugin, 0, 0, 0);
                    PLUGIN_onln_WaitBack(api);
                    redraw = true;
                    continue;
                }
            }
        }
    }
}

PLUGIN_CODE(onln) static void PLUGIN_onln_RunUpdates(const BlurOnlineApi *api)
{
    Result result;

    PLUGIN_onln_DrawUpdateStatus(api, g_updateRecovering, NULL, 0, 0, 0);
    result = PLUGIN_onln_RecoverInterruptedUpdate(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawUpdateStatus(api, g_updateRecoverFailed, NULL, 0, 0, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_DrawUpdateStatus(api, g_updateScanning, NULL, 0, 0, 0);
    result = PLUGIN_onln_ScanSelected(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawUpdateStatus(api, g_updateScanFailed, NULL, 0, 0, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_DrawUpdateStatus(api, g_updateFetching, NULL, 0, 0, 0);
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
    result = api->downloadToFile(g_updateManifestUrl, g_updateManifestPath, ONLN_UPDATE_MANIFEST_MAX);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DrawUpdateStatus(api, g_updateManifestFailed, NULL, 0, 0, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    result = PLUGIN_onln_ParseManifest(api);
    if (R_FAILED(result))
    {
        PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
        PLUGIN_onln_DrawUpdateStatus(api, g_updateManifestFailed, NULL, 0, 0, result);
        PLUGIN_onln_WaitBack(api);
        return;
    }

    PLUGIN_onln_RunUpdateList(api);
    PLUGIN_onln_DeleteIfExists(api, g_updateManifestPath);
}

PLUGIN_MAIN(onln) void PLUGIN_onln_Main(const BlurOnlineApi *api)
{
    u32 selected = 0;

    if (!api ||
        api->version != BLUR_ONLINE_API_VERSION ||
        !api->drawLock ||
        !api->drawUnlock ||
        !api->drawClear ||
        !api->drawString ||
        !api->drawFlush ||
        !api->waitInputWithTimeout ||
        !api->menuShouldExit ||
        !api->downloadToFile ||
        !api->downloadToMemory ||
        !api->getFileSize ||
        !api->readFile ||
        !api->writeFile ||
        !api->setFileSize ||
        !api->deleteFile ||
        !api->renameFile ||
        !api->fileExists ||
        !api->enumerateDirectory)
    {
        return;
    }

    PLUGIN_onln_DrawRoot(api, selected);
    do
    {
        u32 pressed = api->waitInputWithTimeout(50);
        if (pressed & KEY_B)
            return;
        if (pressed & (KEY_UP | KEY_DOWN))
        {
            selected ^= 1u;
            PLUGIN_onln_DrawRoot(api, selected);
            continue;
        }
        if (pressed & KEY_A)
        {
            if (selected == 0)
                PLUGIN_onln_RunUtc(api);
            else
                PLUGIN_onln_RunUpdates(api);
            if (*api->menuShouldExit)
                return;
            PLUGIN_onln_DrawRoot(api, selected);
        }
    } while (!*api->menuShouldExit);
}