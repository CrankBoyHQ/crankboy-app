#include "pgmusic.h"

#include "utility.h"

static const float bpm = 185.0f * 2;
static int beat = 0;
static int time_sig = 8;
static float t = 0;
static struct PDSynth* drumSynth;
static struct PDSynth* bassSynth;
static int root = 0;
static int nextroot = 0;
static int measure = 0;
static int fill = 0;

static float pitch_table[] = {440.1f, 446.0f, 494.0f, 523.25f, 555.0f, 588.0f,
                              622.2f, 659.0f, 698.5f, 741.0f,  784.0f, 830.5f};

static int penta_scale[] = {0, 2, 5, 7, 9, -1};

void pgmusic_init(void)
{
    drumSynth = playdate->sound->synth->newSynth();
    playdate->sound->synth->setWaveform(drumSynth, kWaveformNoise);
    playdate->sound->synth->setAttackTime(drumSynth, 0.0001f);
    playdate->sound->synth->setDecayTime(drumSynth, 0.08f);
    playdate->sound->synth->setSustainLevel(drumSynth, 0.01f);
    playdate->sound->synth->setReleaseTime(drumSynth, 0.05f);

    bassSynth = playdate->sound->synth->newSynth();
    playdate->sound->synth->setWaveform(bassSynth, kWaveformTriangle);
    playdate->sound->synth->setAttackTime(bassSynth, 0.01f);
    playdate->sound->synth->setDecayTime(bassSynth, 0.03f);
    playdate->sound->synth->setSustainLevel(bassSynth, 0.7f);
    playdate->sound->synth->setReleaseTime(bassSynth, 0.1f);
}

void pgmusic_begin(void)
{
    pgmusic_init();

    root = 10;
    nextroot = 10;
    measure = -4;
    fill = 0;
    beat = 0;
    t = 0;
}

static float rng(void)
{
    return (rand() % 64) / 64.0f;
}

static float get_note_freq(float note)
{
    int znote = note + 0.33f;
    float micro = 1.0f + 0.06f * (note - znote);

    float mult = 1.0f;

    while (znote < 0)
    {
        znote += 12;
        mult *= 0.5f;
    }
    while (znote >= 12)
    {
        znote -= 12;
        mult *= 2.0f;
    }

    float hz = pitch_table[znote];

    return hz * mult * micro;
}

static void bass(void)
{
    if (rng() > 0.1f && beat != 0)
        return;
    float hz = 0;

    if (beat % 2 == 1 && rng() > 0.4f)
        return;
    if (beat % 2 == 0 && rng() > 0.9f)
        return;

    int note = root;

    if (beat == 0 && rng() > 0.025f)
    {
        note = root;
    }
    else
    {
        if ((beat == 7 && rng() > 0.3f) || (beat == 4 && rng() < 0.2f))
        {
            note = nextroot - (rng() > 0.3f);
        }
        else if (rng() < 0.77f && (beat != 4))
        {
            note = root;
        }
        else if (rng() < 0.15f || beat == 4)
        {
            note = root + 7;
        }
        else if (rng() < 0.6f || (beat == 7 && rng() < 0.9f))
        {
            note = nextroot - 1 - (rng() > 0.85f);
        }
        else
        {
            note = root + penta_scale[rand() % 5];
        }
    }

    note -= 24;
    if (root > 5)
    {
        note -= 12;
    }

    playdate->sound->synth->playNote(
        bassSynth, get_note_freq(note), 0.25f + rng() * 0.06f, 0.05f + rng() * 0.3f, 0
    );
}

static void drums(void)
{
    float hz = 0;

    if (beat % 2 == 1 && rng() > 0.125f)
        return;
    if (beat % 2 == 0 && rng() > 0.97f)
        return;

    if (beat == 0 || rng() > 0.95f || (beat == 4 && fill))
    {
        playdate->sound->synth->setDecayTime(drumSynth, (2 + rng()) * 0.05f);
        hz = 300 + rng() * 20 - 50 * fill;
    }
    else
    {
        bool high = (beat == 4) || fill;

        playdate->sound->synth->setDecayTime(
            drumSynth, (1 + rng() * rng() + high * (1 + rng() * rng() * 1.3f)) * 0.051f
        );

        hz = 450 + rng() * 100 + 200 * (high);
    }

    if (hz > 0)
    {
        playdate->sound->synth->playNote(drumSynth, hz, 0.11f, 0.08f, 0);
    }
}

void pgmusic_update(float dt)
{
    t += dt * bpm / 60.0f;
    if (t >= 1)
    {
        if (beat == 0)
        {
            measure = (measure + 1);
            root = nextroot;
            if (measure == 2 && rng() > 0.7f)
                fill = 1;
            if (measure >= 4)
            {
                fill = 0;
                measure = 0;
            }
            if (rng() < 0.02f || (measure == 3 && rng() < 0.4f))
            {

                // key change
                if (rng() < 0.5f)
                {
                    nextroot += 7;
                }
                else if (rng() < 0.1f)
                {
                    if (rng() < 0.3f)
                    {
                        nextroot += 5;
                    }
                }
                else if (rng() < 0.01f)
                {
                    nextroot++;
                }
                else if (rng() < 0.001f)
                {
                    nextroot += 11;
                }
                else if (rng() < 0.3f)
                {
                    nextroot = 0;
                }
            }

            nextroot %= 12;
        }

        --t;

        drums();
        bass();

        beat = (beat + 1) % time_sig;
    }
}

void pgmusic_end(void)
{
    playdate->sound->synth->freeSynth(drumSynth);
    playdate->sound->synth->freeSynth(bassSynth);
}
