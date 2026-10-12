#pragma once
#include <cstdint>
#include <cstddef>
struct CommandInfo {
    const char *longName,*shortName;uint32_t opcode;const char* helpText;
    uint16_t needsParent,numParams;void* params;
    bool(*execute)(void*,void*,void*,void*,void*,void*,double*,uint32_t*);
    void *parse,*eval;uint32_t flags;
};
static_assert(sizeof(CommandInfo)==0x28&&offsetof(CommandInfo,execute)==0x18&&offsetof(CommandInfo,params)==0x14);
struct PluginInfo {uint32_t infoVersion;const char* name;uint32_t version;};
struct NVSEInterface {
    uint32_t nvseVersion,runtimeVersion,editorVersion,isEditor;
    bool(*RegisterCommand)(CommandInfo*);void(*SetOpcodeBase)(uint32_t);void*(*QueryInterface)(uint32_t);
    uint32_t(*GetPluginHandle)();bool(*RegisterTypedCommand)(void*,uint32_t);const char*(*GetRuntimeDirectory)();uint32_t isNogore;
};
struct NVSECommandTableInterface {
    uint32_t version;
    const CommandInfo*(*Start)();const CommandInfo*(*End)();const CommandInfo*(*GetByOpcode)(uint32_t);
    const CommandInfo*(*GetByName)(const char*);
};
struct NVSEMessagingInterface {
    struct Message {const char* sender;uint32_t type,dataLen;void* data;};
    using Callback=void(*)(Message*);
    uint32_t version;bool(*RegisterListener)(uint32_t,const char*,Callback);
    bool(*Dispatch)(uint32_t,uint32_t,void*,uint32_t,const char*);
    enum:uint32_t{ExitGame=1,ExitToMainMenu=2,PreLoadGame=6,ExitGameConsole=7,PostLoadGame=8,NewGame=14,DeferredInit=18,MainGameLoop=20};
};
