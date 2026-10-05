/*
 * platform/src/m4a_engine.c
 *
 * C port of reference/src/m4a_1.s: the parts of the M4A ("Sappy") sound
 * engine that are ARM/Thumb assembly on the GBA.
 *   - SoundMain / SoundMainRAM: the per-frame driver and software mixer
 *     (envelopes, fixed-pitch, resampled, DPCM-compressed and reversed
 *     samples, reverb).
 *   - MPlayMain, TrackStop, ply_note, ply_endtie and the ply_* commands:
 *     the sequencer.
 *   - m4aSoundVSync and small helpers (umul3232H32, RealClearChain, ...).
 * The rest of the engine is reference/src/m4a.c and m4a_tables.c, built
 * as-is apart from platform/patches/m4a.c.patch.
 *
 * The arithmetic follows the assembly exactly (8-bit wrapping mix buffer,
 * 23-bit fixed-point resampling, envelope maths, channel allocation order),
 * so the output matches hardware. Only register tricks are written out
 * plainly: the mixer's "four samples rotating through one word" becomes a
 * per-byte add, and the BIOS-address checks (which never reject game data)
 * are dropped.
 *
 * At the end of each SoundMain the finished frame goes to the host's sound
 * hardware (host_audio.c). SoundMain runs from the V-blank interrupt, which
 * on the host may be a signal handler (irq_timer.h), so nothing here may
 * call into libc beyond plain memory operations.
 */

#include <string.h>

#include "global.h"
#include "gba/m4a_internal.h"
#include "platform/host_audio.h"

extern const u8 gClockTable[];
extern const s8 gDeltaEncodingTable[];
extern void *const gMPlayJumpTableTemplate[];

/* Constants only the assembly used (constants/m4a_constants.inc). */
#define TONEDATA_TYPE_REV        0x10
#define TONEDATA_TYPE_CMP        0x20
#define SOUND_CHANNEL_SF_SPECIAL 0x20
#define WAVE_DATA_FLAG_LOOP      0xC000  /* WaveData.status */
#define MPLAY_JUMP_TABLE_SIZE    36
#define VCOUNT_VBLANK            160
#define TOTAL_SCANLINES          228

/* Resampling position: 23 fractional bits; the bits a step can carry into
 * (23-29) are cleared after each advance, as the assembly does. */
#define FW_SHIFT    23
#define FW_INT_MASK 0x3F800000

/* --------------------------------------------------------------------- */
/* Helpers                                                               */
/* --------------------------------------------------------------------- */

u32 umul3232H32(u32 multiplier, u32 multiplicand)
{
    return (u32)(((u64)multiplier * multiplicand) >> 32);
}

/* gMPlayJumpTable[35], reached through Clear64byte. */
void SoundMainBTM(void *x)
{
    memset(x, 0, 64);
}

/* gMPlayJumpTable[34], reached through ClearChain: unlink a channel from its
 * track's channel list. */
void RealClearChain(void *x)
{
    struct SoundChannel *chan = x;
    struct MusicPlayerTrack *track = chan->track;
    struct SoundChannel *next, *prev;

    if (track == NULL)
        return;
    next = chan->nextChannelPointer;
    prev = chan->prevChannelPointer;
    if (prev != NULL)
        prev->nextChannelPointer = next;
    else
        track->chan = next;
    if (next != NULL)
        next->prevChannelPointer = prev;
    chan->track = NULL;
}

void MPlayJumpTableCopy(MPlayFunc *mplayJumpTable)
{
    int i;

    for (i = 0; i < MPLAY_JUMP_TABLE_SIZE; i++)
        mplayJumpTable[i] = (MPlayFunc)gMPlayJumpTableTemplate[i];
}

static u8 ReadCmdByte(struct MusicPlayerTrack *track)
{
    return *track->cmdPtr++;
}

static u8 *ReadCmdPointer(const u8 *p)
{
    return (u8 *)(uintptr_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24));
}

/* REG_VCOUNT as the mixer's line limit sees it: lines after the V-blank
 * count as 228+. Only used when MAX_LINES is set (it isn't in Emerald). */
static u32 VCountForLineLimit(void)
{
    u32 line = *(vu8 *)REG_ADDR_VCOUNT;

    if (line < VCOUNT_VBLANK)
        line += TOTAL_SCANLINES;
    return line;
}

/* --------------------------------------------------------------------- */
/* Track commands                                                        */
/* --------------------------------------------------------------------- */

void ply_fine(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;

    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        if (chan->statusFlags & SOUND_CHANNEL_SF_ON)
            chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
        RealClearChain(chan);
    }
    track->flags = 0;
}

void ply_goto(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->cmdPtr = ReadCmdPointer(track->cmdPtr);
}

void ply_patt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    if (track->patternLevel < 3)
    {
        track->patternStack[track->patternLevel] = track->cmdPtr + 4;
        track->patternLevel++;
        ply_goto(mplayInfo, track);
    }
    else
    {
        ply_fine(mplayInfo, track);
    }
}

void ply_pend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    if (track->patternLevel != 0)
    {
        track->patternLevel--;
        track->cmdPtr = track->patternStack[track->patternLevel];
    }
}

void ply_rept(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 *cmd = track->cmdPtr;

    if (*cmd == 0)  /* repeat forever */
    {
        track->cmdPtr = cmd + 1;
        ply_goto(mplayInfo, track);
        return;
    }
    track->repN++;
    track->cmdPtr = cmd + 1;
    if (track->repN < *cmd)
    {
        ply_goto(mplayInfo, track);
    }
    else
    {
        track->repN = 0;
        track->cmdPtr = cmd + 5;
    }
}

void ply_prio(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->priority = ReadCmdByte(track);
}

void ply_tempo(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 tempo = ReadCmdByte(track) * 2;

    mplayInfo->tempoD = tempo;
    mplayInfo->tempoI = (tempo * mplayInfo->tempoU) >> 8;
}

void ply_keysh(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->keyShift = ReadCmdByte(track);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_voice(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 voice = ReadCmdByte(track);

    memcpy(&track->tone, &mplayInfo->tone[voice], sizeof(struct ToneData));
}

void ply_vol(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->vol = ReadCmdByte(track);
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_pan(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->pan = ReadCmdByte(track) - C_V;
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_bend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->bend = ReadCmdByte(track) - C_V;
    track->flags |= MPT_FLG_PITCHG;
}

void ply_bendr(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->bendRange = ReadCmdByte(track);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_lfodl(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->lfoDelay = ReadCmdByte(track);
}

void ply_modt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 modT = ReadCmdByte(track);

    if (track->modT != modT)
    {
        track->modT = modT;
        track->flags |= MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    }
}

void ply_tune(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->tune = ReadCmdByte(track) - C_V;
    track->flags |= MPT_FLG_PITCHG;
}

/* Write a byte to a sound register (offset from REG_SOUND1CNT_L). */
void ply_port(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 offset = ReadCmdByte(track);
    u8 value = ReadCmdByte(track);

    *(vu8 *)(REG_ADDR_SOUND1CNT_L + offset) = value;
}

void ply_lfos(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->lfoSpeed = ReadCmdByte(track);
    if (track->lfoSpeed == 0)
        ClearModM(track);
}

void ply_mod(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->mod = ReadCmdByte(track);
    if (track->mod == 0)
        ClearModM(track);
}

/* End a tied note: release the first sounding channel playing `key`. */
void ply_endtie(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;
    u8 key = *track->cmdPtr;

    if (key < 0x80)
    {
        track->key = key;
        track->cmdPtr++;
    }
    else
    {
        key = track->key;
    }

    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        if ((chan->statusFlags & (SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_ENV))
         && !(chan->statusFlags & SOUND_CHANNEL_SF_STOP)
         && chan->midiKey == key)
        {
            chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
            return;
        }
    }
}

/* --------------------------------------------------------------------- */
/* Sequencer                                                             */
/* --------------------------------------------------------------------- */

void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;

    if (!(track->flags & MPT_FLG_EXIST))
        return;

    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        if (chan->statusFlags != 0)
        {
            u8 cgbType = chan->type & TONEDATA_TYPE_CGB;

            if (cgbType != 0)
                SOUND_INFO_PTR->CgbOscOff(cgbType);
            chan->statusFlags = 0;
        }
        chan->track = NULL;
    }
    track->chan = NULL;
}

/* Channel volumes from the note's velocity/pan and the track's volume. */
static void ChnVolSet(struct SoundChannel *chan, struct MusicPlayerTrack *track)
{
    s32 pan = (s8)chan->rhythmPan;
    u32 volume;

    volume = (u32)((s32)(track->volMR * ((0x80 + pan) * chan->velocity)) >> 14);
    chan->rightVolume = volume > 0xFF ? 0xFF : volume;
    volume = (u32)((s32)(track->volML * ((0x7F - pan) * chan->velocity)) >> 14);
    chan->leftVolume = volume > 0xFF ? 0xFF : volume;
}

/* One tick of a track: gate times, commands up to the next wait, LFO. */
static void TrackTick(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo,
                      struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;

    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        if (chan->statusFlags & SOUND_CHANNEL_SF_ON)
        {
            if (chan->gateTime != 0 && --chan->gateTime == 0)
                chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
        }
        else
        {
            ClearChain(chan);
        }
    }

    if (track->flags & MPT_FLG_START)
    {
        Clear64byte(track);
        track->flags = MPT_FLG_EXIST;
        track->bendRange = 2;
        track->volX = 64;
        track->lfoSpeed = 22;
        track->tone.type = 1;
    }

    while (track->wait == 0)
    {
        u32 cmd = *track->cmdPtr;

        if (cmd < 0x80)
        {
            cmd = track->runningStatus;
        }
        else
        {
            track->cmdPtr++;
            if (cmd >= 0xBD)
                track->runningStatus = cmd;
        }

        if (cmd >= 0xCF)
        {
            soundInfo->plynote(cmd - 0xCF, mplayInfo, track);
        }
        else if (cmd > 0xB0)
        {
            mplayInfo->cmd = cmd - 0xB1;
            soundInfo->MPlayJumpTable[cmd - 0xB1](mplayInfo, track);
            if (track->flags == 0)
                return;
        }
        else
        {
            track->wait = gClockTable[cmd - 0x80];
        }
    }

    track->wait--;

    if (track->lfoSpeed != 0 && track->mod != 0)
    {
        if (track->lfoDelayC != 0)
        {
            track->lfoDelayC--;
        }
        else
        {
            u8 phase = track->lfoSpeedC += track->lfoSpeed;
            s32 wave, modM;

            /* Triangle wave, -64..64. */
            if ((s8)(phase - 0x40) < 0)
                wave = (s8)phase;
            else
                wave = 0x80 - phase;
            modM = (track->mod * wave) >> 6;
            if ((u8)(track->modM ^ modM) != 0)
            {
                track->modM = modM;
                track->flags |= track->modT == 0 ? MPT_FLG_PITCHG : MPT_FLG_VOLCHG;
            }
        }
    }
}

/* Apply volume/pitch changes of a track to its sounding channels. */
static void TrackApplyVolPit(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo,
                             struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;

    TrkVolPitSet(mplayInfo, track);
    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        u8 cgbType;

        if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
        {
            ClearChain(chan);
            continue;
        }

        cgbType = chan->type & TONEDATA_TYPE_CGB;
        if (track->flags & MPT_FLG_VOLCHG)
        {
            ChnVolSet(chan, track);
            if (cgbType != 0)
                ((struct CgbChannel *)chan)->modify |= CGB_CHANNEL_MO_VOL;
        }
        if (track->flags & MPT_FLG_PITCHG)
        {
            s32 key = chan->key + (s8)track->keyM;

            if (key < 0)
                key = 0;
            if (cgbType != 0)
            {
                struct CgbChannel *cgb = (struct CgbChannel *)chan;

                cgb->frequency = soundInfo->MidiKeyToCgbFreq(cgbType, key, track->pitM);
                cgb->modify |= CGB_CHANNEL_MO_PIT;
            }
            else
            {
                chan->frequency = MidiKeyToFreq(chan->wav, key, track->pitM);
            }
        }
    }
    track->flags &= 0xF0;
}

void MPlayMain(struct MusicPlayerInfo *mplayInfo)
{
    struct SoundInfo *soundInfo;
    struct MusicPlayerTrack *track;
    u32 tempoC;
    s32 i;

    if (mplayInfo->ident != ID_NUMBER)
        return;
    mplayInfo->ident++;

    /* The players form a list; each one runs the next first. */
    if (mplayInfo->MPlayMainNext != NULL)
        mplayInfo->MPlayMainNext(mplayInfo->musicPlayerNext);

    if (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE)
        goto done;
    soundInfo = SOUND_INFO_PTR;
    FadeOutBody(mplayInfo);
    if (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE)
        goto done;

    /* 150 tempo units are one tick. */
    tempoC = mplayInfo->tempoC + mplayInfo->tempoI;
    for (;;)
    {
        u32 trackBit = 1, activeTracks = 0;

        mplayInfo->tempoC = tempoC;
        if (tempoC < 150)
            break;

        i = mplayInfo->trackCount;
        track = mplayInfo->tracks;
        do
        {
            if (track->flags & MPT_FLG_EXIST)
            {
                activeTracks |= trackBit;
                TrackTick(soundInfo, mplayInfo, track);
            }
            track++;
            trackBit <<= 1;
        } while (--i > 0);

        mplayInfo->clock++;
        if (activeTracks == 0)
        {
            mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
            goto done;
        }
        mplayInfo->status = activeTracks;
        tempoC = mplayInfo->tempoC - 150;
    }

    i = mplayInfo->trackCount;
    track = mplayInfo->tracks;
    do
    {
        if ((track->flags & MPT_FLG_EXIST) && (track->flags & (MPT_FLG_VOLCHG | MPT_FLG_PITCHG)))
            TrackApplyVolPit(soundInfo, mplayInfo, track);
        track++;
    } while (--i > 0);

done:
    mplayInfo->ident = ID_NUMBER;
}

/* --------------------------------------------------------------------- */
/* Notes                                                                 */
/* --------------------------------------------------------------------- */

/* A free Direct Sound channel for a note of `priority`, or the one to steal:
 * a releasing channel if any (lowest priority, then latest track), else the
 * lowest-priority playing one if it isn't above the note's. */
static struct SoundChannel *AllocDirectSoundChannel(struct SoundInfo *soundInfo,
                                                    struct MusicPlayerTrack *track, u32 priority)
{
    struct SoundChannel *chan = soundInfo->chans, *best = NULL;
    struct MusicPlayerTrack *bestTrack = track;
    u32 bestPriority = priority;
    bool32 releasing = FALSE;
    s32 i = soundInfo->maxChans;

    do
    {
        u8 flags = chan->statusFlags;

        if (!(flags & SOUND_CHANNEL_SF_ON))
            return chan;

        if (flags & SOUND_CHANNEL_SF_STOP)
        {
            if (!releasing)
            {
                releasing = TRUE;
                bestPriority = chan->priority;
                bestTrack = chan->track;
                best = chan;
                continue;
            }
        }
        else if (releasing)
        {
            continue;
        }

        if (chan->priority < bestPriority)
        {
            bestPriority = chan->priority;
            bestTrack = chan->track;
            best = chan;
        }
        else if (chan->priority == bestPriority)
        {
            if ((uintptr_t)chan->track > (uintptr_t)bestTrack)
            {
                bestTrack = chan->track;
                best = chan;
            }
            else if (chan->track == bestTrack)
            {
                best = chan;
            }
        }
    } while (chan++, --i > 0);

    return best;
}

void ply_note(u32 note_cmd, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    struct ToneData *tone;
    struct SoundChannel *chan;
    u32 key, priority, cgbType, frequency;
    s32 rhythmPan = 0, keyM;
    u8 *cmd;

    /* Optional key, velocity and extra gate time. */
    track->gateTime = gClockTable[note_cmd];
    cmd = track->cmdPtr;
    if (*cmd < 0x80)
    {
        track->key = *cmd++;
        if (*cmd < 0x80)
        {
            track->velocity = *cmd++;
            if (*cmd < 0x80)
                track->gateTime += *cmd++;
        }
        track->cmdPtr = cmd;
    }

    /* Key-split and drum-kit voices pick a sub-voice by key. */
    tone = &track->tone;
    key = track->key;
    if (tone->type & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL))
    {
        struct ToneData *subTone;
        u32 index = track->key;

        if (tone->type & TONEDATA_TYPE_SPL)
        {
            u32 keySplitTable;

            memcpy(&keySplitTable, &tone->attack, sizeof(keySplitTable));
            index = ((const u8 *)(uintptr_t)keySplitTable)[track->key];
        }
        subTone = &((struct ToneData *)tone->wav)[index];
        if (subTone->type & (TONEDATA_TYPE_SPL | TONEDATA_TYPE_RHY))
            return;
        if (tone->type & TONEDATA_TYPE_RHY)
        {
            if (subTone->pan_sweep & 0x80)
                rhythmPan = ((s32)subTone->pan_sweep - TONEDATA_P_S_PAN) << 1;
            key = subTone->key;
        }
        tone = subTone;
    }

    priority = mplayInfo->priority + track->priority;
    if (priority > 0xFF)
        priority = 0xFF;

    cgbType = tone->type & TONEDATA_TYPE_CGB;
    if (cgbType != 0)
    {
        if (soundInfo->cgbChans == NULL)
            return;
        chan = (struct SoundChannel *)&soundInfo->cgbChans[cgbType - 1];
        if ((chan->statusFlags & SOUND_CHANNEL_SF_ON) && !(chan->statusFlags & SOUND_CHANNEL_SF_STOP))
        {
            if (chan->priority > priority)
                return;
            if (chan->priority == priority && (uintptr_t)chan->track < (uintptr_t)track)
                return;
        }
    }
    else
    {
        chan = AllocDirectSoundChannel(soundInfo, track, priority);
        if (chan == NULL)
            return;
    }

    /* Link the channel to the track. */
    ClearChain(chan);
    chan->prevChannelPointer = NULL;
    chan->nextChannelPointer = track->chan;
    if (track->chan != NULL)
        track->chan->prevChannelPointer = chan;
    track->chan = chan;
    chan->track = track;

    track->lfoDelayC = track->lfoDelay;
    if (track->lfoDelay != 0)
        ClearModM(track);
    TrkVolPitSet(mplayInfo, track);

    chan->gateTime = track->gateTime;
    chan->midiKey = track->key;
    chan->velocity = track->velocity;
    chan->priority = priority;
    chan->key = key;
    chan->rhythmPan = rhythmPan;
    chan->type = tone->type;
    chan->wav = tone->wav;
    chan->attack = tone->attack;
    chan->decay = tone->decay;
    chan->sustain = tone->sustain;
    chan->release = tone->release;
    chan->pseudoEchoVolume = track->pseudoEchoVolume;
    chan->pseudoEchoLength = track->pseudoEchoLength;
    ChnVolSet(chan, track);

    keyM = chan->key + (s8)track->keyM;
    if (keyM < 0)
        keyM = 0;
    if (cgbType != 0)
    {
        struct CgbChannel *cgb = (struct CgbChannel *)chan;
        u8 sweep = tone->pan_sweep;

        cgb->length = tone->length;
        if ((sweep & 0x80) || !(sweep & 0x70))
            sweep = 8;
        cgb->sweep = sweep;
        frequency = soundInfo->MidiKeyToCgbFreq(cgbType, keyM, track->pitM);
    }
    else
    {
        chan->count = track->unk_3C;
        frequency = MidiKeyToFreq(chan->wav, keyM, track->pitM);
    }
    chan->frequency = frequency;
    chan->statusFlags = SOUND_CHANNEL_SF_START;
    track->flags &= 0xF0;
}

/* --------------------------------------------------------------------- */
/* Mixer                                                                 */
/* --------------------------------------------------------------------- */

/* Mixing state of one channel for one frame. */
struct Mix
{
    s8 *right;          /* FIFO A half of this frame's buffer segment */
    s8 *left;           /* FIFO B half */
    s32 samples;        /* samples in this frame */
    s32 volRight;
    s32 volLeft;
    u32 count;          /* source samples left */
    u32 fw;             /* resampling position, 23-bit fraction */
    s8 *loopStart;
    u32 loopLength;     /* 0: no loop */
};

/* Add one sample to the buffer, wrapping like the 8-bit hardware buffer. */
static inline void MixOne(struct Mix *mix, s32 j, s32 sample)
{
    mix->right[j] = (s8)(mix->right[j] + ((mix->volRight * sample) >> 8));
    mix->left[j] = (s8)(mix->left[j] + ((mix->volLeft * sample) >> 8));
}

static inline s32 Interpolate(s32 sample, s32 delta, u32 fw)
{
    return sample + ((s32)(fw * (u32)delta) >> FW_SHIFT);
}

/* Fixed-pitch samples (TONEDATA_TYPE_FIX): one source sample per output
 * sample. Returns FALSE when the sample ended (channel stopped). */
static bool32 MixFixed(struct Mix *mix, s8 **pos)
{
    s8 *p = *pos;
    s32 j;

    for (j = 0; j < mix->samples; j++)
    {
        MixOne(mix, j, *p++);
        if (--mix->count == 0)
        {
            if (mix->loopLength == 0)
                return FALSE;
            mix->count = mix->loopLength;
            p = mix->loopStart;
        }
    }
    *pos = p;
    return TRUE;
}

/* Resampled samples, linear interpolation. `*pos` is the current sample. */
static bool32 MixResampled(struct Mix *mix, s8 **pos, u32 step)
{
    s8 *p = *pos;
    s32 sample = p[0], delta = p[1] - sample;
    s32 j;

    p++;  /* the sample after the current one */
    for (j = 0; j < mix->samples; j++)
    {
        u32 advance;

        MixOne(mix, j, Interpolate(sample, delta, mix->fw));
        mix->fw += step;
        advance = mix->fw >> FW_SHIFT;
        if (advance == 0)
            continue;
        mix->fw &= ~FW_INT_MASK;
        mix->count -= advance;
        if ((s32)mix->count <= 0)
        {
            s32 back;

            if (mix->loopLength == 0)
                return FALSE;
            back = -(s32)mix->count;
            for (;;)
            {
                mix->count += mix->loopLength;
                if ((s32)mix->count > 0)
                    break;
                back -= mix->loopLength;
            }
            p = mix->loopStart + back;
            sample = *p;
        }
        else if (--advance == 0)
        {
            sample += delta;
        }
        else
        {
            p += advance;
            sample = *p;
        }
        delta = p[1] - sample;
        p++;
    }
    *pos = p - 1;
    return TRUE;
}

/* DPCM-compressed samples (WaveData.type != 0): blocks of 64 samples in
 * 0x21 bytes -- a start sample, then 4-bit deltas (the first byte's high
 * nibble is unused). One decoded block is cached per channel. */
static s8 sDecodingBuffer[64];

static s32 DecodeSample(struct SoundChannel *chan, u32 index)
{
    u32 block = index >> 6, cached;

#ifdef HOST_BUILD
    /* A reversed sample's last interpolation partner is index -1: block
     * 0x3FFFFFF, far outside the sample (the GBA reads junk, harmlessly). */
    if ((s32)index < 0)
        return 0;
#endif
    memcpy(&cached, &chan->xpi, sizeof(cached));
    if (block != cached)
    {
        const u8 *src = (const u8 *)chan->wav + 0x10 + block * 0x21;
        s8 *dst = sDecodingBuffer;
        u8 sample = *src++, byte = *src++;
        int left = 0x40;

        memcpy(&chan->xpi, &block, sizeof(block));
        *dst++ = sample;
        goto low_nibble;
        do
        {
            byte = *src++;
            sample += gDeltaEncodingTable[byte >> 4];
            *dst++ = sample;
        low_nibble:
            sample += gDeltaEncodingTable[byte & 0xF];
            *dst++ = sample;
            left -= 2;
        } while (left > 0);
    }
    return sDecodingBuffer[index & 0x3F];
}

/* Compressed and/or reversed samples (TONEDATA_TYPE_CMP / _REV; the
 * expansion's SoundMainRAM_Unk1). For compressed samples the channel's
 * currentPointer holds a sample index instead of a pointer. */
static bool32 MixSpecial(struct Mix *mix, struct SoundChannel *chan, uintptr_t *pos, u32 divFreq)
{
    struct WaveData *wav = chan->wav;
    uintptr_t p = *pos;
    u32 step, advance, invalid = 0xFF000000;
    s32 sample, delta, j;

    if (!(chan->statusFlags & SOUND_CHANNEL_SF_SPECIAL))
    {
        chan->statusFlags |= SOUND_CHANNEL_SF_SPECIAL;
        if (chan->type & TONEDATA_TYPE_REV)
            p = wav->size + 2 * (uintptr_t)wav + 0x20 - p;  /* mirror around the data */
        if (wav->type != 0)
            p = p - (uintptr_t)wav - 0x10;                  /* pointer -> index */
    }

    step = (chan->type & TONEDATA_TYPE_FIX) ? (1 << FW_SHIFT) : divFreq * chan->frequency;

    if (wav->type != 0)
    {
        memcpy(&chan->xpi, &invalid, sizeof(invalid));
        if (!(chan->type & TONEDATA_TYPE_REV))
        {
            sample = DecodeSample(chan, p);
            p++;
            delta = DecodeSample(chan, p) - sample;
            for (j = 0; j < mix->samples; j++)
            {
                MixOne(mix, j, Interpolate(sample, delta, mix->fw));
                mix->fw += step;
                advance = mix->fw >> FW_SHIFT;
                if (advance == 0)
                    continue;
                mix->fw &= ~FW_INT_MASK;
                mix->count -= advance;
                if ((s32)mix->count <= 0)
                {
                    s32 back;

                    if (mix->loopLength == 0)
                        return FALSE;
                    p = wav->loopStart;
                    back = -(s32)mix->count;
                    for (;;)
                    {
                        mix->count += mix->loopLength;
                        if ((s32)mix->count > 0)
                            break;
                        back -= mix->loopLength;
                    }
                    p += back;
                    sample = DecodeSample(chan, p);
                }
                else if (--advance == 0)
                {
                    sample += delta;
                }
                else
                {
                    p += advance;
                    sample = DecodeSample(chan, p);
                }
                p++;
                delta = DecodeSample(chan, p) - sample;
            }
            p -= 1;
        }
        else
        {
            p -= 1;
            sample = DecodeSample(chan, p);
            p -= 1;
            delta = DecodeSample(chan, p) - sample;
            for (j = 0; j < mix->samples; j++)
            {
                MixOne(mix, j, Interpolate(sample, delta, mix->fw));
                mix->fw += step;
                advance = mix->fw >> FW_SHIFT;
                if (advance == 0)
                    continue;
                mix->fw &= ~FW_INT_MASK;
                mix->count -= advance;
                if ((s32)mix->count <= 0)
                    return FALSE;
                if (--advance == 0)
                {
                    sample += delta;
                }
                else
                {
                    p -= advance;
                    sample = DecodeSample(chan, p);
                }
                p -= 1;
                delta = DecodeSample(chan, p) - sample;
            }
            p += 2;
        }
    }
    else if (chan->type & TONEDATA_TYPE_REV)
    {
        const s8 *data = (const s8 *)p;

        sample = *--data;
        delta = data[-1] - sample;
        for (j = 0; j < mix->samples; j++)
        {
            MixOne(mix, j, Interpolate(sample, delta, mix->fw));
            mix->fw += step;
            advance = mix->fw >> FW_SHIFT;
            if (advance == 0)
                continue;
            mix->fw &= ~FW_INT_MASK;
            mix->count -= advance;
            if ((s32)mix->count <= 0)
                return FALSE;
            data -= advance;
            sample = *data;
            delta = data[-1] - sample;
        }
        p = (uintptr_t)(data + 1);
    }
    /* else: uncompressed and not reversed -- the assembly mixes nothing. */

    *pos = p;
    return TRUE;
}

/* Envelope step for one channel. Returns the new envelope volume, or -1 if
 * the channel stopped. */
static s32 ChannelEnvelope(struct SoundChannel *chan)
{
    u8 flags = chan->statusFlags;
    u32 env;

    if (flags & SOUND_CHANNEL_SF_START)
    {
        struct WaveData *wav = chan->wav;

        if (flags & SOUND_CHANNEL_SF_STOP)
        {
            chan->statusFlags = 0;
            return -1;
        }
        flags = SOUND_CHANNEL_SF_ENV_ATTACK;
        chan->currentPointer = wav->data + chan->count;  /* count: start offset */
        chan->count = wav->size - chan->count;
        env = 0;
        chan->envelopeVolume = 0;
        chan->fw = 0;
        if (wav->status & WAVE_DATA_FLAG_LOOP)
            flags |= SOUND_CHANNEL_SF_LOOP;
        chan->statusFlags = flags;
        goto attack;
    }

    env = chan->envelopeVolume;
    if (flags & SOUND_CHANNEL_SF_IEC)
    {
        u8 length = chan->pseudoEchoLength--;

        if (length > 1)
            return env;
        chan->statusFlags = 0;
        return -1;
    }
    if (flags & SOUND_CHANNEL_SF_STOP)
    {
        env = (env * chan->release) >> 8;
        if (env > chan->pseudoEchoVolume)
            return env;
    pseudo_echo:
        env = chan->pseudoEchoVolume;
        if (env == 0)
        {
            chan->statusFlags = 0;
            return -1;
        }
        chan->statusFlags = flags | SOUND_CHANNEL_SF_IEC;
        return env;
    }

    switch (flags & SOUND_CHANNEL_SF_ENV)
    {
    case SOUND_CHANNEL_SF_ENV_DECAY:
        env = (env * chan->decay) >> 8;
        if (env > chan->sustain)
            return env;
        env = chan->sustain;
        if (env == 0)
            goto pseudo_echo;
        chan->statusFlags = flags - 1;
        return env;
    case SOUND_CHANNEL_SF_ENV_ATTACK:
    attack:
        env += chan->attack;
        if (env >= 0xFF)
        {
            env = 0xFF;
            chan->statusFlags = flags - 1;
        }
        return env;
    default:
        return env;
    }
}

static void MixChannel(struct SoundInfo *soundInfo, struct SoundChannel *chan, s8 *segment, s32 samples)
{
    struct WaveData *wav;
    struct Mix mix;
    s32 env, volume;
    bool32 playing;

    if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
        return;
    env = ChannelEnvelope(chan);
    if (env < 0)
        return;

    chan->envelopeVolume = env;
    volume = ((soundInfo->masterVolume + 1) * env) >> 4;
    chan->envelopeVolumeRight = (chan->rightVolume * volume) >> 8;
    chan->envelopeVolumeLeft = (chan->leftVolume * volume) >> 8;

    wav = chan->wav;
    mix.right = segment;
    mix.left = segment + PCM_DMA_BUF_SIZE;
    mix.samples = samples;
    mix.volRight = chan->envelopeVolumeRight;
    mix.volLeft = chan->envelopeVolumeLeft;
    mix.count = chan->count;
    mix.fw = chan->fw;
    mix.loopStart = NULL;
    mix.loopLength = 0;
    if (chan->statusFlags & SOUND_CHANNEL_SF_LOOP)
    {
        mix.loopStart = wav->data + wav->loopStart;
        mix.loopLength = wav->size - wav->loopStart;
    }

    if (chan->type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV))
    {
        uintptr_t pos = (uintptr_t)chan->currentPointer;

        playing = MixSpecial(&mix, chan, &pos, soundInfo->divFreq);
        if (!playing)
        {
            chan->statusFlags = 0;
            mix.count = 0;
        }
        chan->fw = mix.fw;
        chan->count = mix.count;
        chan->currentPointer = (s8 *)pos;
        return;
    }

    if (chan->type & TONEDATA_TYPE_FIX)
    {
        playing = MixFixed(&mix, &chan->currentPointer);
    }
    else
    {
        playing = MixResampled(&mix, &chan->currentPointer, soundInfo->divFreq * chan->frequency);
        if (playing)
            chan->fw = mix.fw;
    }
    if (!playing)
    {
        chan->statusFlags = 0;
        return;
    }
    chan->count = mix.count;
}

/* Prepare this frame's buffer segment: reverb from the previous contents,
 * or silence. */
static void ClearSegment(struct SoundInfo *soundInfo, s8 *segment, s32 samples, u8 dmaCounter)
{
    s32 j;

    if (soundInfo->reverb != 0)
    {
        s8 *other = dmaCounter == 2 ? soundInfo->pcmBuffer : segment + samples;

        for (j = 0; j < samples; j++)
        {
            s32 sum = segment[j + PCM_DMA_BUF_SIZE] + segment[j]
                    + other[j + PCM_DMA_BUF_SIZE] + other[j];

            sum = (sum * soundInfo->reverb) >> 9;
            if (sum & 0x80)
                sum++;
            segment[j + PCM_DMA_BUF_SIZE] = sum;
            segment[j] = sum;
        }
    }
    else
    {
        memset(segment, 0, samples & ~3);
        memset(segment + PCM_DMA_BUF_SIZE, 0, samples & ~3);
    }
}

void SoundMain(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    struct SoundChannel *chan;
    s32 samples, i;
    u32 lineLimit;
    u8 dmaCounter;
    s8 *segment;

    if (soundInfo == NULL || soundInfo->ident != ID_NUMBER)
        return;
    soundInfo->ident++;

    lineLimit = soundInfo->maxLines;
    if (lineLimit != 0)
        lineLimit += VCountForLineLimit();

    if (soundInfo->MPlayMainHead != NULL)
        soundInfo->MPlayMainHead(soundInfo->musicPlayerHead);
    soundInfo->CgbSound();

    /* The segment after the one the sound DMA is playing. */
    samples = soundInfo->pcmSamplesPerVBlank;
    segment = soundInfo->pcmBuffer;
    dmaCounter = soundInfo->pcmDmaCounter;
    if (dmaCounter > 1)
        segment += (soundInfo->pcmDmaPeriod - (dmaCounter - 1)) * samples;

    ClearSegment(soundInfo, segment, samples, dmaCounter);

    chan = soundInfo->chans;
    i = soundInfo->maxChans;
    do
    {
        if (lineLimit != 0 && VCountForLineLimit() >= lineLimit)
            break;
        MixChannel(soundInfo, chan, segment, samples);
        chan++;
    } while (--i > 0);

    HostAudio_SoundFrame(segment, segment + PCM_DMA_BUF_SIZE, samples, soundInfo->pcmFreq);

    soundInfo->ident = ID_NUMBER;
}

/* V-count interrupt: count frames until the sound DMA reaches the end of the
 * buffer, then restart it. The host plays each frame as SoundMain finishes
 * it, so only the counter (which picks the segment to mix) matters. */
void m4aSoundVSync(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    s32 counter;

    if (soundInfo == NULL || soundInfo->ident - ID_NUMBER > 1)
        return;

    counter = soundInfo->pcmDmaCounter - 1;
    soundInfo->pcmDmaCounter = counter;
    if (counter > 0)
        return;
    soundInfo->pcmDmaCounter = soundInfo->pcmDmaPeriod;
}
