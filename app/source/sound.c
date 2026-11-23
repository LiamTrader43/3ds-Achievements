// Sound effects: the chosen set's part of data/sounds.bin is copied into linear memory (the DSP
// reads physical memory) and each sound is played on the first of a few DSP channels that's
// free, else the one used longest ago, so quick presses can overlap. Only one set is in memory
// at a time; choosing another replaces it.
#include <3ds.h>

#include <stdio.h>
#include <string.h>

#define SOUND_TABLES            // sounds.h's tables (sound.h includes it too): here only
#include "sounds.h"
#include "sound.h"
#include "sounds_bin.h"

#define CHOICE_PATH "sdmc:/achievements/sounds.txt"     // the set's id, or "off"
#define CHANNELS    4

static int          s_chosen = SOUND_SET_DEFAULT;   // the setting: a set, or -1 (off)
static int          s_loaded = -1;      // the set in s_data, or -1
static bool         s_dsp;              // ndsp is started
static bool         s_broken;           // it couldn't be
static s16         *s_data;
static ndspWaveBuf  s_wave[CHANNELS];
static int          s_next;             // the channel used longest ago

static bool StartDsp(void)
{
    if (s_dsp || s_broken)
        return s_dsp;
    if (R_FAILED(ndspInit()))
    {
        s_broken = true;
        return false;
    }
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = 1.0f;             // front left and right
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int ch = 0; ch < CHANNELS; ch++)
    {
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
        ndspChnSetRate(ch, SOUND_RATE);
        ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetMix(ch, mix);
    }
    s_dsp = true;
    return true;
}

// Stops every sound, and waits (a few DSP frames at most) until the DSP has let go of them.
static void StopAll(void)
{
    if (!s_dsp)
        return;
    for (int ch = 0; ch < CHANNELS; ch++)
        ndspChnWaveBufClear(ch);
    for (int tries = 0; tries < 8; tries++)
    {
        bool playing = false;
        for (int ch = 0; ch < CHANNELS; ch++)
            playing = playing || ndspChnIsPlaying(ch);
        if (!playing)
            break;
        svcSleepThread(5000000LL);
    }
}

// Puts set's sounds in linear memory (in place of the last set's).
static void Load(int set)
{
    if (set == s_loaded || !StartDsp())
        return;
    StopAll();
    if (s_data != NULL)
        linearFree(s_data);
    s_loaded = -1;
    size_t bytes = SOUND_SET_SIZE[set] * sizeof(s16);
    s_data = linearAlloc(bytes);
    if (s_data == NULL)
        return;
    memcpy(s_data, sounds_bin + SOUND_SET_START[set] * sizeof(s16), bytes);
    DSP_FlushDataCache(s_data, bytes);
    s_loaded = set;
}

void Sound_Init(void)
{
    FILE *f = fopen(CHOICE_PATH, "rb");
    if (f != NULL)
    {
        char word[24] = "";
        if (fscanf(f, "%23s", word) == 1)
        {
            if (strcmp(word, "off") == 0)
                s_chosen = -1;
            for (int i = 0; i < SOUND_SET_COUNT; i++)
                if (strcmp(word, SOUND_SET_ID[i]) == 0)
                    s_chosen = i;
        }
        fclose(f);
    }
    if (s_chosen >= 0)
        Load(s_chosen);
}

void Sound_Exit(void)
{
    StopAll();
    if (s_dsp)
        ndspExit();
    s_dsp = false;
    if (s_data != NULL)
        linearFree(s_data);
    s_data = NULL;
    s_loaded = -1;
}

void Sound_Play(SoundId id)
{
    if (s_chosen < 0 || s_loaded != s_chosen || (unsigned)id >= SOUND_COUNT || SOUND_LENGTH[s_loaded][id] == 0)
        return;
    int ch = s_next;
    for (int i = 0; i < CHANNELS; i++)
    {
        int c = (s_next + i) % CHANNELS;
        if (!ndspChnIsPlaying(c))
        {
            ch = c;
            break;
        }
    }
    s_next = (ch + 1) % CHANNELS;

    ndspChnWaveBufClear(ch);
    ndspWaveBuf *w = &s_wave[ch];
    memset(w, 0, sizeof(*w));
    w->data_pcm16 = s_data + SOUND_START[s_loaded][id];
    w->nsamples = SOUND_LENGTH[s_loaded][id];
    ndspChnWaveBufAdd(ch, w);
}

int Sound_SetCount(void)
{
    return SOUND_SET_COUNT;
}

const char *Sound_SetName(int set)
{
    return set >= 0 && set < SOUND_SET_COUNT ? SOUND_SET_NAME[set] : "Off";
}

const char *Sound_SetDescription(int set)
{
    return set >= 0 && set < SOUND_SET_COUNT ? SOUND_SET_DESCRIPTION[set] : "";
}

int Sound_Current(void)
{
    return s_chosen;
}

// Turning them off keeps the DSP and the last set (a sound playing finishes); the next time the
// app opens it doesn't start them at all.
void Sound_Use(int set)
{
    s_chosen = set >= 0 && set < SOUND_SET_COUNT ? set : -1;
    if (s_chosen >= 0)
        Load(s_chosen);
    FILE *f = fopen(CHOICE_PATH, "wb");
    if (f != NULL)
    {
        fprintf(f, "%s\n", s_chosen >= 0 ? SOUND_SET_ID[s_chosen] : "off");
        fclose(f);
    }
}

bool Sound_Broken(void)
{
    return s_chosen >= 0 && s_broken;
}
