#include "libsegaapi.h"
#include <FAudio.h>
#include <FAPOFX.h>
#include <FAPO.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#define OUTPUT_CHANNELS 6 // Routing enum : FL, FR, FC, LFE, RL, RR
#define MAX_CHANNELS 2
#define MAX_SENDS 7
#define REVERB_SUBMIX_INDEX OUTPUT_CHANNELS
#define REVERB_CHANNELS 2
#define MAX_HANDLES 0x80
#define MAX_SOUND_BOARD_IO 21
#define MIN_SAMPLE_RATE 8000
#define MAX_SAMPLE_RATE 48000
#define BUFFER_OWNS_DATA 0x00000001

#define HANDLE_CHECK_BUFFER(handle, buffer, returnValue)                                                                                   \
    Buffer *buffer = lookupBuffer(handle);                                                                                                  \
    if (buffer == NULL)                                                                                                                     \
    {                                                                                                                                       \
        setLastStatus(SEGA_ERROR_BAD_HANDLE);                                                                                               \
        return returnValue;                                                                                                                 \
    }

FAudio *fAudio;
pthread_mutex_t fAudioMutex;
static pthread_mutex_t handleMutex = PTHREAD_MUTEX_INITIALIZER;
FAudioMasteringVoice *fAudioMasteringVoice;
FAudioSubmixVoice *fAudioSubmixVoices[MAX_SENDS];
static FAPO *fAudioReverbEffect;
static int reverbAvailable;
static int fxSlotReverbEnabled[4] = {1, 1, 1, 1};

unsigned int lastStatus;
unsigned int maxInputChannels;
static int segaApiInitialized;
static struct Buffer *activeBuffers[MAX_HANDLES];
static unsigned int activeBufferCount;
static SPDIFOutputSampleRate spdifSampleRate = SPDIFOUT_48KHZ;
static unsigned int spdifChannelStatus;
static unsigned int spdifExtChannelStatus;
static Routing spdifRouting[2] = {UNUSED_PORT, UNUSED_PORT};
static unsigned int ioVolumes[MAX_SOUND_BOARD_IO + 1];

const GUID EAXPROPERTYID_EAX40_SEGA_Custom = {0xa7feec3f, 0x2bfd, 0x4a40, {0x89, 0x1f, 0x74, 0x23, 0xe3, 0x8b, 0xac, 0x1f}};
GUID EAX_NULL_GUID = {0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
GUID EAX_FREQUENCYSHIFTER_EFFECT = {0xdc3e1880, 0x9212, 0x11d3, {0x93, 0x9d, 0x00, 0xc0, 0xf0, 0x2d, 0xd6, 0xf0}};
GUID EAX_ECHO_EFFECT = {0x0e9f1bc0, 0xac82, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_REVERB_EFFECT = {0x0cf95c8f, 0xa3cc, 0x4849, {0xb0, 0xb6, 0x83, 0x2e, 0xcc, 0x18, 0x22, 0xdf}};
GUID EAX_EQUALIZER_EFFECT = {0x65f94ce0, 0x9793, 0x11d3, {0x93, 0x9d, 0x00, 0xc0, 0xf0, 0x2d, 0xd6, 0xf0}};
GUID EAX_DISTORTION_EFFECT = {0x975a4ce0, 0xac7e, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_AGCCOMPRESSOR_EFFECT = {0xbfb7a01e, 0x7825, 0x4039, {0x92, 0x7f, 0x03, 0xaa, 0xbd, 0xa0, 0xc5, 0x60}};
GUID EAX_PITCHSHIFTER_EFFECT = {0xe7905100, 0xafb2, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_FLANGER_EFFECT = {0xa70007c0, 0x07d2, 0x11d3, {0x9b, 0x1e, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_VOCALMORPHER_EFFECT = {0xe41cf10c, 0x3383, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_AUTOWAH_EFFECT = {0xec3130c0, 0xac7a, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_RINGMODULATOR_EFFECT = {0x0b89fe60, 0xafb5, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAX_CHORUS_EFFECT = {0xde6d6fe0, 0xac79, 0x11d2, {0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1}};
GUID EAXPROPERTYID_EAX40_FXSlot0 = {0xc4d79f1e, 0xf1ac, 0x436b, {0xa8, 0x1d, 0xa7, 0x38, 0xe7, 0x04, 0x54, 0x69}};
GUID EAXPROPERTYID_EAX40_FXSlot1 = {0x08c00e96, 0x74be, 0x4491, {0x93, 0xaa, 0xe8, 0xad, 0x35, 0xa4, 0x91, 0x17}};
GUID EAXPROPERTYID_EAX40_FXSlot2 = {0x1d433b88, 0xf0f6, 0x4637, {0x91, 0x9f, 0x60, 0xe7, 0xe0, 0x6b, 0x5e, 0xdd}};
GUID EAXPROPERTYID_EAX40_FXSlot3 = {0xefff08ea, 0xc7d8, 0x44ab, {0x93, 0xad, 0x6d, 0xbd, 0x5f, 0x91, 0x00, 0x64}};
GUID EAXPROPERTYID_EAX40_Source = {0x1b86b823, 0x22df, 0x4eae, {0x8b, 0x3c, 0x12, 0x78, 0xce, 0x54, 0x42, 0x27}};
GUID EAXPROPERTYID_EAX40_Context = {0x1d4870ad, 0x0def, 0x43c0, {0xa4, 0x0c, 0x52, 0x36, 0x32, 0x29, 0x63, 0x42}};
GUID EAX_PrimaryFXSlotID = {0xf317866d, 0x924c, 0x450c, {0x86, 0x1b, 0xe6, 0xda, 0xa2, 0x5e, 0x7c, 0x20}};

typedef struct FAudioVoiceCallbackWithBuffer FAudioVoiceCallbackWithBuffer;

typedef struct Buffer
{
    FAudioWaveFormatEx fAudioFormat;
    FAudioBuffer fAudioBuffer;
    FAudioSourceVoice *fAudioSourceVoice;
    FAudioVoiceCallbackWithBuffer *fAudioVoiceCallback;

    uint8_t *data;
    size_t size;

    void *userData;
    BufferCallback callback;
    int bDoContinuousLooping;
    PlaybackStatus playbackStatus;
    unsigned int priority;

    unsigned int startLoop;
    unsigned int endLoop;
    unsigned int endOffset;
    unsigned int notificationFrequency;
    unsigned int notificationPoint;
    int notificationPointSet;

    OutputFormat outputFormat;
    int updateOutputFormat;
    unsigned int flags;

    Routing routing[MAX_CHANNELS][MAX_SENDS];
    unsigned int sendLevels[MAX_CHANNELS][MAX_SENDS];
    unsigned int channelVolumes[MAX_CHANNELS];

    SynthParams synthParams[MOD_ENV_TO_FILTER_CUTOFF + 1];

    int lastStatus;

} Buffer;

typedef struct FAudioVoiceCallbackWithBuffer
{
    OnBufferEndFunc OnBufferEnd;
    OnBufferStartFunc OnBufferStart;
    OnLoopEndFunc OnLoopEnd;
    OnStreamEndFunc OnStreamEnd;
    OnVoiceErrorFunc OnVoiceError;
    OnVoiceProcessingPassEndFunc OnVoiceProcessingPassEnd;
    OnVoiceProcessingPassStartFunc OnVoiceProcessingPassStart;
    Buffer *buffer;
} FAudioVoiceCallbackWithBuffer;

static void setLastStatus(int status)
{
    lastStatus = (unsigned int)status;
}

static int setLastStatusAndReturn(int status)
{
    setLastStatus(status);
    return status;
}

static int isValidSampleFormat(unsigned int dwSampleFormat)
{
    return (dwSampleFormat & SIGNED_16PCM) != 0;
}

static int isValidSampleRate(unsigned int dwSampleRate)
{
    return dwSampleRate >= MIN_SAMPLE_RATE && dwSampleRate <= MAX_SAMPLE_RATE;
}

static int isValidChannelCount(unsigned int byNumChans)
{
    return byNumChans >= 1 && byNumChans <= MAX_CHANNELS;
}

static int validateOutputFormat(unsigned int dwSampleRate, unsigned int dwSampleFormat, unsigned int byNumChans)
{
    if (!isValidSampleFormat(dwSampleFormat))
        return SEGA_ERROR_BAD_CONFIG;

    if (!isValidSampleRate(dwSampleRate))
        return SEGA_ERROR_BAD_SAMPLERATE;

    if (!isValidChannelCount(byNumChans))
        return SEGA_ERROR_INVALID_CHANNEL;

    return SEGA_SUCCESS;
}

static int isValidRouting(Routing dwDest)
{
    return dwDest == UNUSED_PORT || (dwDest >= FRONT_LEFT_PORT && dwDest <= REAR_RIGHT_PORT) ||
           (dwDest >= FXSLOT0_PORT && dwDest <= FXSLOT3_PORT);
}

static int isValidSpdifRoutingSource(Routing dwSource)
{
    return dwSource == UNUSED_PORT || (dwSource >= FRONT_LEFT_PORT && dwSource <= REAR_RIGHT_PORT);
}

static int isValidSoundBoardIO(SoundBoardIO dwPhysIO)
{
    switch (dwPhysIO)
    {
        case OUT_FRONT_LEFT:
        case OUT_FRONT_RIGHT:
        case OUT_FRONT_CENTER:
        case OUT_LFE_PORT:
        case OUT_REAR_LEFT:
        case OUT_REAR_RIGHT:
        case OUT_OPTICAL_LEFT:
        case OUT_OPTICAL_RIGHT:
        case IN_LINEIN_LEFT:
        case IN_LINEIN_RIGHT:
            return 1;
        default:
            return 0;
    }
}

static int guidEquals(const GUID *lhs, const GUID *rhs)
{
    return lhs != NULL && rhs != NULL && memcmp(lhs, rhs, sizeof(GUID)) == 0;
}

static int fxSlotIndexFromGuid(const GUID *guid)
{
    if (guidEquals(guid, &EAXPROPERTYID_EAX40_FXSlot0))
        return 0;
    if (guidEquals(guid, &EAXPROPERTYID_EAX40_FXSlot1))
        return 1;
    if (guidEquals(guid, &EAXPROPERTYID_EAX40_FXSlot2))
        return 2;
    if (guidEquals(guid, &EAXPROPERTYID_EAX40_FXSlot3))
        return 3;

    return -1;
}

static int fxSlotIndexFromRouting(Routing dest)
{
    if (dest >= FXSLOT0_PORT && dest <= FXSLOT3_PORT)
        return dest - FXSLOT0_PORT;

    return -1;
}

static Buffer *lookupBuffer(void *hHandle)
{
    if (hHandle == NULL)
        return NULL;

    pthread_mutex_lock(&handleMutex);
    for (unsigned int i = 0; i < activeBufferCount; i++)
    {
        if (activeBuffers[i] == (Buffer *)hHandle)
        {
            pthread_mutex_unlock(&handleMutex);
            return activeBuffers[i];
        }
    }
    pthread_mutex_unlock(&handleMutex);

    return NULL;
}

static int registerBuffer(Buffer *buffer)
{
    int result = SEGA_SUCCESS;

    pthread_mutex_lock(&handleMutex);
    if (activeBufferCount >= MAX_HANDLES)
    {
        result = SEGA_ERROR_NO_RESOURCES;
    }
    else
    {
        activeBuffers[activeBufferCount++] = buffer;
    }
    pthread_mutex_unlock(&handleMutex);

    return result;
}

static void unregisterBuffer(Buffer *buffer)
{
    pthread_mutex_lock(&handleMutex);
    for (unsigned int i = 0; i < activeBufferCount; i++)
    {
        if (activeBuffers[i] == buffer)
        {
            activeBuffers[i] = activeBuffers[--activeBufferCount];
            activeBuffers[activeBufferCount] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&handleMutex);
}

static unsigned int samplesToBytes(Buffer *buffer, int value);

static unsigned int sampleBits(unsigned int dwSampleFormat)
{
    return (dwSampleFormat & SIGNED_16PCM) ? 16 : 8;
}

static void updateFAudioFormat(Buffer *buffer)
{
    buffer->fAudioFormat.wFormatTag = 1;
    buffer->fAudioFormat.nChannels = buffer->outputFormat.byNumChans;
    buffer->fAudioFormat.nSamplesPerSec = buffer->outputFormat.dwSampleRate;
    buffer->fAudioFormat.nAvgBytesPerSec = samplesToBytes(buffer, buffer->outputFormat.dwSampleRate);
    buffer->fAudioFormat.nBlockAlign = samplesToBytes(buffer, 1);
    buffer->fAudioFormat.wBitsPerSample = sampleBits(buffer->outputFormat.dwSampleFormat);
    buffer->fAudioFormat.cbSize = sizeof(FAudioWaveFormatEx);
}

static int createSourceVoice(Buffer *buffer)
{
    updateFAudioFormat(buffer);

    if (FAudio_CreateSourceVoice(fAudio, &buffer->fAudioSourceVoice, &buffer->fAudioFormat, 0, FAUDIO_MAX_FREQ_RATIO,
                                 (FAudioVoiceCallback *)buffer->fAudioVoiceCallback, NULL, NULL) != 0)
    {
        buffer->fAudioSourceVoice = NULL;
        return SEGA_ERROR_NO_RESOURCES;
    }

    return SEGA_SUCCESS;
}

static void setTunnelReverbParameters(void)
{
    if (!reverbAvailable || fAudioSubmixVoices[REVERB_SUBMIX_INDEX] == NULL)
        return;

    FAPOFXEchoParameters params;
    params.WetDryMix = 0.70f;
    params.Feedback = 0.22f;
    params.Delay = 120.0f;

    FAudioVoice_SetEffectParameters(fAudioSubmixVoices[REVERB_SUBMIX_INDEX], 0, &params, sizeof(params), FAUDIO_COMMIT_NOW);
}

static void resetDefaultStates(void)
{
    spdifSampleRate = SPDIFOUT_48KHZ;
    spdifChannelStatus = 0;
    spdifExtChannelStatus = 0;
    spdifRouting[0] = UNUSED_PORT;
    spdifRouting[1] = UNUSED_PORT;
    for (int i = 0; i < 4; i++)
        fxSlotReverbEnabled[i] = 1;

    memset(ioVolumes, 0, sizeof(ioVolumes));
    ioVolumes[OUT_FRONT_LEFT] = VOL_MAX;
    ioVolumes[OUT_FRONT_RIGHT] = VOL_MAX;
    ioVolumes[OUT_FRONT_CENTER] = VOL_MAX;
    ioVolumes[OUT_LFE_PORT] = VOL_MAX;
    ioVolumes[OUT_REAR_LEFT] = VOL_MAX;
    ioVolumes[OUT_REAR_RIGHT] = VOL_MAX;
    ioVolumes[OUT_OPTICAL_LEFT] = VOL_MAX;
    ioVolumes[OUT_OPTICAL_RIGHT] = VOL_MAX;
    ioVolumes[IN_LINEIN_LEFT] = 0x99980000;
    ioVolumes[IN_LINEIN_RIGHT] = 0x99980000;
}

static void destroyBufferResources(Buffer *buffer)
{
    if (buffer == NULL)
        return;

    if (buffer->fAudioSourceVoice != NULL)
    {
        FAudioVoice_DestroyVoice(buffer->fAudioSourceVoice);
        buffer->fAudioSourceVoice = NULL;
    }

    if ((buffer->flags & BUFFER_OWNS_DATA) && buffer->data != NULL)
    {
        free(buffer->data);
        buffer->data = NULL;
    }

    free(buffer->fAudioVoiceCallback);
    buffer->fAudioVoiceCallback = NULL;
}

static void destroyAllBuffers(void)
{
    for (;;)
    {
        pthread_mutex_lock(&handleMutex);
        if (activeBufferCount == 0)
        {
            pthread_mutex_unlock(&handleMutex);
            return;
        }

        Buffer *buffer = activeBuffers[--activeBufferCount];
        activeBuffers[activeBufferCount] = NULL;
        pthread_mutex_unlock(&handleMutex);

        destroyBufferResources(buffer);
        free(buffer);
    }
}

void StaticOnBufferEnd(FAudioVoiceCallback *callback, void *pBufferContext)
{
    Buffer *buffer = ((FAudioVoiceCallbackWithBuffer *)callback)->buffer;

    if (!buffer->updateOutputFormat)
        return;

    FAudioVoiceState fAudioVoiceState;
    FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &fAudioVoiceState, 0);

    if (!fAudioVoiceState.BuffersQueued)
    {
        FAudioSourceVoice_SetSourceSampleRate(buffer->fAudioSourceVoice, buffer->outputFormat.dwSampleRate);
        buffer->updateOutputFormat = 0;
        return;
    }

    FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);
}

void StaticOnVoiceProcessingPassStart(FAudioVoiceCallback *callback, uint32_t bytesRequired)
{
}
void StaticOnVoiceProcessingPassEnd(FAudioVoiceCallback *callback)
{
}
void StaticOnStreamEnd(FAudioVoiceCallback *callback)
{
}
void StaticOnBufferStart(FAudioVoiceCallback *callback, void *pBufferContext)
{
}
void StaticOnLoopEnd(FAudioVoiceCallback *callback, void *pBufferContext)
{
}
void StaticOnVoiceError(FAudioVoiceCallback *callback, void *pBufferContext, uint32_t error)
{
}

static unsigned int bytesToSamples(Buffer *buffer, int value)
{
    unsigned int bytesPerSample = sampleBits(buffer->outputFormat.dwSampleFormat) / 8;
    return value / (buffer->outputFormat.byNumChans * bytesPerSample);
}

static unsigned int samplesToBytes(Buffer *buffer, int value)
{
    unsigned int bytesPerSample = sampleBits(buffer->outputFormat.dwSampleFormat) / 8;
    return value * buffer->outputFormat.byNumChans * bytesPerSample;
}

char *getRoutingString(Routing dwDest)
{

    char *dest = "Unknown";
    switch (dwDest)
    {
        case UNUSED_PORT:
            dest = "UNUSED_PORT";
            break;
        case FRONT_LEFT_PORT:
            dest = "FRONT_LEFT_PORT";
            break;
        case FRONT_RIGHT_PORT:
            dest = "FRONT_RIGHT_PORT";
            break;
        case FRONT_CENTER_PORT:
            dest = "FRONT_CENTER_PORT";
            break;
        case LFE_PORT:
            dest = "LFE_PORT";
            break;
        case REAR_LEFT_PORT:
            dest = "REAR_LEFT_PORT";
            break;
        case REAR_RIGHT_PORT:
            dest = "REAR_RIGHT_PORT";
            break;
        case FXSLOT0_PORT:
            dest = "FXSLOT0_PORT";
            break;
        case FXSLOT1_PORT:
            dest = "FXSLOT1_PORT";
            break;
        case FXSLOT2_PORT:
            dest = "FXSLOT2_PORT";
            break;
        case FXSLOT3_PORT:
            dest = "FXSLOT3_PORT";
            break;
        default:
            break;
    }
    return dest;
}

static void processSends(Buffer *buffer)
{
    float matrix[OUTPUT_CHANNELS][MAX_CHANNELS]; // [dest][srcChannel]
    float reverbMatrix[MAX_CHANNELS][REVERB_CHANNELS];
    int destUsed[OUTPUT_CHANNELS] = {0};

    memset(matrix, 0, sizeof(matrix));
    memset(reverbMatrix, 0, sizeof(reverbMatrix));

    for (unsigned int ch = 0; ch < buffer->outputFormat.byNumChans; ch++)
    {
        for (int send = 0; send < MAX_SENDS; send++)
        {
            Routing dest = buffer->routing[ch][send];

            if (dest == UNUSED_PORT)
                continue;

            float gain = (buffer->sendLevels[ch][send] / (float)VOL_MAX) * (buffer->channelVolumes[ch] / (float)VOL_MAX);

            if (dest >= FRONT_LEFT_PORT && dest <= REAR_RIGHT_PORT)
            {
                matrix[dest][ch] += gain;
                destUsed[dest] = 1;
            }
            else if (dest >= FXSLOT0_PORT && dest <= FXSLOT3_PORT && reverbAvailable)
            {
                int fxSlotIndex = fxSlotIndexFromRouting(dest);
                if (fxSlotIndex < 0 || !fxSlotReverbEnabled[fxSlotIndex])
                    continue;

                float reverbGain = gain * 0.42f;

                if (buffer->outputFormat.byNumChans == 1)
                {
                    reverbMatrix[ch][0] += reverbGain;
                    reverbMatrix[ch][1] += reverbGain;
                }
                else if (ch == 0)
                {
                    reverbMatrix[ch][0] += reverbGain;
                }
                else
                {
                    reverbMatrix[ch][1] += reverbGain;
                }

            }
        }
    }

    FAudioSendDescriptor fAudioSendDescriptor[OUTPUT_CHANNELS + 1];
    int destOrder[OUTPUT_CHANNELS + 1];
    int sendCount = 0;

    for (int dest = 0; dest < OUTPUT_CHANNELS; dest++)
    {
        if (!destUsed[dest])
            continue;

        fAudioSendDescriptor[sendCount].Flags = 0;
        fAudioSendDescriptor[sendCount].pOutputVoice = fAudioSubmixVoices[dest];
        destOrder[sendCount] = dest;
        sendCount++;
    }

    if (reverbAvailable && fAudioSubmixVoices[REVERB_SUBMIX_INDEX] != NULL)
    {
        fAudioSendDescriptor[sendCount].Flags = 0;
        fAudioSendDescriptor[sendCount].pOutputVoice = fAudioSubmixVoices[REVERB_SUBMIX_INDEX];
        destOrder[sendCount] = REVERB_SUBMIX_INDEX;
        sendCount++;
    }

    if (sendCount == 0)
        return;

    FAudioVoiceSends fAudioVoiceSends = {.SendCount = sendCount, .pSends = fAudioSendDescriptor};
    FAudioVoice_SetOutputVoices(buffer->fAudioSourceVoice, &fAudioVoiceSends);

    for (int i = 0; i < sendCount; i++)
    {
        if (destOrder[i] == REVERB_SUBMIX_INDEX)
        {
            FAudioVoice_SetOutputMatrix(buffer->fAudioSourceVoice, fAudioSubmixVoices[REVERB_SUBMIX_INDEX],
                                        buffer->outputFormat.byNumChans, REVERB_CHANNELS, &reverbMatrix[0][0], FAUDIO_COMMIT_NOW);
        }
        else
        {
            FAudioVoice_SetOutputMatrix(buffer->fAudioSourceVoice, fAudioSubmixVoices[destOrder[i]], buffer->outputFormat.byNumChans, 1,
                                        &matrix[destOrder[i]][0], FAUDIO_COMMIT_NOW);
        }
    }
}

int SEGAAPI_Play(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    processSends(buffer);

    int result = SEGAAPI_UpdateBuffer(hHandle, -1, -1);
    if (result != SEGA_SUCCESS)
        return result;

    if (FAudioSourceVoice_Start(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW) != 0)
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);

    buffer->playbackStatus = PLAYBACK_STATUS_ACTIVE;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_Pause(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW) != 0)
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);

    buffer->playbackStatus = PLAYBACK_STATUS_PAUSE;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_Stop(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW) != 0)
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);

    if (FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice) != 0)
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);

    buffer->playbackStatus = PLAYBACK_STATUS_STOP;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_PlayWithSetup(void *hHandle, unsigned int dwNumSendRouteParams, SendRouteParamSet *pSendRouteParams,
                          unsigned int dwNumSendLevelParams, SendLevelParamSet *pSendLevelParams, unsigned int dwNumVoiceParams,
                          VoiceParamSet *pVoiceParams, unsigned int dwNumSynthParams, SynthParamSet *pSynthParams)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (buffer->outputFormat.byNumChans != 1)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_CHANNEL);

    if ((dwNumSendRouteParams != 0 && pSendRouteParams == NULL) || (dwNumSendLevelParams != 0 && pSendLevelParams == NULL) ||
        (dwNumVoiceParams != 0 && pVoiceParams == NULL) || (dwNumSynthParams != 0 && pSynthParams == NULL))
    {
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);
    }

    for (unsigned int i = 0; i < dwNumSendRouteParams; i++)
    {
        int result = SEGAAPI_SetSendRouting(hHandle, pSendRouteParams[i].dwChannel, pSendRouteParams[i].dwSend, pSendRouteParams[i].dwDest);
        if (result != SEGA_SUCCESS)
            return result;
    }

    for (unsigned int i = 0; i < dwNumSendLevelParams; i++)
    {
        int result = SEGAAPI_SetSendLevel(hHandle, pSendLevelParams[i].dwChannel, pSendLevelParams[i].dwSend, pSendLevelParams[i].dwLevel);
        if (result != SEGA_SUCCESS)
            return result;
    }

    for (unsigned int i = 0; i < dwNumVoiceParams; i++)
    {
        int result = SEGA_SUCCESS;
        switch (pVoiceParams[i].VoiceIoctl)
        {
            case SET_START_LOOP_OFFSET:
                result = SEGAAPI_SetStartLoopOffset(hHandle, pVoiceParams[i].dwParam1);
                break;
            case SET_END_LOOP_OFFSET:
                result = SEGAAPI_SetEndLoopOffset(hHandle, pVoiceParams[i].dwParam1);
                break;
            case SET_END_OFFSET:
                result = SEGAAPI_SetEndOffset(hHandle, pVoiceParams[i].dwParam1);
                break;
            case SET_PLAY_POSITION:
                result = SEGAAPI_SetPlaybackPosition(hHandle, pVoiceParams[i].dwParam1);
                break;
            case SET_LOOP_STATE:
                result = SEGAAPI_SetLoopState(hHandle, pVoiceParams[i].dwParam1);
                break;
            case SET_NOTIFICATION_POINT:
                result = SEGAAPI_SetNotificationPoint(hHandle, pVoiceParams[i].dwParam1);
                break;
            case CLEAR_NOTIFICATION_POINT:
                result = SEGAAPI_ClearNotificationPoint(hHandle, pVoiceParams[i].dwParam1);
                break;
            case SET_NOTIFICATION_FREQUENCY:
                result = SEGAAPI_SetNotificationFrequency(hHandle, pVoiceParams[i].dwParam1);
                break;
            default:
                break;
        }

        if (result != SEGA_SUCCESS)
            return result;
    }

    for (unsigned int i = 0; i < dwNumSynthParams; i++)
    {
        int result = SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);
        if (result != SEGA_SUCCESS)
            return result;
    }

    return SEGAAPI_Play(hHandle);
}

PlaybackStatus SEGAAPI_GetPlaybackStatus(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, PLAYBACK_STATUS_INVALID);

    pthread_mutex_lock(&fAudioMutex);

    FAudioVoiceState fAudioVoiceState;
    FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &fAudioVoiceState, 0);

    PlaybackStatus status = buffer->playbackStatus;

    if (status == PLAYBACK_STATUS_ACTIVE && fAudioVoiceState.BuffersQueued == 0)
    {
        status = PLAYBACK_STATUS_STOP;
        buffer->playbackStatus = status;
    }

    pthread_mutex_unlock(&fAudioMutex);
    setLastStatus(SEGA_SUCCESS);
    return status;
}

int SEGAAPI_SetFormat(void *hHandle, OutputFormat *pFormat)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (pFormat == NULL)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_POINTER);

    int result = validateOutputFormat(pFormat->dwSampleRate, pFormat->dwSampleFormat, pFormat->byNumChans);
    if (result != SEGA_SUCCESS)
        return setLastStatusAndReturn(result);

    if (SEGAAPI_GetPlaybackStatus(hHandle) != PLAYBACK_STATUS_STOP)
        return setLastStatusAndReturn(SEGA_ERROR_PLAYING);

    buffer->outputFormat.byNumChans = pFormat->byNumChans;
    buffer->outputFormat.dwSampleFormat = pFormat->dwSampleFormat;
    buffer->outputFormat.dwSampleRate = pFormat->dwSampleRate;

    FAudioVoice_DestroyVoice(buffer->fAudioSourceVoice);
    buffer->fAudioSourceVoice = NULL;

    result = createSourceVoice(buffer);
    if (result != SEGA_SUCCESS)
        return setLastStatusAndReturn(result);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_GetFormat(void *hHandle, OutputFormat *pFormat)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (pFormat == NULL)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_POINTER);

    pFormat->byNumChans = buffer->outputFormat.byNumChans;
    pFormat->dwSampleFormat = buffer->outputFormat.dwSampleFormat;
    pFormat->dwSampleRate = buffer->outputFormat.dwSampleRate;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_SetSampleRate(void *hHandle, unsigned int dwSampleRate)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (!isValidSampleRate(dwSampleRate))
        return setLastStatusAndReturn(SEGA_ERROR_BAD_SAMPLERATE);

    buffer->outputFormat.dwSampleRate = dwSampleRate;

    FAudioVoiceState fAudioVoiceState;
    FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &fAudioVoiceState, 0);

    // If there are no buffers playing, set the sample rate immidiately.
    if (!fAudioVoiceState.BuffersQueued)
    {
        if (FAudioSourceVoice_SetSourceSampleRate(buffer->fAudioSourceVoice, dwSampleRate) != 0)
            return setLastStatusAndReturn(SEGA_ERROR_FAIL);
        return setLastStatusAndReturn(SEGA_SUCCESS);
    }

    // Flush the buffers and set the sample rate on next update
    buffer->updateOutputFormat = 1;
    if (FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice) != 0)
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetSampleRate(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    setLastStatus(SEGA_SUCCESS);
    return buffer->outputFormat.dwSampleRate;
}

int SEGAAPI_SetPriority(void *hHandle, unsigned int dwPriority)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    buffer->priority = dwPriority;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetPriority(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    setLastStatus(SEGA_SUCCESS);
    return buffer->priority;
}

int SEGAAPI_SetUserData(void *hHandle, void *hUserData)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    buffer->userData = hUserData;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

void *SEGAAPI_GetUserData(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, NULL);

    setLastStatus(SEGA_SUCCESS);
    return buffer->userData;
}

int SEGAAPI_SetSendRouting(void *hHandle, unsigned int dwChannel, unsigned int dwSend, Routing dwDest)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (dwChannel >= buffer->outputFormat.byNumChans)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_CHANNEL);

    if (dwSend >= MAX_SENDS)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_SEND);

    if (!isValidRouting(dwDest))
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

    buffer->routing[dwChannel][dwSend] = dwDest;

    processSends(buffer);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

Routing SEGAAPI_GetSendRouting(void *hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, UNUSED_PORT);

    if (dwChannel >= buffer->outputFormat.byNumChans)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return UNUSED_PORT;
    }

    if (dwSend >= MAX_SENDS)
    {
        setLastStatus(SEGA_ERROR_INVALID_SEND);
        return UNUSED_PORT;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->routing[dwChannel][dwSend];
}

int SEGAAPI_SetSendLevel(void *hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwLevel)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (dwChannel >= buffer->outputFormat.byNumChans)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_CHANNEL);

    if (dwSend >= MAX_SENDS)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_SEND);

    buffer->sendLevels[dwChannel][dwSend] = dwLevel;

    processSends(buffer);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetSendLevel(void *hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    if (dwChannel >= buffer->outputFormat.byNumChans)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return 0;
    }

    if (dwSend >= MAX_SENDS)
    {
        setLastStatus(SEGA_ERROR_INVALID_SEND);
        return 0;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->sendLevels[dwChannel][dwSend];
}

int SEGAAPI_SetChannelVolume(void *hHandle, unsigned int dwChannel, unsigned int dwVolume)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (dwChannel >= buffer->outputFormat.byNumChans)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_CHANNEL);

    buffer->channelVolumes[dwChannel] = dwVolume;

    processSends(buffer);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetChannelVolume(void *hHandle, unsigned int dwChannel)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    if (dwChannel >= buffer->outputFormat.byNumChans)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return 0;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->channelVolumes[dwChannel];
}

int SEGAAPI_SetPlaybackPosition(void *hHandle, unsigned int dwPlaybackPos)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (dwPlaybackPos != 0)
    {
        return setLastStatusAndReturn(SEGA_SUCCESS);
    }

    int status = SEGAAPI_GetPlaybackStatus(hHandle);
    char *statusString = "UNKNOWN";
    switch (status)
    {
        case PLAYBACK_STATUS_STOP:
            statusString = "PLAYBACK_STATUS_STOP";
            break;
        case PLAYBACK_STATUS_PAUSE:
            statusString = "PLAYBACK_STATUS_PAUSE";
            break;
        case PLAYBACK_STATUS_ACTIVE:
            statusString = "PLAYBACK_STATUS_ACTIVE";
            break;
        case PLAYBACK_STATUS_INVALID:
            statusString = "PLAYBACK_STATUS_INVALID";
            break;
        default:
            break;
    }
    // Retrofan Test
    if (buffer->bDoContinuousLooping && status == PLAYBACK_STATUS_STOP && buffer->fAudioFormat.nChannels == 1)
    {
        return setLastStatusAndReturn(SEGA_SUCCESS);
    }

    FAudioVoice_DestroyVoice(buffer->fAudioSourceVoice);
    buffer->fAudioSourceVoice = NULL;

    int result = createSourceVoice(buffer);
    if (result != SEGA_SUCCESS)
        return setLastStatusAndReturn(result);

    if (status == PLAYBACK_STATUS_ACTIVE)
    {
        result = SEGAAPI_Play(hHandle);
        if (result != SEGA_SUCCESS)
            return result;
    }

    // Should we flush the buffers or stop here?

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetPlaybackPosition(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    FAudioVoiceState fAudioVoiceState;
    FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &fAudioVoiceState, 0);

    unsigned int bytesPlayed = fAudioVoiceState.SamplesPlayed * buffer->fAudioFormat.nChannels * (buffer->fAudioFormat.wBitsPerSample / 8);

    if (buffer->size == 0)
    {
        setLastStatus(SEGA_SUCCESS);
        return 0;
    }

    setLastStatus(SEGA_SUCCESS);
    return bytesPlayed % buffer->size;
}

int SEGAAPI_SetNotificationFrequency(void *hHandle, unsigned int dwFrameCount)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    buffer->notificationFrequency = dwFrameCount;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_SetNotificationPoint(void *hHandle, unsigned int dwBufferOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    buffer->notificationPoint = dwBufferOffset;
    buffer->notificationPointSet = 1;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_ClearNotificationPoint(void *hHandle, unsigned int dwBufferOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (!buffer->notificationPointSet || buffer->notificationPoint == dwBufferOffset)
        buffer->notificationPointSet = 0;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_SetStartLoopOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    buffer->startLoop = dwOffset;

    int result = SEGAAPI_UpdateBuffer(hHandle, -1, -1);
    if (result != SEGA_SUCCESS)
        return result;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetStartLoopOffset(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    setLastStatus(SEGA_SUCCESS);
    return buffer->startLoop;
}

int SEGAAPI_SetEndLoopOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    buffer->endLoop = dwOffset;

    int result = SEGAAPI_UpdateBuffer(hHandle, -1, -1);
    if (result != SEGA_SUCCESS)
        return result;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetEndLoopOffset(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    setLastStatus(SEGA_SUCCESS);
    return buffer->endLoop;
}

int SEGAAPI_SetEndOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    buffer->endOffset = dwOffset;

    int result = SEGAAPI_UpdateBuffer(hHandle, -1, -1);
    if (result != SEGA_SUCCESS)
        return result;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetEndOffset(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    setLastStatus(SEGA_SUCCESS);
    return buffer->endOffset;
}

int SEGAAPI_SetLoopState(void *hHandle, int bDoContinuousLooping)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    buffer->bDoContinuousLooping = bDoContinuousLooping;

    int result = SEGAAPI_UpdateBuffer(hHandle, -1, -1);
    if (result != SEGA_SUCCESS)
        return result;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_GetLoopState(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, 0);

    setLastStatus(SEGA_SUCCESS);
    return buffer->bDoContinuousLooping;
}

int SEGAAPI_UpdateBuffer(void *hHandle, unsigned int dwStartOffset, unsigned int dwLength)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    // Ignore the call if we're waiting to update the output format
    if (buffer->updateOutputFormat)
        return setLastStatusAndReturn(SEGA_SUCCESS);

    pthread_mutex_lock(&fAudioMutex);

    if (FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice) != 0)
    {
        pthread_mutex_unlock(&fAudioMutex);
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);
    }

    // memset(buffer->fAudioBuffer, 0, sizeof(FAudioBuffer));

    unsigned int playBegin = buffer->startLoop;
    unsigned int playEnd = buffer->endOffset;
    unsigned int loopEnd = buffer->endLoop;

    if (playBegin > buffer->size)
        playBegin = buffer->size;
    if (playEnd < playBegin || playEnd > buffer->size)
        playEnd = buffer->size;
    if (loopEnd < playBegin || loopEnd > buffer->size)
        loopEnd = playEnd;

    buffer->fAudioBuffer.Flags = 0;
    buffer->fAudioBuffer.AudioBytes = buffer->size;
    buffer->fAudioBuffer.pAudioData = buffer->data;
    buffer->fAudioBuffer.PlayBegin = bytesToSamples(buffer, playBegin);
    buffer->fAudioBuffer.PlayLength = bytesToSamples(buffer, playEnd - playBegin);
    buffer->fAudioBuffer.LoopBegin = 0;
    buffer->fAudioBuffer.LoopLength = 0;
    buffer->fAudioBuffer.LoopCount = 0;
    buffer->fAudioBuffer.pContext = NULL;

    if (buffer->bDoContinuousLooping)
    {
        buffer->fAudioBuffer.LoopBegin = bytesToSamples(buffer, playBegin);
        buffer->fAudioBuffer.LoopLength = bytesToSamples(buffer, loopEnd - playBegin);
        buffer->fAudioBuffer.LoopCount = FAUDIO_LOOP_INFINITE;
    }

    if (FAudioSourceVoice_SubmitSourceBuffer(buffer->fAudioSourceVoice, &buffer->fAudioBuffer, NULL) != 0)
    {
        pthread_mutex_unlock(&fAudioMutex);
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);
    }

    pthread_mutex_unlock(&fAudioMutex);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_SetSynthParam(void *hHandle, SynthParams param, int lPARWValue)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (buffer->outputFormat.byNumChans != 1)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_CHANNEL);

    if (param > MOD_ENV_TO_FILTER_CUTOFF)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

    buffer->synthParams[param] = lPARWValue;

    switch (param)
    {
        case ATTENUATION: // I think this is probably wrong and needs a fix
        {
            float volumeInDecibels = 0.0f - lPARWValue / 10.0f;
            float volumeInGain = volumeInDecibels > -100.0f ? powf(10.0f, volumeInDecibels * 0.05f) : 0.0f;
            if (FAudioVoice_SetVolume(buffer->fAudioSourceVoice, volumeInGain, FAUDIO_COMMIT_NOW) != 0)
                return setLastStatusAndReturn(SEGA_ERROR_FAIL);
        }
        break;

        case PITCH:
        {
            float semiTones = lPARWValue / 100.0f;
            float freqRatio = powf(2.0f, semiTones / 12.0f);
            if (FAudioSourceVoice_SetFrequencyRatio(buffer->fAudioSourceVoice, freqRatio, FAUDIO_COMMIT_NOW) != 0)
                return setLastStatusAndReturn(SEGA_ERROR_FAIL);
        }
        break;

        default:
            break;
    }

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_GetSynthParam(void *hHandle, SynthParams param)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, -1);

    if (buffer->outputFormat.byNumChans != 1)
    {
        setLastStatus(SEGA_ERROR_INVALID_CHANNEL);
        return -1;
    }

    if (param > MOD_ENV_TO_FILTER_CUTOFF)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return -1;
    }

    setLastStatus(SEGA_SUCCESS);
    return buffer->synthParams[param];
}

int SEGAAPI_SetSynthParamMultiple(void *hHandle, unsigned int dwNumParams, SynthParamSet *pSynthParams)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (buffer->outputFormat.byNumChans != 1)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_CHANNEL);

    if (dwNumParams != 0 && pSynthParams == NULL)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

    for (unsigned int i = 0; i < dwNumParams; i++)
    {
        int result = SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);
        if (result != SEGA_SUCCESS)
            return result;
    }

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_GetSynthParamMultiple(void *hHandle, unsigned int dwNumParams, SynthParamSet *pSynthParams)
{

    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (buffer->outputFormat.byNumChans != 1)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_CHANNEL);

    if (dwNumParams != 0 && pSynthParams == NULL)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

    for (unsigned int i = 0; i < dwNumParams; i++)
    {
        if (pSynthParams[i].param > MOD_ENV_TO_FILTER_CUTOFF)
            return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

        pSynthParams[i].lPARWValue = buffer->synthParams[pSynthParams[i].param];
    }

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_SetReleaseState(void *hHandle, int bSet)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    if (buffer->outputFormat.byNumChans != 1)
        return setLastStatusAndReturn(SEGA_ERROR_INVALID_CHANNEL);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_CreateBuffer(BufferConfig *pConfig, BufferCallback pCallback, unsigned int dwFlags, void **phHandle)
{
    if (pConfig == NULL || phHandle == NULL)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_POINTER);

    *phHandle = NULL;

    if (!segaApiInitialized || fAudio == NULL)
        return setLastStatusAndReturn(SEGA_ERROR_INIT_FAILED);

    if (pConfig->mapData.dwSize < pConfig->mapData.dwOffset)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_CONFIG);

    int result = validateOutputFormat(pConfig->dwSampleRate, pConfig->dwSampleFormat, pConfig->byNumChans);
    if (result != SEGA_SUCCESS)
        return setLastStatusAndReturn(result);

    if ((dwFlags & SYNTH_BUFFER) && pConfig->byNumChans != 1)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

    if ((dwFlags & ALLOC_USER_MEM) && (dwFlags & USE_MAPPED_MEM))
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

    Buffer *buffer = (Buffer *)malloc(sizeof(Buffer));
    if (buffer == NULL)
        return setLastStatusAndReturn(SEGA_ERROR_OUT_OF_MEMORY);

    memset(buffer, 0, sizeof(Buffer));

    buffer->playbackStatus = PLAYBACK_STATUS_STOP;
    buffer->callback = pCallback;
    buffer->priority = pConfig->dwPriority;
    buffer->flags = dwFlags;

    buffer->outputFormat.dwSampleRate = pConfig->dwSampleRate;
    buffer->outputFormat.dwSampleFormat = pConfig->dwSampleFormat;
    buffer->outputFormat.byNumChans = pConfig->byNumChans;
    buffer->updateOutputFormat = 0;

    buffer->userData = pConfig->hUserData;
    buffer->size = pConfig->mapData.dwSize;
    pConfig->mapData.dwOffset = 0;

    buffer->fAudioVoiceCallback = (FAudioVoiceCallbackWithBuffer *)malloc(sizeof(FAudioVoiceCallbackWithBuffer));
    if (buffer->fAudioVoiceCallback == NULL)
    {
        free(buffer);
        return setLastStatusAndReturn(SEGA_ERROR_OUT_OF_MEMORY);
    }

    buffer->fAudioVoiceCallback->OnBufferStart = StaticOnBufferStart;
    buffer->fAudioVoiceCallback->OnBufferEnd = StaticOnBufferEnd;
    buffer->fAudioVoiceCallback->OnBufferStart = StaticOnBufferStart;
    buffer->fAudioVoiceCallback->OnLoopEnd = StaticOnLoopEnd;
    buffer->fAudioVoiceCallback->OnStreamEnd = StaticOnStreamEnd;
    buffer->fAudioVoiceCallback->OnVoiceError = StaticOnVoiceError;
    buffer->fAudioVoiceCallback->OnVoiceProcessingPassEnd = StaticOnVoiceProcessingPassEnd;
    buffer->fAudioVoiceCallback->OnVoiceProcessingPassStart = StaticOnVoiceProcessingPassStart;
    buffer->fAudioVoiceCallback->buffer = buffer;

    // Use buffer supplied by caller
    if (dwFlags & ALLOC_USER_MEM)
    {
        buffer->data = (uint8_t *)pConfig->mapData.hBufferHdr;
    }
    // Reuse buffer
    else if (dwFlags & USE_MAPPED_MEM)
    {
        buffer->data = (uint8_t *)pConfig->mapData.hBufferHdr;
    }
    // Allocate new buffer (caller will fill it later)
    else
    {
        if (buffer->size != 0)
        {
            buffer->data = (uint8_t *)malloc(buffer->size);
            if (buffer->data == NULL)
            {
                destroyBufferResources(buffer);
                free(buffer);
                return setLastStatusAndReturn(SEGA_ERROR_OUT_OF_MEMORY);
            }
            memset(buffer->data, 0, buffer->size);
        }
        buffer->flags |= BUFFER_OWNS_DATA;
    }

    if (buffer->size != 0 && buffer->data == NULL)
    {
        destroyBufferResources(buffer);
        free(buffer);
        return setLastStatusAndReturn(SEGA_ERROR_BAD_CONFIG);
    }

    pConfig->mapData.hBufferHdr = buffer->data;

    pthread_mutex_lock(&fAudioMutex);
    result = createSourceVoice(buffer);
    pthread_mutex_unlock(&fAudioMutex);
    if (result != SEGA_SUCCESS)
    {
        destroyBufferResources(buffer);
        free(buffer);
        return setLastStatusAndReturn(result);
    }

    memset(&buffer->fAudioBuffer, 0, sizeof(FAudioBuffer));

    buffer->startLoop = 0;
    buffer->endOffset = buffer->size;
    buffer->endLoop = buffer->size;
    buffer->bDoContinuousLooping = 0;

    for (int channel = 0; channel < MAX_CHANNELS; channel++)
    {
        buffer->channelVolumes[channel] = VOL_MAX;
        for (int send = 0; send < MAX_SENDS; send++)
        {
            buffer->sendLevels[channel][send] = 0;
            buffer->routing[channel][send] = UNUSED_PORT;
        }
    }

    switch (pConfig->byNumChans)
    {
        case 1:
            buffer->routing[0][0] = FRONT_LEFT_PORT;
            buffer->routing[0][1] = FRONT_RIGHT_PORT;
            break;

        case 2:
            buffer->routing[0][0] = FRONT_LEFT_PORT;
            buffer->routing[1][1] = FRONT_RIGHT_PORT;
            break;

        default:
            break;
    }

    result = registerBuffer(buffer);
    if (result != SEGA_SUCCESS)
    {
        destroyBufferResources(buffer);
        free(buffer);
        return setLastStatusAndReturn(result);
    }

    *phHandle = buffer;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_DestroyBuffer(void *hHandle)
{
    HANDLE_CHECK_BUFFER(hHandle, buffer, SEGA_ERROR_BAD_HANDLE);

    unregisterBuffer(buffer);
    destroyBufferResources(buffer);
    free(buffer);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_SetGlobalEAXProperty(GUID *guid, unsigned long ulProperty, void *pData, unsigned long ulDataSize)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return 0;
    }

    if (pData == NULL && ulDataSize != 0)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return 0;
    }

    if (guidEquals(guid, &EAXPROPERTYID_EAX40_SEGA_Custom))
    {
        if (pData == NULL || ulProperty > 1 || ulDataSize <= 3)
        {
            setLastStatus(SEGA_ERROR_BAD_PARAM);
            return 0;
        }

        unsigned int customValue = *(unsigned int *)pData;
        if (customValue > 1)
        {
            setLastStatus(SEGA_ERROR_BAD_PARAM);
            return 0;
        }
    }

    int fxSlotIndex = fxSlotIndexFromGuid(guid);
    if (fxSlotIndex >= 0)
    {
        if (pData != NULL && ulDataSize >= sizeof(GUID))
        {
            GUID *effectGuid = (GUID *)pData;
            fxSlotReverbEnabled[fxSlotIndex] = !guidEquals(effectGuid, &EAX_NULL_GUID);

            if (fxSlotReverbEnabled[fxSlotIndex])
                setTunnelReverbParameters();
        }
    }
    else if (guidEquals(guid, &EAX_REVERB_EFFECT) || guidEquals(guid, &EAXPROPERTYID_EAX40_Context) ||
             guidEquals(guid, &EAXPROPERTYID_EAX40_Source))
    {
        setTunnelReverbParameters();
    }

    setLastStatus(SEGA_SUCCESS);
    return 1;
}

int SEGAAPI_GetGlobalEAXProperty(GUID *guid, unsigned long ulProperty, void *pData, unsigned long ulDataSize)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return 0;
    }

    if (pData == NULL)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return 0;
    }

    if (guidEquals(guid, &EAXPROPERTYID_EAX40_SEGA_Custom))
    {
        if (ulProperty > 1 || ulDataSize <= 3)
        {
            setLastStatus(SEGA_ERROR_BAD_PARAM);
            return 0;
        }

        memset(pData, 0, ulDataSize);
    }

    setLastStatus(SEGA_SUCCESS);
    return 1;
}

int SEGAAPI_SetSPDIFOutChannelStatus(unsigned int dwChannelStatus, unsigned int dwExtChannelStatus)
{
    if (!segaApiInitialized)
        return setLastStatusAndReturn(SEGA_ERROR_INIT_FAILED);

    spdifChannelStatus = dwChannelStatus;
    spdifExtChannelStatus = dwExtChannelStatus;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_GetSPDIFOutChannelStatus(unsigned int *pdwChannelStatus, unsigned int *pdwExtChannelStatus)
{
    if (!segaApiInitialized)
        return setLastStatusAndReturn(SEGA_ERROR_INIT_FAILED);

    if (pdwChannelStatus == NULL || pdwExtChannelStatus == NULL)
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

    *pdwChannelStatus = spdifChannelStatus;
    *pdwExtChannelStatus = spdifExtChannelStatus;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_SetSPDIFOutSampleRate(SPDIFOutputSampleRate dwSamplingRate)
{
    if (!segaApiInitialized)
        return setLastStatusAndReturn(SEGA_ERROR_INIT_FAILED);

    if (dwSamplingRate != SPDIFOUT_44_1KHZ && dwSamplingRate != SPDIFOUT_48KHZ && dwSamplingRate != SPDIFOUT_96KHZ)
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);

    spdifSampleRate = dwSamplingRate;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

SPDIFOutputSampleRate SEGAAPI_GetSPDIFOutSampleRate(void)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return SPDIFOUT_48KHZ;
    }

    setLastStatus(SEGA_SUCCESS);
    return spdifSampleRate;
}

int SEGAAPI_SetSPDIFOutChannelRouting(unsigned int dwChannel, Routing dwSource)
{
    if (!segaApiInitialized)
        return setLastStatusAndReturn(SEGA_ERROR_INIT_FAILED);

    if (dwChannel > 1 || !isValidSpdifRoutingSource(dwSource))
        return setLastStatusAndReturn(SEGA_ERROR_BAD_PARAM);

    spdifRouting[dwChannel] = dwSource;

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

Routing SEGAAPI_GetSPDIFOutChannelRouting(unsigned int dwChannel)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return UNUSED_PORT;
    }

    if (dwChannel > 1)
    {
        setLastStatus(SEGA_ERROR_BAD_PARAM);
        return UNUSED_PORT;
    }

    setLastStatus(SEGA_SUCCESS);
    return spdifRouting[dwChannel];
}

int SEGAAPI_SetIOVolume(SoundBoardIO dwPhysIO, unsigned int dwVolume)
{
    if (!segaApiInitialized)
        return setLastStatusAndReturn(SEGA_ERROR_INIT_FAILED);

    if (!isValidSoundBoardIO(dwPhysIO))
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);

    ioVolumes[dwPhysIO] = dwVolume;

    if (dwPhysIO >= OUT_FRONT_LEFT && dwPhysIO <= OUT_REAR_RIGHT && fAudioSubmixVoices[dwPhysIO] != NULL)
        FAudioVoice_SetVolume(fAudioSubmixVoices[dwPhysIO], dwVolume / (float)VOL_MAX, FAUDIO_COMMIT_NOW);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

unsigned int SEGAAPI_GetIOVolume(SoundBoardIO dwPhysIO)
{
    if (!segaApiInitialized)
    {
        setLastStatus(SEGA_ERROR_INIT_FAILED);
        return VOL_MAX;
    }

    if (!isValidSoundBoardIO(dwPhysIO))
    {
        setLastStatus(SEGA_ERROR_FAIL);
        return VOL_MAX;
    }

    setLastStatus(SEGA_SUCCESS);
    return ioVolumes[dwPhysIO];
}

void SEGAAPI_SetLastStatus(int LastStatus)
{
    setLastStatus(LastStatus);
}

int SEGAAPI_GetLastStatus(void)
{
    return lastStatus;
}

int SEGAAPI_Reset(void)
{
    if (!segaApiInitialized)
        return setLastStatusAndReturn(SEGA_ERROR_INIT_FAILED);

    destroyAllBuffers();
    resetDefaultStates();

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_Init(void)
{
    if (segaApiInitialized)
        return setLastStatusAndReturn(SEGA_SUCCESS);

    pthread_mutex_init(&fAudioMutex, NULL);
    pthread_mutex_lock(&fAudioMutex);

    if (FAudioCreate(&fAudio, 0, FAUDIO_DEFAULT_PROCESSOR) != 0)
    {
        pthread_mutex_unlock(&fAudioMutex);
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);
    }

    if (FAudio_CreateMasteringVoice(fAudio, &fAudioMasteringVoice, FAUDIO_DEFAULT_CHANNELS, FAUDIO_DEFAULT_SAMPLERATE, 0, 0, NULL) != 0)
    {
        FAudio_Release(fAudio);
        fAudio = NULL;
        pthread_mutex_unlock(&fAudioMutex);
        return setLastStatusAndReturn(SEGA_ERROR_FAIL);
    }

    FAudioVoiceDetails fAudioVoiceDetails;
    FAudioVoice_GetVoiceDetails(fAudioMasteringVoice, &fAudioVoiceDetails);

    maxInputChannels = fAudioVoiceDetails.InputChannels;

    for (int i = 0; i < OUTPUT_CHANNELS; i++)
    {
        if (FAudio_CreateSubmixVoice(fAudio, &fAudioSubmixVoices[i], 1, fAudioVoiceDetails.InputSampleRate, 0, 0, NULL, NULL) != 0)
        {
            for (int j = 0; j < i; j++)
            {
                FAudioVoice_DestroyVoice(fAudioSubmixVoices[j]);
                fAudioSubmixVoices[j] = NULL;
            }
            FAudioVoice_DestroyVoice(fAudioMasteringVoice);
            fAudioMasteringVoice = NULL;
            FAudio_Release(fAudio);
            fAudio = NULL;
            pthread_mutex_unlock(&fAudioMutex);
            return setLastStatusAndReturn(SEGA_ERROR_FAIL);
        }
        float matrix[OUTPUT_CHANNELS] = {0};
        if (fAudioVoiceDetails.InputChannels == 6)
        {
            matrix[i] = 1.0f;
        }
        else if (fAudioVoiceDetails.InputChannels == 2)
        {
            // Downmix, L, R, C, S, BL, BR
            if (i < 2)
            {
                matrix[i] = 1.0f;
            }
            else if (i == 2)
            {
                matrix[0] = 1.0f;
                matrix[1] = 1.0f;
            }
            else if (i == 3)
            {
                matrix[0] = 0.5f;
                matrix[1] = 0.5f;
            }
            else
            {
                matrix[i - 4] = 0.5f;
            }
        }
        FAudioVoice_SetOutputMatrix(fAudioSubmixVoices[i], fAudioMasteringVoice, 1, fAudioVoiceDetails.InputChannels, matrix,
                                    FAUDIO_COMMIT_NOW);
    }

    reverbAvailable = 0;
    fAudioReverbEffect = NULL;
    FAPOFXEchoParameters echoParams;
    echoParams.WetDryMix = 0.70f;
    echoParams.Feedback = 0.22f;
    echoParams.Delay = 120.0f;

    if (FAPOFX_CreateFX(&FAPOFX_CLSID_FXEcho, &fAudioReverbEffect, &echoParams, sizeof(echoParams)) == 0 && fAudioReverbEffect != NULL)
    {
        FAudioEffectDescriptor effectDescriptor;
        effectDescriptor.pEffect = fAudioReverbEffect;
        effectDescriptor.InitialState = 1;
        effectDescriptor.OutputChannels = REVERB_CHANNELS;

        FAudioEffectChain effectChain;
        effectChain.EffectCount = 1;
        effectChain.pEffectDescriptors = &effectDescriptor;

        if (FAudio_CreateSubmixVoice(fAudio, &fAudioSubmixVoices[REVERB_SUBMIX_INDEX], REVERB_CHANNELS,
                                     fAudioVoiceDetails.InputSampleRate, 0, 1, NULL, &effectChain) == 0)
        {
            float reverbOutputMatrix[REVERB_CHANNELS * FAUDIO_MAX_AUDIO_CHANNELS] = {0};
            if (fAudioVoiceDetails.InputChannels == 6)
            {
                reverbOutputMatrix[0] = 1.00f;
                reverbOutputMatrix[1] = 0.25f;
                reverbOutputMatrix[4] = 0.65f;
                reverbOutputMatrix[5] = 0.25f;
                reverbOutputMatrix[6] = 0.25f;
                reverbOutputMatrix[7] = 1.00f;
                reverbOutputMatrix[10] = 0.25f;
                reverbOutputMatrix[11] = 0.65f;
            }
            else
            {
                if (fAudioVoiceDetails.InputChannels == 1)
                {
                    reverbOutputMatrix[0] = 1.00f;
                    reverbOutputMatrix[1] = 1.00f;
                }
                else
                {
                    reverbOutputMatrix[0] = 1.00f;
                    reverbOutputMatrix[1] = 0.25f;
                    reverbOutputMatrix[fAudioVoiceDetails.InputChannels] = 0.25f;
                    reverbOutputMatrix[fAudioVoiceDetails.InputChannels + 1] = 1.00f;
                }
            }

            FAudioVoice_SetOutputMatrix(fAudioSubmixVoices[REVERB_SUBMIX_INDEX], fAudioMasteringVoice, REVERB_CHANNELS,
                                        fAudioVoiceDetails.InputChannels, reverbOutputMatrix, FAUDIO_COMMIT_NOW);
            reverbAvailable = 1;
            setTunnelReverbParameters();
        }
        else
        {
            fAudioReverbEffect->Release(fAudioReverbEffect);
            fAudioReverbEffect = NULL;
        }
    }

    resetDefaultStates();
    segaApiInitialized = 1;

    pthread_mutex_unlock(&fAudioMutex);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}

int SEGAAPI_Exit(void)
{
    if (!segaApiInitialized)
        return setLastStatusAndReturn(SEGA_SUCCESS);

    destroyAllBuffers();

    pthread_mutex_lock(&fAudioMutex);

    for (int i = 0; i < MAX_SENDS; i++)
    {
        if (fAudioSubmixVoices[i] != NULL)
        {
            FAudioVoice_DestroyVoice(fAudioSubmixVoices[i]);
            fAudioSubmixVoices[i] = NULL;
        }
    }

    if (fAudioReverbEffect != NULL)
    {
        fAudioReverbEffect->Release(fAudioReverbEffect);
        fAudioReverbEffect = NULL;
    }
    reverbAvailable = 0;

    if (fAudioMasteringVoice != NULL)
    {
        FAudioVoice_DestroyVoice(fAudioMasteringVoice);
        fAudioMasteringVoice = NULL;
    }

    if (fAudio != NULL)
    {
        FAudio_Release(fAudio);
        fAudio = NULL;
    }

    maxInputChannels = 0;
    segaApiInitialized = 0;
    pthread_mutex_unlock(&fAudioMutex);
    pthread_mutex_destroy(&fAudioMutex);

    return setLastStatusAndReturn(SEGA_SUCCESS);
}
