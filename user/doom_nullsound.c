// ============================================================
// doom_nullsound.c - no-op sound & music backends for the NyxOS userspace DOOM (v5.9.34)
// ============================================================
// The doomgeneric engine dereferences DG_sound_module / DG_music_module as full
// sound_module_t / music_module_t structs (see i_sound.c InitSfxModule/InitMusicModule).
// The backends that normally define them (SDL, Allegro, GUS) are excluded from our
// build, so we supply real no-op structs here instead of a void* stub — the stub had
// the wrong layout and the engine called through a garbage function pointer, page-
// faulting at RIP 0 during I_Init.
//
// num_sound_devices = 0 makes InitSfxModule find no matching device and skip the module
// entirely (sound_module stays NULL). Every function pointer is still a valid no-op, so
// nothing is ever called through NULL even if a code path reaches one.
#include "doomtype.h"
#include "i_sound.h"

// ---- sound (SFX) ----
static boolean Snd_Init(boolean use_sfx_prefix)                 { (void)use_sfx_prefix; return false; }
static void    Snd_Shutdown(void)                              {}
static int     Snd_GetSfxLumpNum(sfxinfo_t *s)                 { (void)s; return 0; }
static void    Snd_Update(void)                               {}
static void    Snd_UpdateSoundParams(int ch, int vol, int sep){ (void)ch; (void)vol; (void)sep; }
static int     Snd_StartSound(sfxinfo_t *s,int ch,int v,int sp){ (void)s;(void)ch;(void)v;(void)sp; return -1; }
static void    Snd_StopSound(int ch)                          { (void)ch; }
static boolean Snd_SoundIsPlaying(int ch)                     { (void)ch; return false; }
static void    Snd_CacheSounds(sfxinfo_t *s, int n)           { (void)s; (void)n; }

sound_module_t DG_sound_module =
{
    NULL,                 // sound_devices
    0,                    // num_sound_devices -> InitSfxModule skips this module
    Snd_Init,
    Snd_Shutdown,
    Snd_GetSfxLumpNum,
    Snd_Update,
    Snd_UpdateSoundParams,
    Snd_StartSound,
    Snd_StopSound,
    Snd_SoundIsPlaying,
    Snd_CacheSounds,
};

// ---- music ----
static boolean Mus_Init(void)                     { return false; }
static void    Mus_Shutdown(void)                 {}
static void    Mus_SetMusicVolume(int v)          { (void)v; }
static void    Mus_PauseMusic(void)               {}
static void    Mus_ResumeMusic(void)              {}
static void   *Mus_RegisterSong(void *d, int l)   { (void)d; (void)l; return NULL; }
static void    Mus_UnRegisterSong(void *h)        { (void)h; }
static void    Mus_PlaySong(void *h, boolean lp)  { (void)h; (void)lp; }
static void    Mus_StopSong(void)                 {}
static boolean Mus_MusicIsPlaying(void)           { return false; }
static void    Mus_Poll(void)                     {}

music_module_t DG_music_module =
{
    NULL,                 // sound_devices
    0,                    // num_sound_devices
    Mus_Init,
    Mus_Shutdown,
    Mus_SetMusicVolume,
    Mus_PauseMusic,
    Mus_ResumeMusic,
    Mus_RegisterSong,
    Mus_UnRegisterSong,
    Mus_PlaySong,
    Mus_StopSong,
    Mus_MusicIsPlaying,
    Mus_Poll,
};
