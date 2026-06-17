#include "libsegaapi.h"

#include <FAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

GUID EAX_NULL_GUID;
GUID EAX_FREQUENCYSHIFTER_EFFECT;
GUID EAX_ECHO_EFFECT;
GUID EAX_REVERB_EFFECT;
GUID EAX_EQUALIZER_EFFECT;
GUID EAX_DISTORTION_EFFECT;
GUID EAX_AGCCOMPRESSOR_EFFECT;
GUID EAX_PITCHSHIFTER_EFFECT;
GUID EAX_FLANGER_EFFECT;
GUID EAX_VOCALMORPHER_EFFECT;
GUID EAX_AUTOWAH_EFFECT;
GUID EAX_RINGMODULATOR_EFFECT;
GUID EAX_CHORUS_EFFECT;
GUID EAXPROPERTYID_EAX40_FXSlot0;
GUID EAXPROPERTYID_EAX40_FXSlot1;
GUID EAXPROPERTYID_EAX40_FXSlot2;
GUID EAXPROPERTYID_EAX40_FXSlot3;

#define HANDLE_CHECK(handle, returnValue)                                                                              \
    if (handle == NULL)                                                                                                \
    {                                                                                                                  \
        debug(1, "Error: Failed a handle check\n");                                                                    \
        return returnValue;                                                                                            \
    }

#define DEBUG_LEVEL -1

#define OUTPUT_CHANNELS 6 // FL, FR, BL, BR, C, SW
#define MAX_CHANNELS 6
#define MAX_SENDS 13

FAudio *fAudio;
pthread_mutex_t fAudioMutex;
FAudioMasteringVoice *fAudioMasteringVoice;
FAudioSubmixVoice *fAudioSubmixVoices[MAX_SENDS];

unsigned int lastStatus;
unsigned int maxInputChannels;

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

    unsigned int startLoop;
    unsigned int endLoop;
    unsigned int endOffset;

    OutputFormat outputFormat;
    int updateOutputFormat;

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

void debug(int level, const char *format, ...)
{
    if (DEBUG_LEVEL < level)
        return;

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    fflush(stdout);
    va_end(args);
}

static unsigned int bytesToSamples(Buffer *buffer, int value)
{
    switch (buffer->outputFormat.dwSampleFormat)
    {
    case SIGNED_16PCM:
        return value / (buffer->outputFormat.byNumChans * 2);
        break;

    case UNSIGNED_8PCM:
    default:
        return value / buffer->outputFormat.byNumChans;
        break;
    }
}

static unsigned int samplesToBytes(Buffer *buffer, int value)
{
    switch (buffer->outputFormat.dwSampleFormat)
    {
    case SIGNED_16PCM:
        return value * buffer->outputFormat.byNumChans * 2;
        break;

    case UNSIGNED_8PCM:
    default:
        return value * buffer->outputFormat.byNumChans;
        break;
    }
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
    float matrix[MAX_SENDS][MAX_CHANNELS];

    FAudioSubmixVoice *voices[MAX_CHANNELS * MAX_SENDS];
    Routing routes[MAX_CHANNELS * MAX_SENDS];

    int voiceIndex = 0;

    for (unsigned int i = 0; i < buffer->outputFormat.byNumChans; i++)
    {
        for (unsigned int j = 0; j < MAX_SENDS; j++)
        {
            matrix[j][i] = 0;

            debug(1, "Entry channel %u send %u to %s with level %X and channel level %X\n", i, j,
                  getRoutingString(buffer->routing[i][j]), buffer->sendLevels[i][j], buffer->channelVolumes[i]);

            if (buffer->routing[i][j] != UNUSED_PORT && buffer->routing[i][j] < OUTPUT_CHANNELS)
            {
                voices[voiceIndex] = fAudioSubmixVoices[buffer->routing[i][j]];
                routes[voiceIndex] = buffer->routing[i][j];

                matrix[voiceIndex][i] =
                    (buffer->sendLevels[i][j] / (float)VOL_MAX) * (buffer->channelVolumes[i] / (float)VOL_MAX);

                voiceIndex++;
            }
        }
    }

    debug(1, "-- matrix --\n");
    for (unsigned int j = 0; j < buffer->outputFormat.byNumChans; j++)
    {
        debug(1, "CHANNEL_%u ", j + 1);
    }
    debug(1, "\n");
    for (int i = 0; i < voiceIndex; i++)
    {
        debug(1, "%s ", getRoutingString(routes[i]));
        for (unsigned int j = 0; j < buffer->outputFormat.byNumChans; j++)
        {
            debug(1, "%f ", matrix[i][j]);
        }
        debug(1, "\n");
    }
    debug(1, "--\n");

    if (voiceIndex == 0)
        return;

    FAudioSendDescriptor fAudioSendDescriptor[MAX_CHANNELS * MAX_SENDS];
    for (int i = 0; i < voiceIndex; i++)
    {
        fAudioSendDescriptor[i].Flags = 0;
        fAudioSendDescriptor[i].pOutputVoice = voices[i];
    }

    FAudioVoiceSends fAudioVoiceSends = {.SendCount = voiceIndex, .pSends = fAudioSendDescriptor};
    FAudioVoice_SetOutputVoices(buffer->fAudioSourceVoice, &fAudioVoiceSends);

    for (int i = 0; i < voiceIndex; i++)
        FAudioVoice_SetOutputMatrix(buffer->fAudioSourceVoice, voices[i], buffer->outputFormat.byNumChans, 1,
                                    &matrix[i][0], FAUDIO_COMMIT_NOW);
}

int SEGAAPI_Play(void *hHandle)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    processSends(buffer);

    SEGAAPI_UpdateBuffer(buffer, -1, -1);

    buffer->playbackStatus = PLAYBACK_STATUS_ACTIVE;

    FAudioSourceVoice_Start(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);

    return SEGA_SUCCESS;
}

int SEGAAPI_Pause(void *hHandle)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->playbackStatus = PLAYBACK_STATUS_PAUSE;

    FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);

    return SEGA_SUCCESS;
}

int SEGAAPI_Stop(void *hHandle)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->playbackStatus = PLAYBACK_STATUS_STOP;

    FAudioSourceVoice_Stop(buffer->fAudioSourceVoice, 0, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);

    return SEGA_SUCCESS;
}

int SEGAAPI_PlayWithSetup(void *hHandle, unsigned int dwNumSendRouteParams, SendRouteParamSet *pSendRouteParams,
                          unsigned int dwNumSendLevelParams, SendLevelParamSet *pSendLevelParams,
                          unsigned int dwNumVoiceParams, VoiceParamSet *pVoiceParams, unsigned int dwNumSynthParams,
                          SynthParamSet *pSynthParams)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    // Buffer *buffer = (Buffer *)hHandle;

    for (unsigned int i = 0; i < dwNumSendRouteParams; i++)
        SEGAAPI_SetSendRouting(hHandle, pSendRouteParams[i].dwChannel, pSendRouteParams[i].dwSend,
                               pSendRouteParams[i].dwDest);

    for (unsigned int i = 0; i < dwNumSendLevelParams; i++)
        SEGAAPI_SetSendLevel(hHandle, pSendLevelParams[i].dwChannel, pSendLevelParams[i].dwSend,
                             pSendLevelParams[i].dwLevel);

    for (unsigned int i = 0; i < dwNumVoiceParams; i++)
    {
        switch (pVoiceParams[i].VoiceIoctl)
        {
        case SET_START_LOOP_OFFSET:
            SEGAAPI_SetStartLoopOffset(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_END_LOOP_OFFSET:
            SEGAAPI_SetEndLoopOffset(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_END_OFFSET:
            SEGAAPI_SetEndOffset(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_PLAY_POSITION:
            SEGAAPI_SetPlaybackPosition(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_LOOP_STATE:
            SEGAAPI_SetLoopState(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_NOTIFICATION_POINT:
            SEGAAPI_SetNotificationPoint(hHandle, pVoiceParams[i].dwParam1);
            break;
        case CLEAR_NOTIFICATION_POINT:
            SEGAAPI_ClearNotificationPoint(hHandle, pVoiceParams[i].dwParam1);
            break;
        case SET_NOTIFICATION_FREQUENCY:
            SEGAAPI_SetNotificationFrequency(hHandle, pVoiceParams[i].dwParam1);
            break;
        default:
            break;
        }
    }

    for (unsigned int i = 0; i < dwNumSynthParams; i++)
        SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);

    return SEGAAPI_Play(hHandle);
}

PlaybackStatus SEGAAPI_GetPlaybackStatus(void *hHandle)
{
    HANDLE_CHECK(hHandle, PLAYBACK_STATUS_INVALID);
    Buffer *buffer = (Buffer *)hHandle;

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
    return status;
}

int SEGAAPI_SetFormat(void *hHandle, OutputFormat *pFormat)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    debug(1, "Warning: This game used SetFormat\n");

    buffer->outputFormat.byNumChans = pFormat->byNumChans;
    buffer->outputFormat.dwSampleFormat = pFormat->dwSampleFormat;
    buffer->outputFormat.dwSampleRate = pFormat->dwSampleRate;

    return SEGA_SUCCESS;
}

int SEGAAPI_GetFormat(void *hHandle, OutputFormat *pFormat)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    debug(1, "Warning: This game used GetFormat\n");

    pFormat->byNumChans = buffer->outputFormat.byNumChans;
    pFormat->dwSampleFormat = buffer->outputFormat.dwSampleFormat;
    pFormat->dwSampleRate = buffer->outputFormat.dwSampleRate;

    return SEGA_SUCCESS;
}

int SEGAAPI_SetSampleRate(void *hHandle, unsigned int dwSampleRate)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->outputFormat.dwSampleRate = dwSampleRate;

    FAudioVoiceState fAudioVoiceState;
    FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &fAudioVoiceState, 0);

    // If there are no buffers playing, set the sample rate immidiately.
    if (!fAudioVoiceState.BuffersQueued)
    {
        FAudioSourceVoice_SetSourceSampleRate(buffer->fAudioSourceVoice, dwSampleRate);
        return SEGA_SUCCESS;
    }

    // Flush the buffers and set the sample rate on next update
    buffer->updateOutputFormat = 1;
    FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);

    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetSampleRate(void *hHandle)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    return buffer->outputFormat.dwSampleRate;
}

int SEGAAPI_SetPriority(void *hHandle, unsigned int dwPriority)
{
    // We have an unlimited number of buffers, so we can ignore this function.
    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetPriority(void *hHandle)
{
    // Assume everything has maximum priority and won't get cut.
    return P_MAXIMUM;
}

int SEGAAPI_SetUserData(void *hHandle, void *hUserData)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->userData = hUserData;

    return SEGA_SUCCESS;
}

void *SEGAAPI_GetUserData(void *hHandle)
{
    HANDLE_CHECK(hHandle, NULL);

    Buffer *buffer = (Buffer *)hHandle;

    return buffer->userData;
}

int SEGAAPI_SetSendRouting(void *hHandle, unsigned int dwChannel, unsigned int dwSend, Routing dwDest)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    if (dwChannel >= MAX_CHANNELS)
        return SEGA_ERROR_INVALID_CHANNEL;

    if (dwSend >= MAX_SENDS)
        return SEGA_ERROR_INVALID_SEND;

    debug(1, "Info: Set send routing called for buffer %p channel %d send %d to destination %s\n", hHandle, dwChannel,
          dwSend, getRoutingString(dwDest));

    buffer->routing[dwChannel][dwSend] = dwDest;

    processSends(buffer);

    return SEGA_SUCCESS;
}

Routing SEGAAPI_GetSendRouting(void *hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    HANDLE_CHECK(hHandle, UNUSED_PORT);

    Buffer *buffer = (Buffer *)hHandle;

    if (dwChannel >= MAX_CHANNELS)
        return SEGA_ERROR_INVALID_CHANNEL;

    if (dwSend >= MAX_SENDS)
        return SEGA_ERROR_INVALID_SEND;

    return buffer->routing[dwChannel][dwSend];
}

int SEGAAPI_SetSendLevel(void *hHandle, unsigned int dwChannel, unsigned int dwSend, unsigned int dwLevel)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    if (dwChannel >= MAX_CHANNELS)
        return SEGA_ERROR_INVALID_CHANNEL;

    if (dwSend >= MAX_SENDS)
        return SEGA_ERROR_INVALID_SEND;

    buffer->sendLevels[dwChannel][dwSend] = dwLevel;

    processSends(buffer);

    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetSendLevel(void *hHandle, unsigned int dwChannel, unsigned int dwSend)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    if (dwChannel >= MAX_CHANNELS)
        return 0;

    if (dwSend >= MAX_SENDS)
        return 0;

    return buffer->sendLevels[dwChannel][dwSend];
}

int SEGAAPI_SetChannelVolume(void *hHandle, unsigned int dwChannel, unsigned int dwVolume)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    if (dwChannel >= MAX_CHANNELS)
        return SEGA_ERROR_INVALID_CHANNEL;

    buffer->channelVolumes[dwChannel] = dwVolume;

    processSends(buffer);

    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetChannelVolume(void *hHandle, unsigned int dwChannel)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    if (dwChannel >= MAX_CHANNELS)
        return 0;

    return buffer->channelVolumes[dwChannel];
}

int SEGAAPI_SetPlaybackPosition(void *hHandle, unsigned int dwPlaybackPos)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    if (dwPlaybackPos != 0)
    {
        debug(1, "Error: This game is trying to set the playback position which is not yet supported set to %d.\n",
              dwPlaybackPos);
        return SEGA_SUCCESS;
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
        // printf("RETURNED SEGAAPI_SetPlaybackPosition(%p, %d) Looping %d Status %s\n", hHandle, dwPlaybackPos,
        //        buffer->bDoContinuousLooping, statusString);
        // printf("Buffer Size: %d bytes, Channels: %d, Sample Rate: %d\n", buffer->fAudioFormat.nAvgBytesPerSec,
        //        buffer->fAudioFormat.nChannels, buffer->fAudioFormat.nSamplesPerSec);
        return SEGA_SUCCESS;
    }

    // printf("Buffer Size: %d bytes, Channels: %d, Sample Rate: %d\n", buffer->fAudioFormat.nAvgBytesPerSec,
    //        buffer->fAudioFormat.nChannels, buffer->fAudioFormat.nSamplesPerSec);

    // Retrofan Test

    // printf("SEGAAPI_SetPlaybackPosition(%p, %d) Looping %d Status %s\n", hHandle, dwPlaybackPos,
    //        buffer->bDoContinuousLooping, statusString);

    FAudioVoice_DestroyVoice(buffer->fAudioSourceVoice);

    buffer->fAudioFormat.wFormatTag = 1;
    buffer->fAudioFormat.nChannels = buffer->outputFormat.byNumChans;
    buffer->fAudioFormat.nSamplesPerSec = buffer->outputFormat.dwSampleRate;
    buffer->fAudioFormat.nAvgBytesPerSec = samplesToBytes(buffer, buffer->outputFormat.dwSampleRate);
    buffer->fAudioFormat.nBlockAlign = samplesToBytes(buffer, 1);
    buffer->fAudioFormat.wBitsPerSample = buffer->outputFormat.dwSampleFormat == SIGNED_16PCM ? 16 : 8;
    buffer->fAudioFormat.cbSize = sizeof(FAudioWaveFormatEx);

    FAudio_CreateSourceVoice(fAudio, &buffer->fAudioSourceVoice, &buffer->fAudioFormat, 0, FAUDIO_MAX_FREQ_RATIO,
                             (FAudioVoiceCallback *)buffer->fAudioVoiceCallback, NULL, NULL);

    if (status == 1)
    {
        SEGAAPI_Play(hHandle);
    }

    // Should we flush the buffers or stop here?

    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetPlaybackPosition(void *hHandle)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    FAudioVoiceState fAudioVoiceState;
    FAudioSourceVoice_GetState(buffer->fAudioSourceVoice, &fAudioVoiceState, 0);

    unsigned int bytesPlayed =
        fAudioVoiceState.SamplesPlayed * buffer->fAudioFormat.nChannels * (buffer->fAudioFormat.wBitsPerSample / 8);

    return bytesPlayed % buffer->size;
}

int SEGAAPI_SetNotificationFrequency(void *hHandle, unsigned int dwFrameCount)
{
    debug(1, "Warning: This game is trying to use the notification frequency API\n");
    return SEGA_SUCCESS;
}

int SEGAAPI_SetNotificationPoint(void *hHandle, unsigned int dwBufferOffset)
{
    debug(1, "Warning: This game is trying to use the notification frequency API\n");
    return SEGA_SUCCESS;
}

int SEGAAPI_ClearNotificationPoint(void *hHandle, unsigned int dwBufferOffset)
{
    debug(1, "Warning: This game is trying to use the notification frequency API\n");
    return SEGA_SUCCESS;
}

int SEGAAPI_SetStartLoopOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->startLoop = dwOffset;

    SEGAAPI_UpdateBuffer(hHandle, -1, -1);

    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetStartLoopOffset(void *hHandle)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    return buffer->startLoop;
}

int SEGAAPI_SetEndLoopOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->endLoop = dwOffset;

    SEGAAPI_UpdateBuffer(hHandle, -1, -1);

    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetEndLoopOffset(void *hHandle)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    return buffer->endLoop;
}

int SEGAAPI_SetEndOffset(void *hHandle, unsigned int dwOffset)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->endOffset = dwOffset;

    SEGAAPI_UpdateBuffer(hHandle, -1, -1);

    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetEndOffset(void *hHandle)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    return buffer->endOffset;
}

int SEGAAPI_SetLoopState(void *hHandle, int bDoContinuousLooping)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->bDoContinuousLooping = bDoContinuousLooping;

    SEGAAPI_UpdateBuffer(hHandle, -1, -1);

    return SEGA_SUCCESS;
}

int SEGAAPI_GetLoopState(void *hHandle)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    return buffer->bDoContinuousLooping;
}

int SEGAAPI_UpdateBuffer(void *hHandle, unsigned int dwStartOffset, unsigned int dwLength)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    // Ignore the call if we're waiting to update the output format
    if (buffer->updateOutputFormat)
        return SEGA_SUCCESS;

    pthread_mutex_lock(&fAudioMutex);

    FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);

    // memset(buffer->fAudioBuffer, 0, sizeof(FAudioBuffer));

    buffer->fAudioBuffer.Flags = 0;
    buffer->fAudioBuffer.AudioBytes = buffer->size;
    buffer->fAudioBuffer.pAudioData = buffer->data;
    buffer->fAudioBuffer.PlayBegin = bytesToSamples(buffer, buffer->startLoop);
    buffer->fAudioBuffer.PlayLength = bytesToSamples(buffer, buffer->endOffset - buffer->startLoop);
    buffer->fAudioBuffer.LoopBegin = 0;
    buffer->fAudioBuffer.LoopLength = 0;
    buffer->fAudioBuffer.LoopCount = 0;
    buffer->fAudioBuffer.pContext = NULL;

    if (buffer->bDoContinuousLooping)
    {
        buffer->fAudioBuffer.LoopBegin = bytesToSamples(buffer, buffer->startLoop);
        buffer->fAudioBuffer.LoopLength = bytesToSamples(buffer, buffer->endLoop - buffer->startLoop);
        buffer->fAudioBuffer.LoopCount = FAUDIO_LOOP_INFINITE;
    }

    FAudioSourceVoice_SubmitSourceBuffer(buffer->fAudioSourceVoice, &buffer->fAudioBuffer, NULL);

    pthread_mutex_unlock(&fAudioMutex);

    return SEGA_SUCCESS;
}

int SEGAAPI_SetSynthParam(void *hHandle, SynthParams param, int lPARWValue)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    buffer->synthParams[param] = lPARWValue;

    switch (param)
    {
    case ATTENUATION:   // I think this is probably wrong and needs a fix
    {
        float volumeInDecibels = 0.0f - lPARWValue / 10.0f;
        float volumeInGain = volumeInDecibels > -100.f ? powf(10.0f, volumeInDecibels * 0.05f) : 0;
        FAudioVoice_SetVolume(buffer->fAudioSourceVoice, volumeInGain, FAUDIO_COMMIT_NOW);
    }
    break;

    case PITCH:
    {
        float semiTones = lPARWValue / 100.0f;
        float freqRatio = powf(2.0f, semiTones / 12.0f);
        FAudioSourceVoice_SetFrequencyRatio(buffer->fAudioSourceVoice, freqRatio, FAUDIO_COMMIT_NOW);
    }
    break;

    default:
        break;
    }

    return SEGA_SUCCESS;
}

int SEGAAPI_GetSynthParam(void *hHandle, SynthParams param)
{
    HANDLE_CHECK(hHandle, 0);

    Buffer *buffer = (Buffer *)hHandle;

    return buffer->synthParams[param];
}

int SEGAAPI_SetSynthParamMultiple(void *hHandle, unsigned int dwNumParams, SynthParamSet *pSynthParams)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    // Buffer *buffer = (Buffer *)hHandle;

    for (unsigned int i = 0; i < dwNumParams; i++)
    {
        SEGAAPI_SetSynthParam(hHandle, pSynthParams[i].param, pSynthParams[i].lPARWValue);
    }

    return SEGA_SUCCESS;
}

int SEGAAPI_GetSynthParamMultiple(void *hHandle, unsigned int dwNumParams, SynthParamSet *pSynthParams)
{

    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    for (unsigned int i = 0; i < dwNumParams; i++)
    {
        pSynthParams[i].lPARWValue = buffer->synthParams[pSynthParams[i].param];
    }

    return SEGA_SUCCESS;
}

int SEGAAPI_SetReleaseState(void *hHandle, int bSet)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    if (!bSet)
        return SEGA_SUCCESS;

    FAudioSourceVoice_FlushSourceBuffers(buffer->fAudioSourceVoice);

    return SEGAAPI_Stop(hHandle);
}

int SEGAAPI_CreateBuffer(BufferConfig *pConfig, BufferCallback pCallback, unsigned int dwFlags, void **phHandle)
{
    HANDLE_CHECK(pConfig, SEGA_ERROR_BAD_CONFIG);
    HANDLE_CHECK(phHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)malloc(sizeof(Buffer));

    buffer->playbackStatus = PLAYBACK_STATUS_STOP;
    buffer->callback = pCallback;

    buffer->outputFormat.dwSampleRate = pConfig->dwSampleRate;
    buffer->outputFormat.dwSampleFormat = pConfig->dwSampleFormat;
    buffer->outputFormat.byNumChans = pConfig->byNumChans;
    buffer->updateOutputFormat = 0;

    buffer->userData = pConfig->hUserData;
    buffer->size = pConfig->mapData.dwSize;
    pConfig->mapData.dwOffset = 0;

    debug(1, "Info: Buffer created with %d channels\n", pConfig->byNumChans);

    buffer->fAudioVoiceCallback = (FAudioVoiceCallbackWithBuffer *)malloc(sizeof(FAudioVoiceCallbackWithBuffer));
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
        buffer->data = (uint8_t *)malloc(buffer->size);
        memset(buffer->data, 0, buffer->size);
    }

    pConfig->mapData.hBufferHdr = buffer->data;

    buffer->fAudioFormat.wFormatTag = 1;
    buffer->fAudioFormat.nChannels = buffer->outputFormat.byNumChans;
    buffer->fAudioFormat.nSamplesPerSec = buffer->outputFormat.dwSampleRate;
    buffer->fAudioFormat.nAvgBytesPerSec = samplesToBytes(buffer, buffer->outputFormat.dwSampleRate);
    buffer->fAudioFormat.nBlockAlign = samplesToBytes(buffer, 1);
    buffer->fAudioFormat.wBitsPerSample = pConfig->dwSampleFormat == SIGNED_16PCM ? 16 : 8;
    buffer->fAudioFormat.cbSize = sizeof(FAudioWaveFormatEx);

    pthread_mutex_lock(&fAudioMutex);
    FAudio_CreateSourceVoice(fAudio, &buffer->fAudioSourceVoice, &buffer->fAudioFormat, 0, FAUDIO_MAX_FREQ_RATIO,
                             (FAudioVoiceCallback *)buffer->fAudioVoiceCallback, NULL, NULL);
    pthread_mutex_unlock(&fAudioMutex);

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

    *phHandle = buffer;

    return SEGA_SUCCESS;
}

int SEGAAPI_DestroyBuffer(void *hHandle)
{
    HANDLE_CHECK(hHandle, SEGA_ERROR_BAD_HANDLE);

    Buffer *buffer = (Buffer *)hHandle;

    FAudioVoice_DestroyVoice(buffer->fAudioSourceVoice);

    // TODO: hHandle = NULL?
    debug(1, "SEGAAPI_DestroyBuffer called\n");

    return SEGA_SUCCESS;
}

int SEGAAPI_SetGlobalEAXProperty(GUID *guid, unsigned long ulProperty, void *pData, unsigned long ulDataSize)
{
    // debug(1, "Warning: This game tried to use SEGAAPI_SetGlobalEAXProperty\n");
    return SEGA_SUCCESS;
}

int SEGAAPI_GetGlobalEAXProperty(GUID *guid, unsigned long ulProperty, void *pData, unsigned long ulDataSize)
{
    debug(1, "Warning: This game tried to use SEGAAPI_GetGlobalEAXProperty\n");
    return SEGA_SUCCESS;
}

int SEGAAPI_SetSPDIFOutChannelStatus(unsigned int dwChannelStatus, unsigned int dwExtChannelStatus)
{
    debug(1, "Warning: This game tried to use SEGAAPI_SetSPDIFOutChannelStatus\n");
    return SEGA_SUCCESS;
}

int SEGAAPI_GetSPDIFOutChannelStatus(unsigned int *pdwChannelStatus, unsigned int *pdwExtChannelStatus)
{
    debug(1, "Warning: This game tried to use SEGAAPI_GetSPDIFOutChannelStatus\n");
    return SEGA_SUCCESS;
}

int SEGAAPI_SetSPDIFOutSampleRate(SPDIFOutputSampleRate dwSamplingRate)
{
    debug(1, "Warning: This game tried to use SEGAAPI_SetSPDIFOutSampleRate\n");
    return SEGA_SUCCESS;
}

SPDIFOutputSampleRate SEGAAPI_GetSPDIFOutSampleRate(void)
{
    debug(1, "Warning: This game tried to use SEGAAPI_GetSPDIFOutSampleRate\n");
    return SPDIFOUT_44_1KHZ;
}

int SEGAAPI_SetSPDIFOutChannelRouting(unsigned int dwChannel, Routing dwSource)
{
    debug(1, "Warning: This game tried to use SEGAAPI_SetSPDIFOutChannelRouting\n");
    return SEGA_SUCCESS;
}

Routing SEGAAPI_GetSPDIFOutChannelRouting(unsigned int dwChannel)
{
    debug(1, "Warning: This game tried to use SEGAAPI_GetSPDIFOutChannelRouting\n");
    return UNUSED_PORT;
}

int SEGAAPI_SetIOVolume(SoundBoardIO dwPhysIO, unsigned int dwVolume)
{
    FAudioVoice_SetVolume(fAudioMasteringVoice, dwVolume / (float)VOL_MAX, FAUDIO_COMMIT_NOW);
    return SEGA_SUCCESS;
}

unsigned int SEGAAPI_GetIOVolume(SoundBoardIO dwPhysIO)
{
    float volume;
    FAudioVoice_GetVolume(fAudioMasteringVoice, &volume);
    return (int)(volume * VOL_MAX);
}

void SEGAAPI_SetLastStatus(int LastStatus)
{
    lastStatus = LastStatus;
}

int SEGAAPI_GetLastStatus(void)
{
    return lastStatus;
}

int SEGAAPI_Reset(void)
{
    return SEGA_SUCCESS;
}

int SEGAAPI_Init(void)
{
    pthread_mutex_init(&fAudioMutex, NULL);
    pthread_mutex_lock(&fAudioMutex);

    FAudioCreate(&fAudio, 0, FAUDIO_DEFAULT_PROCESSOR);

    FAudio_CreateMasteringVoice(fAudio, &fAudioMasteringVoice, FAUDIO_DEFAULT_CHANNELS, FAUDIO_DEFAULT_SAMPLERATE, 0, 0,
                                NULL);

    FAudioVoiceDetails fAudioVoiceDetails;
    FAudioVoice_GetVoiceDetails(fAudioMasteringVoice, &fAudioVoiceDetails);

    maxInputChannels = fAudioVoiceDetails.InputChannels;

    for (int i = 0; i < OUTPUT_CHANNELS; i++)
    {
        FAudio_CreateSubmixVoice(fAudio, &fAudioSubmixVoices[i], 1, fAudioVoiceDetails.InputSampleRate, 0, 0, NULL,
                                 NULL);
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
        FAudioVoice_SetOutputMatrix(fAudioSubmixVoices[i], fAudioMasteringVoice, 1, fAudioVoiceDetails.InputChannels,
                                    matrix, FAUDIO_COMMIT_NOW);
    }

    pthread_mutex_unlock(&fAudioMutex);

    return SEGA_SUCCESS;
}

int SEGAAPI_Exit(void)
{
    debug(1, "SEGAAPI_Exit called\n");

    return SEGA_SUCCESS;
}
