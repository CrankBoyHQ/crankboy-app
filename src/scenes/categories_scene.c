#include "categories_scene.h"

#include "../app.h"
#include "library_scene.h"
#include "modal.h"

enum
{
    EDIT_ROW_NAME,
    EDIT_ROW_ENABLED,
    // EDIT_ROW_ICON,  // icon system not implemented yet
    EDIT_ROW_DELETE,
    EDIT_ROW_SEPARATOR,
    EDIT_ROW_ROMS
};

#define CB_CATEGORIES_HEADER_GAP 4

#ifdef CRANKBOY_PDKEYBOARD
static void draw_name_cursor(CB_CategoriesScene* self);
static void open_name_keyboard(CB_CategoriesScene* self, bool play_sound);
static void update_name_field(CB_CategoriesScene* self);
#endif

static const char* category_edit_title(const RomCategory* cat)
{
    // empty = still-default -> keyboard field starts cleared
    return cat->name;
}

static void rebuild_list(CB_CategoriesScene* self)
{
    CB_Array* items = self->listView->items;

    CB_ListItemButton* add_button = CB_ListItemButton_new(T(cat_new));
    add_button->ud.ptr = NULL;
    array_push(items, add_button);

    // divider between the add row and the category list
    CB_ListItemButton* divider = CB_ListItemButton_new("");
    divider->is_header = true;
    divider->unselectable = true;
    array_push(items, divider);

    // one reorderable block in array order (user categories and genres);
    // hidden fixed categories are skipped; Packed only when it has games
    for (RomCategory** it = CB_App->romcategories; it && *it; ++it)
    {
        RomCategory* cat = *it;

        if (!romcategory_is_listed(cat) ||
            (cat->type == ROMCAT_PACKED && romcategory_count(cat) == 0))
            continue;

        size_t count = cat->type == ROMCAT_ALL ? (size_t)CB_App->gameListCache->length
                                               : romcategory_count(cat);

        char* title = aprintf("%s (%d)", romcategory_display_name(cat), (int)count);
        CB_ListItemCheckbox* checkbox = CB_ListItemCheckbox_new(title);
        cb_free(title);
        checkbox->ud.ptr = cat;
        checkbox->checked = cat->enabled;
        array_push(items, checkbox);
    }
}

static void rebuild_edit(CB_CategoriesScene* self)
{
    CB_Array* items = self->listView->items;

    array_push(items, CB_ListItemButton_new(romcategory_display_name(self->editing)));

    CB_ListItemCheckbox* enabled_cb = CB_ListItemCheckbox_new(T(cat_show_in_library));
    enabled_cb->checked = self->editing->enabled;
    array_push(items, enabled_cb);

    // array_push(items, CB_ListItemButton_new(T(cat_icon)));  // icon system TBD
    array_push(items, CB_ListItemButton_new(T(cat_delete)));

    CB_ListItemButton* separator = CB_ListItemButton_new(T(cat_roms));
    separator->is_header = true;
    separator->unselectable = true;
    array_push(items, separator);

    RomCategoryNameIndex* name_index = romcategory_name_index_build();

    CB_Array* games = CB_App->gameListCache;
    for (int i = 0; games && i < games->length; ++i)
    {
        CB_Game* game = games->items[i];
        int index = romcategory_name_index_lookup(name_index, game->names);
        if (index < 0)
            continue;

        CB_ListItemCheckbox* checkbox = CB_ListItemCheckbox_new(game->displayName);
        checkbox->ud.uint = (uintptr_t)index;
        checkbox->checked = romcategory_contains(self->editing, index);
        array_push(items, checkbox);
    }

    romcategory_name_index_free(name_index);
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

        if (self->select_on_rebuild)
        {
            // reselect the edited or created row
            for (int i = 0; i < self->listView->items->length; ++i)
            {
                CB_ListItem* item = self->listView->items->items[i];
                if (item->type == CB_ListViewItemTypeCheckbox &&
                    ((CB_ListItemCheckbox*)item)->ud.ptr == self->select_on_rebuild)
                {
                    selected = i;
                    break;
                }
            }
            self->select_on_rebuild = NULL;
        }
    }

    self->listView->selectedItem = selected;
    CB_ListView_reload(self->listView);
}

static void draw(CB_CategoriesScene* self)
{
    self->listView->needsDisplay = true;
    CB_ListView_draw(self->listView);

    // Chevron marks editable rows only.
    if (self->state == CATSCENE_LIST)
    {
        CB_ListView* listView = self->listView;
        LCDFont* font = listView->font ? listView->font : CB_App->bodyFont;
        playdate->graphics->setFont(font);
        int font_h = playdate->graphics->getFontHeight(font);
        int arrow_w = playdate->graphics->getTextWidth(font, "›", 1, kUTF8Encoding, 0);

        for (int i = 0; i < listView->items->length; ++i)
        {
            CB_ListItem* item = listView->items->items[i];
            if (item->type != CB_ListViewItemTypeCheckbox)
                continue;

            CB_ListItemCheckbox* checkbox = (CB_ListItemCheckbox*)item;
            RomCategory* cat = checkbox->ud.ptr;
            if (!cat || cat->type != ROMCAT_STANDARD)
                continue;

            int rowY = listView->frame.y + item->offsetY - listView->contentOffset;
            if (rowY + item->height < listView->frame.y)
                continue;
            if (rowY > listView->frame.y + listView->frame.height)
                break;

            bool selected = (i == listView->selectedItem);
            playdate->graphics->setDrawMode(selected ? kDrawModeFillWhite : kDrawModeFillBlack);
            playdate->graphics->drawText(
                "›", 1, kUTF8Encoding, listView->frame.x + listView->frame.width - arrow_w - 6,
                rowY + (item->height - font_h) / 2
            );
        }

        playdate->graphics->setDrawMode(kDrawModeCopy);
    }

#ifdef CRANKBOY_PDKEYBOARD
    draw_name_cursor(self);
#endif

    playdate->graphics->fillRect(
        0, CB_HEADER_HEIGHT, LCD_COLUMNS, CB_CATEGORIES_HEADER_GAP, kColorWhite
    );

    cb_draw_header(
        self->state == CATSCENE_EDIT ? T(cat_edit_header) : T(cat_header), CB_HEADER_HEIGHT
    );
    playdate->graphics->setDrawMode(kDrawModeCopy);
}

static void enter_edit(CB_CategoriesScene* self, RomCategory* cat)
{
    self->editing = cat;
    self->select_on_rebuild = NULL;
    self->drag = (CB_ListViewDragState){0};
    self->listView->ignoreButtons = false;
    self->listView->checkboxDrag = false;
    self->state = CATSCENE_EDIT;
    rebuild(self);
    cb_play_ui_sound(CB_UISound_Confirm);
}
static void create_category(CB_CategoriesScene* self)
{
    // empty name: display falls back to the localized default
    RomCategory* cat = romcategory_new(ROMCAT_STANDARD, NULL);
    if (!cat)
        return;

    // appends at the block end; the user drags it from there
    if (!romcategories_append(&CB_App->romcategories, cat))
        return;

    self->dirty = true;
    enter_edit(self, cat);

#ifdef CRANKBOY_PDKEYBOARD
    open_name_keyboard(self, false);
    update_name_field(self);
#endif
}

#ifdef CRANKBOY_PDKEYBOARD
static void draw_name_cursor(CB_CategoriesScene* self)
{
    if (!self->keyboard || self->state != CATSCENE_EDIT)
        return;

    CB_ListView* listView = self->listView;
    CB_ListItemButton* button = listView->items->items[EDIT_ROW_NAME];

    int font_h = playdate->graphics->getFontHeight(listView->font);
    int row_h = button->item.height;
    int row_y = listView->frame.y + button->item.offsetY - listView->contentOffset;
    int text_x = listView->frame.x + listView->textInset + button->textScrollOffset;
    int text_w = playdate->graphics->getTextWidth(
        listView->font, button->title, strlen(button->title), kUTF8Encoding, 0
    );
    int cursor_x = text_x + text_w + 2;

    if (cursor_x + 2 > listView->frame.x + listView->frame.width - listView->textInset)
        return;

    if ((playdate->system->getCurrentTimeMilliseconds() / 500) % 2 == 0)
        return;

    playdate->graphics->fillRect(cursor_x, row_y + (row_h - font_h) / 2, 2, font_h, kColorWhite);
    playdate->graphics->setDrawMode(kDrawModeCopy);
}

static void open_name_keyboard(CB_CategoriesScene* self, bool play_sound)
{
    PDKeyboard* kb = CB_init_keyboard(PDKBF_DEFAULT, NULL, NULL);
    if (!kb)
        return;

    pdkb_set_max_bytes(kb, MAX_CATEGORY_NAME - 1);
    pdkb_set_content(kb, category_edit_title(self->editing));
    pdkb_open(kb);

    self->keyboard = kb;
    self->keyboard_result_handled = false;
    if (play_sound)
        cb_play_ui_sound(CB_UISound_Confirm);
}

static void update_name_field(CB_CategoriesScene* self)
{
    if (self->state != CATSCENE_EDIT || !self->keyboard || self->keyboard_result_handled)
        return;

    CB_ListItemButton* button = self->listView->items->items[EDIT_ROW_NAME];
    const char* content = pdkb_get_content(self->keyboard);
    const char* text = (content && *content) ? content : "";

    if (strcmp(button->title, text) != 0)
    {
        cb_free(button->title);
        button->title = cb_strdup(text);
        self->listView->needsDisplay = true;
    }
}

// only duplicate user-category names are blocked
static bool category_name_is_taken(const CB_CategoriesScene* self, const char* name)
{
    if (!name || !*name)
        return false;

    for (RomCategory** it = CB_App->romcategories; it && *it; ++it)
    {
        if (*it == self->editing)  // own current name is fine
            continue;
        if ((*it)->type == ROMCAT_STANDARD && strcmp((*it)->name, name) == 0)
            return true;
    }
    return false;
}

// unique auto-numbered name into out; true when adjusted
static bool category_name_make_unique(
    const CB_CategoriesScene* self, const char* content, char out[MAX_CATEGORY_NAME]
)
{
    snprintf(out, MAX_CATEGORY_NAME, "%s", content);
    if (!category_name_is_taken(self, out))
        return false;

    // 16 chars + NUL: room for "_999" + NUL in the out buffer below
    char base[MAX_CATEGORY_NAME - 4];
    snprintf(base, sizeof(base), "%s", content);

    for (int n = 1; n <= 999; ++n)
    {
        snprintf(out, MAX_CATEGORY_NAME, "%s_%d", base, n);
        if (!category_name_is_taken(self, out))
            return true;
    }

    snprintf(out, MAX_CATEGORY_NAME, "%s", base);
    return true;
}

static void update_keyboard(CB_CategoriesScene* self, float dt)
{
    if (!self->keyboard)
        return;

    if (!self->keyboard_result_handled && pdkb_get_result(self->keyboard) != 0)
    {
        self->keyboard_result_handled = true;

        if (pdkb_get_result(self->keyboard) > 0)
        {
            const char* content = pdkb_get_content(self->keyboard);

            if (content && *content)
            {
                char adjusted[MAX_CATEGORY_NAME];
                bool changed = category_name_make_unique(self, content, adjusted);
                snprintf(self->editing->name, MAX_CATEGORY_NAME, "%s", adjusted);
                self->dirty = true;

                if (changed)
                    self->pending_name_notice = cb_strdup(adjusted);
            }
            else
            {
                self->editing->name[0] = '\0';  // empty -> display fallback
                self->dirty = true;
            }
        }

        self->needs_rebuild = true;
    }

    update_name_field(self);

    pdkb_update(self->keyboard, dt);

    if (pdkb_get_state(self->keyboard) == PDKBS_CLOSED)
        self->keyboard = NULL;
}
#endif

static void delete_confirmed(void* ud, int option)
{
    CB_CategoriesScene* self = ud;

    if (option != 1 || !self->editing)
        return;

    romcategories_remove(&CB_App->romcategories, self->editing);
    self->editing = NULL;
    self->select_on_rebuild = NULL;
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

    char* msg = aprintf(T(cat_delete_confirm), romcategory_display_name(self->editing));
    if (!msg)
        return;

    CB_Modal* modal = CB_Modal_new(msg, yes_no_options, delete_confirmed, self);
    cb_free(msg);

    if (modal)
    {
        cb_play_ui_sound(CB_UISound_Confirm);
        CB_presentModal(modal->scene);
        self->modal_input_guard = true;
    }
}

static int category_index(const RomCategory* cat)
{
    if (!CB_App->romcategories)
        return -1;
    for (RomCategory** it = CB_App->romcategories; *it; ++it)
    {
        if (*it == cat)
            return (int)(it - CB_App->romcategories);
    }
    return -1;
}

static void reorder_selected(CB_CategoriesScene* self, int ydir)
{
    CB_ListView* listView = self->listView;
    int sel = listView->selectedItem;
    int len = listView->items->length;
    int other = sel + ydir;
    if (sel < 0 || sel >= len || other < 0 || other >= len)
        return;

    CB_ListItemCheckbox* a = listView->items->items[sel];
    CB_ListItemCheckbox* b = listView->items->items[other];
    RomCategory* ca = a->ud.ptr;
    RomCategory* cb_cat = b->ud.ptr;
    if (!ca || !cb_cat)
        return;
    // movable = everything except the packed category; the add row ends the block
    bool movable_a = ca->type != ROMCAT_PACKED;
    bool movable_b = cb_cat->type != ROMCAT_PACKED;
    if (!movable_a || !movable_b)
        return;

    int ia = category_index(ca);
    int ib = category_index(cb_cat);
    if (ia < 0 || ib < 0)
        return;

    memswap(&CB_App->romcategories[ia], &CB_App->romcategories[ib], sizeof(RomCategory*));

    CB_ListItemCheckbox_swap(a, b);

    CB_ListView_selectItem(listView, other, true);
    self->dirty = true;
    cb_play_ui_sound(CB_UISound_Navigate);
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
            open_name_keyboard(self, true);
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

    if (self->state == CATSCENE_EDIT)
    {
        if (sel == EDIT_ROW_ENABLED)
        {
            self->editing->enabled = !self->editing->enabled;
            checkbox->checked = self->editing->enabled;
            self->dirty = true;
            listView->needsDisplay = true;
            cb_play_ui_sound(CB_UISound_Confirm);
            return;
        }

        size_t index = (size_t)checkbox->ud.uint;
        bool contains = romcategory_contains(self->editing, index);
        romcategory_put(self->editing, index, !contains);
        checkbox->checked = !contains;

        self->dirty = true;
        listView->needsDisplay = true;
        cb_play_ui_sound(CB_UISound_Confirm);
        return;
    }

    RomCategory* cat = checkbox->ud.ptr;
    if (!cat)
        return;

    if (cat->type == ROMCAT_STANDARD)
    {
        // the list checkbox is a status indicator; A opens the edit view
        enter_edit(self, cat);
        return;
    }

    // All + genre rows: checkbox toggles visibility in the library cycle
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
    CB_ListView* listView = self->listView;

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

    // keyboard closed: show the auto-number notice modal, if any
    if (self->pending_name_notice)
    {
        char* msg = aprintf(T(cat_name_taken), self->pending_name_notice);
        cb_free(self->pending_name_notice);
        self->pending_name_notice = NULL;

        if (msg)
        {
            CB_Modal* modal = CB_Modal_new(msg, NULL, NULL, NULL);
            cb_free(msg);
            if (modal)
            {
                cb_play_ui_sound(CB_UISound_Confirm);
                CB_presentModal(modal->scene);
                self->modal_input_guard = true;
            }
        }
    }

    if (self->modal_input_guard && CB_App->scene == self->scene)
    {
        self->modal_input_guard = false;
        CB_ListView_update(self->listView);
        draw(self);
        return;
    }

    if (self->state == CATSCENE_LIST)
    {
        int ydir = 0;
        bool short_tap = CB_ListView_drag_update(
            listView, &self->drag, dt, CB_App->buttons_down, CB_App->buttons_pressed,
            CB_App->buttons_released, &ydir
        );

        if (self->drag.dragging && ydir != 0)
            reorder_selected(self, ydir);

        if (short_tap)
            toggle_selected(self);

        if (CB_App->buttons_pressed & kButtonB)
            self->dismiss = true;
    }
    else
    {
        if (CB_App->buttons_pressed & kButtonA)
        {
            toggle_selected(self);
        }
        else if (CB_App->buttons_pressed & kButtonB)
        {
            self->select_on_rebuild = self->editing;
            self->drag = (CB_ListViewDragState){0};
            self->state = CATSCENE_LIST;
            self->editing = NULL;
            rebuild(self);
        }
    }

    CB_ListView_update(self->listView);

    draw(self);
}

static void CB_CategoriesScene_free(void* object)
{
    CB_CategoriesScene* self = object;

    cb_free(self->pending_name_notice);
    self->pending_name_notice = NULL;

    CB_ListView_free(self->listView);
    CB_Scene_free(self->scene);
    cb_free(self);
}

static void CB_CategoriesScene_menuLibrary(void* userdata)
{
    CB_CategoriesScene* self = userdata;
    self->dismiss = true;  // update saves dirty categories + dismisses
}

static void categories_show_genres_changed(void* userdata)
{
    CB_CategoriesScene* self = userdata;

    romcategories_set_genres_visible(!romcategories_genres_visible());
    self->needs_rebuild = true;  // editor list updates live
}

static void CB_CategoriesScene_menu(void* object)
{
    playdate->system->addMenuItem(T(pdmenu_library), CB_CategoriesScene_menuLibrary, object);
    playdate->system->addCheckmarkMenuItem(
        T(pdmenu_show_genres), romcategories_genres_visible() ? 1 : 0,
        categories_show_genres_changed, object
    );
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
    scene->menu = CB_CategoriesScene_menu;

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
