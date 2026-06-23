// ============================================================
// doom_nyxos_sound.c - DOOM sound module para NyxOS (SB16)
// ============================================================
#define CMAP256 1
#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200
#include "kernel.h"
#include "sb16.h"
#include "doom_src/doomtype.h"
#include "doom_src/doomfeatures.h"
#include "doom_src/z_zone.h"
#include "doom_src/i_sound.h"
#include "doom_src/w_wad.h"
#include "doom_src/m_misc.h"
#include "doom_src/deh_str.h"

#ifndef false
#define false 0
#define true 1
#endif

#define NUM_CHANNELS 8

// DMX sound lump format
typedef struct __attribute__((packed)) {
    uint8_t magic[2];
    uint16_t samplerate;
    uint32_t length;
} dmx_header_t;

typedef struct {
    uint8_t* data;
    uint32_t length;
    uint32_t samplerate;
    uint32_t pos;
    uint8_t  playing;
    uint8_t  vol;
    uint8_t  sep;
    sfxinfo_t* sfx;
} sound_channel_t;

static sound_channel_t channels[NUM_CHANNELS];
static boolean sound_initialized = false;
static boolean use_sfx_prefix;

// Stubs needed by i_sound.c when FEATURE_SOUND is enabled
int use_libsamplerate = 0;
float libsamplerate_scale = 1.0f;

static void Nyx_StopSound(int channel);
static boolean Nyx_SoundIsPlaying(int channel);

// ----- DMX sound loading -----
static int Nyx_GetSfxLumpNum(sfxinfo_t* sfx) {
    char namebuf[9];
    if (sfx->link != NULL) sfx = sfx->link;
    if (use_sfx_prefix) {
        M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
    } else {
        M_StringCopy(namebuf, DEH_String(sfx->name), sizeof(namebuf));
    }
    return W_GetNumForName(namebuf);
}

static boolean load_dmx_sound(sfxinfo_t* sfxinfo) {
    int lumpnum = sfxinfo->lumpnum;
    if (lumpnum < 0) {
        lumpnum = Nyx_GetSfxLumpNum(sfxinfo);
        sfxinfo->lumpnum = lumpnum;
    }
    unsigned int lumplen = W_LumpLength(lumpnum);
    uint8_t* data = W_CacheLumpNum(lumpnum, PU_STATIC);
    if (!data || lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
        return false;

    uint32_t rate = (data[3] << 8) | data[2];
    uint32_t len = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
    if (len > lumplen - 8 || len <= 48) return false;

    data += 16;
    len -= 32;
    uint8_t* pcm = data + 8;  // Skip first 16 bytes, then 8 more = offset 24 total

    uint32_t total_samples = len;
    uint32_t alloc_size = sizeof(int16_t) * total_samples + 8;
    int16_t* converted = (int16_t*)kmalloc(alloc_size);
    if (!converted) return false;

    for (uint32_t i = 0; i < total_samples; i++)
        converted[i] = ((int16_t)((int)pcm[i] - 128)) << 8;

    sfxinfo->driver_data = converted;
    // Store rate + length in first 8 bytes of the buffer
    ((uint32_t*)converted)[0] = rate;
    ((uint32_t*)converted)[1] = total_samples;
    W_ReleaseLumpNum(lumpnum);
    return true;
}

// ----- Module implementation -----
static boolean Nyx_InitSound(boolean _use_sfx_prefix) {
    use_sfx_prefix = _use_sfx_prefix;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        channels[i].playing = 0;
        channels[i].data = NULL;
        channels[i].sfx = NULL;
    }
    sound_initialized = true;
    return true;
}

static void Nyx_ShutdownSound(void) {
    sound_initialized = false;
}

static void Nyx_UpdateSound(void) {
    if (!sound_initialized) return;

    // Check for finished channels
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (channels[i].playing && !Nyx_SoundIsPlaying(i))
            Nyx_StopSound(i);
    }
}

static void Nyx_UpdateSoundParams(int channel, int vol, int sep) {
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS) return;
    channels[channel].vol = (uint8_t)vol;
    channels[channel].sep = (uint8_t)sep;
}

static int Nyx_StartSound(sfxinfo_t* sfxinfo, int channel, int vol, int sep) {
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS) return -1;

    Nyx_StopSound(channel);
    if (!sfxinfo->driver_data) {
        if (!load_dmx_sound(sfxinfo)) return -1;
    }

    int16_t* data = (int16_t*)sfxinfo->driver_data;
    uint32_t rate = ((uint32_t*)data)[0];
    uint32_t len = ((uint32_t*)data)[1];

    channels[channel].data = (uint8_t*)(data + 4);
    channels[channel].length = len;
    channels[channel].samplerate = rate;
    channels[channel].pos = 0;
    channels[channel].playing = 1;
    channels[channel].vol = (uint8_t)vol;
    channels[channel].sep = (uint8_t)sep;
    channels[channel].sfx = sfxinfo;
    return channel;
}

static void Nyx_StopSound(int channel) {
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS) return;
    channels[channel].playing = 0;
    channels[channel].data = NULL;
    channels[channel].sfx = NULL;
}

static boolean Nyx_SoundIsPlaying(int channel) {
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS) return false;
    return channels[channel].playing;
}

static void Nyx_CacheSounds(sfxinfo_t* sounds, int num_sounds) {
    (void)sounds;
    (void)num_sounds;
}

// ----- Module struct -----
static snddevice_t nyx_sound_devices[] = {
    SNDDEVICE_SB,
    SNDDEVICE_PCSPEAKER,
};

sound_module_t DG_sound_module = {
    nyx_sound_devices,
    1,
    Nyx_InitSound,
    Nyx_ShutdownSound,
    Nyx_GetSfxLumpNum,
    Nyx_UpdateSound,
    Nyx_UpdateSoundParams,
    Nyx_StartSound,
    Nyx_StopSound,
    Nyx_SoundIsPlaying,
    Nyx_CacheSounds,
};

// ----- Music module stub -----
static boolean Nyx_InitMusic(void) { return false; }
static void Nyx_ShutdownMusic(void) {}
static void Nyx_SetMusicVolume(int volume) { (void)volume; }
static void Nyx_PauseMusic(void) {}
static void Nyx_ResumeMusic(void) {}
static void* Nyx_RegisterSong(void* data, int len) { (void)data; (void)len; return NULL; }
static void Nyx_UnRegisterSong(void* handle) { (void)handle; }
static void Nyx_PlaySong(void* handle, boolean looping) { (void)handle; (void)looping; }
static void Nyx_StopSong(void) {}
static boolean Nyx_MusicIsPlaying(void) { return false; }
static void Nyx_MusicPoll(void) {}

music_module_t DG_music_module = {
    nyx_sound_devices,
    1,
    Nyx_InitMusic,
    Nyx_ShutdownMusic,
    Nyx_SetMusicVolume,
    Nyx_PauseMusic,
    Nyx_ResumeMusic,
    Nyx_RegisterSong,
    Nyx_UnRegisterSong,
    Nyx_PlaySong,
    Nyx_StopSong,
    Nyx_MusicIsPlaying,
    Nyx_MusicPoll,
};
