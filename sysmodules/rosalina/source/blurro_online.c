#include <3ds.h>
#include "csvc.h"
#include "draw.h"
#include "menu.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define BLUR_ONLINE_MAGIC 0x24584E33u
#define BLUR_ONLINE_ID 0x6E6C6E6Fu
#define BLUR_ONLINE_HEADER_SIZE 0x2Cu
#define BLUR_ONLINE_API_VERSION 1u
#define BLUR_ONLINE_LOW 0x10000000u
#define BLUR_ONLINE_HIGH 0x14000000u
#define BLUR_ONLINE_MAX_FILE_SIZE 0x20000u
#define BLUR_ONLINE_MAX_RUNTIME_SIZE 0x20000u
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
} BlurOnline3nxHeader;
#pragma pack(pop)

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
} BlurOnlineApi;

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

PLUGIN_RODATA(blur) static const char g_blurOnlinePath[] = "/luma/plugins/onlineblurrotemporary.3on";
PLUGIN_RODATA(blur) static const char g_blurOnlineEmptyPath[] = "";
PLUGIN_RODATA(blur) static const char g_blurOnlineErrorTitle[] = "Online Menu";
PLUGIN_RODATA(blur) static const char g_blurOnlineErrorText[] = "Could not load onlineblurrotemporary.3on";
PLUGIN_RODATA(blur) static const char g_blurOnlineBackText[] = "press B to go back";
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
PLUGIN_DATA(blur) static const char *g_blurOnlineLastStage = g_blurOnlineStageValidate;
PLUGIN_DATA(blur) static Result g_blurOnlineLastResult = 0;
PLUGIN_BSS(blur) static char g_blurOnlineResultHex[9];
PLUGIN_RODATA(blur) static const char g_blurOnlinePlus[] = "+";
PLUGIN_RODATA(blur) static const char g_blurOnlinePipe[] = "|";
PLUGIN_RODATA(blur) static const char g_blurOnlineRail[] = "----------------------";

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
    u64 value = (u64)a * (u64)b;
    if (value > 0xFFFFFFFFULL)
        return false;
    *out = (u32)value;
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
    u64 offset,
    void *buffer,
    u32 size
)
{
    u32 read = 0;
    return R_SUCCEEDED(ONLINE_HOST__FSFILE_Read(file, &read, offset, buffer, size)) && read == size;
}

PLUGIN_CODE(blur) static bool PLUGIN_blur_OnlineFindFreeRange(u32 size, u32 *outBase)
{
    MemInfo info;
    PageInfo page;
    u32 scan = BLUR_ONLINE_LOW;

    while (scan < BLUR_ONLINE_HIGH)
    {
        u32 end;
        u32 base;

        if (R_FAILED(ONLINE_HOST__svcQueryMemory(&info, &page, scan)))
            return false;

        end = info.base_addr + info.size;
        if (end <= scan)
            return false;

        if (info.state == MEMSTATE_FREE)
        {
            base = (info.base_addr + 0xFFFu) & ~0xFFFu;
            if (base < BLUR_ONLINE_LOW)
                base = BLUR_ONLINE_LOW;

            if (base < BLUR_ONLINE_HIGH &&
                size <= BLUR_ONLINE_HIGH - base &&
                size <= end - base)
            {
                *outBase = base;
                return true;
            }
        }

        scan = end;
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
            group[0] != BLUR_ONLINE_ID ||
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

PLUGIN_CODE(blur) static void PLUGIN_blur_OnlineFree(u32 base, u32 codeSize, u32 totalSize)
{
    u32 out;

    if (!base)
        return;

    if (codeSize)
        (void)PLUGIN_blur_OnlineProtect(base, codeSize, MEMPERM_READWRITE);

    (void)ONLINE_HOST__svcControlMemoryUnsafe(
        &out,
        base,
        totalSize,
        MEMOP_FREE | MEMOP_REGION_SYSTEM,
        MEMPERM_DONTCARE
    );
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

PLUGIN_CODE(blur) static bool PLUGIN_blur_RunOnlineMenu(void)
{
    FS_Archive archive = 0;
    Handle file = 0;
    BlurOnline3nxHeader header;
    BlurOnlineApi api;
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
    bool codeProtected = false;
    bool ok = false;

    PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageValidate, 0);

    {
        Result result = ONLINE_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            ONLINE_HOST__fsMakePath(PATH_EMPTY, g_blurOnlineEmptyPath));
        if (R_FAILED(result))
        {
            PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageOpenArchive, result);
            goto done;
        }
    }
    archiveOpen = true;

    {
        Result result = ONLINE_HOST__FSUSER_OpenFile(
            &file,
            archive,
            ONLINE_HOST__fsMakePath(PATH_ASCII, g_blurOnlinePath),
            FS_OPEN_READ,
            0);
        if (R_FAILED(result))
        {
            PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageOpenFile, result);
            goto done;
        }
    }
    fileOpen = true;

    if (R_FAILED(ONLINE_HOST__FSFILE_GetSize(file, &fileSize64)) ||
        fileSize64 < BLUR_ONLINE_HEADER_SIZE ||
        fileSize64 > BLUR_ONLINE_MAX_FILE_SIZE ||
        !PLUGIN_blur_OnlineReadExact(file, 0, &header, sizeof(header)) ||
        header.magic != BLUR_ONLINE_MAGIC ||
        header.plgid != BLUR_ONLINE_ID ||
        !header.codeSize ||
        !PLUGIN_blur_OnlineAdd32(BLUR_ONLINE_HEADER_SIZE, header.fastRelocSize, &codeOffset) ||
        !PLUGIN_blur_OnlineAdd32(codeOffset, header.codeSize, &dataOffset) ||
        !PLUGIN_blur_OnlineAdd32(dataOffset, header.dataSize, &repairOffset) ||
        !PLUGIN_blur_OnlineAdd32(repairOffset, header.repairSize, &fileEnd) ||
        fileEnd > (u32)fileSize64 ||
        !PLUGIN_blur_OnlineAlignPage(header.codeSize, &codeSize) ||
        !PLUGIN_blur_OnlineAdd32(header.dataSize, header.bssSize, &dataAndBssSize) ||
        !PLUGIN_blur_OnlineAlignPage(dataAndBssSize, &dataSize) ||
        !PLUGIN_blur_OnlineAdd32(codeSize, dataSize, &totalSize) ||
        !totalSize ||
        totalSize > BLUR_ONLINE_MAX_RUNTIME_SIZE ||
        !PLUGIN_blur_OnlineValidateRepair(file, repairOffset, header.repairSize))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageValidate, (Result)0xD8A0A046u);
        goto done;
    }

    if (!PLUGIN_blur_OnlineFindFreeRange(totalSize, &base))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageFindMemory, (Result)0xD8A0A047u);
        goto done;
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
            PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageAllocate, result);
            goto done;
        }
    }
    base = allocated;
    memoryAllocated = true;
    dataAddress = base + codeSize;

    if (!PLUGIN_blur_OnlineReadExact(file, codeOffset, (void *)base, header.codeSize) ||
        (header.dataSize && !PLUGIN_blur_OnlineReadExact(file, dataOffset, (void *)dataAddress, header.dataSize)))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageRead, (Result)0xD8A0A048u);
        goto done;
    }

    for (u32 i = 0; i < header.bssSize; i++)
        *(volatile u8 *)(dataAddress + header.dataSize + i) = 0;

    if (!PLUGIN_blur_OnlineApplyRelocs(
        file,
        BLUR_ONLINE_HEADER_SIZE,
        header.fastRelocSize,
        base,
        totalSize))
    {
        PLUGIN_blur_OnlineSetFailure(g_blurOnlineStageRelocate, (Result)0xD8A0A049u);
        goto done;
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
            goto done;
        }
    }
    codeProtected = true;
    ONLINE_HOST__svcInvalidateEntireInstructionCache();

    api.version = BLUR_ONLINE_API_VERSION;
    api.drawLock = ONLINE_HOST__Draw_Lock;
    api.drawUnlock = ONLINE_HOST__Draw_Unlock;
    api.drawClear = ONLINE_HOST__Draw_ClearFramebuffer;
    api.drawString = ONLINE_HOST__Draw_DrawString;
    api.drawFlush = ONLINE_HOST__Draw_FlushFramebuffer;
    api.waitInputWithTimeout = ONLINE_HOST__waitInputWithTimeout;
    api.menuShouldExit = (volatile bool *)pluginTable_blur_online[17];

    ((void (*)(const BlurOnlineApi *))base)(&api);
    ok = true;

done:
    if (fileOpen)
        ONLINE_HOST__FSFILE_Close(file);
    if (archiveOpen)
        ONLINE_HOST__FSUSER_CloseArchive(archive);
    if (memoryAllocated)
        PLUGIN_blur_OnlineFree(base, codeProtected ? codeSize : 0u, totalSize);
    return ok;
}

PLUGIN_CODE(blur) static void PLUGIN_blur_DrawOnlineError(void)
{
    ONLINE_HOST__Draw_Lock();
    ONLINE_HOST__Draw_ClearFramebuffer();
    ONLINE_HOST__Draw_DrawString(10, 8, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePlus);
    ONLINE_HOST__Draw_DrawString(16, 8, BLUR_ONLINE_BORDER_COLOR, g_blurOnlineRail);
    ONLINE_HOST__Draw_DrawString(148, 8, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePlus);
    ONLINE_HOST__Draw_DrawString(10, 16, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePipe);
    ONLINE_HOST__Draw_DrawString(148, 16, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePipe);
    ONLINE_HOST__Draw_DrawString(10, 24, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePlus);
    ONLINE_HOST__Draw_DrawString(16, 24, BLUR_ONLINE_BORDER_COLOR, g_blurOnlineRail);
    ONLINE_HOST__Draw_DrawString(148, 24, BLUR_ONLINE_BORDER_COLOR, g_blurOnlinePlus);
    ONLINE_HOST__Draw_DrawString(20, 16, BLUR_ONLINE_TITLE_COLOR, g_blurOnlineErrorTitle);
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
    if (!PLUGIN_blur_RunOnlineMenu())
        PLUGIN_blur_DrawOnlineError();
}

PLUGIN_RODATA(onln) static const char g_onlineTitle[] = "Online Menu";
PLUGIN_RODATA(onln) static const char g_onlineHello[] = "hello from github";
PLUGIN_RODATA(onln) static const char g_onlineBack[] = "press B to go back";
PLUGIN_RODATA(onln) static const char g_onlinePlus[] = "+";
PLUGIN_RODATA(onln) static const char g_onlinePipe[] = "|";
PLUGIN_RODATA(onln) static const char g_onlineRail[] = "----------------------";

PLUGIN_CODE(onln) static void PLUGIN_onln_Draw(const BlurOnlineApi *api)
{
    api->drawLock();
    api->drawClear();
    api->drawString(10, 8, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 8, BLUR_ONLINE_BORDER_COLOR, g_onlineRail);
    api->drawString(148, 8, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(10, 16, BLUR_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(148, 16, BLUR_ONLINE_BORDER_COLOR, g_onlinePipe);
    api->drawString(10, 24, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(16, 24, BLUR_ONLINE_BORDER_COLOR, g_onlineRail);
    api->drawString(148, 24, BLUR_ONLINE_BORDER_COLOR, g_onlinePlus);
    api->drawString(20, 16, BLUR_ONLINE_TITLE_COLOR, g_onlineTitle);
    api->drawString(20, 45, COLOR_WHITE, g_onlineHello);
    api->drawString(20, 120, COLOR_GRAY, g_onlineBack);
    api->drawFlush();
    api->drawUnlock();
}

PLUGIN_MAIN(onln) void PLUGIN_onln_Main(const BlurOnlineApi *api)
{
    if (!api ||
        api->version != BLUR_ONLINE_API_VERSION ||
        !api->drawLock ||
        !api->drawUnlock ||
        !api->drawClear ||
        !api->drawString ||
        !api->drawFlush ||
        !api->waitInputWithTimeout ||
        !api->menuShouldExit)
    {
        return;
    }

    PLUGIN_onln_Draw(api);
    do
    {
        if (api->waitInputWithTimeout(50) & KEY_B)
            return;
    } while (!*api->menuShouldExit);
}