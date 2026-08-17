// gg_ui.c - the status band and the conversation panel.
#include "ui/gg_ui.h"
#include "gfx/gg_font.h"
#include "gfx/gg_atlas.h"

// A parchment-and-ink palette, sampled to sit beside the LPC art rather than
// fight it: the panel is the dark earth of the tileset, the rules are its
// cliff brown, and the highlight is the amber of the desert sand.
static const SDL_Color INK    = { 226, 216, 190, 255 };
static const SDL_Color DIM    = { 150, 142, 120, 255 };
static const SDL_Color AMBER  = { 217, 145,  63, 255 };
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
    gg_font_printf(ren, x, y, DIM, "Level %d", p->level);
    y += line + 2;

    gg_font_draw(ren, x, y, DIM, "Health");
    gauge(ren, x + 56, y + 3, 110, 9, p->hp, p->hp_max, BLOOD);
    gg_font_printf(ren, x + 174, y, INK, "%d/%d", p->hp, p->hp_max);
    y += line + 4;

    // Gold, and the load. The load is here rather than only on the pack screen
    // because it is the number that stops a player picking something up, and
    // finding that out while standing over it is too late.
    const int carried = gg_pack_weight(g);
    char load[24];
    gg_ui_weight(load, sizeof load, carried);
    gg_font_printf(ren, x, y, DIM, "Gold %d", gg_pack_count(g, GG_ITEM_GOLD));
    gg_font_printf(ren, x + 110, y, carried * 10 >= GG_CARRY_MAX * 9 ? AMBER : DIM,
                   "Load %s/%d st", load, GG_CARRY_MAX / 100);
    y += line + 2;

    // Whoever walks with you, in the order they walk. Health as a number
    // rather than a bar: four bars in this space would be four smears, and the
    // number is what a player actually reads before deciding to turn back.
    for (int slot = 1; slot <= GG_PARTY_MAX; slot++) {
        const int who = gg_party_at(g, slot);
        if (who < 0) continue;
        const gg_actor *c = &g->actor[who];
        const bool hurt = c->hp_max > 0 && c->hp * 3 <= c->hp_max;
        gg_font_printf(ren, x, y, DIM, "%d.", slot);
        gg_font_printf(ren, x + 20, y, INK, "%s", c->name);
        gg_font_printf(ren, x + 130, y, hurt ? BLOOD : DIM, "%d/%d",
                       c->hp, c->hp_max);
        y += line;
    }

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

void gg_ui_weight(char *out, size_t n, int hundredths) {
    SDL_snprintf(out, n, "%d.%d", hundredths / 100, (hundredths % 100) / 10);
}

void gg_ui_pack(const gg_game *g, SDL_Renderer *ren, SDL_Texture *items) {
    const int line = gg_font_height();
    const int row_h = 36;

    // Tall enough for what is actually carried, and no taller. A fixed panel
    // with four things in it is mostly empty box.
    const int rows = g->packn > 0 ? g->packn : 1;
    const int inner = (line + 8) + rows * row_h + (line + 30);
    const int height = gg_clampi(inner, 140, GG_VIEW_H - 80);

    const SDL_FRect box = { 120, (float)((GG_VIEW_H - height) / 2),
                            (float)(GG_SCREEN_W - 240), (float)height };
    panel(ren, box, 238);

    const int x = (int)box.x + 16;
    int y = (int)box.y + 12;

    const int carried = gg_pack_weight(g);
    char load[24];
    gg_ui_weight(load, sizeof load, carried);
    gg_font_draw(ren, x, y, AMBER, "What thou carriest");
    gg_font_printf(ren, x + 260, y, carried * 10 >= GG_CARRY_MAX * 9 ? AMBER : DIM,
                   "%s of %d stone", load, GG_CARRY_MAX / 100);
    y += line + 8;

    if (g->packn == 0) {
        gg_font_draw(ren, x, y, DIM, "Nothing at all.");
    }

    for (int i = 0; i < g->packn; i++) {
        const gg_item_def *d = &GG_ITEM[g->pack[i].kind];
        const bool here = (i == g->pack_cursor);
        const int ry = y + i * row_h;

        if (here) {
            SDL_SetRenderDrawColor(ren, 217, 145, 63, 45);
            const SDL_FRect bar = { box.x + 8, (float)(ry - 4),
                                    box.w - 16, (float)(row_h - 4) };
            SDL_RenderFillRect(ren, &bar);
        }

        // The thing's own picture, drawn from its bottom edge so a two-tile
        // torch sits on the row rather than hanging above it.
        if (items) {
            const gg_rect *r = &GG_ITEM_RECT[g->pack[i].kind];
            const SDL_FRect src = { (float)r->x, (float)r->y, (float)r->w, (float)r->h };
            const SDL_FRect dst = { (float)x, (float)(ry + row_h - 8 - r->h),
                                    (float)r->w, (float)r->h };
            SDL_RenderTexture(ren, items, &src, &dst);
        }

        // Held things say so, because the pack is the only place it shows.
        const bool held = (d->slot != GG_SLOT_NONE &&
                           g->equipped[d->slot] == i);
        const int tx = x + 40;
        if (g->pack[i].count == 1)
            gg_font_printf(ren, tx, ry, here ? AMBER : INK, "%s", d->one);
        else
            gg_font_printf(ren, tx, ry, here ? AMBER : INK, "%d %s",
                           g->pack[i].count, d->many);

        char w[24];
        gg_ui_weight(w, sizeof w, d->weight * g->pack[i].count);
        gg_font_printf(ren, tx, ry + line - 2, DIM, "%s st%s", w,
                       held ? "   - in hand" : "");
    }

    const int fy = (int)(box.y + box.h) - line - 12;
    gg_font_draw(ren, x, fy, DIM,
                 "U or A use   R or Y ready   P or X set down   G take   I or B close");
}

void gg_ui_converse(const gg_game *g, SDL_Renderer *ren) {
    if (g->talking_to < 0 || g->talking_to >= g->actors) return;
    const gg_actor *a = &g->actor[g->talking_to];
    const int line = gg_font_height();

    // Sized to what is actually being said and asked. A conversation is mostly
    // two lines and four words; a fixed box for it is mostly empty box.
    const int says = g->saids > 0 ? g->saids : 1;
    const int words = g->askables;
    const int height = 14 + line + 6 + says * line + 10 +
                       (words ? line + words * line + 8 : 0) + line + 14;

    const SDL_FRect box = { 60, (float)(GG_VIEW_H - height - 24),
                            (float)(GG_SCREEN_W - 120), (float)height };
    panel(ren, box, 236);

    const int x = (int)box.x + 14;
    int y = (int)box.y + 12;

    gg_font_printf(ren, x, y, AMBER, "%s", a->name);
    y += line + 6;

    if (g->saids == 0) {
        gg_font_draw(ren, x, y, INK, "\"Hail.\"");
        y += line;
    }
    for (int i = 0; i < g->saids; i++) {
        // Quoted on the first line and last only, so a three-line answer reads
        // as one speech rather than three separate remarks.
        gg_font_printf(ren, x, y, INK, "%s%s%s",
                       i == 0 ? "\"" : " ", g->said[i],
                       i == g->saids - 1 ? "\"" : "");
        y += line;
    }
    y += 10;

    // The words this person will answer to, out of the words the player knows.
    if (words > 0) {
        gg_font_draw(ren, x, y, DIM, "Ask about:");
        y += line;
        for (int i = 0; i < words; i++) {
            const bool here = (i == g->ask_cursor);
            if (here) {
                SDL_SetRenderDrawColor(ren, 217, 145, 63, 45);
                const SDL_FRect bar = { box.x + 8, (float)(y - 2),
                                        box.w - 16, (float)line };
                SDL_RenderFillRect(ren, &bar);
            }
            gg_font_printf(ren, x + 16, y, here ? AMBER : INK, "%s", g->askable[i]);
            y += line;
        }
        y += 8;
    } else {
        gg_font_draw(ren, x, y, DIM, "Thou knowest no word this one will answer to.");
        y += line;
    }

    gg_font_draw(ren, x, (int)(box.y + box.h) - line - 10, DIM,
                 words > 0 ? "T or A to ask   Space or B to take thy leave"
                           : "Space or B to take thy leave");
}

