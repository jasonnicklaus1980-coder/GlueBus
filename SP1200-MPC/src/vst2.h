// Minimal hand-written VST2 ABI (no Steinberg SDK needed). Layout matches the public VST 2.4 ABI.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

struct AEffect;
typedef intptr_t (*audioMasterCallback)(struct AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
typedef intptr_t (*AEffectDispatcherProc)(struct AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
typedef void (*AEffectProcessProc)(struct AEffect*, float** inputs, float** outputs, int32_t sampleFrames);
typedef void (*AEffectSetParameterProc)(struct AEffect*, int32_t index, float parameter);
typedef float (*AEffectGetParameterProc)(struct AEffect*, int32_t index);

struct AEffect
{
    int32_t magic;                       // 'VstP'
    AEffectDispatcherProc dispatcher;
    AEffectProcessProc process;          // deprecated accumulating process
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;
    int32_t numPrograms, numParams, numInputs, numOutputs;
    int32_t flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueID;
    int32_t version;
    AEffectProcessProc processReplacing;
    void* processDoubleReplacing;
    char future[56];
};

enum { kEffectMagic = 0x56737450 };      // 'VstP'
enum { effFlagsHasEditor = 1 << 0, effFlagsCanReplacing = 1 << 4, effFlagsProgramChunks = 1 << 5,
       effFlagsIsSynth = 1 << 8 };
enum { kPlugCategEffect = 1 };
enum { audioMasterAutomate = 0, audioMasterUpdateDisplay = 42 };

enum
{
    effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4,
    effGetProgramName = 5, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12,
    effProcessEvents = 25, effCanBeAutomated = 26, effString2Parameter = 27,
    effGetProgramNameIndexed = 29, effGetPlugCategory = 35, effSetBypass = 44,
    effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effGetVendorVersion = 49, effCanDo = 51, effGetTailSize = 52, effGetVstVersion = 58,
    effStartProcess = 71, effStopProcess = 72, effSetProcessPrecision = 77
};

#ifdef __cplusplus
}
#endif
