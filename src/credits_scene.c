#include "credits_scene.h"

#include "app.h"
#include "jparse.h"
#include "pgmusic.h"
#include "version.h"

#include <stdlib.h>
#include <time.h>

// pixels per second
#define AUTO_SCROLL_RATE 20.3f

// how long to wait before starting to scroll
#define INITIAL_WAIT 0.8f

// pixels per degree
#define CRANK_RATE 1.1f

static void shuffle_array(JsonArray* array)
{
    for (size_t i = 1; i < array->n; ++i)
    {
        size_t swap_pos = rand() % (i + 1);
        if (swap_pos != i)
        {
            json_value tmp = array->data[i];
            array->data[i] = array->data[swap_pos];
            array->data[swap_pos] = tmp;
        }
    }
}

static void PGB_CreditsScene_didSelectBack(void* userdata)
{
    PGB_CreditsScene* creditsScene = userdata;
    creditsScene->shouldDismiss = true;
}

static void PGB_CreditsScene_update(void* object, uint32_t u32enc_dt)
{
    PGB_CreditsScene* creditsScene = object;
    JsonArray* carray = creditsScene->jcred.data.arrayval;

    if (creditsScene->shouldDismiss)
    {
        PGB_dismiss(creditsScene->scene);
        return;
    }

    playdate->graphics->clear(kColorWhite);

    int margin = 12;
    int width = LCD_COLUMNS - margin * 2;
    int tracking = 0;
    int extraLeading = 0;

    int space_after_each = 24;

    float dt = UINT32_AS_FLOAT(u32enc_dt);
    pgmusic_update(dt);
    int HEADER_SPACE = 48;
    int FOOTER_SPACE = 48;
    int y = HEADER_SPACE - creditsScene->scroll;
    int entry_spacing = 8;

    for (size_t i = 0; i < carray->n; ++i)
    {
        json_value entry = carray->data[i];
        if (entry.type != kJSONTable)
            continue;

        if (y >= LCD_ROWS)
            break;

        if (creditsScene->y_advance_by_item && creditsScene->y_advance_by_item[i] >= 0)
        {
            if (y + creditsScene->y_advance_by_item[i] <= 0)
            {
                y += creditsScene->y_advance_by_item[i];
                continue;
            }
        }

        const bool first_visit = creditsScene->y_advance_by_item[i] < 0;

        creditsScene->y_advance_by_item[i] = 0;

#define ADVANCE(i, x)                                 \
    do                                                \
    {                                                 \
        int py = y;                                   \
        y += (x);                                     \
        creditsScene->y_advance_by_item[i] += y - py; \
    } while (0)

        json_value section = json_get_table_value(entry, "section");
        if (section.type == kJSONString)
        {
            json_value contributors = json_get_table_value(entry, "contributors");
            json_value subtitle = json_get_table_value(entry, "subtitle");

            // title
            {
                playdate->graphics->setFont(PGB_App->titleFont);
                int advance = playdate->graphics->getTextHeightForMaxWidth(
                    PGB_App->titleFont, section.data.stringval, strlen(section.data.stringval),
                    width, kUTF8Encoding, kWrapWord, tracking, extraLeading
                );

                playdate->graphics->drawTextInRect(
                    section.data.stringval, strlen(section.data.stringval), kUTF8Encoding, margin,
                    y, width, advance * 10, kWrapWord, kAlignTextCenter
                );
                ADVANCE(i, advance);
            }

            // subtitle
            if (subtitle.type == kJSONString)
            {
                playdate->graphics->setFont(PGB_App->labelFont);
                int advance = playdate->graphics->getTextHeightForMaxWidth(
                    PGB_App->labelFont, subtitle.data.stringval, strlen(subtitle.data.stringval),
                    width, kUTF8Encoding, kWrapWord, tracking, extraLeading
                );

                playdate->graphics->drawTextInRect(
                    subtitle.data.stringval, strlen(subtitle.data.stringval), kUTF8Encoding, margin,
                    y, width, advance * 10, kWrapWord, kAlignTextCenter
                );
                ADVANCE(i, advance);
            }

            // contributors
            if (contributors.type == kJSONArray && contributors.data.arrayval)
            {
                JsonArray* contsObj = contributors.data.arrayval;
                if (first_visit)
                    shuffle_array(contsObj);

                for (size_t j = 0; j < contsObj->n; ++j)
                {
                    json_value contributor = contsObj->data[j];

                    playdate->graphics->setFont(PGB_App->bodyFont);
                    int advance = playdate->graphics->getTextHeightForMaxWidth(
                        PGB_App->bodyFont, contributor.data.stringval,
                        strlen(contributor.data.stringval), width, kUTF8Encoding, kWrapWord,
                        tracking, extraLeading
                    );

                    ADVANCE(i, entry_spacing);
                    playdate->graphics->drawTextInRect(
                        contributor.data.stringval, strlen(contributor.data.stringval),
                        kUTF8Encoding, margin, y, width, advance * 10, kWrapWord, kAlignTextCenter
                    );
                    ADVANCE(i, advance);
                }
            }
        }

        // info
        json_value info = json_get_table_value(entry, "info");
        if (info.type == kJSONArray)
        {
            JsonArray* infoArr = info.data.tableval;
            for (size_t j = 0; j < infoArr->n; ++j)
            {
                json_value line = infoArr->data[j];
                if (line.type != kJSONString)
                    continue;
                if (line.data.stringval[0] == 0)
                {
                    ADVANCE(i, 10);
                }
                else
                {
                    playdate->graphics->setFont(PGB_App->labelFont);
                    int advance =
                        playdate->graphics->getTextHeightForMaxWidth(
                            PGB_App->labelFont, line.data.stringval, strlen(line.data.stringval),
                            width, kUTF8Encoding, kWrapWord, tracking, extraLeading
                        ) +
                        1;

                    playdate->graphics->drawTextInRect(
                        line.data.stringval, strlen(line.data.stringval), kUTF8Encoding, margin, y,
                        width, advance * 10, kWrapWord, kAlignTextLeft
                    );
                    ADVANCE(i, advance);
                }
            }
        }

        json_value logo = json_get_table_value(entry, "logo");
        if (logo.type == kJSONTrue)
        {
            playdate->graphics->setFont(PGB_App->labelFont);
            const char* version = get_current_version();

            if (version)
            {
                playdate->graphics->drawTextInRect(
                    version, strlen(version), kUTF8Encoding, margin + creditsScene->scroll * 8,
                    margin, width, 100, kWrapWord, kAlignTextRight
                );
            }

            if (creditsScene->logo)
            {
                playdate->graphics->setDrawMode(kDrawModeCopy);
                int lwidth, lheight;
                playdate->graphics->getBitmapData(
                    creditsScene->logo, &lwidth, &lheight, NULL, NULL, NULL
                );
                playdate->graphics->drawBitmap(
                    creditsScene->logo, (LCD_COLUMNS - lwidth) / 2, y, kBitmapUnflipped
                );
                ADVANCE(i, lheight + 24);
            }
        }

        if (i + 1 != carray->n)
            ADVANCE(i, space_after_each);
    }

    int credits_height = y + FOOTER_SPACE + creditsScene->scroll;

    creditsScene->initial_wait += dt;

    if (playdate->system->isCrankDocked())
    {
        if (creditsScene->initial_wait > INITIAL_WAIT)
        {
            creditsScene->time += dt * 0.5f;
            creditsScene->scroll +=
                AUTO_SCROLL_RATE * dt * (creditsScene->time > 1 ? 1 : creditsScene->time);
            creditsScene->time += dt * 0.5f;
        }
    }
    else
    {
        creditsScene->time = 0;
        creditsScene->scroll += playdate->system->getCrankChange() * CRANK_RATE;
    }

    creditsScene->scroll = MAX(0, creditsScene->scroll);

    if (creditsScene->scroll + LCD_ROWS > credits_height)
    {
        creditsScene->scroll = credits_height - LCD_ROWS;
    }

    if (PGB_App->buttons_pressed & kButtonB)
    {
        creditsScene->shouldDismiss = true;
    }
}

static void PGB_CreditsScene_menu(void* object)
{
    PGB_CreditsScene* creditsScene = object;
    playdate->system->removeAllMenuItems();

    if (!PGB_App->bundled_rom)
    {
        playdate->system->addMenuItem("Library", PGB_CreditsScene_didSelectBack, creditsScene);
    }
    else
    {
        if (preferences_bundle_hidden != (preferences_bitfield_t)-1)
        {
            // Back to settings
            playdate->system->addMenuItem("Back", PGB_CreditsScene_didSelectBack, creditsScene);
        }
        else
        {
            // Back to game
            playdate->system->addMenuItem("Resume", PGB_CreditsScene_didSelectBack, creditsScene);
        }
    }
}

static void PGB_CreditsScene_free(void* object)
{
    pgmusic_end();
    PGB_CreditsScene* creditsScene = object;
    PGB_Scene_free(creditsScene->scene);
    free_json_data(creditsScene->jcred);
    if (creditsScene->logo)
        playdate->graphics->freeBitmap(creditsScene->logo);
    free(creditsScene);
}

PGB_CreditsScene* PGB_CreditsScene_new(void)
{
    pgmusic_begin();
    playdate->system->getCrankChange();
    PGB_CreditsScene* creditsScene = pgb_malloc(sizeof(PGB_CreditsScene));
    if (!creditsScene)
        return NULL;
    memset(creditsScene, 0, sizeof(*creditsScene));
    creditsScene->scroll = 0.0f;
    creditsScene->time = 0.0f;

    PGB_Scene* scene = PGB_Scene_new();
    if (!scene)
    {
        free(creditsScene);
        return NULL;
    }

    scene->managedObject = creditsScene;
    scene->update = PGB_CreditsScene_update;
    scene->free = PGB_CreditsScene_free;
    scene->menu = PGB_CreditsScene_menu;

    creditsScene->scene = scene;
    creditsScene->logo = playdate->graphics->loadBitmap("images/logo", NULL);

    json_value j;
    int result = parse_json("./credits.json", &j, kFileRead | kFileReadData);
    if (!result || j.type != kJSONArray)
    {
        free_json_data(j);
        free(creditsScene);
        free(scene);
        return NULL;
    }
    creditsScene->jcred = j;

    creditsScene->y_advance_by_item = malloc(sizeof(int) * ((JsonArray*)j.data.tableval)->n);
    if (creditsScene->y_advance_by_item)
    {
        for (size_t i = 0; i < ((JsonArray*)j.data.tableval)->n; ++i)
        {
            creditsScene->y_advance_by_item[i] = -1;
        }
    }

    return creditsScene;
}

void PGB_showCredits(void* userdata)
{
    PGB_CreditsScene* creditsScene = PGB_CreditsScene_new();
    PGB_presentModal(creditsScene->scene);
}