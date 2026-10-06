/*
 * platform/src/host_audio.c
 *
 * GBA sound hardware: see platform/host_audio.h.
 *
 * The PSG (the Game Boy's four channels) is emulated from the sound
 * registers at frame granularity, which is how the game drives it: CgbSound
 * writes the registers once per frame from SoundMain. A write of NRx4 with
 * bit 7 set starts (triggers) a channel; that bit is write-only on hardware,
 * so it is cleared here once seen. Within the frame the channels run on their
 * own -- frequency timers, duty cycles, the wave table, the noise LFSR, and
 * the 512 Hz frame sequencer (length 256 Hz, sweep 128 Hz, envelope 64 Hz) --
 * which matters because the game leaves envelope ramps to the hardware.
 *
 * Mixing follows the hardware: PSG volume (NR50) and routing (NR51), PSG and
 * Direct Sound ratios and routing (SOUNDCNT_H), master enable (SOUNDCNT_X),
 * then bias and 10-bit clamp (SOUNDBIAS).
 *
 * Two output styles:
 *   - raw (default): exactly what the DAC puts out -- each Direct Sound
 *     sample held for its full period, as the FIFO does, and the 10-bit value
 *     cut to the DAC resolution (8 bits in this game), as heard through an
 *     unfiltered line-out or an emulator.
 *   - smooth (--smooth-sound): Direct Sound samples are interpolated, the DAC's
 *     resolution step is skipped, and a gentle low-pass (10 kHz) stands in
 *     for the analog stage after the GBA's DAC (amplifier, speaker or
 *     headphones), which keeps the raw output's ultrasonic hash from reaching
 *     the ears.
 * Both remove DC (the PSG is unipolar; the real output is AC-coupled) and
 * box-filter the oversampled PSG to tame aliasing.
 *
 * Everything runs in HostAudio_SoundFrame, which may be called from a signal
 * handler: no allocation, no locks, no libc calls besides memcpy/memset. The
 * output ring buffer is single-producer/single-consumer with atomic indices.
 */

#include "platform/host_audio.h"

#include <string.h>

#include "global.h"

/* --------------------------------------------------------------------- */
/* Output ring buffer                                                    */
/* --------------------------------------------------------------------- */

#define RING_FRAMES     16384                   /* power of two, ~340 ms */
#define TARGET_BUFFERED (HOST_AUDIO_RATE / 20)  /* 50 ms */
#define MAX_BUFFERED    (HOST_AUDIO_RATE / 4)   /* drop frames beyond 250 ms */
/* Output rate correction: up to +-0.5% (inaudible) keeps the buffer near the
 * target although the frame clock and the audio clock drift apart. */
#define MAX_RATE_ADJUST 0.005

static int16_t sRing[RING_FRAMES * 2];
static uint32_t sRingWrite;  /* frames ever written (producer) */
static uint32_t sRingRead;   /* frames ever read (consumer) */
static volatile bool sEnabled;
static uint32_t sUnderrunFrames;  /* frames the device wanted but didn't get */
static uint32_t sDroppedFrames;   /* rendered frames that didn't fit */

void HostAudio_SetEnabled(bool enabled)
{
    sEnabled = enabled;
}

void HostAudio_GetStats(uint32_t *underrunFrames, uint32_t *droppedFrames)
{
    *underrunFrames = __atomic_load_n(&sUnderrunFrames, __ATOMIC_RELAXED);
    *droppedFrames = __atomic_load_n(&sDroppedFrames, __ATOMIC_RELAXED);
}

int HostAudio_Buffered(void)
{
    return (int)(__atomic_load_n(&sRingWrite, __ATOMIC_ACQUIRE)
               - __atomic_load_n(&sRingRead, __ATOMIC_ACQUIRE));
}

int HostAudio_Read(int16_t *out, int frames)
{
    uint32_t read = sRingRead;
    uint32_t available = __atomic_load_n(&sRingWrite, __ATOMIC_ACQUIRE) - read;
    int n = (uint32_t)frames < available ? frames : (int)available;
    int i;

    for (i = 0; i < n; i++)
    {
        uint32_t at = ((read + i) & (RING_FRAMES - 1)) * 2;

        out[i * 2] = sRing[at];
        out[i * 2 + 1] = sRing[at + 1];
    }
    memset(out + n * 2, 0, (size_t)(frames - n) * 2 * sizeof(int16_t));
    if (n < frames && sEnabled)
        __atomic_fetch_add(&sUnderrunFrames, frames - n, __ATOMIC_RELAXED);
    __atomic_store_n(&sRingRead, read + n, __ATOMIC_RELEASE);
    return n;
}

/* --------------------------------------------------------------------- */
/* Registers                                                             */
/* --------------------------------------------------------------------- */

#define NR(offset) (*(vu8 *)(REG_BASE + (offset)))
#define OFS_NR10 0x60
#define OFS_NR11 0x62
#define OFS_NR12 0x63
#define OFS_NR13 0x64
#define OFS_NR14 0x65
#define OFS_NR21 0x68
#define OFS_NR22 0x69
#define OFS_NR23 0x6C
#define OFS_NR24 0x6D
#define OFS_NR30 0x70
#define OFS_NR31 0x72
#define OFS_NR32 0x73
#define OFS_NR33 0x74
#define OFS_NR34 0x75
#define OFS_NR41 0x78
#define OFS_NR42 0x79
#define OFS_NR43 0x7C
#define OFS_NR44 0x7D
#define OFS_NR50 0x80
#define OFS_NR51 0x81
#define OFS_WAVE 0x90

#define NRX4_TRIGGER       0x80
#define NRX4_LENGTH_ENABLE 0x40

/* --------------------------------------------------------------------- */
/* PSG channels                                                          */
/* --------------------------------------------------------------------- */

struct Envelope
{
    int volume;     /* 0..15 */
    int period;     /* 0: off */
    int timer;
    bool increase;
};

struct Channel
{
    bool on;
    int length;              /* length counter; stops the channel at 0 */
    bool lengthEnable;
    struct Envelope env;
    uint32_t freq;           /* 11-bit frequency register (ch1-3) */
    double phase;            /* square: 0..1 per period; wave: 0..32 */
    /* square 1 sweep */
    int sweepTimer;
    bool sweepEnabled;
    uint32_t sweepShadow;
    /* noise */
    uint16_t lfsr;
    double lfsrClock;
};

static struct Channel sCh[4];
static unsigned sSequencerStep;
static double sSequencerTime;

static const uint8_t sDutyPatterns[4][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 1 },  /* 12.5% */
    { 1, 0, 0, 0, 0, 0, 0, 1 },  /* 25%   */
    { 1, 0, 0, 0, 0, 1, 1, 1 },  /* 50%   */
    { 0, 1, 1, 1, 1, 1, 1, 0 },  /* 75%   */
};

static void EnvelopeTrigger(struct Envelope *env, uint8_t nrx2)
{
    env->volume = nrx2 >> 4;
    env->increase = (nrx2 & 0x08) != 0;
    env->period = nrx2 & 0x07;
    env->timer = env->period;
}

static void EnvelopeClock(struct Envelope *env)
{
    if (env->period == 0 || --env->timer > 0)
        return;
    env->timer = env->period;
    if (env->increase && env->volume < 15)
        env->volume++;
    else if (!env->increase && env->volume > 0)
        env->volume--;
}

static uint32_t SquareFreqRegister(int ch)
{
    int base = ch == 0 ? OFS_NR13 : OFS_NR23;

    return NR(base) | ((NR(base + 1) & 7) << 8);
}

/* Square 1 sweep: the next frequency, or > 2047 for overflow. */
static uint32_t SweepNext(struct Channel *c)
{
    uint8_t nr10 = NR(OFS_NR10);
    uint32_t delta = c->sweepShadow >> (nr10 & 7);

    return (nr10 & 0x08) ? c->sweepShadow - delta : c->sweepShadow + delta;
}

static void SweepClock(struct Channel *c)
{
    uint8_t nr10 = NR(OFS_NR10);
    int period = (nr10 >> 4) & 7;
    uint32_t next;

    if (--c->sweepTimer > 0)
        return;
    c->sweepTimer = period ? period : 8;
    if (!c->sweepEnabled || period == 0)
        return;
    next = SweepNext(c);
    if (next > 2047)
    {
        c->on = false;
        return;
    }
    if ((nr10 & 7) != 0)
    {
        c->sweepShadow = next;
        c->freq = next;
        /* The hardware updates the frequency registers too. */
        NR(OFS_NR13) = next & 0xFF;
        NR(OFS_NR14) = (NR(OFS_NR14) & ~7) | (next >> 8);
        if (SweepNext(c) > 2047)
            c->on = false;
    }
}

static void SequencerClock(void)
{
    int i;

    if ((sSequencerStep & 1) == 0)  /* length, 256 Hz */
    {
        for (i = 0; i < 4; i++)
        {
            if (sCh[i].lengthEnable && sCh[i].length > 0 && --sCh[i].length == 0)
                sCh[i].on = false;
        }
    }
    if (sSequencerStep == 2 || sSequencerStep == 6)  /* sweep, 128 Hz */
        SweepClock(&sCh[0]);
    if (sSequencerStep == 7)  /* envelope, 64 Hz */
    {
        EnvelopeClock(&sCh[0].env);
        EnvelopeClock(&sCh[1].env);
        EnvelopeClock(&sCh[3].env);
    }
    sSequencerStep = (sSequencerStep + 1) & 7;
}

/* Pick up what the game wrote to the registers since the last frame. */
static void ReadRegisters(void)
{
    static const int sNrx1[4] = { OFS_NR11, OFS_NR21, OFS_NR31, OFS_NR41 };
    static const int sNrx2[4] = { OFS_NR12, OFS_NR22, OFS_NR32, OFS_NR42 };
    static const int sNrx4[4] = { OFS_NR14, OFS_NR24, OFS_NR34, OFS_NR44 };
    int i;

    for (i = 0; i < 4; i++)
    {
        struct Channel *c = &sCh[i];
        uint8_t nrx4 = NR(sNrx4[i]);
        uint8_t nrx2 = NR(sNrx2[i]);

        c->lengthEnable = (nrx4 & NRX4_LENGTH_ENABLE) != 0;
        if (nrx4 & NRX4_TRIGGER)
        {
            NR(sNrx4[i]) = nrx4 & ~NRX4_TRIGGER;
            c->on = true;
            if (i == 2)
            {
                c->length = 256 - NR(OFS_NR31);
                c->phase = 0;
            }
            else
            {
                c->length = 64 - (NR(sNrx1[i]) & 0x3F);
                EnvelopeTrigger(&c->env, nrx2);
            }
            if (i == 0)
            {
                uint8_t nr10 = NR(OFS_NR10);
                int period = (nr10 >> 4) & 7;

                c->sweepShadow = SquareFreqRegister(0);
                c->sweepTimer = period ? period : 8;
                c->sweepEnabled = period != 0 || (nr10 & 7) != 0;
                if ((nr10 & 7) != 0 && SweepNext(c) > 2047)
                    c->on = false;
            }
            if (i == 3)
                c->lfsr = 0x7FFF;
        }

        /* A channel whose DAC is off is silent. */
        if (i == 2 ? !(NR(OFS_NR30) & 0x80) : (nrx2 & 0xF8) == 0)
            c->on = false;
    }

    sCh[0].freq = SquareFreqRegister(0);
    sCh[1].freq = SquareFreqRegister(1);
    sCh[2].freq = NR(OFS_NR33) | ((NR(OFS_NR34) & 7) << 8);
}

/* Channel outputs (0..15) for one moment, advancing them by `dt` seconds. */
static void SampleChannels(double dt, float out[4])
{
    struct Channel *c;
    uint8_t nr43;
    int shift;

    c = &sCh[0];
    for (int i = 0; i < 2; i++, c++)
    {
        int duty = NR(i == 0 ? OFS_NR11 : OFS_NR21) >> 6;

        c->phase += 131072.0 / (2048 - c->freq) * dt;
        c->phase -= (int)c->phase;
        out[i] = c->on && sDutyPatterns[duty][(int)(c->phase * 8) & 7] ? c->env.volume : 0;
    }

    /* Wave: 32 4-bit samples, high nibble first. */
    c = &sCh[2];
    c->phase += 2097152.0 / (2048 - c->freq) * dt;
    c->phase -= (int)(c->phase / 32) * 32;
    if (c->on)
    {
        static const uint8_t sShift[4] = { 4, 0, 1, 2 };  /* mute, 100%, 50%, 25% */
        uint8_t nr32 = NR(OFS_NR32);
        int index = (int)c->phase & 31;
        uint8_t byte = NR(OFS_WAVE + index / 2);
        int sample = (index & 1) ? (byte & 0xF) : (byte >> 4);

        if (nr32 & 0x80)
            out[2] = sample * 3 / 4.0f;
        else
            out[2] = sample >> sShift[(nr32 >> 5) & 3];
    }
    else
    {
        out[2] = 0;
    }

    /* Noise: LFSR clocked at 524288 / r / 2^(s+1) Hz (r = 0 counts as 0.5). */
    c = &sCh[3];
    nr43 = NR(OFS_NR43);
    shift = nr43 >> 4;
    if (shift < 14)
    {
        int ratio = nr43 & 7;
        double rate = (ratio ? 524288.0 / ratio : 1048576.0) / (2 << shift);

        c->lfsrClock += rate * dt;
        while (c->lfsrClock >= 1)
        {
            uint16_t bit = (c->lfsr ^ (c->lfsr >> 1)) & 1;

            c->lfsr = (c->lfsr >> 1) | (bit << 14);
            if (nr43 & 0x08)
                c->lfsr = (c->lfsr & ~0x40) | (bit << 6);
            c->lfsrClock -= 1;
        }
    }
    out[3] = c->on && !(c->lfsr & 1) ? c->env.volume : 0;
}

/* --------------------------------------------------------------------- */
/* Mixing                                                                */
/* --------------------------------------------------------------------- */

#define OVERSAMPLE 4
#define DC_BLOCK   0.998f

/* 2nd-order Butterworth low-pass, 10 kHz at 48 kHz (RBJ cookbook, Q = 1/sqrt 2). */
#define LP_B0 0.2201947f
#define LP_B1 0.4403894f
#define LP_B2 0.2201947f
#define LP_A1 (-0.3075664f)
#define LP_A2 0.1883452f

static int8_t sDsRight[PCM_DMA_BUF_SIZE];  /* the frame that plays now */
static int8_t sDsLeft[PCM_DMA_BUF_SIZE];
static int sDsCount;
static double sFrameFraction;
static volatile bool sRawOutput = true;

struct OutputFilter
{
    float dcIn, dcOut;          /* DC blocker */
    float x1, x2, y1, y2;       /* low-pass history */
};
static struct OutputFilter sFilter[2];

void HostAudio_SetRawOutput(bool raw)
{
    sRawOutput = raw;
}

/* One output side: PSG + Direct Sound through the hardware's mixer and DAC
 * (DAC resolution only for raw output). */
static float MixSide(float psg, float dsA, float dsB, bool aOn, bool bOn, bool raw)
{
    uint16_t cntH = REG_SOUNDCNT_H;
    uint16_t bias = REG_SOUNDBIAS;
    int resolution = bias >> 14;
    int level = bias & 0x3FE;
    int psgRatio = cntH & 3;
    float value;

    if (psgRatio == 3)
        psgRatio = 2;
    value = psg * 8 / (1 << (4 - psgRatio));
    if (aOn)
        value += dsA * ((cntH & 0x04) ? 4 : 2);
    if (bOn)
        value += dsB * ((cntH & 0x08) ? 4 : 2);

    value += level;
    if (value < 0)
        value = 0;
    else if (value > 0x3FF)
        value = 0x3FF;
    if (raw)
        value = (int)value & ~((2 << resolution) - 1);
    return value - level;
}

static int16_t Output(struct OutputFilter *f, float x, bool raw)
{
    float y = x - f->dcIn + DC_BLOCK * f->dcOut;

    f->dcIn = x;
    f->dcOut = y;
    if (!raw)
    {
        float lp = LP_B0 * y + LP_B1 * f->x1 + LP_B2 * f->x2 - LP_A1 * f->y1 - LP_A2 * f->y2;

        f->x2 = f->x1;
        f->x1 = y;
        f->y2 = f->y1;
        f->y1 = lp;
        y = lp;
    }
    if (y > 32767)
        return 32767;
    if (y < -32768)
        return -32768;
    return (int16_t)y;
}

/* Direct Sound sample at position `t` (in samples) of the frame now playing;
 * `nextRight/Left` continue it (the frame mixed just now). */
static void DirectSoundAt(double t, bool raw, const int8_t *nextRight, const int8_t *nextLeft,
                          float *right, float *left)
{
    int i = (int)t;
    float frac = (float)(t - i);
    int nextA, nextB;

    if (raw || sDsCount == 0)
    {
        *right = sDsCount ? sDsRight[i] : 0;
        *left = sDsCount ? sDsLeft[i] : 0;
        return;
    }
    nextA = i + 1 < sDsCount ? sDsRight[i + 1] : nextRight[0];
    nextB = i + 1 < sDsCount ? sDsLeft[i + 1] : nextLeft[0];
    *right = sDsRight[i] + (nextA - sDsRight[i]) * frac;
    *left = sDsLeft[i] + (nextB - sDsLeft[i]) * frac;
}

void HostAudio_SoundFrame(const int8_t *right, const int8_t *left, int count, int pcmFreq)
{
    uint32_t write, buffered, at;
    double frameSeconds, ideal, adjust, dt;
    bool raw = sRawOutput;
    int frames, k;

    if (!sEnabled || pcmFreq <= 0 || count <= 0 || count > PCM_DMA_BUF_SIZE)
        return;

    ReadRegisters();

    /* Output frames for this frame's duration, nudged toward the target
     * buffer level. */
    write = sRingWrite;
    buffered = write - __atomic_load_n(&sRingRead, __ATOMIC_ACQUIRE);
    adjust = (double)((int)TARGET_BUFFERED - (int)buffered) / TARGET_BUFFERED;
    if (adjust > 1)
        adjust = 1;
    else if (adjust < -1)
        adjust = -1;
    frameSeconds = (double)count / pcmFreq;
    ideal = frameSeconds * HOST_AUDIO_RATE * (1 + MAX_RATE_ADJUST * adjust) + sFrameFraction;
    frames = (int)ideal;
    sFrameFraction = ideal - frames;
    dt = frameSeconds / frames / OVERSAMPLE;

    for (k = 0; k < frames; k++)
    {
        uint8_t nr50 = NR(OFS_NR50), nr51 = NR(OFS_NR51);
        uint16_t cntH = REG_SOUNDCNT_H;
        float sum[4] = { 0, 0, 0, 0 }, out[4], psgRight = 0, psgLeft = 0;
        float dsA, dsB, sideRight, sideLeft;
        int i, s;

        DirectSoundAt((double)k * sDsCount / frames, raw, right, left, &dsA, &dsB);

        for (s = 0; s < OVERSAMPLE; s++)
        {
            SampleChannels(dt, out);
            for (i = 0; i < 4; i++)
                sum[i] += out[i];
        }
        for (i = 0; i < 4; i++)
        {
            if (nr51 & (0x01 << i))
                psgRight += sum[i] / OVERSAMPLE;
            if (nr51 & (0x10 << i))
                psgLeft += sum[i] / OVERSAMPLE;
        }
        psgRight *= 1 + (nr50 & 7);
        psgLeft *= 1 + ((nr50 >> 4) & 7);

        if (REG_SOUNDCNT_X & 0x80)
        {
            sideRight = MixSide(psgRight, dsA, dsB, cntH & 0x0100, cntH & 0x1000, raw);
            sideLeft = MixSide(psgLeft, dsA, dsB, cntH & 0x0200, cntH & 0x2000, raw);
        }
        else
        {
            sideRight = sideLeft = 0;
        }

        /* 10-bit DAC range to 16 bits. */
        if (buffered + k < MAX_BUFFERED)
        {
            at = ((write + k) & (RING_FRAMES - 1)) * 2;
            sRing[at] = Output(&sFilter[0], sideLeft * 64.0f, raw);
            sRing[at + 1] = Output(&sFilter[1], sideRight * 64.0f, raw);
        }

        sSequencerTime += frameSeconds / frames;
        while (sSequencerTime >= 1.0 / 512)
        {
            sSequencerTime -= 1.0 / 512;
            SequencerClock();
        }
    }
    if (buffered + frames > MAX_BUFFERED)
        __atomic_fetch_add(&sDroppedFrames,
                           buffered >= MAX_BUFFERED ? (uint32_t)frames : buffered + frames - MAX_BUFFERED,
                           __ATOMIC_RELAXED);
    if (buffered < MAX_BUFFERED)
    {
        uint32_t written = MAX_BUFFERED - buffered;

        __atomic_store_n(&sRingWrite, write + (written < (uint32_t)frames ? written : (uint32_t)frames),
                         __ATOMIC_RELEASE);
    }

    /* This frame's Direct Sound samples play next frame. */
    memcpy(sDsRight, right, count);
    memcpy(sDsLeft, left, count);
    sDsCount = count;
}
