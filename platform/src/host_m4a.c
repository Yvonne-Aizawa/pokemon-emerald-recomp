/*
 * platform/src/host_m4a.c
 *
 * Silent stand-in for the GBA's M4A sound engine (refrence/src/m4a.c,
 * m4a_tables.c and m4a_1.s, all excluded from the host build).
 *
 * Every music player stays idle: their status words remain 0, so the game's
 * "is this song/SE/cry still playing?" checks see nothing playing and never
 * wait on audio. Phase 13 replaces this with real playback.
 */

#include "global.h"
#include "m4a.h"

struct SoundInfo gSoundInfo;
struct MusicPlayerInfo gMPlayInfo_BGM;
struct MusicPlayerInfo gMPlayInfo_SE1;
struct MusicPlayerInfo gMPlayInfo_SE2;
struct MusicPlayerInfo gMPlayInfo_SE3;
struct PokemonCrySong gPokemonCrySongs[MAX_POKEMON_CRIES];

/* Returned by SetPokemonCryTone; callers only hand it back to
 * IsPokemonCryPlaying or the m4aMPlay* controls. */
static struct MusicPlayerInfo sCryPlayer;

void m4aSoundInit(void) { }
void m4aSoundMain(void) { }
void m4aSoundVSync(void) { }
void m4aSoundVSyncOn(void) { }
void m4aSoundVSyncOff(void) { }

void m4aSongNumStart(u16 n) { (void)n; }
void m4aSongNumStartOrChange(u16 n) { (void)n; }
void m4aSongNumStop(u16 n) { (void)n; }

void m4aMPlayAllStop(void) { }
void m4aMPlayImmInit(struct MusicPlayerInfo *mplayInfo) { (void)mplayInfo; }
void m4aMPlayStop(struct MusicPlayerInfo *mplayInfo) { (void)mplayInfo; }
void m4aMPlayContinue(struct MusicPlayerInfo *mplayInfo) { (void)mplayInfo; }
void m4aMPlayFadeOut(struct MusicPlayerInfo *mplayInfo, u16 speed) { (void)mplayInfo; (void)speed; }
void m4aMPlayFadeOutTemporarily(struct MusicPlayerInfo *mplayInfo, u16 speed) { (void)mplayInfo; (void)speed; }
void m4aMPlayFadeIn(struct MusicPlayerInfo *mplayInfo, u16 speed) { (void)mplayInfo; (void)speed; }
void m4aMPlayTempoControl(struct MusicPlayerInfo *mplayInfo, u16 tempo) { (void)mplayInfo; (void)tempo; }
void m4aMPlayVolumeControl(struct MusicPlayerInfo *mplayInfo, u16 trackBits, u16 volume) { (void)mplayInfo; (void)trackBits; (void)volume; }
void m4aMPlayPitchControl(struct MusicPlayerInfo *mplayInfo, u16 trackBits, s16 pitch) { (void)mplayInfo; (void)trackBits; (void)pitch; }
void m4aMPlayPanpotControl(struct MusicPlayerInfo *mplayInfo, u16 trackBits, s8 pan) { (void)mplayInfo; (void)trackBits; (void)pan; }

struct MusicPlayerInfo *SetPokemonCryTone(struct ToneData *tone)
{
    (void)tone;
    return &sCryPlayer;
}

bool32 IsPokemonCryPlaying(struct MusicPlayerInfo *mplayInfo)
{
    (void)mplayInfo;
    return FALSE;
}

void SetPokemonCryVolume(u8 val) { (void)val; }
void SetPokemonCryPanpot(s8 val) { (void)val; }
void SetPokemonCryPitch(s16 val) { (void)val; }
void SetPokemonCryLength(u16 val) { (void)val; }
void SetPokemonCryRelease(u8 val) { (void)val; }
void SetPokemonCryProgress(u32 val) { (void)val; }
void SetPokemonCryChorus(s8 val) { (void)val; }
void SetPokemonCryStereo(u32 val) { (void)val; }
void SetPokemonCryPriority(u8 val) { (void)val; }
