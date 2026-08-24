#include <3ds.h>
#include "minisoc.h"

#define PLUGIN_CODE(id)   __attribute__((section(".plugin_" #id), used))
#define PLUGIN_MAIN(id)   __attribute__((section(".plugin_" #id "_entry"), used))
#define PLUGIN_RODATA(id) __attribute__((section(".pluginrodata_" #id), used))
#define PLUGIN_DATA(id)   __attribute__((section(".plugindata_" #id), used))
#define PLUGIN_BSS(id)    __attribute__((section(".pluginbss_" #id), used))

#define BLUR_HTTPS_HOST_API_VERSION 1u
#define BLUR_HTTPS_API_VERSION 2u
#define HTTPS_MAX_FILE_SIZE 0x20000u
#define HTTPS_DOWNLOAD_CHUNK_SIZE 0x1000u
#define HTTPS_LOW 0x10000000u
#define HTTPS_HIGH 0x14000000u
#define HTTPS_TLS_HEAP_SIZE 0x18000u
#define HTTPS_TLS_DNS_BUFFER_SIZE 512u
#define HTTPS_TLS_TCP_PORT 443u
#define HTTPS_TLS_DNS_PORT 53u
#define HTTPS_TLS_DNS_TIMEOUT_MS 3000

#define PLUGIN_htps_OnlineSetFailure(stage, result) ((void)0)

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

typedef struct
{
    u32 size;
    u32 used;
} HttpsTlsHeapBlock;

PLUGIN_DATA(htps) static const BlurHttpsHostApi *g_httpsHost = NULL;
PLUGIN_RODATA(htps) static const char g_httpsOnlineEmptyPath[] = "";
PLUGIN_RODATA(htps) static const char g_httpsTlsSslService[] = "ssl:C";
PLUGIN_RODATA(htps) static const char g_httpsTlsPsService[] = "ps:ps";
PLUGIN_RODATA(htps) static const char g_httpsTlsHttpsPrefix[] = "https://";
PLUGIN_RODATA(htps) static const char g_httpsTlsRootPath[] = "/";
PLUGIN_RODATA(htps) static const char g_httpsTlsHttpGet[] = "GET ";
PLUGIN_RODATA(htps) static const char g_httpsTlsHttpVersionHost[] = " HTTP/1.1\r\nHost: ";
PLUGIN_RODATA(htps) static const char g_httpsTlsHttpTail[] = "\r\nUser-Agent: Nexus3DS-SysPlugin/1\r\nConnection: close\r\n\r\n";
PLUGIN_RODATA(htps) static const char g_httpsTlsContentLength[] = "Content-Length";
PLUGIN_RODATA(htps) static const u32 g_httpsTlsDnsServers[] = { 0x08080808u, 0x01010101u };
PLUGIN_BSS(htps) static char g_httpsTlsHost[96];
PLUGIN_BSS(htps) static u8 g_httpsTlsDnsBuffer[HTTPS_TLS_DNS_BUFFER_SIZE];
PLUGIN_BSS(htps) static u32 g_httpsTlsHeapBase;
PLUGIN_BSS(htps) static u32 g_httpsTlsHeapSize;

#define HTTPS_HOST__FSUSER_OpenArchive (g_httpsHost->FSUSER_OpenArchive)
#define HTTPS_HOST__FSUSER_CloseArchive (g_httpsHost->FSUSER_CloseArchive)
#define HTTPS_HOST__FSUSER_OpenFile (g_httpsHost->FSUSER_OpenFile)
#define HTTPS_HOST__FSFILE_Write (g_httpsHost->FSFILE_Write)
#define HTTPS_HOST__FSFILE_SetSize (g_httpsHost->FSFILE_SetSize)
#define HTTPS_HOST__FSFILE_Close (g_httpsHost->FSFILE_Close)
#define HTTPS_HOST__FSUSER_DeleteFile (g_httpsHost->FSUSER_DeleteFile)
#define HTTPS_HOST__fsMakePath (g_httpsHost->fsMakePath)
#define HTTPS_HOST__svcControlMemoryUnsafe (g_httpsHost->svcControlMemoryUnsafe)
#define HTTPS_HOST__svcQueryMemory (g_httpsHost->svcQueryMemory)
#define HTTPS_HOST__srvGetServiceHandle (g_httpsHost->srvGetServiceHandle)
#define HTTPS_HOST__miniSocInit (g_httpsHost->miniSocInit)
#define HTTPS_HOST__miniSocExit (g_httpsHost->miniSocExit)
#define HTTPS_HOST__socSocket (g_httpsHost->socSocket)
#define HTTPS_HOST__socConnect (g_httpsHost->socConnect)
#define HTTPS_HOST__socPoll (g_httpsHost->socPoll)
#define HTTPS_HOST__socSendto (g_httpsHost->socSendto)
#define HTTPS_HOST__socRecvfrom (g_httpsHost->socRecvfrom)
#define HTTPS_HOST__socClose (g_httpsHost->socClose)

extern Result PLUGIN_htps_OnlineSvcSendSyncRequest(Handle handle);
extern Result PLUGIN_htps_OnlineSvcCloseHandle(Handle handle);
extern int PLUGIN_htps_TlsConnect(
    void **out,
    const char *host,
    void *bio,
    int (*rng)(void *, unsigned char *, size_t),
    void *rngCtx,
    int (*sendFn)(void *, const unsigned char *, size_t),
    int (*recvFn)(void *, unsigned char *, size_t)
);
extern int PLUGIN_htps_TlsWrite(void *tls, const void *buffer, size_t size);
extern int PLUGIN_htps_TlsRead(void *tls, void *buffer, size_t size);
extern void PLUGIN_htps_TlsClose(void *tls);

__asm__(
    ".arm\n"
    ".section .plugin_htps, \"ax\", %progbits\n"
    ".balign 4\n"
    ".global PLUGIN_htps_OnlineSvcSendSyncRequest\n"
    ".type PLUGIN_htps_OnlineSvcSendSyncRequest, %function\n"
    "PLUGIN_htps_OnlineSvcSendSyncRequest:\n"
    "svc 0x32\n"
    "bx lr\n"
    ".global PLUGIN_htps_OnlineSvcCloseHandle\n"
    ".type PLUGIN_htps_OnlineSvcCloseHandle, %function\n"
    "PLUGIN_htps_OnlineSvcCloseHandle:\n"
    "svc 0x23\n"
    "bx lr\n"
);

PLUGIN_CODE(htps) void *PLUGIN_htps_TlsMemcpy(void *dst, const void *src, size_t size)
{
    volatile u8 *d = (volatile u8 *)dst;
    volatile const u8 *s = (volatile const u8 *)src;
    while (size--)
        *d++ = *s++;
    return dst;
}

PLUGIN_CODE(htps) void *PLUGIN_htps_TlsMemmove(void *dst, const void *src, size_t size)
{
    volatile u8 *d = (volatile u8 *)dst;
    volatile const u8 *s = (volatile const u8 *)src;

    if (d < s)
    {
        while (size--)
            *d++ = *s++;
    }
    else if (d > s)
    {
        d += size;
        s += size;
        while (size--)
            *--d = *--s;
    }
    return dst;
}

PLUGIN_CODE(htps) void *PLUGIN_htps_TlsMemset(void *dst, int value, size_t size)
{
    volatile u8 *d = (volatile u8 *)dst;
    while (size--)
        *d++ = (u8)value;
    return dst;
}

PLUGIN_CODE(htps) int PLUGIN_htps_TlsMemcmp(const void *a, const void *b, size_t size)
{
    volatile const u8 *x = (volatile const u8 *)a;
    volatile const u8 *y = (volatile const u8 *)b;
    while (size--)
    {
        if (*x != *y)
            return *x < *y ? -1 : 1;
        x++;
        y++;
    }
    return 0;
}

PLUGIN_CODE(htps) size_t PLUGIN_htps_TlsStrlen(const char *text)
{
    size_t size = 0;
    while (*(volatile const char *)&text[size])
        size++;
    return size;
}

PLUGIN_CODE(htps) int PLUGIN_htps_TlsStrcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (u8)*a - (u8)*b;
}

PLUGIN_CODE(htps) int PLUGIN_htps_TlsStrncmp(const char *a, const char *b, size_t size)
{
    while (size && *a && *a == *b)
    {
        a++;
        b++;
        size--;
    }
    return size ? (u8)*a - (u8)*b : 0;
}

PLUGIN_CODE(htps) char *PLUGIN_htps_TlsStrchr(const char *text, int value)
{
    char target = (char)value;
    for (;;)
    {
        if (*text == target)
            return (char *)text;
        if (!*text)
            return NULL;
        text++;
    }
}

PLUGIN_CODE(htps) static u16 PLUGIN_htps_TlsNet16(u16 value)
{
    return (u16)((value << 8) | (value >> 8));
}

PLUGIN_CODE(htps) static Result PLUGIN_htps_TlsRandomFromService(
    const char *service,
    u32 command,
    unsigned char *output,
    size_t size
)
{
    Handle handle = 0;
    u32 *cmdbuf;
    Result result;

    if (!output || !size || size > 0x0FFFFFFFu)
        return (Result)0xD8A0A061u;

    result = HTTPS_HOST__srvGetServiceHandle(&handle, service);
    if (R_FAILED(result))
        return result;

    cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = command;
    cmdbuf[1] = (u32)size;
    cmdbuf[2] = ((u32)size << 4) | 12u;
    cmdbuf[3] = (u32)output;
    result = PLUGIN_htps_OnlineSvcSendSyncRequest(handle);
    if (R_SUCCEEDED(result))
        result = (Result)cmdbuf[1];
    (void)PLUGIN_htps_OnlineSvcCloseHandle(handle);
    return result;
}

PLUGIN_CODE(htps) static int PLUGIN_htps_TlsRandom(
    void *ctx,
    unsigned char *output,
    size_t size
)
{
    Result result;
    (void)ctx;

    result = PLUGIN_htps_TlsRandomFromService(
        g_httpsTlsSslService,
        0x00110042u,
        output,
        size);
    if (R_FAILED(result))
        result = PLUGIN_htps_TlsRandomFromService(
            g_httpsTlsPsService,
            0x000D0042u,
            output,
            size);
    return R_SUCCEEDED(result) ? 0 : -1;
}

PLUGIN_CODE(htps) static int PLUGIN_htps_TlsBioSend(
    void *ctx,
    const unsigned char *buffer,
    size_t size
)
{
    int socket = (int)(u32)ctx;
    ssize_t sent = HTTPS_HOST__socSendto(socket, buffer, size, 0, NULL, 0);
    return sent < 0 ? -1 : (int)sent;
}

PLUGIN_CODE(htps) static int PLUGIN_htps_TlsBioRecv(
    void *ctx,
    unsigned char *buffer,
    size_t size
)
{
    int socket = (int)(u32)ctx;
    ssize_t received = HTTPS_HOST__socRecvfrom(socket, buffer, size, 0, NULL, NULL);
    return received < 0 ? -1 : (int)received;
}

PLUGIN_CODE(htps) static u16 PLUGIN_htps_TlsRead16(const u8 *data)
{
    return (u16)(((u16)data[0] << 8) | data[1]);
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsSkipDnsName(
    const u8 *buffer,
    u32 size,
    u32 *position
)
{
    u32 pos = *position;
    u32 labels = 0;

    while (pos < size && labels++ < 128u)
    {
        u8 length = buffer[pos++];
        if (!length)
        {
            *position = pos;
            return true;
        }
        if ((length & 0xC0u) == 0xC0u)
        {
            if (pos >= size)
                return false;
            *position = pos + 1u;
            return true;
        }
        if ((length & 0xC0u) || length > 63u || length > size - pos)
            return false;
        pos += length;
    }
    return false;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsBuildDnsQuery(
    const char *host,
    u8 *buffer,
    u32 size,
    u32 *querySize
)
{
    u32 pos = 12u;
    const char *cursor = host;

    if (size < 18u || PLUGIN_htps_TlsRandom(NULL, buffer, 2u) != 0)
        return false;

    for (u32 i = 2; i < 12u; i++)
        buffer[i] = 0;
    buffer[2] = 1u;
    buffer[5] = 1u;

    while (*cursor)
    {
        const char *label = cursor;
        u32 length = 0;

        while (*cursor && *cursor != '.')
        {
            length++;
            cursor++;
        }
        if (!length || length > 63u || pos + length + 1u >= size)
            return false;
        buffer[pos++] = (u8)length;
        for (u32 i = 0; i < length; i++)
            buffer[pos++] = (u8)label[i];
        if (*cursor == '.')
            cursor++;
    }

    if (pos + 5u > size)
        return false;
    buffer[pos++] = 0;
    buffer[pos++] = 0;
    buffer[pos++] = 1;
    buffer[pos++] = 0;
    buffer[pos++] = 1;
    *querySize = pos;
    return true;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsParseDnsResponse(
    const u8 *buffer,
    u32 size,
    u8 id0,
    u8 id1,
    u32 *ip
)
{
    u32 pos = 12u;
    u16 questions;
    u16 answers;

    if (size < 12u || buffer[0] != id0 || buffer[1] != id1 ||
        !(buffer[2] & 0x80u) || (buffer[3] & 0x0Fu))
        return false;

    questions = PLUGIN_htps_TlsRead16(buffer + 4u);
    answers = PLUGIN_htps_TlsRead16(buffer + 6u);

    for (u32 i = 0; i < questions; i++)
    {
        if (!PLUGIN_htps_TlsSkipDnsName(buffer, size, &pos) || pos + 4u > size)
            return false;
        pos += 4u;
    }

    for (u32 i = 0; i < answers; i++)
    {
        u16 type;
        u16 classId;
        u16 dataSize;

        if (!PLUGIN_htps_TlsSkipDnsName(buffer, size, &pos) || pos + 10u > size)
            return false;
        type = PLUGIN_htps_TlsRead16(buffer + pos);
        classId = PLUGIN_htps_TlsRead16(buffer + pos + 2u);
        dataSize = PLUGIN_htps_TlsRead16(buffer + pos + 8u);
        pos += 10u;
        if (dataSize > size - pos)
            return false;

        if (type == 1u && classId == 1u && dataSize == 4u)
        {
            u8 *dst = (u8 *)ip;
            dst[0] = buffer[pos];
            dst[1] = buffer[pos + 1u];
            dst[2] = buffer[pos + 2u];
            dst[3] = buffer[pos + 3u];
            return true;
        }
        pos += dataSize;
    }
    return false;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsResolve(const char *host, u32 *ip)
{
    u32 querySize = 0;

    if (!PLUGIN_htps_TlsBuildDnsQuery(
            host,
            g_httpsTlsDnsBuffer,
            sizeof(g_httpsTlsDnsBuffer),
            &querySize))
        return false;

    for (u32 server = 0; server < 2u; server++)
    {
        int socket = HTTPS_HOST__socSocket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in address;
        struct pollfd pollfd;
        ssize_t received;
        u8 id0 = g_httpsTlsDnsBuffer[0];
        u8 id1 = g_httpsTlsDnsBuffer[1];
        bool ok = false;

        if (socket < 0)
            continue;

        PLUGIN_htps_TlsMemset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = PLUGIN_htps_TlsNet16(HTTPS_TLS_DNS_PORT);
        address.sin_addr.s_addr = g_httpsTlsDnsServers[server];

        if (HTTPS_HOST__socSendto(
                socket,
                g_httpsTlsDnsBuffer,
                querySize,
                0,
                (const struct sockaddr *)&address,
                sizeof(address)) < 0)
            goto dns_done;

        pollfd.fd = socket;
        pollfd.events = POLLIN;
        pollfd.revents = 0;
        if (HTTPS_HOST__socPoll(&pollfd, 1, HTTPS_TLS_DNS_TIMEOUT_MS) <= 0 ||
            !(pollfd.revents & POLLIN))
            goto dns_done;

        received = HTTPS_HOST__socRecvfrom(
            socket,
            g_httpsTlsDnsBuffer,
            sizeof(g_httpsTlsDnsBuffer),
            0,
            NULL,
            NULL);
        if (received > 0)
            ok = PLUGIN_htps_TlsParseDnsResponse(
                g_httpsTlsDnsBuffer,
                (u32)received,
                id0,
                id1,
                ip);

dns_done:
        (void)HTTPS_HOST__socClose(socket);
        if (ok)
            return true;
    }
    return false;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsParseUrl(
    const char *url,
    const char **path
)
{
    const char *prefix = g_httpsTlsHttpsPrefix;
    u32 hostSize = 0;

    while (*prefix)
    {
        if (*url++ != *prefix++)
            return false;
    }

    while (*url && *url != '/')
    {
        if (hostSize + 1u >= sizeof(g_httpsTlsHost) || *url == ':')
            return false;
        g_httpsTlsHost[hostSize++] = *url++;
    }
    if (!hostSize)
        return false;
    g_httpsTlsHost[hostSize] = 0;
    *path = *url ? url : g_httpsTlsRootPath;
    return true;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsAppend(
    u8 *buffer,
    u32 size,
    u32 *position,
    const char *text
)
{
    u32 pos = *position;
    while (*text)
    {
        if (pos >= size)
            return false;
        buffer[pos++] = (u8)*text++;
    }
    *position = pos;
    return true;
}

PLUGIN_CODE(htps) static char PLUGIN_htps_TlsLower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsHeaderNameEqual(
    const u8 *line,
    u32 lineSize,
    const char *name
)
{
    u32 i = 0;
    while (name[i])
    {
        if (i >= lineSize ||
            PLUGIN_htps_TlsLower((char)line[i]) != PLUGIN_htps_TlsLower(name[i]))
            return false;
        i++;
    }
    return i == lineSize;
}

PLUGIN_CODE(htps) static bool PLUGIN_htps_TlsParseHttpHeaders(
    const u8 *buffer,
    u32 headerSize,
    u32 *contentSize
)
{
    u32 pos = 0;
    u32 lineEnd = 0;
    bool foundLength = false;
    u32 lengthValue = 0;

    while (lineEnd + 1u < headerSize &&
           !(buffer[lineEnd] == '\r' && buffer[lineEnd + 1u] == '\n'))
        lineEnd++;
    if (lineEnd + 1u >= headerSize || lineEnd < 12u ||
        buffer[0] != 'H' || buffer[1] != 'T' || buffer[2] != 'T' || buffer[3] != 'P')
        return false;

    while (pos < lineEnd && buffer[pos] != ' ')
        pos++;
    if (pos + 4u > lineEnd || buffer[pos + 1u] != '2' ||
        buffer[pos + 2u] != '0' || buffer[pos + 3u] != '0')
        return false;

    pos = lineEnd + 2u;
    while (pos + 1u < headerSize)
    {
        u32 start = pos;
        u32 end = pos;
        u32 colon;

        while (end + 1u < headerSize &&
               !(buffer[end] == '\r' && buffer[end + 1u] == '\n'))
            end++;
        if (end + 1u >= headerSize)
            return false;
        if (end == start)
            break;

        colon = start;
        while (colon < end && buffer[colon] != ':')
            colon++;
        if (colon < end &&
            PLUGIN_htps_TlsHeaderNameEqual(
                buffer + start,
                colon - start,
                g_httpsTlsContentLength))
        {
            u32 value = 0;
            u32 at = colon + 1u;
            bool digit = false;
            while (at < end && (buffer[at] == ' ' || buffer[at] == '\t'))
                at++;
            while (at < end && buffer[at] >= '0' && buffer[at] <= '9')
            {
                u32 next = value * 10u + (u32)(buffer[at] - '0');
                if (next < value)
                    return false;
                value = next;
                digit = true;
                at++;
            }
            if (!digit)
                return false;
            lengthValue = value;
            foundLength = true;
        }
        pos = end + 2u;
    }

    if (!foundLength)
        return false;
    *contentSize = lengthValue;
    return true;
}


PLUGIN_CODE(htps) static bool PLUGIN_htps_OnlineAdd32(u32 a, u32 b, u32 *out)
{
    u32 value = a + b;
    if (value < a)
        return false;
    *out = value;
    return true;
}


PLUGIN_CODE(htps) static bool PLUGIN_htps_OnlineFindFreeRange(u32 size, u32 *outBase)
{
    MemInfo info;
    PageInfo page;
    u32 scan = HTTPS_LOW;

    while (scan < HTTPS_HIGH)
    {
        u32 end;
        u32 base;

        if (R_FAILED(HTTPS_HOST__svcQueryMemory(&info, &page, scan)))
            return false;

        end = info.base_addr + info.size;
        if (end <= scan)
            return false;

        if (info.state == MEMSTATE_FREE)
        {
            base = (info.base_addr + 0xFFFu) & ~0xFFFu;
            if (base < HTTPS_LOW)
                base = HTTPS_LOW;

            if (base < HTTPS_HIGH &&
                size <= HTTPS_HIGH - base &&
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


PLUGIN_CODE(htps) static Result PLUGIN_htps_TlsHeapInit(void)
{
    u32 base = 0;
    u32 allocated = 0;
    HttpsTlsHeapBlock *block;
    Result result;

    if (g_httpsTlsHeapBase)
        return 0;
    if (!PLUGIN_htps_OnlineFindFreeRange(HTTPS_TLS_HEAP_SIZE, &base))
        return (Result)0xD8A0A047u;

    result = HTTPS_HOST__svcControlMemoryUnsafe(
        &allocated,
        base,
        HTTPS_TLS_HEAP_SIZE,
        MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
        MEMPERM_READWRITE);
    if (R_FAILED(result) || !allocated)
        return R_FAILED(result) ? result : (Result)0xD8A0A047u;

    g_httpsTlsHeapBase = allocated;
    g_httpsTlsHeapSize = HTTPS_TLS_HEAP_SIZE;
    block = (HttpsTlsHeapBlock *)allocated;
    block->size = HTTPS_TLS_HEAP_SIZE - sizeof(*block);
    block->used = 0;
    return 0;
}

PLUGIN_CODE(htps) static void PLUGIN_htps_TlsHeapDestroy(void)
{
    u32 out;
    if (!g_httpsTlsHeapBase)
        return;
    (void)HTTPS_HOST__svcControlMemoryUnsafe(
        &out,
        g_httpsTlsHeapBase,
        g_httpsTlsHeapSize,
        MEMOP_FREE | MEMOP_REGION_SYSTEM,
        MEMPERM_DONTCARE);
    g_httpsTlsHeapBase = 0;
    g_httpsTlsHeapSize = 0;
}

PLUGIN_CODE(htps) void *PLUGIN_htps_TlsCalloc(size_t count, size_t itemSize)
{
    u32 bytes;
    u32 at;
    u32 end;

    if (!g_httpsTlsHeapBase || !count || !itemSize ||
        count > 0xFFFFFFFFu / itemSize)
        return NULL;
    bytes = (u32)(count * itemSize);
    if (bytes > 0xFFFFFFF8u)
        return NULL;
    bytes = (bytes + 7u) & ~7u;
    at = g_httpsTlsHeapBase;
    end = g_httpsTlsHeapBase + g_httpsTlsHeapSize;

    while (at + sizeof(HttpsTlsHeapBlock) <= end)
    {
        HttpsTlsHeapBlock *block = (HttpsTlsHeapBlock *)at;
        u32 payload = at + sizeof(*block);
        u32 next;

        if (block->size > end - payload)
            return NULL;
        next = payload + block->size;
        if (!block->used && block->size >= bytes)
        {
            u32 remaining = block->size - bytes;
            if (remaining >= sizeof(*block) + 8u)
            {
                HttpsTlsHeapBlock *split = (HttpsTlsHeapBlock *)(payload + bytes);
                split->size = remaining - sizeof(*split);
                split->used = 0;
                block->size = bytes;
            }
            block->used = 1;
            PLUGIN_htps_TlsMemset((void *)payload, 0, block->size);
            return (void *)payload;
        }
        if (next <= at || next == end)
            break;
        at = next;
    }
    return NULL;
}

PLUGIN_CODE(htps) void PLUGIN_htps_TlsFree(void *pointer)
{
    u32 target = (u32)pointer;
    u32 at;
    u32 end;

    if (!pointer || !g_httpsTlsHeapBase)
        return;
    at = g_httpsTlsHeapBase;
    end = g_httpsTlsHeapBase + g_httpsTlsHeapSize;

    while (at + sizeof(HttpsTlsHeapBlock) <= end)
    {
        HttpsTlsHeapBlock *block = (HttpsTlsHeapBlock *)at;
        u32 payload = at + sizeof(*block);
        u32 next;

        if (block->size > end - payload)
            return;
        next = payload + block->size;
        if (payload == target)
        {
            block->used = 0;
            break;
        }
        if (next <= at || next == end)
            return;
        at = next;
    }

    at = g_httpsTlsHeapBase;
    while (at + sizeof(HttpsTlsHeapBlock) <= end)
    {
        HttpsTlsHeapBlock *block = (HttpsTlsHeapBlock *)at;
        u32 payload = at + sizeof(*block);
        u32 next = payload + block->size;
        if (block->size > end - payload || next >= end)
            break;
        {
            HttpsTlsHeapBlock *following = (HttpsTlsHeapBlock *)next;
            if (!block->used && !following->used)
            {
                block->size += sizeof(*following) + following->size;
                continue;
            }
        }
        at = next;
    }
}


PLUGIN_CODE(htps) static Result PLUGIN_htps_OnlineDownload(
    const char *url,
    const char *path,
    void *output,
    u32 maxSize,
    u32 *actualSize
)
{
    const char *urlPath = NULL;
    FS_Archive archive = 0;
    Handle file = 0;
    void *tls = NULL;
    int tcpSocket = -1;
    u32 ip = 0;
    u32 bufferBase = 0;
    u32 allocated = 0;
    u32 headerBytes = 0;
    u32 headerEnd = 0;
    u32 contentSize = 0;
    u32 totalWritten = 0;
    bool toFile = path != NULL;
    bool archiveOpen = false;
    bool fileOpen = false;
    bool socReady = false;
    bool heapReady = false;
    bool bufferReady = false;
    bool ok = false;
    Result result = 0;

    if (actualSize)
        *actualSize = 0;

    if (!url || !*url || !maxSize || maxSize > HTTPS_MAX_FILE_SIZE ||
        (toFile && !*path) || (!toFile && !output))
    {
        result = (Result)0xD8A0A04Bu;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsParseUrl, result);
        goto done;
    }

    if (!PLUGIN_htps_TlsParseUrl(url, &urlPath))
    {
        result = (Result)0xD8A0A04Bu;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsParseUrl, result);
        goto done;
    }

    result = PLUGIN_htps_TlsHeapInit();
    if (R_FAILED(result))
    {
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsAllocate, result);
        goto done;
    }
    heapReady = true;

    result = HTTPS_HOST__miniSocInit();
    if (R_FAILED(result))
    {
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsInitSoc, result);
        goto done;
    }
    socReady = true;

    if (!PLUGIN_htps_TlsResolve(g_httpsTlsHost, &ip))
    {
        result = (Result)0xD8A0A062u;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsDns, result);
        goto done;
    }

    tcpSocket = HTTPS_HOST__socSocket(AF_INET, SOCK_STREAM, 0);
    if (tcpSocket < 0)
    {
        result = (Result)0xD8A0A063u;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsConnect, result);
        goto done;
    }

    {
        struct sockaddr_in address;
        PLUGIN_htps_TlsMemset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = PLUGIN_htps_TlsNet16(HTTPS_TLS_TCP_PORT);
        address.sin_addr.s_addr = ip;
        if (HTTPS_HOST__socConnect(
                tcpSocket,
                (const struct sockaddr *)&address,
                sizeof(address)) < 0)
        {
            result = (Result)0xD8A0A064u;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsConnect, result);
            goto done;
        }
    }

    {
        int tlsResult = PLUGIN_htps_TlsConnect(
            &tls,
            g_httpsTlsHost,
            (void *)(u32)tcpSocket,
            PLUGIN_htps_TlsRandom,
            NULL,
            PLUGIN_htps_TlsBioSend,
            PLUGIN_htps_TlsBioRecv);
        if (tlsResult != 0)
        {
            result = (Result)tlsResult;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHandshake, result);
            goto done;
        }
    }

    if (!PLUGIN_htps_OnlineFindFreeRange(HTTPS_DOWNLOAD_CHUNK_SIZE, &bufferBase))
    {
        result = (Result)0xD8A0A047u;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsAllocate, result);
        goto done;
    }

    result = HTTPS_HOST__svcControlMemoryUnsafe(
        &allocated,
        bufferBase,
        HTTPS_DOWNLOAD_CHUNK_SIZE,
        MEMOP_ALLOC | MEMOP_REGION_SYSTEM,
        MEMPERM_READWRITE);
    if (R_FAILED(result) || !allocated)
    {
        if (R_SUCCEEDED(result))
            result = (Result)0xD8A0A047u;
        PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsAllocate, result);
        goto done;
    }
    bufferBase = allocated;
    bufferReady = true;

    {
        u8 *buffer = (u8 *)bufferBase;
        u32 requestSize = 0;

        if (!PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, g_httpsTlsHttpGet) ||
            !PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, urlPath) ||
            !PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, g_httpsTlsHttpVersionHost) ||
            !PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, g_httpsTlsHost) ||
            !PLUGIN_htps_TlsAppend(buffer, HTTPS_DOWNLOAD_CHUNK_SIZE, &requestSize, g_httpsTlsHttpTail))
        {
            result = (Result)0xD8A0A065u;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHttp, result);
            goto done;
        }

        {
            int tlsResult = PLUGIN_htps_TlsWrite(tls, buffer, requestSize);
            if (tlsResult != 0)
            {
                result = (Result)tlsResult;
                PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHttp, result);
                goto done;
            }
        }

        while (!headerEnd && headerBytes < HTTPS_DOWNLOAD_CHUNK_SIZE)
        {
            int received = PLUGIN_htps_TlsRead(
                tls,
                buffer + headerBytes,
                HTTPS_DOWNLOAD_CHUNK_SIZE - headerBytes);
            if (received <= 0)
            {
                result = received < 0 ? (Result)received : (Result)0xD8A0A066u;
                PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHttp, result);
                goto done;
            }
            headerBytes += (u32)received;
            for (u32 i = 3u; i < headerBytes; i++)
            {
                if (buffer[i - 3u] == '\r' && buffer[i - 2u] == '\n' &&
                    buffer[i - 1u] == '\r' && buffer[i] == '\n')
                {
                    headerEnd = i + 1u;
                    break;
                }
            }
        }

        if (!headerEnd ||
            !PLUGIN_htps_TlsParseHttpHeaders(buffer, headerEnd, &contentSize) ||
            !contentSize || contentSize > maxSize)
        {
            result = (Result)0xD8A0A04Du;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsHttp, result);
            goto done;
        }
    }

    if (toFile)
    {
        result = HTTPS_HOST__FSUSER_OpenArchive(
            &archive,
            ARCHIVE_SDMC,
            HTTPS_HOST__fsMakePath(PATH_EMPTY, g_httpsOnlineEmptyPath));
        if (R_FAILED(result))
        {
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
            goto done;
        }
        archiveOpen = true;

        (void)HTTPS_HOST__FSUSER_DeleteFile(
            archive,
            HTTPS_HOST__fsMakePath(PATH_ASCII, path));

        result = HTTPS_HOST__FSUSER_OpenFile(
            &file,
            archive,
            HTTPS_HOST__fsMakePath(PATH_ASCII, path),
            FS_OPEN_CREATE | FS_OPEN_WRITE,
            0);
        if (R_FAILED(result))
        {
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
            goto done;
        }
        fileOpen = true;

        result = HTTPS_HOST__FSFILE_SetSize(file, contentSize);
        if (R_FAILED(result))
        {
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
            goto done;
        }
    }

    {
        u8 *buffer = (u8 *)bufferBase;
        u32 bodyBytes = headerBytes - headerEnd;

        if (bodyBytes > contentSize)
        {
            result = (Result)0xD8A0A04Cu;
            PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsReceive, result);
            goto done;
        }

        if (bodyBytes)
        {
            if (toFile)
            {
                u32 written = 0;
                result = HTTPS_HOST__FSFILE_Write(
                    file,
                    &written,
                    0,
                    buffer + headerEnd,
                    bodyBytes,
                    FS_WRITE_FLUSH);
                if (R_FAILED(result) || written != bodyBytes)
                {
                    result = R_FAILED(result) ? result : (Result)0xD8A0A04Au;
                    PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
                    goto done;
                }
            }
            else
            {
                PLUGIN_htps_TlsMemcpy(output, buffer + headerEnd, bodyBytes);
            }
            totalWritten = bodyBytes;
        }

        while (totalWritten < contentSize)
        {
            u32 remaining = contentSize - totalWritten;
            u32 request = remaining > HTTPS_DOWNLOAD_CHUNK_SIZE ?
                          HTTPS_DOWNLOAD_CHUNK_SIZE : remaining;
            int received = PLUGIN_htps_TlsRead(tls, buffer, request);

            if (received <= 0 || (u32)received > request)
            {
                result = received < 0 ? (Result)received : (Result)0xD8A0A049u;
                PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsReceive, result);
                goto done;
            }

            if (toFile)
            {
                u32 written = 0;
                result = HTTPS_HOST__FSFILE_Write(
                    file,
                    &written,
                    totalWritten,
                    buffer,
                    (u32)received,
                    FS_WRITE_FLUSH);
                if (R_FAILED(result) || written != (u32)received)
                {
                    result = R_FAILED(result) ? result : (Result)0xD8A0A04Au;
                    PLUGIN_htps_OnlineSetFailure(g_httpsOnlineStageTlsSave, result);
                    goto done;
                }
            }
            else
            {
                PLUGIN_htps_TlsMemcpy((u8 *)output + totalWritten, buffer, (u32)received);
            }
            totalWritten += (u32)received;
        }
    }

    ok = totalWritten == contentSize;
    if (ok && actualSize)
        *actualSize = contentSize;
    result = ok ? 0 : (Result)0xD8A0A049u;

done:
    if (fileOpen)
        HTTPS_HOST__FSFILE_Close(file);
    if (archiveOpen)
    {
        if (!ok)
            (void)HTTPS_HOST__FSUSER_DeleteFile(
                archive,
                HTTPS_HOST__fsMakePath(PATH_ASCII, path));
        HTTPS_HOST__FSUSER_CloseArchive(archive);
    }
    if (tls)
        PLUGIN_htps_TlsClose(tls);
    if (tcpSocket >= 0)
        (void)HTTPS_HOST__socClose(tcpSocket);
    if (socReady)
        (void)HTTPS_HOST__miniSocExit();
    if (bufferReady)
    {
        u32 out;
        (void)HTTPS_HOST__svcControlMemoryUnsafe(
            &out,
            bufferBase,
            HTTPS_DOWNLOAD_CHUNK_SIZE,
            MEMOP_FREE | MEMOP_REGION_SYSTEM,
            MEMPERM_DONTCARE);
    }
    if (heapReady)
        PLUGIN_htps_TlsHeapDestroy();
    return ok ? 0 : result;
}

PLUGIN_CODE(htps) static Result PLUGIN_htps_OnlineDownloadToFile(
    const char *url,
    const char *path,
    u32 maxSize
)
{
    return PLUGIN_htps_OnlineDownload(url, path, NULL, maxSize, NULL);
}

PLUGIN_CODE(htps) static Result PLUGIN_htps_OnlineDownloadToMemory(
    const char *url,
    void *buffer,
    u32 bufferSize,
    u32 *actualSize
)
{
    return PLUGIN_htps_OnlineDownload(url, NULL, buffer, bufferSize, actualSize);
}



PLUGIN_MAIN(htps) bool PLUGIN_htps_Main(const BlurHttpsHostApi *host, BlurHttpsApi *api)
{
    if (!host || !api ||
        host->version != BLUR_HTTPS_HOST_API_VERSION ||
        !host->FSUSER_OpenArchive || !host->FSUSER_CloseArchive ||
        !host->FSUSER_OpenFile || !host->FSFILE_Write || !host->FSFILE_SetSize ||
        !host->FSFILE_Close || !host->FSUSER_DeleteFile || !host->fsMakePath ||
        !host->svcControlMemoryUnsafe || !host->svcQueryMemory ||
        !host->srvGetServiceHandle || !host->miniSocInit || !host->miniSocExit ||
        !host->socSocket || !host->socConnect || !host->socPoll ||
        !host->socSendto || !host->socRecvfrom || !host->socClose)
    {
        return false;
    }

    g_httpsHost = host;
    api->version = BLUR_HTTPS_API_VERSION;
    api->downloadToFile = PLUGIN_htps_OnlineDownloadToFile;
    api->downloadToMemory = PLUGIN_htps_OnlineDownloadToMemory;
    return true;
}