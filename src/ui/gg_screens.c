// gg_screens.c - the title, profile picker, name entry, options and pause menus.
#include "ui/gg_screens.h"
#include "gfx/gg_font.h"

static const SDL_Color INK   = { 226, 216, 190, 255 };
static const SDL_Color DIM   = { 138, 132, 116, 255 };
static const SDL_Color AMBER = { 217, 145,  63, 255 };
static const SDL_Color MOSS  = { 124, 184,  84, 255 };
static const SDL_Color WARN  = { 200,  96,  72, 255 };

#define TITLE_SCALE 4

// ---------------------------------------------------------------------------
// The on-screen alphabet
//
// Every character gg_profile_name_ok will accept and nothing else - 26 capitals,
// 26 lower case, 10 digits, space, hyphen, underscore and apostrophe. That is
// exactly 66, which lays out as a full rectangle 11 across and 6 down, so moving
// about it needs no special cases and no dead cells.
//
// Below the grid is one more row holding two wide keys, "Rub out" and "Begin".
// Keeping those off the grid is what makes the count come out even, and it puts
// the two destructive-or-final choices somewhere a thumb cannot reach by
// accident while spelling.
// ---------------------------------------------------------------------------
#define KEYS_COLS 11
#define KEYS_ROWS 6
#define KEY_CELL_W 30
#define KEY_CELL_H 24

static const char *const KEYS[KEYS_ROWS] = {
    "ABCDEFGHIJK",
    "LMNOPQRSTUV",
    "WXYZabcdefg",
    "hijklmnopqr",
    "stuvwxyz012",
    "3456789 -_'",
};

// The row past the last, and the two keys on it.
#define KEY_ROW_ENDS  KEYS_ROWS
#define KEY_END_ERASE 0
#define KEY_END_BEGIN 1

static char key_at(int row, int col) {
    if (row < 0 || row >= KEYS_ROWS || col < 0 || col >= KEYS_COLS) return '\0';
    return KEYS[row][col];
}

// What to paint in a cell. Space is the one character that would otherwise
// leave a key looking blank and broken.
static const char *key_label(char ch, char one[2]) {
    if (ch == ' ') return "sp";
    one[0] = ch;
    one[1] = '\0';
    return one;
}

// Rows on the profile screen, after the profiles themselves.
static int extra_row_new(const gg_screens *s)    { return s->profile_count; }
static int extra_row_delete(const gg_screens *s) { return s->profile_count + 1; }
static int extra_row_back(const gg_screens *s)   { return s->profile_count + 2; }

// ---------------------------------------------------------------------------
// Building each screen's menu
// ---------------------------------------------------------------------------
static void build_title(gg_screens *s, const char *base) {
    gg_profile list[GG_PROFILES_MAX];
    const int n = gg_profile_list(base, list, GG_PROFILES_MAX);
    const bool any = n > 0 && list[0].has_save;

    gg_menu_reset(&s->menu, nullptr);
    // "Continue" is first and pre-selected when there is something to
    // continue: the commonest thing a returning player wants should need no
    // keystrokes at all beyond Enter.
    if (any)
        gg_menu_add(&s->menu, true, "Continue", "%s - day %u, %02u:%02u, %s",
                    list[0].name, list[0].day, list[0].minutes / 60,
                    list[0].minutes % 60, list[0].place);
    else
        gg_menu_add(&s->menu, false, "Continue", "no saved journey");

    gg_menu_add(&s->menu, true, "New journey", nullptr);
    gg_menu_add(&s->menu, n > 0, "Journeys", n > 0 ? "%d saved" : "none yet", n);
    gg_menu_add(&s->menu, true, "Options", nullptr);
    gg_menu_add(&s->menu, true, "Quit", nullptr);
    gg_menu_select(&s->menu, any ? 0 : 1);
}

static void build_profiles(gg_screens *s, const char *base) {
    s->profile_count = gg_profile_list(base, s->profiles, GG_PROFILES_MAX);
    s->confirming_delete = false;

    gg_menu_reset(&s->menu, "Journeys");
    for (int i = 0; i < s->profile_count && i < GG_MENU_MAX - 3; i++) {
        const gg_profile *p = &s->profiles[i];
        gg_menu_add(&s->menu, p->has_save, p->name,
                    "day %u, %02u:%02u, %s - turn %u",
                    p->day, p->minutes / 60, p->minutes % 60, p->place, p->turns);
    }
    gg_menu_add(&s->menu, true, "New journey", nullptr);
    gg_menu_add(&s->menu, s->profile_count > 0, "Forget a journey", nullptr);
    gg_menu_add(&s->menu, true, "Back", nullptr);
    gg_menu_select(&s->menu, 0);
}

static void build_options(gg_screens *s, const gg_settings *set) {
    gg_menu_reset(&s->menu, "Options");
    gg_menu_add(&s->menu, true, "Window size", "%dx", set->scale);
    gg_menu_add(&s->menu, true, "Fullscreen", set->fullscreen ? "on" : "off");
    gg_menu_add(&s->menu, true, "Gamepad rumble", set->rumble ? "on" : "off");
    gg_menu_add(&s->menu, true, "Music", set->music > 0 ? "%d of 10" : "off",
                set->music);
    gg_menu_add(&s->menu, true, "Effects", set->effects > 0 ? "%d of 10" : "off",
                set->effects);
    gg_menu_add(&s->menu, true, "Back", nullptr);
    gg_menu_select(&s->menu, 0);
}

static void build_pause(gg_screens *s, const gg_game *g) {
    gg_menu_reset(&s->menu, "Paused");
    gg_menu_add(&s->menu, true, "Resume", nullptr);
    gg_menu_add(&s->menu, true, "Save", "%s - day %u, turn %u",
                g->profile, g->day, g->turn);
    gg_menu_add(&s->menu, true, "Options", nullptr);
    gg_menu_add(&s->menu, true, "Leave for the title", "saves first");
    gg_menu_add(&s->menu, true, "Quit", "saves first");
    gg_menu_select(&s->menu, 0);
}

static void build_name(gg_screens *s) {
    gg_menu_reset(&s->menu, "Name thy journey");
    s->typed[0] = '\0';
    s->menu.n = 0;
    s->key_row = -1;      // untouched: Enter still begins, as it always did
    s->key_col = 0;
}

void gg_screens_enter(gg_screens *s, gg_screen_id id, const char *base,
                      const gg_settings *set, const gg_game *g, bool have_game) {
    const gg_screen_id from = s->id;
    s->id = id;
    s->notice[0] = '\0';

    switch (id) {
    case GG_SCREEN_TITLE:    build_title(s, base); break;
    case GG_SCREEN_PROFILES: build_profiles(s, base); break;
    case GG_SCREEN_NAME:     build_name(s); break;
    case GG_SCREEN_OPTIONS:
        // Remembered on the way in, because on the way out it is too late to
        // know: everything else here has exactly one way back.
        s->options_from = (from == GG_SCREEN_PAUSE && have_game)
                        ? GG_SCREEN_PAUSE : GG_SCREEN_TITLE;
        build_options(s, set);
        break;
    case GG_SCREEN_PAUSE:
        if (have_game) build_pause(s, g);
        break;
    default:
        gg_menu_reset(&s->menu, nullptr);
        break;
    }
}

// The alphabet, which wraps in both directions and treats the two wide keys as
// one more row below the last.
static void move_alphabet(gg_screens *s, int dx, int dy) {
    if (s->key_row < 0) {
        // First touch lands on 'A' rather than jumping somewhere by the delta,
        // so the grid always opens in the same place.
        s->key_row = 0;
        s->key_col = 0;
        return;
    }

    if (dy) {
        const int was = s->key_row;
        s->key_row += dy > 0 ? 1 : -1;
        if (s->key_row > KEY_ROW_ENDS) s->key_row = 0;
        if (s->key_row < 0)            s->key_row = KEY_ROW_ENDS;

        // Eleven narrow keys and two wide ones do not line up, so crossing
        // between them keeps whichever side the cursor was already on.
        if (s->key_row == KEY_ROW_ENDS && was != KEY_ROW_ENDS)
            s->key_col = s->key_col < KEYS_COLS / 2 ? KEY_END_ERASE : KEY_END_BEGIN;
        else if (was == KEY_ROW_ENDS && s->key_row != KEY_ROW_ENDS)
            s->key_col = s->key_col == KEY_END_ERASE ? 0 : KEYS_COLS - 1;
    }
    if (dx) {
        const int wide = s->key_row == KEY_ROW_ENDS ? 2 : KEYS_COLS;
        s->key_col = (s->key_col + (dx > 0 ? 1 : -1) + wide) % wide;
    }
}

void gg_screens_move(gg_screens *s, int dx, int dy) {
    s->notice[0] = '\0';

    if (s->id == GG_SCREEN_NAME) {
        move_alphabet(s, dx, dy);
        return;
    }
    s->confirming_delete = false;
    if (dy) gg_menu_move(&s->menu, dy);
}

void gg_screens_ready(gg_screens *s) {
    if (s->id != GG_SCREEN_NAME) return;
    s->key_row = KEY_ROW_ENDS;
    s->key_col = KEY_END_BEGIN;
}

// ---------------------------------------------------------------------------
// Choosing
// ---------------------------------------------------------------------------
static gg_screen_result go(gg_screen_id next) {
    gg_screen_result r = { .action = GG_ACTION_GO, .next = next };
    return r;
}

static gg_screen_result nothing(void) {
    gg_screen_result r = { .action = GG_ACTION_NONE };
    return r;
}

static void cycle_option(gg_screens *s, gg_settings *set, int row) {
    switch (row) {
    case 0: set->scale = set->scale >= 4 ? 1 : set->scale + 1; break;
    case 1: set->fullscreen = !set->fullscreen; break;
    case 2: set->rumble = !set->rumble; break;
    // Round the loop rather than stopping at either end, so a player who wants
    // silence gets there by holding one direction like everything else here.
    case 3: set->music = set->music >= 10 ? 0 : set->music + 1; break;
    case 4: set->effects = set->effects >= 10 ? 0 : set->effects + 1; break;
    default: return;
    }
    const int keep = s->menu.cursor;
    build_options(s, set);
    gg_menu_select(&s->menu, keep);
}

void gg_screens_adjust(gg_screens *s, int dir, gg_settings *set) {
    if (s->id != GG_SCREEN_OPTIONS || dir == 0) return;
    const int row = gg_menu_chosen(&s->menu);
    // Everything but the last row, which is "Back" and has nothing to cycle.
    if (row < 0 || row >= s->menu.n - 1) return;

    s->notice[0] = '\0';
    if (dir > 0) {
        cycle_option(s, set, row);
        return;
    }
    // Backwards is the same cycle run the other way. Only "Window size" has
    // more than two states, so only it needs the arithmetic.
    // Backwards is the same cycle run the other way. Only the ones with more
    // than two states need the arithmetic; the toggles are their own inverse.
    if (row == 0)      set->scale = set->scale <= 1 ? 4 : set->scale - 1;
    else if (row == 3) set->music = set->music <= 0 ? 10 : set->music - 1;
    else if (row == 4) set->effects = set->effects <= 0 ? 10 : set->effects - 1;
    else               cycle_option(s, set, row);

    if (row == 0 || row == 3 || row == 4) {
        const int keep = s->menu.cursor;
        build_options(s, set);
        gg_menu_select(&s->menu, keep);
    }
}

gg_screen_result gg_screens_choose(gg_screens *s, const char *base,
                                   gg_settings *set, bool have_game) {
    const int row = gg_menu_chosen(&s->menu);

    switch (s->id) {
    case GG_SCREEN_TITLE:
        switch (row) {
        case 0: { gg_screen_result r = { .action = GG_ACTION_CONTINUE };
                  gg_profile list[GG_PROFILES_MAX];
                  if (gg_profile_list(base, list, GG_PROFILES_MAX) > 0)
                      SDL_strlcpy(r.name, list[0].name, sizeof r.name);
                  return r; }
        case 1: return go(GG_SCREEN_NAME);
        case 2: return go(GG_SCREEN_PROFILES);
        case 3: return go(GG_SCREEN_OPTIONS);
        case 4: { gg_screen_result r = { .action = GG_ACTION_QUIT }; return r; }
        default: return nothing();
        }

    case GG_SCREEN_PROFILES:
        if (row < 0) return nothing();
        if (row < s->profile_count) {
            if (s->confirming_delete) {
                gg_screen_result r = { .action = GG_ACTION_DELETE };
                SDL_strlcpy(r.name, s->profiles[row].name, sizeof r.name);
                return r;
            }
            gg_screen_result r = { .action = GG_ACTION_CONTINUE };
            SDL_strlcpy(r.name, s->profiles[row].name, sizeof r.name);
            return r;
        }
        if (row == extra_row_new(s))    return go(GG_SCREEN_NAME);
        if (row == extra_row_delete(s)) {
            // Arming rather than acting: choosing a journey now forgets it,
            // and the screen says so. One keypress must not be able to
            // destroy somebody's game.
            s->confirming_delete = true;
            SDL_strlcpy(s->notice, "Choose a journey to forget it. Esc to stop.",
                        sizeof s->notice);
            gg_menu_select(&s->menu, 0);
            return nothing();
        }
        if (row == extra_row_back(s))   return go(GG_SCREEN_TITLE);
        return nothing();

    case GG_SCREEN_OPTIONS:
        if (row == s->menu.n - 1) return go(s->options_from);
        cycle_option(s, set, row);
        return nothing();

    case GG_SCREEN_NAME: {
        // On the alphabet, choosing means typing. Only "Begin" - or an
        // untouched alphabet, which is the keyboard flow - starts the journey.
        if (s->key_row >= 0 && s->key_row < KEYS_ROWS) {
            gg_screens_type(s, key_at(s->key_row, s->key_col));
            return nothing();
        }
        if (s->key_row == KEY_ROW_ENDS && s->key_col == KEY_END_ERASE) {
            gg_screens_type(s, '\b');
            return nothing();
        }

        gg_screen_result r = { .action = GG_ACTION_NEW_GAME };
        if (!gg_profile_name_ok(s->typed)) {
            SDL_strlcpy(s->notice,
                        "Letters, digits, spaces, - and _ only, and not empty.",
                        sizeof s->notice);
            return nothing();
        }
        if (gg_save_exists(base, s->typed)) {
            SDL_snprintf(s->notice, sizeof s->notice,
                         "%s already has a journey. Pick another name.", s->typed);
            return nothing();
        }
        SDL_strlcpy(r.name, s->typed, sizeof r.name);
        return r;
    }

    case GG_SCREEN_PAUSE:
        if (!have_game) return go(GG_SCREEN_TITLE);
        switch (row) {
        case 0: return go(GG_SCREEN_PLAY);
        case 1: { gg_screen_result r = { .action = GG_ACTION_SAVE }; return r; }
        case 2: return go(GG_SCREEN_OPTIONS);
        case 3: { gg_screen_result r = { .action = GG_ACTION_QUIT_TO_TITLE }; return r; }
        case 4: { gg_screen_result r = { .action = GG_ACTION_QUIT }; return r; }
        default: return nothing();
        }

    default:
        return nothing();
    }
}

gg_screen_result gg_screens_back(gg_screens *s) {
    if (s->confirming_delete) {
        s->confirming_delete = false;
        s->notice[0] = '\0';
        return nothing();
    }
    switch (s->id) {
    case GG_SCREEN_PROFILES:
    case GG_SCREEN_NAME:     return go(GG_SCREEN_TITLE);
    case GG_SCREEN_OPTIONS:  return go(s->options_from);
    case GG_SCREEN_PAUSE:    return go(GG_SCREEN_PLAY);
    case GG_SCREEN_PLAY:     return go(GG_SCREEN_PAUSE);
    default: {
        gg_screen_result r = { .action = GG_ACTION_QUIT };
        return r;
    }
    }
}

void gg_screens_type(gg_screens *s, char ch) {
    if (s->id != GG_SCREEN_NAME) return;
    s->notice[0] = '\0';

    size_t n = SDL_strlen(s->typed);
    if (ch == '\b') {
        if (n > 0) s->typed[n - 1] = '\0';
        return;
    }
    if (n + 1 >= GG_PROFILE_NAME_MAX) return;
    s->typed[n] = ch;
    s->typed[n + 1] = '\0';
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void draw_backdrop(SDL_Renderer *ren) {
    SDL_SetRenderDrawColor(ren, 14, 17, 12, 255);
    SDL_RenderClear(ren);
}

static void draw_wordmark(SDL_Renderer *ren, int cx, int y) {
    SDL_SetRenderScale(ren, (float)TITLE_SCALE, (float)TITLE_SCALE);
    gg_font_center(ren, cx / TITLE_SCALE, y / TITLE_SCALE, AMBER, "GIGANTIMA");
    SDL_SetRenderScale(ren, 1.0f, 1.0f);
}

// One key: a letter of the alphabet, or one of the two wide ones below it.
// `framed` outlines the key even when the cursor is elsewhere, which is what
// makes "Rub out" and "Begin" read as things to press rather than as a caption.
static void draw_key(SDL_Renderer *ren, int x, int y, int w, int h,
                     const char *label, bool here, bool framed) {
    const SDL_FRect box = { (float)x, (float)y, (float)w, (float)h };
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    if (here) {
        SDL_SetRenderDrawColor(ren, 217, 145, 63, 55);
        SDL_RenderFillRect(ren, &box);
        SDL_SetRenderDrawColor(ren, 217, 145, 63, 190);
        SDL_RenderRect(ren, &box);
    } else if (framed) {
        SDL_SetRenderDrawColor(ren, 95, 72, 46, 190);
        SDL_RenderRect(ren, &box);
    }
    gg_font_center(ren, x + w / 2, y + (h - gg_font_height()) / 2,
                   here ? AMBER : INK, label);
}

// Returns the y below the grid.
static int draw_alphabet(const gg_screens *s, SDL_Renderer *ren, int cx, int top) {
    const int grid_w = KEYS_COLS * KEY_CELL_W;
    const int left   = cx - grid_w / 2;

    for (int r = 0; r < KEYS_ROWS; r++) {
        for (int c = 0; c < KEYS_COLS; c++) {
            char one[2];
            draw_key(ren, left + c * KEY_CELL_W, top + r * KEY_CELL_H,
                     KEY_CELL_W, KEY_CELL_H,
                     key_label(KEYS[r][c], one),
                     s->key_row == r && s->key_col == c, false);
        }
    }

    const int by = top + KEYS_ROWS * KEY_CELL_H + 10;
    const bool ends = s->key_row == KEY_ROW_ENDS;
    draw_key(ren, cx - 130, by, 120, KEY_CELL_H, "Rub out",
             ends && s->key_col == KEY_END_ERASE, true);
    draw_key(ren, cx + 10, by, 120, KEY_CELL_H, "Begin",
             ends && s->key_col == KEY_END_BEGIN, true);
    return by + KEY_CELL_H;
}

void gg_screens_draw(const gg_screens *s, SDL_Renderer *ren, uint64_t frame) {
    const int cx = GG_SCREEN_W / 2;
    const int line = gg_font_height();

    if (s->id == GG_SCREEN_PAUSE) {
        // Over the world, dimmed - so it is obvious the game is still there
        // and merely waiting.
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 10, 12, 9, 205);
        const SDL_FRect all = { 0, 0, (float)GG_SCREEN_W, (float)GG_SCREEN_H };
        SDL_RenderFillRect(ren, &all);
    } else {
        draw_backdrop(ren);
        draw_wordmark(ren, cx, 96);
        gg_font_center(ren, cx, 96 + line * TITLE_SCALE + 10, DIM,
                       "a world in the manner of Ultima VI");
    }

    int y = (s->id == GG_SCREEN_PAUSE) ? 170 : 230;

    if (s->id == GG_SCREEN_NAME) {
        // Below the wordmark and its line, with the grid and its two hint
        // lines this screen is the tallest of them - so it sets its own start
        // rather than sharing the menu screens'.
        y = 224;
        gg_font_center(ren, cx, y, AMBER, "Name thy journey");
        y += line * 2;

        // A caret that blinks slowly enough to read as waiting rather than as
        // a fault.
        char shown[GG_PROFILE_NAME_MAX + 2];
        SDL_snprintf(shown, sizeof shown, "%s%s", s->typed,
                     (frame / 30) % 2 ? "_" : " ");
        gg_font_center(ren, cx, y, INK, shown[0] == ' ' ? "_" : shown);
        y += line * 2;

        y = draw_alphabet(s, ren, cx, y);
        y += line;
        gg_font_center(ren, cx, y, DIM,
                       "Type, or pick letters with the arrows or stick");
        y += line;
        gg_font_center(ren, cx, y, DIM,
                       "Enter or A chooses   Y rubs out   Start begins   B back");
        y += line;
    } else {
        y = gg_menu_draw(&s->menu, ren, cx, y);
    }

    if (s->notice[0]) {
        y += line;
        gg_font_center(ren, cx, y, s->confirming_delete ? WARN : MOSS, s->notice);
    }

    if (s->id == GG_SCREEN_TITLE) {
        int cy = GG_SCREEN_H - line * 3 - 20;
        gg_font_center(ren, cx, cy, DIM,
                       "Arrows or the stick to move   Enter or A to choose");
        cy += line;
        gg_font_center(ren, cx, cy, DIM,
                       "Esc or B to go back   Start pauses   F11 fullscreen");
    }
}
