#if defined(_WIN32) || defined(__MINGW32__)

#include "symbolResolver.hpp"

#include <cstddef>
#include <cstring>

namespace
{
int bridgeAlsaSuccess(...)
{
    return 0;
}

void *g_pcm = reinterpret_cast<void *>(0x1);
void *g_params = reinterpret_cast<void *>(0x2);
void *g_mixer = reinterpret_cast<void *>(0x3);

int bridgePcmOpen(void **pcm, const char *name, int stream, int mode)
{
    (void)name;
    (void)stream;
    (void)mode;
    if (pcm) *pcm = g_pcm;
    return 0;
}

int bridgePcmParamsMalloc(void **params)
{
    if (params) *params = g_params;
    return 0;
}

int bridgeMixerOpen(void **mixer, int streams)
{
    (void)streams;
    if (mixer) *mixer = g_mixer;
    return 0;
}

int bridgeMixerRegister(void *mixer, void *classArgs, void *callbacks)
{
    (void)mixer;
    (void)classArgs;
    (void)callbacks;
    return 0;
}

const char *bridgeAlsaStrerror(int error)
{
    (void)error;
    return "pacloader ES1 silent audio";
}

size_t bridgeCtlCardInfoSizeof()
{
    return 256;
}

const char *bridgeMixerElementName(void *element)
{
    (void)element;
    return "Master";
}

int bridgePcmBufferSize(void *params, unsigned long *size)
{
    (void)params;
    /* Keep the silent PCM compatible with the game's 256-frame period. */
    if (size) *size = 1024;
    return 0;
}

int bridgePcmPeriodSize(void *params, unsigned long *size, int *direction)
{
    (void)params;
    if (size) *size = 256;
    if (direction) *direction = 0;
    return 0;
}

int bridgePcmAvail(void *pcm)
{
    (void)pcm;
    return 1024;
}

int bridgePcmState(void *pcm)
{
    (void)pcm;
    /* SNDRV_PCM_STATE_RUNNING. */
    return 3;
}

long bridgePcmWritei(void *pcm, const void *buffer, unsigned long frames)
{
    (void)pcm;
    (void)buffer;
    return static_cast<long>(frames);
}

int bridgeMixerVolumeRange(void *element, long *minimum, long *maximum)
{
    (void)element;
    if (minimum) *minimum = 0;
    if (maximum) *maximum = 100;
    return 0;
}

template <typename T>
void map(const char *name, T function)
{
    SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(function));
}
}

namespace AlsaCompatBridge
{
void initBridges()
{
    map("snd_ctl_card_info", bridgeAlsaSuccess);
    map("snd_ctl_card_info_sizeof", bridgeCtlCardInfoSizeof);
    map("snd_ctl_close", bridgeAlsaSuccess);
    map("snd_ctl_open", bridgeAlsaSuccess);
    map("snd_mixer_attach", bridgeAlsaSuccess);
    map("snd_mixer_close", bridgeAlsaSuccess);
    map("snd_mixer_detach", bridgeAlsaSuccess);
    map("snd_mixer_elem_next", bridgeAlsaSuccess);
    map("snd_mixer_first_elem", bridgeAlsaSuccess);
    map("snd_mixer_free", bridgeAlsaSuccess);
    map("snd_mixer_load", bridgeAlsaSuccess);
    map("snd_mixer_open", bridgeMixerOpen);
    map("snd_mixer_selem_get_capture_volume_range", bridgeMixerVolumeRange);
    map("snd_mixer_selem_get_name", bridgeMixerElementName);
    map("snd_mixer_selem_get_playback_volume_range", bridgeMixerVolumeRange);
    map("snd_mixer_selem_has_capture_switch", bridgeAlsaSuccess);
    map("snd_mixer_selem_has_capture_volume", bridgeAlsaSuccess);
    map("snd_mixer_selem_has_capture_volume_joined", bridgeAlsaSuccess);
    map("snd_mixer_selem_has_common_volume", bridgeAlsaSuccess);
    map("snd_mixer_selem_has_playback_switch", bridgeAlsaSuccess);
    map("snd_mixer_selem_has_playback_volume", bridgeAlsaSuccess);
    map("snd_mixer_selem_has_playback_volume_joined", bridgeAlsaSuccess);
    map("snd_mixer_selem_is_active", bridgeAlsaSuccess);
    map("snd_mixer_selem_register", bridgeMixerRegister);
    map("snd_mixer_selem_set_capture_switch_all", bridgeAlsaSuccess);
    map("snd_mixer_selem_set_capture_volume", bridgeAlsaSuccess);
    map("snd_mixer_selem_set_capture_volume_all", bridgeAlsaSuccess);
    map("snd_mixer_selem_set_playback_switch_all", bridgeAlsaSuccess);
    map("snd_mixer_selem_set_playback_volume", bridgeAlsaSuccess);
    map("snd_mixer_selem_set_playback_volume_all", bridgeAlsaSuccess);
    map("snd_pcm_avail_update", bridgePcmAvail);
    map("snd_pcm_close", bridgeAlsaSuccess);
    map("snd_pcm_hw_params", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_any", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_free", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_get_buffer_size", bridgePcmBufferSize);
    map("snd_pcm_hw_params_get_period_size", bridgePcmPeriodSize);
    map("snd_pcm_hw_params_malloc", bridgePcmParamsMalloc);
    map("snd_pcm_hw_params_set_access", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_set_buffer_time_near", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_set_channels", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_set_channels_max", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_set_format", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_set_period_time_near", bridgeAlsaSuccess);
    map("snd_pcm_hw_params_set_rate_near", bridgeAlsaSuccess);
    map("snd_pcm_open", bridgePcmOpen);
    map("snd_pcm_prepare", bridgeAlsaSuccess);
    map("snd_pcm_start", bridgeAlsaSuccess);
    map("snd_pcm_state", bridgePcmState);
    map("snd_pcm_sw_params", bridgeAlsaSuccess);
    map("snd_pcm_sw_params_current", bridgeAlsaSuccess);
    map("snd_pcm_sw_params_free", bridgeAlsaSuccess);
    map("snd_pcm_sw_params_malloc", bridgePcmParamsMalloc);
    map("snd_pcm_sw_params_set_avail_min", bridgeAlsaSuccess);
    map("snd_pcm_sw_params_set_start_threshold", bridgeAlsaSuccess);
    map("snd_pcm_sw_params_set_xrun_mode", bridgeAlsaSuccess);
    map("snd_pcm_wait", bridgeAlsaSuccess);
    map("snd_pcm_writei", bridgePcmWritei);
    map("snd_strerror", bridgeAlsaStrerror);
}
}

#endif
