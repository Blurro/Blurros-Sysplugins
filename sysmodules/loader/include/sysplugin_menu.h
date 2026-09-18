#pragma once

#include <3ds.h>
#include "sysplugin_symbols.h"

#define SYSPLUGIN_MENU_PROVIDER_ID 0x554E454Du
#define SYSPLUGIN_MENU_LOADER_API_REVISION 2u
#define SYSPLUGIN_MENU_BRIDGE_MAX_PAYLOAD 0xC0u

typedef struct PluginMenuLoaderContext
{
    u64 titleId;
    u16 titleVersion;
    u16 reserved;
    u8 *code;
    u32 codeSize;
    u32 textSize;
    u32 roSize;
    u32 dataSize;
    u32 roAddress;
    u32 dataAddress;
    CodeSetHeader *codeSet;
    Handle process;
} PluginMenuLoaderContext;

typedef bool (*PluginMenuLoaderPrepareCallback)(PluginMenuLoaderContext *context);
typedef void (*PluginMenuLoaderStageCallback)(PluginMenuLoaderContext *context);

typedef struct PluginMenuLoaderTitlePatch
{
    const u64 *titleIds;
    u32 titleIdCount;
    PluginMenuLoaderPrepareCallback prepare;
    PluginMenuLoaderStageCallback processCreated;
    PluginMenuLoaderStageCallback loaderFinished;
    struct PluginMenuLoaderTitlePatch *next;
    u32 menuEpoch;
} PluginMenuLoaderTitlePatch;

typedef struct PluginMenuLoaderHomePatch
{
    PluginMenuLoaderPrepareCallback prepare;
    struct PluginMenuLoaderHomePatch *next;
} PluginMenuLoaderHomePatch;

bool PLUGIN_MENU_FindFreeRange(u32 size, u32 *outBase);
bool PLUGIN_MENU_TempAlloc(u32 size, u32 *outBase);
void PLUGIN_MENU_TempFree(u32 base, u32 size);
bool PLUGIN_MENU_MapPage(Handle sourceProcess, u32 sourceAddress, u32 *mappedBase, u32 *mappedAddress);
void PLUGIN_MENU_UnmapPage(u32 mappedBase);

bool PLUGIN_MENU_RegisterTitlePatch(PluginMenuLoaderTitlePatch *registration);
bool PLUGIN_MENU_UnregisterTitlePatch(PluginMenuLoaderTitlePatch *registration);
bool PLUGIN_MENU_RegisterHomePatch(PluginMenuLoaderHomePatch *registration);
bool PLUGIN_MENU_UnregisterHomePatch(PluginMenuLoaderHomePatch *registration);
bool PLUGIN_MENU_BridgeSend(u32 targetPluginId, u32 command, const void *payload, u32 payloadSize);

NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_FindFreeRange);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_TempAlloc);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_TempFree);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_MapPage);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_UnmapPage);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_RegisterTitlePatch);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_UnregisterTitlePatch);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_RegisterHomePatch);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_UnregisterHomePatch);
NEXUS_PLUGIN_EXTERNAL_FUNC(PLUGIN_MENU_BridgeSend);
