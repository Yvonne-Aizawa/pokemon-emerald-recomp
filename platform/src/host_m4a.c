/*
 * platform/src/host_m4a.c
 *
 * Silent stand-in for the GBA's M4A sound engine (refrence/src/m4a.c,
 * m4a_tables.c and m4a_1.s, all excluded from the host build).
 *
 * No sound is produced, but the players' status words follow the real
 * engine closely enough for game logic that watches them:
 *   - Music started on the BGM player counts as playing until it is stopped,
 *     paused or faded out. (Without audio we can't tell when a song would
 *     end; the title screen, for one, returns to the intro when its music
 *     ends, so "already ended" would skip it at once.)
 *   - Sound effects, fanfare and cries finish immediately, so nothing ever
 *     waits on them.
 * Phase 13 replaces this with real playback.
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

static void HostMPlayStart(struct MusicPlayerInfo *mplayInfo, struct SongHeader *songHeader)
{
    mplayInfo->songHeader = songHeader;
    mplayInfo->fadeOI = 0;
    if (mplayInfo == &gMPlayInfo_BGM && songHeader->trackCount != 0)
        mplayInfo->status = (1u << songHeader->trackCount) - 1;  /* tracks running */
    else
        mplayInfo->status = 0;  /* finished */
}

static struct MusicPlayerInfo *SongPlayer(u16 n)
{
    return gMPlayTable[gSongTable[n].ms].info;
}

void m4aSongNumStart(u16 n)
{
    HostMPlayStart(SongPlayer(n), gSongTable[n].header);
}

void m4aSongNumStartOrChange(u16 n)
{
    struct MusicPlayerInfo *mplayInfo = SongPlayer(n);

    if (mplayInfo->songHeader != gSongTable[n].header
     || (mplayInfo->status & MUSICPLAYER_STATUS_TRACK) == 0
     || (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE))
        HostMPlayStart(mplayInfo, gSongTable[n].header);
}

void m4aSongNumStop(u16 n)
{
    struct MusicPlayerInfo *mplayInfo = SongPlayer(n);

    if (mplayInfo->songHeader == gSongTable[n].header)
        m4aMPlayStop(mplayInfo);
}

void m4aMPlayStop(struct MusicPlayerInfo *mplayInfo)
{
    mplayInfo->status |= MUSICPLAYER_STATUS_PAUSE;
}

void m4aMPlayContinue(struct MusicPlayerInfo *mplayInfo)
{
    mplayInfo->status &= ~MUSICPLAYER_STATUS_PAUSE;
}

void m4aMPlayAllStop(void)
{
    m4aMPlayStop(&gMPlayInfo_BGM);
    m4aMPlayStop(&gMPlayInfo_SE1);
    m4aMPlayStop(&gMPlayInfo_SE2);
    m4aMPlayStop(&gMPlayInfo_SE3);
    m4aMPlayStop(&sCryPlayer);
}

/* Fades complete at once: a finished fade-out stops the song (clearing its
 * tracks), a temporary one pauses it so a fade-in can resume it. */
void m4aMPlayFadeOut(struct MusicPlayerInfo *mplayInfo, u16 speed)
{
    (void)speed;
    mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
}

void m4aMPlayFadeOutTemporarily(struct MusicPlayerInfo *mplayInfo, u16 speed)
{
    (void)speed;
    mplayInfo->status |= MUSICPLAYER_STATUS_PAUSE;
}

void m4aMPlayFadeIn(struct MusicPlayerInfo *mplayInfo, u16 speed)
{
    (void)speed;
    m4aMPlayContinue(mplayInfo);
}

void m4aMPlayImmInit(struct MusicPlayerInfo *mplayInfo) { (void)mplayInfo; }
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
