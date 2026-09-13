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

#define CB_CATEGORIES_HEADER_GAP 4

static const char* category_display_name(const RomCategory* cat)
{
    return cat->name[0] ? cat->name : T(cat_default_name);
}

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

        if (cat->type != ROMCAT_STANDARD)
        {
            CB_ListItemCheckbox* checkbox = CB_ListItemCheckbox_new(category_display_name(cat));
            checkbox->ud.ptr = cat;
            checkbox->checked = cat->enabled;
            array_push(items, checkbox);
        }
        else
        {
            CB_ListItemButton* button = CB_ListItemButton_new(category_display_name(cat));
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

    array_push(items, CB_ListItemButton_new(category_display_name(self->editing)));
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

    playdate->graphics->fillRect(
        0, CB_HEADER_HEIGHT, LCD_COLUMNS, CB_CATEGORIES_HEADER_GAP, kColorWhite
    );

    cb_draw_header(
        self->state == CATSCENE_EDIT
            ? T(cat_edit_header)
            : T(cat_header),
            CB_HEADER_HEIGHT
    );
    playdate->graphics->setDrawMode(kDrawModeCopy);
}

static void enter_edit(CB_CategoriesScene* self, RomCategory* cat)
{
    self->editing = cat;
    self->state = CATSCENE_EDIT;
    rebuild(self);
    cb_play_ui_sound(CB_UISound_Confirm);
}

static void create_category(CB_CategoriesScene* self)
{
    RomCategory* cat = romcategory_new(ROMCAT_STANDARD, T(cat_default_name));
    if (!cat)
        return;

    romcategories_append(&CB_App->romcategories, cat);
    self->dirty = true;
    enter_edit(self, cat);
}

#ifdef CRANKBOY_PDKEYBOARD
static void open_name_keyboard(CB_CategoriesScene* self)
{
    PDKeyboard* kb = CB_init_keyboard(PDKBF_DEFAULT, NULL, NULL);
    if (!kb)
        return;

    pdkb_set_max_bytes(kb, MAX_CATEGORY_NAME - 1);
    const char* name = self->editing->name;
    pdkb_set_content(kb, strcmp(name, T(cat_default_name)) == 0 ? "" : name);
    pdkb_open(kb);

    self->keyboard = kb;
    cb_play_ui_sound(CB_UISound_Confirm);
}

static bool update_keyboard(CB_CategoriesScene* self, float dt)
{
    if (!self->keyboard)
        return false;

    if (pdkb_get_state(self->keyboard) == PDKBS_OPEN)
    {
        int result = pdkb_get_result(self->keyboard);

        if (result > 0)
        {
            const char* content = pdkb_get_content(self->keyboard);
            char* name = self->editing->name;

            if (content && *content)
                snprintf(name, MAX_CATEGORY_NAME, "%s", content);
            else if (!name[0])
                snprintf(name, MAX_CATEGORY_NAME, "%s", T(cat_default_name));

            self->dirty = true;
            rebuild(self);
        }

        if (result != 0)
            pdkb_close(self->keyboard);
    }

    pdkb_update(self->keyboard, dt);

    if (pdkb_get_state(self->keyboard) == PDKBS_CLOSED)
        self->keyboard = NULL;

    return true;
}
#endif

static void delete_confirmed(void* ud, int option)
{
    CB_CategoriesScene* self = ud;

    if (option != 1 || !self->editing)
        return;

    romcategories_remove(&CB_App->romcategories, self->editing);
    self->editing = NULL;
    self->state = CATSCENE_LIST;
    self->dirty = true;
    self->needs_rebuild = true;
}

static void confirm_delete(CB_CategoriesScene* self)
{
    const char* yes_no_options[3];
    yes_no_options[0] = T(label_no);
    yes_no_options[1] = T(label_yes);
    yes_no_options[2] = NULL;

    char* msg = aprintf(T(cat_delete_confirm), category_display_name(self->editing));
    if (!msg)
        return;

    CB_Modal* modal = CB_Modal_new(msg, yes_no_options, delete_confirmed, self);
    cb_free(msg);

    if (modal)
    {
        cb_play_ui_sound(CB_UISound_Confirm);
        CB_presentModal(modal->scene);
    }
}

static void toggle_selected(CB_CategoriesScene* self)
{
    CB_ListView* listView = self->listView;
    int sel = listView->selectedItem;
    if (sel < 0 || sel >= listView->items->length)
        return;

    CB_ListItem* item = listView->items->items[sel];

    if (item->type == CB_ListViewItemTypeButton && self->state == CATSCENE_LIST)
    {
        CB_ListItemButton* button = (CB_ListItemButton*)item;
        if (button->ud.ptr)
            enter_edit(self, button->ud.ptr);
        else
            create_category(self);
        return;
    }

    if (item->type == CB_ListViewItemTypeButton && self->state == CATSCENE_EDIT)
    {
        if (sel == EDIT_ROW_NAME)
        {
#ifdef CRANKBOY_PDKEYBOARD
            open_name_keyboard(self);
#endif
            return;
        }
        if (sel == EDIT_ROW_DELETE)
        {
            confirm_delete(self);
            return;
        }
    }

    if (item->type != CB_ListViewItemTypeCheckbox)
        return;

    CB_ListItemCheckbox* checkbox = (CB_ListItemCheckbox*)item;
    RomCategory* cat = checkbox->ud.ptr;
    if (!cat)
        return;

    cat->enabled = !cat->enabled;
    checkbox->checked = cat->enabled;
    self->dirty = true;
    listView->needsDisplay = true;
    cb_play_ui_sound(CB_UISound_Confirm);
}

static void CB_CategoriesScene_update(void* object, uint32_t u32enc_dt)
{
    CB_CategoriesScene* self = object;
    float dt = UINT32_AS_FLOAT(u32enc_dt);

    if (self->needs_rebuild)
    {
        self->needs_rebuild = false;
        rebuild(self);
    }

    if (self->dismiss)
    {
        if (self->dirty)
        {
            romcategories_write_all(CB_App->romcategories);
            self->dirty = false;
        }
        CB_dismiss(self->scene);
        return;
    }

#ifdef CRANKBOY_PDKEYBOARD
    if (self->keyboard)
    {
        draw(self);
        update_keyboard(self, dt);
        return;
    }
#endif

    if (CB_App->buttons_pressed & kButtonA)
    {
        toggle_selected(self);
    }
    else if (CB_App->buttons_pressed & kButtonB)
    {
        if (self->state == CATSCENE_EDIT)
        {
            self->state = CATSCENE_LIST;
            self->editing = NULL;
            rebuild(self);
        }
        else
        {
            self->dismiss = true;
        }
    }

    CB_ListView_update(self->listView);

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
    self->listView->frame = PDRectMake(
        0, CB_HEADER_HEIGHT + CB_CATEGORIES_HEADER_GAP, LCD_COLUMNS,
        LCD_ROWS - CB_HEADER_HEIGHT - CB_CATEGORIES_HEADER_GAP
    );

    rebuild(self);

    return self;
}
