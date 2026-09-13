#include "categories_scene.h"

#include "../app.h"
#include "library_scene.h"
#include "modal.h"

enum
{
    EDIT_ROW_NAME,
    EDIT_ROW_ICON,
    EDIT_ROW_DELETE,
    EDIT_ROW_SEPARATOR,
    EDIT_ROW_ROMS
};

static bool category_is_visible(const RomCategory* cat)
{
#ifndef CRANKBOY_OFFICIAL_CATALOG
    if (cat->requires_catalog)
        return false;
#endif
    return true;
}

static void rebuild_list(CB_CategoriesScene* self)
{
    CB_Array* items = self->listView->items;

    for (RomCategory** it = CB_App->romcategories; it && *it; ++it)
    {
        RomCategory* cat = *it;
        if (!category_is_visible(cat))
            continue;

        // fixed categories are only turned on and off, so they show as checklist items
        if (cat->type != ROMCAT_STANDARD)
        {
            CB_ListItemCheckbox* checkbox = CB_ListItemCheckbox_new(cat->name);
            checkbox->ud.ptr = cat;
            checkbox->checked = cat->enabled;
            array_push(items, checkbox);
        }
        else
        {
            CB_ListItemButton* button = CB_ListItemButton_new(cat->name);
            button->ud.ptr = cat;
            array_push(items, button);
        }
    }

    CB_ListItemButton* button = CB_ListItemButton_new(T(cat_new));
    button->ud.ptr = NULL;
    array_push(items, button);
}

static int gamename_index(const CB_GameName* names)
{
    for (int i = 0; i < CB_App->gameNameCache->length; ++i)
    {
        if (CB_App->gameNameCache->items[i] == names)
            return i;
    }
    return -1;
}

static void rebuild_edit(CB_CategoriesScene* self)
{
    CB_Array* items = self->listView->items;

    array_push(items, CB_ListItemButton_new(self->editing->name));
    array_push(items, CB_ListItemButton_new(T(cat_icon)));
    array_push(items, CB_ListItemButton_new(T(cat_delete)));

    CB_ListItemButton* separator = CB_ListItemButton_new(T(cat_roms));
    separator->is_header = true;
    separator->unselectable = true;
    array_push(items, separator);

    CB_Array* games = CB_App->gameListCache;
    for (int i = 0; games && i < games->length; ++i)
    {
        CB_Game* game = games->items[i];
        int index = gamename_index(game->names);
        if (index < 0)
            continue;

        CB_ListItemCheckbox* checkbox = CB_ListItemCheckbox_new(game->displayName);
        checkbox->ud.uint = (uintptr_t)index;
        checkbox->checked = romcategory_contains(self->editing, index);
        array_push(items, checkbox);
    }
}

static void rebuild(CB_CategoriesScene* self)
{
    int selected = self->listView->selectedItem;
    CB_ListView_clear(self->listView);

    if (self->state == CATSCENE_EDIT)
    {
        rebuild_edit(self);
        selected = EDIT_ROW_NAME;
    }
    else
    {
        rebuild_list(self);
    }

    self->listView->selectedItem = selected;
    CB_ListView_reload(self->listView);
}

static void draw(CB_CategoriesScene* self)
{
    self->listView->needsDisplay = true;
    CB_ListView_draw(self->listView);

    cb_draw_header(
        self->state == CATSCENE_EDIT
            ? T(cat_edit_header)
            : T(cat_header),
            CB_HEADER_HEIGHT
    );
    playdate->graphics->setDrawMode(kDrawModeCopy);
}

static void CB_CategoriesScene_update(void* object, uint32_t u32enc_dt)
{
    CB_CategoriesScene* self = object;

    draw(self);
}

static void CB_CategoriesScene_free(void* object)
{
    CB_CategoriesScene* self = object;

    CB_ListView_free(self->listView);
    CB_Scene_free(self->scene);
    cb_free(self);
}

CB_CategoriesScene* CB_CategoriesScene_new(void)
{
    if (!CB_App->romcategories)
    {
        CB_App->romcategories = romcategories_load_all(NULL);
        if (!CB_App->romcategories)
            return NULL;
    }

    CB_CategoriesScene* self = allocz(CB_CategoriesScene);
    if (!self)
        return NULL;

    CB_Scene* scene = CB_Scene_new();
    if (!scene)
    {
        cb_free(self);
        return NULL;
    }

    scene->id = "categories";
    scene->managedObject = self;
    scene->update = CB_CategoriesScene_update;
    scene->free = CB_CategoriesScene_free;

    self->scene = scene;
    self->state = CATSCENE_LIST;

    self->listView = CB_ListView_new();
    self->listView->font = CB_App->bodyFont;
    self->listView->textInset = 8;
    self->listView->frame =
        PDRectMake(0, CB_HEADER_HEIGHT, LCD_COLUMNS, LCD_ROWS - CB_HEADER_HEIGHT);

    rebuild(self);

    return self;
}
