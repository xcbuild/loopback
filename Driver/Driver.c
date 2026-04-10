/*
 * AudioLoopbackDriver - Minimal virtual audio loopback driver
 * Built from scratch using Apple's AudioServerPlugIn API
 */

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define kDriver_Name            "Audio Loopback"
#define kDriver_UID           "AudioLoopback_UID"
#define kDriver_ModelUID      "AudioLoopback_Model"
#define kDriver_Manufacturer "xcbuild"

#define kChannels             2
#define kBitsPerChannel       32
#define kBytesPerFrame       (kChannels * (kBitsPerChannel / 8))
#define kSampleRate         48000.0
#define kRingBufferFrames   16384

enum {
    kObjectID_PlugIn = 1,
    kObjectID_Device = 2,
    kObjectID_Stream_Input = 3,
    kObjectID_Stream_Output = 4,
    kObjectID_Volume_Master = 5,
    kObjectID_Mute_Master = 6,
    kObjectID_Clock = 7
};

typedef struct {
    AudioServerPlugInHostRef host;
    pthread_mutex_t mutex;
    bool isRunning;
    Float64 sampleRate;
    float masterVolume;
    bool masterMute;
    float* ringBuffer;
    UInt32 ringBufferWriteIndex;
    UInt64 anchorHostTime;
    UInt64 anchorSampleTime;
} DriverState;

static DriverState gState = {
    .host = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .isRunning = false,
    .sampleRate = kSampleRate,
    .masterVolume = 1.0f,
    .masterMute = false,
    .ringBuffer = NULL,
    .ringBufferWriteIndex = 0,
    .anchorHostTime = 0,
    .anchorSampleTime = 0
};

static ULONG gRefCount = 1;

static HRESULT LoopbackDriver_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface);
static ULONG LoopbackDriver_AddRef(void* inDriver);
static ULONG LoopbackDriver_Release(void* inDriver);
static OSStatus LoopbackDriver_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost);
static OSStatus LoopbackDriver_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID);
static OSStatus LoopbackDriver_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID);
static OSStatus LoopbackDriver_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus LoopbackDriver_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus LoopbackDriver_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static OSStatus LoopbackDriver_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static Boolean LoopbackDriver_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus LoopbackDriver_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus LoopbackDriver_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus LoopbackDriver_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus LoopbackDriver_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);
static OSStatus LoopbackDriver_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, UInt32 inClientID);
static OSStatus LoopbackDriver_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, UInt32 inClientID);
static OSStatus LoopbackDriver_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed);
static OSStatus LoopbackDriver_WillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace);
static OSStatus LoopbackDriver_BeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);
static OSStatus LoopbackDriver_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer);
static OSStatus LoopbackDriver_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);

static AudioServerPlugInDriverInterface gAudioServerPlugInDriverInterface = {
    NULL,
    LoopbackDriver_QueryInterface,
    LoopbackDriver_AddRef,
    LoopbackDriver_Release,
    LoopbackDriver_Initialize,
    LoopbackDriver_CreateDevice,
    LoopbackDriver_DestroyDevice,
    LoopbackDriver_AddDeviceClient,
    LoopbackDriver_RemoveDeviceClient,
    LoopbackDriver_PerformDeviceConfigurationChange,
    LoopbackDriver_AbortDeviceConfigurationChange,
    LoopbackDriver_HasProperty,
    LoopbackDriver_IsPropertySettable,
    LoopbackDriver_GetPropertyDataSize,
    LoopbackDriver_GetPropertyData,
    LoopbackDriver_SetPropertyData,
    LoopbackDriver_StartIO,
    LoopbackDriver_StopIO,
    LoopbackDriver_GetZeroTimeStamp,
    LoopbackDriver_WillDoIOOperation,
    LoopbackDriver_BeginIOOperation,
    LoopbackDriver_DoIOOperation,
    LoopbackDriver_EndIOOperation
};

static AudioServerPlugInDriverInterface* gAudioServerPlugInDriverInterfacePtr = &gAudioServerPlugInDriverInterface;
static AudioServerPlugInDriverRef gAudioServerPlugInDriverRef = &gAudioServerPlugInDriverInterfacePtr;

void* LoopbackDriver_Create(CFAllocatorRef allocator, CFUUIDRef requestedTypeUUID) {
    if (CFEqual(requestedTypeUUID, kAudioServerPlugInTypeUUID)) {
        return gAudioServerPlugInDriverRef;
    }
    return NULL;
}

static HRESULT LoopbackDriver_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface) {
    gRefCount++;
    *outInterface = &gAudioServerPlugInDriverInterface;
    return S_OK;
}

static ULONG LoopbackDriver_AddRef(void* inDriver) {
    return ++gRefCount;
}

static ULONG LoopbackDriver_Release(void* inDriver) {
    if (gRefCount > 0) {
        gRefCount--;
    }
    return gRefCount;
}

static OSStatus LoopbackDriver_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost) {
    gState.host = inHost;
    UInt32 bufferSize = kRingBufferFrames * kChannels;
    gState.ringBuffer = (float*)calloc(bufferSize, sizeof(float));
    return gState.ringBuffer ? 0 : 1;
}

static OSStatus LoopbackDriver_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID) {
    *outDeviceObjectID = kObjectID_Device;
    return 0;
}

static OSStatus LoopbackDriver_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID) {
    return 0;
}

static OSStatus LoopbackDriver_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo) {
    return 0;
}

static OSStatus LoopbackDriver_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo) {
    return 0;
}

static OSStatus LoopbackDriver_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo) {
    return 0;
}

static OSStatus LoopbackDriver_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo) {
    return 0;
}

static Boolean LoopbackDriver_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress) {
    switch (inObjectID) {
        case kObjectID_PlugIn:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyManufacturer:
                case kAudioObjectPropertyOwnedObjects:
                case kAudioPlugInPropertyDeviceList:
                    return true;
            }
            break;
        case kObjectID_Device:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioObjectPropertyName:
                case kAudioObjectPropertyManufacturer:
                case kAudioDevicePropertyDeviceUID:
                case kAudioDevicePropertyModelUID:
                case kAudioDevicePropertyTransportType:
                case kAudioDevicePropertyClockDomain:
                case kAudioDevicePropertyDeviceIsAlive:
                case kAudioDevicePropertyDeviceIsRunning:
                case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                case kAudioDevicePropertyStreams:
                case kAudioObjectPropertyControlList:
                case kAudioDevicePropertyRelatedDevices:
                case kAudioDevicePropertyNominalSampleRate:
                    return true;
            }
            break;
        case kObjectID_Stream_Input:
        case kObjectID_Stream_Output:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioStreamPropertyIsActive:
                case kAudioStreamPropertyDirection:
                case kAudioStreamPropertyTerminalType:
                case kAudioStreamPropertyStartingChannel:
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                    return true;
            }
            break;
        case kObjectID_Volume_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                case kAudioLevelControlPropertyScalarValue:
                    return true;
            }
            break;
        case kObjectID_Mute_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                case kAudioBooleanControlPropertyValue:
                    return true;
            }
            break;
    }
    return false;
}

static OSStatus LoopbackDriver_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable) {
    *outIsSettable = false;
    if (inObjectID == kObjectID_Device && inAddress->mSelector == kAudioDevicePropertyNominalSampleRate) {
        *outIsSettable = true;
    }
    return 0;
}

static OSStatus LoopbackDriver_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize) {
    *outDataSize = 0;
    
    switch (inObjectID) {
        case kObjectID_PlugIn:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyManufacturer:
                case kAudioPlugInPropertyResourceBundle:
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                case kAudioPlugInPropertyDeviceList:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
            }
            break;
        case kObjectID_Device:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyName:
                case kAudioObjectPropertyManufacturer:
                case kAudioDevicePropertyDeviceUID:
                case kAudioDevicePropertyModelUID:
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioDevicePropertyTransportType:
                case kAudioDevicePropertyClockDomain:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyDeviceIsAlive:
                case kAudioDevicePropertyDeviceIsRunning:
                case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                    *outDataSize = 4 * sizeof(AudioObjectID);
                    break;
                case kAudioDevicePropertyStreams:
                    *outDataSize = 2 * sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyControlList:
                    *outDataSize = 2 * sizeof(AudioObjectID);
                    break;
                case kAudioDevicePropertyRelatedDevices:
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioDevicePropertyNominalSampleRate:
                    *outDataSize = sizeof(Float64);
                    break;
            }
            break;
        case kObjectID_Stream_Input:
        case kObjectID_Stream_Output:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioStreamPropertyIsActive:
                case kAudioStreamPropertyDirection:
                case kAudioStreamPropertyTerminalType:
                case kAudioStreamPropertyStartingChannel:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat:
                    *outDataSize = sizeof(AudioStreamBasicDescription);
                    break;
            }
            break;
        case kObjectID_Volume_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioLevelControlPropertyScalarValue:
                    *outDataSize = sizeof(Float32);
                    break;
            }
            break;
        case kObjectID_Mute_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                case kAudioObjectPropertyClass:
                case kAudioObjectPropertyOwner:
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioControlPropertyScope:
                case kAudioControlPropertyElement:
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioBooleanControlPropertyValue:
                    *outDataSize = sizeof(UInt32);
                    break;
            }
            break;
    }
    return 0;
}

static OSStatus LoopbackDriver_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData) {
    switch (inObjectID) {
        case kObjectID_PlugIn:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *((AudioClassID*)outData) = kAudioObjectClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *((AudioClassID*)outData) = kAudioPlugInClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *((AudioObjectID*)outData) = kAudioObjectUnknown;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyManufacturer:
                    *((CFStringRef*)outData) = CFSTR(kDriver_Manufacturer);
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                case kAudioPlugInPropertyDeviceList:
                    ((AudioObjectID*)outData)[0] = kObjectID_Device;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioPlugInPropertyResourceBundle:
                    *((CFStringRef*)outData) = CFSTR("");
                    *outDataSize = sizeof(CFStringRef);
                    break;
            }
            break;
        case kObjectID_Device:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *((AudioClassID*)outData) = kAudioObjectClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *((AudioClassID*)outData) = kAudioDeviceClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *((AudioObjectID*)outData) = kObjectID_PlugIn;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyName:
                    *((CFStringRef*)outData) = CFSTR(kDriver_Name);
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioObjectPropertyManufacturer:
                    *((CFStringRef*)outData) = CFSTR(kDriver_Manufacturer);
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioDevicePropertyDeviceUID:
                    *((CFStringRef*)outData) = CFSTR(kDriver_UID);
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioDevicePropertyModelUID:
                    *((CFStringRef*)outData) = CFSTR(kDriver_ModelUID);
                    *outDataSize = sizeof(CFStringRef);
                    break;
                case kAudioDevicePropertyTransportType:
                    *((UInt32*)outData) = kAudioDeviceTransportTypeVirtual;
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyClockDomain:
                    *((UInt32*)outData) = 0;
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyDeviceIsAlive:
                    *((UInt32*)outData) = 1;
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyDeviceIsRunning:
                    pthread_mutex_lock(&gState.mutex);
                    *((UInt32*)outData) = gState.isRunning ? 1 : 0;
                    pthread_mutex_unlock(&gState.mutex);
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioDevicePropertyDeviceCanBeDefaultDevice:
                case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
                    *((UInt32*)outData) = 1;
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioObjectPropertyOwnedObjects:
                    ((AudioObjectID*)outData)[0] = kObjectID_Stream_Input;
                    ((AudioObjectID*)outData)[1] = kObjectID_Stream_Output;
                    ((AudioObjectID*)outData)[2] = kObjectID_Volume_Master;
                    ((AudioObjectID*)outData)[3] = kObjectID_Mute_Master;
                    *outDataSize = 4 * sizeof(AudioObjectID);
                    break;
                case kAudioDevicePropertyStreams:
                    ((AudioObjectID*)outData)[0] = kObjectID_Stream_Input;
                    ((AudioObjectID*)outData)[1] = kObjectID_Stream_Output;
                    *outDataSize = 2 * sizeof(AudioObjectID);
                    break;
                case kAudioObjectPropertyControlList:
                    ((AudioObjectID*)outData)[0] = kObjectID_Volume_Master;
                    ((AudioObjectID*)outData)[1] = kObjectID_Mute_Master;
                    *outDataSize = 2 * sizeof(AudioObjectID);
                    break;
                case kAudioDevicePropertyRelatedDevices:
                    *((AudioObjectID*)outData) = kObjectID_Device;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioDevicePropertyNominalSampleRate:
                    pthread_mutex_lock(&gState.mutex);
                    *((Float64*)outData) = gState.sampleRate;
                    pthread_mutex_unlock(&gState.mutex);
                    *outDataSize = sizeof(Float64);
                    break;
            }
            break;
        case kObjectID_Stream_Input:
        case kObjectID_Stream_Output: {
            Boolean isInput = (inObjectID == kObjectID_Stream_Input);
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *((AudioClassID*)outData) = kAudioObjectClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *((AudioClassID*)outData) = kAudioStreamClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *((AudioObjectID*)outData) = kObjectID_Device;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioStreamPropertyIsActive:
                    pthread_mutex_lock(&gState.mutex);
                    *((UInt32*)outData) = gState.isRunning ? 1 : 0;
                    pthread_mutex_unlock(&gState.mutex);
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyDirection:
                    *((UInt32*)outData) = isInput ? 1 : 0;
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyTerminalType:
                    *((UInt32*)outData) = isInput ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker;
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyStartingChannel:
                    *((UInt32*)outData) = 1;
                    *outDataSize = sizeof(UInt32);
                    break;
                case kAudioStreamPropertyVirtualFormat:
                case kAudioStreamPropertyPhysicalFormat: {
                    AudioStreamBasicDescription* format = (AudioStreamBasicDescription*)outData;
                    pthread_mutex_lock(&gState.mutex);
                    format->mSampleRate = gState.sampleRate;
                    pthread_mutex_unlock(&gState.mutex);
                    format->mFormatID = kAudioFormatLinearPCM;
                    format->mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
                    format->mBytesPerPacket = kBytesPerFrame;
                    format->mFramesPerPacket = 1;
                    format->mBytesPerFrame = kBytesPerFrame;
                    format->mChannelsPerFrame = kChannels;
                    format->mBitsPerChannel = kBitsPerChannel;
                    *outDataSize = sizeof(AudioStreamBasicDescription);
                    break;
                }
            }
            break;
        }
        case kObjectID_Volume_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *((AudioClassID*)outData) = kAudioControlClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *((AudioClassID*)outData) = kAudioLevelControlClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *((AudioObjectID*)outData) = kObjectID_Device;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioControlPropertyScope:
                    *((AudioObjectPropertyScope*)outData) = kAudioObjectPropertyScopeOutput;
                    *outDataSize = sizeof(AudioObjectPropertyScope);
                    break;
                case kAudioControlPropertyElement:
                    *((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
                    *outDataSize = sizeof(AudioObjectPropertyElement);
                    break;
                case kAudioLevelControlPropertyScalarValue:
                    pthread_mutex_lock(&gState.mutex);
                    *((Float32*)outData) = gState.masterVolume;
                    pthread_mutex_unlock(&gState.mutex);
                    *outDataSize = sizeof(Float32);
                    break;
            }
            break;
        case kObjectID_Mute_Master:
            switch (inAddress->mSelector) {
                case kAudioObjectPropertyBaseClass:
                    *((AudioClassID*)outData) = kAudioControlClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyClass:
                    *((AudioClassID*)outData) = kAudioBooleanControlClassID;
                    *outDataSize = sizeof(AudioClassID);
                    break;
                case kAudioObjectPropertyOwner:
                    *((AudioObjectID*)outData) = kObjectID_Device;
                    *outDataSize = sizeof(AudioObjectID);
                    break;
                case kAudioControlPropertyScope:
                    *((AudioObjectPropertyScope*)outData) = kAudioObjectPropertyScopeOutput;
                    *outDataSize = sizeof(AudioObjectPropertyScope);
                    break;
                case kAudioControlPropertyElement:
                    *((AudioObjectPropertyElement*)outData) = kAudioObjectPropertyElementMain;
                    *outDataSize = sizeof(AudioObjectPropertyElement);
                    break;
                case kAudioBooleanControlPropertyValue:
                    pthread_mutex_lock(&gState.mutex);
                    *((UInt32*)outData) = gState.masterMute ? 1 : 0;
                    pthread_mutex_unlock(&gState.mutex);
                    *outDataSize = sizeof(UInt32);
                    break;
            }
            break;
    }
    return 0;
}

static OSStatus LoopbackDriver_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData) {
    if (inObjectID == kObjectID_Device && inAddress->mSelector == kAudioDevicePropertyNominalSampleRate) {
        pthread_mutex_lock(&gState.mutex);
        gState.sampleRate = *((Float64*)inData);
        pthread_mutex_unlock(&gState.mutex);
    }
    if (inObjectID == kObjectID_Volume_Master && inAddress->mSelector == kAudioLevelControlPropertyScalarValue) {
        pthread_mutex_lock(&gState.mutex);
        gState.masterVolume = *((Float32*)inData);
        pthread_mutex_unlock(&gState.mutex);
    }
    if (inObjectID == kObjectID_Mute_Master && inAddress->mSelector == kAudioBooleanControlPropertyValue) {
        pthread_mutex_lock(&gState.mutex);
        gState.masterMute = (*((UInt32*)inData) != 0);
        pthread_mutex_unlock(&gState.mutex);
    }
    return 0;
}

static OSStatus LoopbackDriver_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, UInt32 inClientID) {
    pthread_mutex_lock(&gState.mutex);
    if (!gState.isRunning) {
        gState.anchorHostTime = mach_absolute_time();
        gState.anchorSampleTime = 0;
        gState.isRunning = true;
    }
    pthread_mutex_unlock(&gState.mutex);
    return 0;
}

static OSStatus LoopbackDriver_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, UInt32 inClientID) {
    pthread_mutex_lock(&gState.mutex);
    gState.isRunning = false;
    pthread_mutex_unlock(&gState.mutex);
    return 0;
}

static OSStatus LoopbackDriver_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed) {
    pthread_mutex_lock(&gState.mutex);
    UInt64 currentHostTime = mach_absolute_time();
    
    static mach_timebase_info_data_t sTimebaseInfo;
    if (sTimebaseInfo.denom == 0) mach_timebase_info(&sTimebaseInfo);
    
    UInt64 hostTimeDelta = currentHostTime - gState.anchorHostTime;
    Float64 hostTimeNanos = (Float64)hostTimeDelta * sTimebaseInfo.numer / sTimebaseInfo.denom;
    Float64 sampleTimeDelta = hostTimeNanos * gState.sampleRate / 1000000000.0;
    
    UInt64 sampleTime = gState.anchorSampleTime + (UInt64)sampleTimeDelta;
    UInt64 wrapInterval = 4096;
    sampleTime = (sampleTime / wrapInterval) * wrapInterval;
    
    *outSampleTime = (Float64)sampleTime;
    *outHostTime = currentHostTime;
    *outSeed = 1;
    
    pthread_mutex_unlock(&gState.mutex);
    return 0;
}

static OSStatus LoopbackDriver_WillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace) {
    *outWillDo = false;
    *outWillDoInPlace = true;
    if (inOperationID == kAudioServerPlugInIOOperationReadInput || inOperationID == kAudioServerPlugInIOOperationWriteMix) {
        *outWillDo = true;
    }
    return 0;
}

static OSStatus LoopbackDriver_BeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo) {
    return 0;
}

static OSStatus LoopbackDriver_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer) {
    float* buffer = (float*)ioMainBuffer;
    
    pthread_mutex_lock(&gState.mutex);
    float volume = gState.masterMute ? 0.0f : gState.masterVolume;
    
    if (inOperationID == kAudioServerPlugInIOOperationWriteMix) {
        for (UInt32 i = 0; i < inIOBufferFrameSize * kChannels; i++) {
            buffer[i] *= volume;
        }
    } else if (inOperationID == kAudioServerPlugInIOOperationReadInput) {
        for (UInt32 i = 0; i < inIOBufferFrameSize * kChannels; i++) {
            buffer[i] *= volume;
        }
    }
    
    pthread_mutex_unlock(&gState.mutex);
    return 0;
}

static OSStatus LoopbackDriver_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo) {
    return 0;
}