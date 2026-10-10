#include "uisfx.h"

typedef struct
{
    uint8_t synth;
    float frequency_hz;
    float start_ms;
    float duration_ms;
    float volume;
    float attack_s;
    float decay_s;
    float sustain;
    float release_s;
    float curvature;
    float filter_frequency_hz;
    float filter_resonance;
} CB_SFXNote;

typedef struct
{
    const CB_SFXNote* notes;
    int count;
} CB_SFXSound;

static PDSynth* s_voices[6];
static SoundChannel* s_channels[2];
static TwoPoleFilter* s_filters[2];
static bool s_ok;

static const CB_SFXNote s_nav_down[] = {
    {0, 1450.0f, 0.0f, 10.6f, 0.20f, 0.004f, 0.006f, 0.0f, 0.0f, 1.0f},
    {1, 3100.0f, 0.0f, 5.0f, 0.10f, 0.001f, 0.004f, 0.0f, 0.0f, 1.0f},
    {4, 1450.0f, 0.0f, 7.0f, 0.04f, 0.001f, 0.006f, 0.0f, 0.0f, 0.7f, 1450.0f, 1.8f},
    {5, 3100.0f, 0.0f, 4.0f, 0.03f, 0.001f, 0.003f, 0.0f, 0.0f, 1.0f, 3100.0f, 1.5f},
};

static const CB_SFXNote s_nav_up[] = {
    {0, 1780.0f, 0.0f, 18.6f, 0.20f, 0.002f, 0.010f, 0.0f, 0.004f, 1.0f},
    {1, 2900.0f, 0.0f, 6.0f, 0.05f, 0.001f, 0.005f, 0.0f, 0.0f, 1.0f},
    {2, 3600.0f, 0.0f, 4.0f, 0.03f, 0.001f, 0.003f, 0.0f, 0.0f, 1.0f},
    {4, 1780.0f, 0.0f, 12.0f, 0.04f, 0.002f, 0.009f, 0.0f, 0.0f, 1.0f, 1780.0f, 3.0f},
    {5, 3500.0f, 0.0f, 12.0f, 0.04f, 0.002f, 0.009f, 0.0f, 0.0f, 1.0f, 3500.0f, 1.2f},
};

static const CB_SFXNote s_confirm[] = {
    {0, 1795.0f, 0.0f, 12.0f, 0.70f, 0.002f, 0.010f, 0.0f, 0.0f, 0.7f},
    {1, 1818.0f, 0.0f, 12.0f, 0.33f, 0.002f, 0.010f, 0.0f, 0.0f, 0.7f},
    {0, 1800.0f, 16.0f, 10.0f, 0.47f, 0.002f, 0.010f, 0.0f, 0.0f, 0.7f},
    {0, 1795.0f, 92.0f, 10.0f, 0.10f, 0.002f, 0.010f, 0.0f, 0.0f, 0.7f},
    {0, 1795.0f, 114.0f, 10.0f, 0.11f, 0.002f, 0.010f, 0.0f, 0.0f, 0.7f},
    {4, 2800.0f, 0.0f, 14.0f, 0.60f, 0.001f, 0.010f, 0.0f, 0.0f, 0.7f, 2800.0f, 3.0f},
    {4, 2800.0f, 16.0f, 10.0f, 0.24f, 0.001f, 0.010f, 0.0f, 0.0f, 0.7f, 2800.0f, 3.0f},
    {4, 2800.0f, 92.0f, 10.0f, 0.04f, 0.001f, 0.010f, 0.0f, 0.0f, 0.7f, 2800.0f, 3.0f},
    {4, 2800.0f, 116.0f, 10.0f, 0.07f, 0.001f, 0.010f, 0.0f, 0.0f, 0.7f, 2800.0f, 3.0f},
};

static const CB_SFXNote s_denial[] = {
    {0, 302.0f, 0.0f, 132.6f, 0.26f, 0.008f, 0.015f, 0.25f, 0.060f, 0.7f},
    {1, 332.0f, 0.0f, 110.0f, 0.15f, 0.008f, 0.014f, 0.20f, 0.050f, 0.7f},
    {2, 626.0f, 0.0f, 45.0f, 0.05f, 0.004f, 0.040f, 0.0f, 0.010f, 1.0f},
    {2, 626.0f, 60.0f, 30.0f, 0.03f, 0.004f, 0.026f, 0.0f, 0.0f, 1.0f},
    {3, 1335.0f, 0.0f, 30.0f, 0.016f, 0.004f, 0.025f, 0.0f, 0.0f, 1.0f},
    {4, 310.0f, 0.0f, 120.0f, 0.016f, 0.008f, 0.100f, 0.05f, 0.05f, 1.0f, 310.0f, 1.2f},
    {3, 302.0f, 36.0f, 70.0f, 0.08f, 0.010f, 0.030f, 0.10f, 0.030f, 1.0f},
};

static const CB_SFXNote s_alert[] = {
    {0, 1322.0f, 0.0f, 170.0f, 1.0f, 0.010f, 0.012f, 0.18f, 0.010f, 0.3f},
    {1, 1984.0f, 0.0f, 170.0f, 0.64f, 0.010f, 0.012f, 0.18f, 0.010f, 0.3f},
    {2, 3312.0f, 0.0f, 130.0f, 0.51f, 0.010f, 0.012f, 0.15f, 0.040f, 0.3f},
    {3, 1322.0f, 64.0f, 90.0f, 0.15f, 0.030f, 0.040f, 0.06f, 0.020f, 0.8f},
};

#define CB_SFX_NUM(x) (int)(sizeof(x) / sizeof(x[0]))

// indexed by CB_UISound enum order
static const CB_SFXSound s_sounds[] = {
    {s_nav_down, CB_SFX_NUM(s_nav_down)}, {s_nav_up, CB_SFX_NUM(s_nav_up)},
    {s_confirm, CB_SFX_NUM(s_confirm)},   {s_denial, CB_SFX_NUM(s_denial)},
    {s_alert, CB_SFX_NUM(s_alert)},
};

bool cb_uisfx_init(void)
{
    if (s_ok)
        return true;

#define CB_UISFX_FAIL(tag)                                   \
    do                                                       \
    {                                                        \
        playdate->system->logToConsole("cb uisfx: %s", tag); \
        cb_uisfx_free();                                     \
        return false;                                        \
    } while (0)

    for (int i = 0; i < 4; ++i)
    {
        s_voices[i] = playdate->sound->synth->newSynth();
        if (!s_voices[i])
            CB_UISFX_FAIL("newSynth (sine)");
        playdate->sound->synth->setWaveform(s_voices[i], kWaveformSine);
        playdate->sound->synth->setVolume(s_voices[i], 1.0f, 1.0f);
    }

    for (int i = 0; i < 2; ++i)
    {
        s_voices[4 + i] = playdate->sound->synth->newSynth();
        s_channels[i] = playdate->sound->channel->newChannel();
        s_filters[i] = playdate->sound->effect->twopolefilter->newFilter();
        if (!s_voices[4 + i])
            CB_UISFX_FAIL("newSynth (noise)");
        if (!s_channels[i])
            CB_UISFX_FAIL("newChannel");
        if (!s_filters[i])
            CB_UISFX_FAIL("newFilter");

        // returns 0 if the attach calls below registered it implicitly
        if (!playdate->sound->addChannel(s_channels[i]))
            playdate->system->logToConsole("cb uisfx: addChannel returned 0 (ignored)");

        playdate->sound->synth->setWaveform(s_voices[4 + i], kWaveformNoise);
        playdate->sound->synth->setVolume(s_voices[4 + i], 1.0f, 1.0f);

        playdate->sound->effect->twopolefilter->setType(s_filters[i], kFilterTypeBandPass);
        playdate->sound->effect->twopolefilter->setFrequency(s_filters[i], 1000.0f);
        playdate->sound->effect->twopolefilter->setResonance(s_filters[i], 1.4f);
        if (!playdate->sound->channel->addEffect(s_channels[i], (SoundEffect*)s_filters[i]))
            CB_UISFX_FAIL("addEffect");
        if (!playdate->sound->channel->addSource(s_channels[i], (SoundSource*)s_voices[4 + i]))
            CB_UISFX_FAIL("addSource");
    }

#undef CB_UISFX_FAIL

    s_ok = true;
    return true;
}

void cb_uisfx_free(void)
{
    for (int i = 0; i < 2; ++i)
    {
        if (s_channels[i])
        {
            if (s_voices[4 + i])
            {
                playdate->sound->channel->removeSource(
                    s_channels[i], (SoundSource*)s_voices[4 + i]
                );
            }
            if (s_filters[i])
            {
                playdate->sound->channel->removeEffect(s_channels[i], (SoundEffect*)s_filters[i]);
            }
        }
    }
    for (int i = 0; i < 6; ++i)
    {
        if (s_voices[i])
        {
            playdate->sound->synth->freeSynth(s_voices[i]);
            s_voices[i] = NULL;
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        if (s_channels[i])
        {
            playdate->sound->channel->freeChannel(s_channels[i]);
            s_channels[i] = NULL;
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        if (s_filters[i])
        {
            playdate->sound->effect->twopolefilter->freeFilter(s_filters[i]);
            s_filters[i] = NULL;
        }
    }
    s_ok = false;
}

bool cb_uisfx_play(CB_UISound sound)
{
    if (!s_ok || sound < 0 || sound >= CB_SFX_NUM(s_sounds))
        return false;

    const CB_SFXSound* snd = &s_sounds[sound];
    uint32_t now = playdate->sound->getCurrentTime();

    for (int i = 0; i < snd->count; ++i)
    {
        const CB_SFXNote* n = &snd->notes[i];
        PDSynth* synth = s_voices[n->synth];

        PDSynthEnvelope* env = playdate->sound->synth->getEnvelope(synth);
        playdate->sound->envelope->setAttack(env, n->attack_s);
        playdate->sound->envelope->setDecay(env, n->decay_s);
        playdate->sound->envelope->setSustain(env, n->sustain);
        playdate->sound->envelope->setRelease(env, n->release_s);
        playdate->sound->envelope->setCurvature(env, n->curvature);

        if (n->synth >= 4)
        {
            int li = n->synth - 4;
            playdate->sound->effect->twopolefilter->setFrequency(
                s_filters[li], n->filter_frequency_hz
            );
            playdate->sound->effect->twopolefilter->setResonance(
                s_filters[li], n->filter_resonance
            );
        }

        playdate->sound->synth->playNote(
            synth, n->frequency_hz, n->volume, n->duration_ms / 1000.0f,
            now + (uint32_t)(n->start_ms * 44.1f + 0.5f)  // 44.1 frames per ms
        );
    }
    return true;
}
