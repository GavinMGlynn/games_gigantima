// gg_ui.c - status band, conversation panel and title screen.
#include "ui/gg_ui.h"
#include "gfx/gg_font.h"

// A parchment-and-ink palette, sampled to sit beside the LPC art rather than
// fight it: the panel is the dark earth of the tileset, the rules are its
// cliff brown, and the highlight is the amber of the desert sand.
static const SDL_Color INK    = { 226, 216, 190, 255 };
static const SDL_Color DIM    = { 150, 142, 120, 255 };
static const SDL_Color AMBER  = { 217, 145,  63, 255 };
static const SDL_Color MOSS   = { 124, 184,  84, 255 };
static const SDL_Color BLOOD  = { 190,  72,  60, 255 };

static void panel(SDL_Renderer *ren, SDL_FRect r, uint8_t alpha) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 22, 18, 14, alpha);
    SDL_RenderFillRect(ren, &r);
    SDL_SetRenderDrawColor(ren, 95, 72, 46, 255);
    SDL_RenderRect(ren, &r);
}

// A left-to-right bar. Used for health, and written to take any pair so the
// same code can serve the other gauges when they exist.
static void gauge(SDL_Renderer *ren, int x, int y, int w, int h,
                  int value, int max, SDL_Color fill) {
    const SDL_FRect back = { (float)x, (float)y, (float)w, (float)h };
    SDL_SetRenderDrawColor(ren, 48, 38, 30, 255);
    SDL_RenderFillRect(ren, &back);

    if (max > 0 && value > 0) {
        const int fw = gg_clampi(value * w / max, 0, w);
        const SDL_FRect front = { (float)x, (float)y, (float)fw, (float)h };
        SDL_SetRenderDrawColor(ren, fill.r, fill.g, fill.b, 255);
        SDL_RenderFillRect(ren, &front);
    }
    SDL_SetRenderDrawColor(ren, 95, 72, 46, 255);
    SDL_RenderRect(ren, &back);
}

void gg_ui_hud(const gg_game *g, SDL_Renderer *ren) {
    const int top = GG_VIEW_H;
    const SDL_FRect band = { 0, (float)top, (float)GG_SCREEN_W, (float)GG_HUD_H };
    panel(ren, band, 245);

    const gg_actor *p = gg_player_const(g);
    const int line = gg_font_height();

    // --- left column: who you are ------------------------------------------
    int x = 10, y = top + 8;
    gg_font_printf(ren, x, y, AMBER, "%s", p->name);
    y += line;
    gg_font_printf(ren, x, y, DIM, "Level %d", g->level);
    y += line + 2;

    gg_font_draw(ren, x, y, DIM, "Health");
    gauge(ren, x + 56, y + 3, 110, 9, g->hp, g->hp_max, BLOOD);
    gg_font_printf(ren, x + 174, y, INK, "%d/%d", g->hp, g->hp_max);
    y += line + 4;

    gg_font_printf(ren, x, y, DIM, "Gold %d   Food %d   Torches %d",
                   g->item[GG_ITEM_GOLD], g->item[GG_ITEM_FOOD],
                   g->item[GG_ITEM_TORCH]);

    // --- right column: where and when --------------------------------------
    const int rx = GG_SCREEN_W - 210;
    y = top + 8;
    gg_font_printf(ren, rx, y, AMBER, "%s", gg_game_place(g));
    y += line;
    gg_font_printf(ren, rx, y, INK, "Day %u, %02d:%02d",
                   g->day, gg_game_hour(g), gg_game_minute(g));
    y += line;

    // Naming the part of the day is worth a line: the player needs to know
    // night is coming before it is dark enough to notice.
    const int h = gg_game_hour(g);
    const char *part = h < 5  ? "deep night" : h < 8  ? "dawn"
                     : h < 12 ? "morning"    : h < 14 ? "midday"
                     : h < 18 ? "afternoon"  : h < 21 ? "dusk" : "night";
    gg_font_printf(ren, rx, y, DIM, "%s", part);
    y += line;
    gg_font_printf(ren, rx, y, DIM, "turn %u", g->turn);

    // --- centre: the message log -------------------------------------------
    // Drawn last so it can be the widest thing here without the columns
    // having to leave room for its longest possible line.
    const int lx = 250;
    y = top + 8;
    for (int i = 0; i < g->logn; i++) {
        // The newest line is bright, older ones fade back - so the eye lands
        // on what just happened without having to read the whole log.
        const bool newest = (i == g->logn - 1);
        gg_font_draw(ren, lx, y, newest ? INK : DIM, g->log[i]);
        y += line;
    }
}

void gg_ui_converse(const gg_game *g, SDL_Renderer *ren) {
    if (g->talking_to < 0 || g->talking_to >= g->actors) return;
    const gg_actor *a = &g->actor[g->talking_to];

    const SDL_FRect box = { 60, (float)(GG_VIEW_H - 150),
                            (float)(GG_SCREEN_W - 120), 120 };
    panel(ren, box, 236);

    int x = (int)box.x + 14, y = (int)box.y + 12;
    gg_font_printf(ren, x, y, AMBER, "%s", a->name);
    y += gg_font_height() + 4;
    gg_font_printf(ren, x, y, INK, "\"%s\"",
                   a->greeting ? a->greeting : "Hail.");

    gg_font_draw(ren, x, (int)(box.y + box.h) - gg_font_height() - 10, DIM,
                 "(any key to take thy leave)");
}

void gg_ui_title(SDL_Renderer *ren, uint64_t t, const char *profile) {
    SDL_SetRenderDrawColor(ren, 14, 17, 12, 255);
    SDL_RenderClear(ren);

    const int cx = GG_SCREEN_W / 2;

    // The title is drawn from the font sheet at 4x. Scaling the baked glyphs
    // rather than shipping a second larger font keeps the look identical and
    // the atlas one texture; nearest filtering means it stays crisp.
    SDL_SetRenderScale(ren, 4.0f, 4.0f);
    gg_font_center(ren, cx / 4, 70 / 4, AMBER, "GIGANTIMA");
    SDL_SetRenderScale(ren, 1.0f, 1.0f);

    gg_font_center(ren, cx, 150, DIM, "a world in the manner of Ultima VI");

    int y = 250;
    gg_font_center(ren, cx, y, INK, profile && *profile
                   ? "Thy journey awaits, Avatar." : "Thy journey awaits.");
    y += gg_font_height() * 2;

    if (profile && *profile) {
        gg_font_center(ren, cx, y, MOSS, profile);
        y += gg_font_height() * 2;
    }

    // A slow pulse rather than a hard blink: at 60 Hz a blink reads as a
    // rendering fault, and this says "waiting for you" without shouting.
    const double phase = (double)(t % 120) / 120.0;
    const uint8_t a = (uint8_t)(120 + 135 * (0.5 + 0.5 * SDL_cos(phase * 6.283185)));
    const SDL_Color prompt = { INK.r, INK.g, INK.b, a };
    gg_font_center(ren, cx, y, prompt, "Press Enter to begin");

    y += gg_font_height() * 3;
    gg_font_center(ren, cx, y, DIM, "Arrows or WASD to walk   T talk   L look   O open");
    y += gg_font_height();
    gg_font_center(ren, cx, y, DIM, "F1 debug window   F11 fullscreen   Esc quit");
}
