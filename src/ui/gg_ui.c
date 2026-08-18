// gg_ui.c - the status band and the conversation panel.
#include "ui/gg_ui.h"
#include "gfx/gg_font.h"
#include "gfx/gg_atlas.h"
#include "core/gg_magic.h"

// A parchment-and-ink palette, sampled to sit beside the LPC art rather than
// fight it: the panel is the dark earth of the tileset, the rules are its
// cliff brown, and the highlight is the amber of the desert sand.
static const SDL_Color INK    = { 226, 216, 190, 255 };
static const SDL_Color DIM    = { 150, 142, 120, 255 };
static const SDL_Color AMBER  = { 217, 145,  63, 255 };
static const SDL_Color BLOOD  = { 190,  72,  60, 255 };
// A thing you cannot use, and a thing you have not got. Distinct, because
// "beyond thy circle" and "thou hast no ginseng" are different problems.
static const SDL_Color GREY_OUT = {  96,  92,  82, 255 };
static const SDL_Color WANT     = { 186, 104,  84, 255 };

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

// `text` shortened until it fits `width` pixels, with an ellipsis where it was
// cut. The message log is the widest thing in the band and its lines are
// whatever the world had to say, so one long line used to be drawn straight
// through the clock in the right-hand column.
static const char *fit(char *out, size_t n, const char *text, int width) {
    if (gg_font_width(text) <= width) return text;

    SDL_strlcpy(out, text, n);
    size_t len = SDL_strlen(out);
    while (len > 3) {
        out[len - 3] = '\0';
        SDL_strlcat(out, "...", n);
        if (gg_font_width(out) <= width) return out;
        len -= 3;
        out[len] = '\0';
    }
    return out;
}

void gg_ui_hud(const gg_game *g, SDL_Renderer *ren) {
    const int top = GG_VIEW_H;
    const SDL_FRect band = { 0, (float)top, (float)GG_SCREEN_W, (float)GG_HUD_H };
    panel(ren, band, 245);

    const gg_actor *p = gg_player_const(g);
    const int line = gg_font_height();

    // **The band lays itself out from what fits, not from a fixed shape.** At
    // the ordinary size there is room for four rows of status down the left
    // and five lines of log beside them; at twice that there is room for
    // three rows of anything. So the large layout puts the same facts in
    // three rows and gives up log lines rather than giving up the health
    // gauge - the band is furniture of a fixed height, and something has to
    // go. Deciding from the height rather than from the setting means any
    // future size is handled by the same two branches.
    const bool roomy = line * 4 + 24 <= GG_HUD_H;

    const int carried = gg_pack_weight(g);
    char load[24];
    gg_ui_weight(load, sizeof load, carried);
    const int party = gg_party_size(g);

    const int h = gg_game_hour(g);
    const char *part = h < 5  ? "deep night" : h < 8  ? "dawn"
                     : h < 12 ? "morning"    : h < 14 ? "midday"
                     : h < 18 ? "afternoon"  : h < 21 ? "dusk" : "night";

    if (!roomy) {
        // Three rows: who and how hale, then where and when, then the two
        // newest things that happened.
        int x = 10, y = top + 6;
        char health[24];
        SDL_snprintf(health, sizeof health, "%d/%d", p->hp, p->hp_max);

        // Laid out from the widths of what is actually there, so nothing
        // drifts when a name is long or health reaches three figures.
        gg_font_printf(ren, x, y, AMBER, "%s", p->name);
        int at = x + gg_font_width(p->name) + line / 2;
        gg_font_draw(ren, at, y, INK, health);
        at += gg_font_width(health) + line / 2;
        gauge(ren, at, y + line / 3, GG_SCREEN_W - at - 10, line / 3,
              p->hp, p->hp_max, BLOOD);
        y += line;

        gg_font_printf(ren, x, y, DIM, "%s, day %u, %02d:%02d, %s",
                       gg_game_place(g), g->day, h, gg_game_minute(g), part);
        if (party > 0)
            gg_font_printf(ren, x, y, DIM, "%s, day %u, %02d:%02d, %s, %d with thee",
                           gg_game_place(g), g->day, h, gg_game_minute(g), part,
                           party);
        y += line;

        char cut[GG_LOG_WIDTH + 4];
        if (g->logn > 0)
            gg_font_draw(ren, x, y, INK,
                         fit(cut, sizeof cut, g->log[g->logn - 1],
                             GG_SCREEN_W - 20));
        return;
    }

    // --- left column: who you are ------------------------------------------
    int x = 10, y = top + 8;
    gg_font_printf(ren, x, y, AMBER, "%s", p->name);
    y += line;
    // The level, and how far along it is. A bare "Level 1" for the whole of a
    // first journey is what an experience counter that spent on nothing looked
    // like from the outside.
    if (p->level < GG_LEVEL_MAX) {
        const int have = g->exp - gg_level_cost(p->level);
        const int want = gg_level_cost(p->level + 1) - gg_level_cost(p->level);
        gg_font_printf(ren, x, y, DIM, "Level %d", p->level);
        gg_font_printf(ren, x + gg_font_width("Level 00") + 8, y, DIM,
                       "%d/%d", have, want);
    } else {
        gg_font_printf(ren, x, y, DIM, "Level %d", p->level);
    }
    y += line + 2;

    gg_font_draw(ren, x, y, DIM, "Health");
    gauge(ren, x + 56, y + 3, 110, 9, p->hp, p->hp_max, BLOOD);
    gg_font_printf(ren, x + 174, y, INK, "%d/%d", p->hp, p->hp_max);
    y += line + 4;

    // Gold, and the load. The load is here rather than only on the pack screen
    // because it is the number that stops a player picking something up, and
    // finding that out while standing over it is too late.
    gg_font_printf(ren, x, y, DIM, "Gold %d", gg_pack_count(g, GG_ITEM_GOLD));
    gg_font_printf(ren, x + 110, y, carried * 10 >= GG_CARRY_MAX * 9 ? AMBER : DIM,
                   "Load %s/%d st", load, GG_CARRY_MAX / 100);
    y += line + 2;

    // A ward is the one spell with nothing to see: the light lights, the sleep
    // dims what it fell on, and this changes a number in a fight. So it says
    // so, and it says how much longer, because the whole of it is the clock.
    if (g->ward_turns > 0) {
        gg_font_printf(ren, x, y, AMBER, "Warded +%d", g->ward_power);
        gg_font_printf(ren, x + 110, y, DIM, "%d turns", g->ward_turns);
        y += line + 2;
    }

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
    gg_font_printf(ren, rx, y, DIM, "%s", part);
    y += line;
    gg_font_printf(ren, rx, y, DIM, "turn %u", g->turn);

    // --- centre: the message log -------------------------------------------
    // Drawn last so it can be the widest thing here without the columns
    // having to leave room for its longest possible line.
    const int lx = 250;
    const int room = rx - lx - 12;
    y = top + 8;
    char cut[GG_LOG_WIDTH + 4];
    for (int i = 0; i < g->logn; i++) {
        // The newest line is bright, older ones fade back - so the eye lands
        // on what just happened without having to read the whole log.
        const bool newest = (i == g->logn - 1);
        gg_font_draw(ren, lx, y, newest ? INK : DIM,
                     fit(cut, sizeof cut, g->log[i], room));
        y += line;
    }
}

void gg_ui_weight(char *out, size_t n, int hundredths) {
    SDL_snprintf(out, n, "%d.%d", hundredths / 100, (hundredths % 100) / 10);
}

// Which row a list should start at so that `cursor` is on screen, given how
// many rows there are and how many fit. A list longer than its panel has to
// scroll rather than draw through the bottom of it - which is what the spell
// book did the first time it was drawn at large text.
static int scrolled_to(int cursor, int rows, int fits) {
    if (rows <= fits) return 0;
    int top = cursor - fits / 2;
    if (top < 0) top = 0;
    if (top > rows - fits) top = rows - fits;
    return top;
}

void gg_ui_pack(const gg_game *g, SDL_Renderer *ren, SDL_Texture *items) {
    const int line = gg_font_height();
    // Two lines of text and a little air. Fixed at 36 this was exactly right
    // for the baked font size and exactly wrong for any other, which is what
    // the first screenshot at large text showed: every row through the one
    // below it.
    const int row_h = line * 2 + 2;
    const int icon = line * 40 / GG_FONT_CELL_H;

    // Tall enough for what is actually carried, and no taller. A fixed panel
    // with four things in it is mostly empty box.
    const int rows = g->packn > 0 ? g->packn : 1;
    const int inner = (line + 8) + rows * row_h + (line + 30);
    const int height = gg_clampi(inner, 140, GG_VIEW_H - 80);
    const int fits = (height - (line + 8) - (line + 30)) / row_h;
    const int top_row = scrolled_to(g->pack_cursor, g->packn, fits > 0 ? fits : 1);

    const SDL_FRect box = { 120, (float)((GG_VIEW_H - height) / 2),
                            (float)(GG_SCREEN_W - 240), (float)height };
    panel(ren, box, 238);

    const int x = (int)box.x + 16;
    int y = (int)box.y + 12;

    const int carried = gg_pack_weight(g);
    char load[24];
    gg_ui_weight(load, sizeof load, carried);
    gg_font_draw(ren, x, y, AMBER, "What thou carriest");
    gg_font_printf(ren, x + gg_font_width("What thou carriest") + line, y,
                   carried * 10 >= GG_CARRY_MAX * 9 ? AMBER : DIM,
                   "%s of %d stone", load, GG_CARRY_MAX / 100);
    y += line + 8;

    if (g->packn == 0) {
        gg_font_draw(ren, x, y, DIM, "Nothing at all.");
    }

    for (int i = top_row; i < g->packn && i - top_row < fits; i++) {
        const gg_item_def *d = &GG_ITEM[g->pack[i].kind];
        const bool here = (i == g->pack_cursor);
        const int ry = y + (i - top_row) * row_h;

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
            const SDL_FRect dst = { (float)x, (float)(ry + row_h - 6 - r->h),
                                    (float)r->w, (float)r->h };
            SDL_RenderTexture(ren, items, &src, &dst);
        }

        // Held things say so, because the pack is the only place it shows.
        const bool held = (d->slot != GG_SLOT_NONE &&
                           g->equipped[d->slot] == i);
        const int tx = x + icon;
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
    // The long hint if it fits, the short one if it does not: a line of help
    // running off the edge of its own panel helps nobody.
    const char *hint =
        "U or A use   R or Y ready   P or X set down   G take   I or B close";
    if (gg_font_width(hint) > (int)box.w - 32)
        hint = "U use  R ready  P set down  I close";
    gg_font_draw(ren, x, fy, DIM, hint);
}

void gg_ui_journal(const gg_game *g, SDL_Renderer *ren) {
    const int line = gg_font_height();
    const int row = line + 4;
    const int rows = 9;             // as many entries as fit without crowding

    // Room for the entries *and* the headings between them. Counting only the
    // entries overflowed the panel the first time this was drawn, because a
    // heading is a line too.
    const int height = (line + 8) + (rows + 3) * row + (line + 20);
    const SDL_FRect box = { 70, (float)((GG_VIEW_H - height) / 2),
                            (float)(GG_SCREEN_W - 140), (float)height };
    panel(ren, box, 240);

    const int x = (int)box.x + 16;
    const int floor_y = (int)(box.y + box.h) - line - 16;
    int y = (int)box.y + 12;

    const int n = gg_journal_lines(g);
    gg_font_draw(ren, x, y, AMBER, "What has happened");
    gg_font_printf(ren, (int)(box.x + box.w) - 16 - gg_font_width("00 entries"),
                   y, DIM, "%d entries", n);
    y += line + 8;

    if (n == 0) {
        gg_font_draw(ren, x, y, DIM,
                     "Nothing yet. Ask somebody what troubles the vale.");
    }

    // Scrolled so the cursor is on screen, oldest first - a journal reads
    // forwards.
    int top = gg_clampi(g->journal_cursor - rows / 2, 0, n - rows);
    if (top < 0) top = 0;

    const char *last_quest = nullptr;
    for (int i = top; i < n; i++) {
        const char *quest = nullptr, *text = nullptr;
        bool done = false;
        if (!gg_journal_line(g, i, &quest, &text, &done)) break;

        // Bounded by the panel rather than by a count, so a heading can never
        // push the last entry through the bottom of the box.
        const bool heading = (quest != last_quest);
        if (y + (heading ? 2 : 1) * row > floor_y) break;

        if (heading) {
            gg_font_printf(ren, x, y, done ? DIM : AMBER, "%s%s", quest,
                           done ? " - finished" : "");
            last_quest = quest;
            y += row;
        }

        if (i == g->journal_cursor) {
            SDL_SetRenderDrawColor(ren, 217, 145, 63, 40);
            const SDL_FRect bar = { box.x + 8, (float)(y - 2),
                                    box.w - 16, (float)row };
            SDL_RenderFillRect(ren, &bar);
        }
        char cut[GG_JOURNAL_LINE_MAX + 4];
        gg_font_draw(ren, x + line, y, i == g->journal_cursor ? INK : DIM,
                     fit(cut, sizeof cut, text, (int)box.w - line * 2 - 24));
        y += row;
    }

    gg_font_draw(ren, x, (int)(box.y + box.h) - line - 12, DIM,
                 "arrows to read   J or any key to close");
}

// Words wrapped to a pixel width, drawn one line under another, returning
// where the next line would go. Measured with the font's own widths rather
// than counted in characters: VT323 is proportional, so a character count
// wraps early on a line of thin letters and overruns on a line of wide ones.
// With a renderer, draws and returns where the next line would go. Without
// one, draws nothing and returns the number of lines it would have taken -
// call it with y = 0 and read the count off the result.
static int wrapped(SDL_Renderer *ren, int x, int y, int width, SDL_Color c,
                   const char *text) {
    const int line = gg_font_height() + 4;
    char buf[256];

    while (*text) {
        int take = 0, last_space = -1;
        for (int i = 0; text[i] && i < (int)sizeof buf - 1; i++) {
            buf[i] = text[i];
            buf[i + 1] = '\0';
            if (gg_font_width(buf) > width) break;
            take = i + 1;
            if (text[i] == ' ') last_space = i;
        }
        // Break at the last space that fitted, unless one word is wider than
        // the panel - then it is cut, because the alternative is a blank line
        // followed by the same word forever.
        if (text[take] && last_space > 0) take = last_space;

        SDL_memcpy(buf, text, (size_t)take);
        buf[take] = '\0';
        // No renderer means "how many lines would this be" - which is what the
        // panel needs before it knows how tall to be.
        if (ren) gg_font_draw(ren, x, y, c, buf);
        y += line;

        text += take;
        while (*text == ' ') text++;
    }
    return ren ? y : y / line;
}

void gg_ui_ending(const gg_game *g, SDL_Renderer *ren) {
    const int line = gg_font_height();
    const int row = line + 4;
    const int width = GG_SCREEN_W - 120 - 40;

    const char *quest = nullptr, *words = nullptr;
    const bool finished = gg_ending(g, &quest, &words);
    const char *closing = finished
        ? (words && *words ? words : "It is done.")
        : "The vale keeps its troubles a while longer.";

    // Sized to what it has to say, rather than to a guess: the closing line is
    // a sentence out of a text file and its length is not this file's to know.
    const int lines = wrapped(nullptr, 0, 0, width, INK, closing);
    const int height = row + 6                       // the heading
                     + (finished && quest && *quest ? row : 0)
                     + lines * (line + 4) + 6        // the words
                     + row + 8                       // the tally
                     + row + 12;                     // the way out
    const SDL_FRect box = { 60, (float)((GG_VIEW_H - height) / 2),
                            (float)(GG_SCREEN_W - 120), (float)height };
    panel(ren, box, 252);

    const int x = (int)box.x + 20;
    int y = (int)box.y + 14;

    if (finished) {
        gg_font_draw(ren, x, y, AMBER, "Here endeth the tale");
        y += row + 6;
        if (quest && *quest) {
            gg_font_printf(ren, x, y, DIM, "%s", quest);
            y += row;
        }
    } else {
        gg_font_draw(ren, x, y, BLOOD, "Thou art slain");
        y += row + 6;
    }

    y = wrapped(ren, x, y, width, INK, closing);
    y += 6;
    gg_font_printf(ren, x, y, DIM, "%u turns, %u %s, %d fallen", g->turn,
                   g->day, g->day == 1 ? "day" : "days", (int)g->slain);
    y += row + 8;
    gg_font_draw(ren, x, y, DIM, "any key to leave the world");
}

void gg_ui_spells(const gg_game *g, SDL_Renderer *ren) {
    const int line = gg_font_height();
    const int row_h = line * 2;

    // The three columns, set from the widest phrase and the widest name rather
    // than from 150 and 300 - which were the right numbers for one font size
    // and put every column through the next one at any other.
    int phrase_w = gg_font_width("VAS IN FLAM"), name_w = gg_font_width("Great Light");
    for (int i = 0; i < gg_magic_spells(); i++) {
        const gg_spell *sp = gg_magic_spell(i);
        if (!sp || !gg_spell_known(g, i)) continue;
        char full[64] = { 0 };
        for (int r = 0; r < sp->runes; r++) {
            if (r) SDL_strlcat(full, " ", sizeof full);
            SDL_strlcat(full, sp->rune[r], sizeof full);
        }
        const int w = gg_font_width(full);
        if (w > phrase_w) phrase_w = w;
        const int nw = gg_font_width(sp->name);
        if (nw > name_w) name_w = nw;
    }
    const int col2 = phrase_w + line;
    const int col3 = col2 + name_w + line;

    // Only what is known, because the book is the answer to "what can I do".
    int known = 0;
    for (int i = 0; i < gg_magic_spells(); i++)
        if (gg_spell_known(g, i)) known++;

    const int rows = known > 0 ? known : 1;
    const int height = gg_clampi((line + 8) + rows * row_h + (line + 30),
                                 140, GG_VIEW_H - 60);
    const int fits = (height - (line + 8) - (line + 30)) / row_h;
    const SDL_FRect box = { 100, (float)((GG_VIEW_H - height) / 2),
                            (float)(GG_SCREEN_W - 200), (float)height };
    panel(ren, box, 238);

    const int x = (int)box.x + 16;
    int y = (int)box.y + 12;

    gg_font_draw(ren, x, y, AMBER, "Words of power");
    gg_font_printf(ren, x + col3, y, DIM, "%d known of %d", known,
                   gg_magic_spells());
    y += line + 8;

    if (known == 0) {
        gg_font_draw(ren, x, y, DIM, "Thou knowest no words at all. Ask a mage.");
    }

    // Which of the known spells the cursor is on, so the list can be scrolled
    // by row rather than by spell id - the two are not the same list.
    int at = 0, seen = 0;
    for (int i = 0; i < gg_magic_spells(); i++) {
        if (!gg_spell_known(g, i)) continue;
        if (i == g->spell_cursor) at = seen;
        seen++;
    }
    const int top_row = scrolled_to(at, known, fits > 0 ? fits : 1);

    int row = 0;
    for (int i = 0; i < gg_magic_spells(); i++) {
        if (!gg_spell_known(g, i)) continue;
        const int mine = row++;
        if (mine < top_row || mine - top_row >= fits) continue;
        const gg_spell *sp = gg_magic_spell(i);
        const bool here = (i == g->spell_cursor);
        const bool can = gg_spell_afford(g, i) &&
                         gg_player_const(g)->level >= sp->circle;
        const int ry = y + (mine - top_row) * row_h;

        if (here) {
            SDL_SetRenderDrawColor(ren, 217, 145, 63, 45);
            const SDL_FRect bar = { box.x + 8, (float)(ry - 4),
                                    box.w - 16, (float)(row_h - 4) };
            SDL_RenderFillRect(ren, &bar);
        }

        // The phrase first: it is the name of the thing, and the point of the
        // whole system is that a player learns to read them.
        char phrase[64] = { 0 };
        for (int r = 0; r < sp->runes; r++) {
            if (r) SDL_strlcat(phrase, " ", sizeof phrase);
            SDL_strlcat(phrase, sp->rune[r], sizeof phrase);
        }
        gg_font_printf(ren, x, ry, here ? AMBER : (can ? INK : GREY_OUT),
                       "%s", phrase);
        gg_font_printf(ren, x + col2, ry, can ? INK : GREY_OUT, "%s", sp->name);
        gg_font_printf(ren, x + col3, ry, DIM, "circle %u", sp->circle);

        // And the price, with what is missing shown in the colour of a thing
        // you have not got.
        int px = x + col2;
        for (int r = 0; r < sp->reagents; r++) {
            const gg_item_def *d = &GG_ITEM[sp->reagent[r]];
            const bool enough = gg_pack_count(g, (gg_item_id)sp->reagent[r]) >=
                                sp->reagent_count[r];
            char one[48];
            SDL_snprintf(one, sizeof one, "%u %s", sp->reagent_count[r],
                         d->short_name);
            gg_font_draw(ren, px, ry + line - 2, enough ? DIM : WANT, one);
            px += gg_font_width(one) + 12;
        }
    }

    const char *say = "U or A to speak it   C or B to close the book";
    if (gg_font_width(say) > (int)box.w - 32) say = "U speak   C close";
    gg_font_draw(ren, x, (int)(box.y + box.h) - line - 12, DIM, say);
}

void gg_ui_converse(const gg_game *g, SDL_Renderer *ren) {
    if (g->talking_to < 0 || g->talking_to >= g->actors) return;
    const gg_actor *a = &g->actor[g->talking_to];
    const int line = gg_font_height();

    // Sized to what is actually being said and asked. A conversation is mostly
    // two lines and four words; a fixed box for it is mostly empty box.
    const int words = g->askables;
    const int room = GG_SCREEN_W - 120 - 28;

    // How many lines the speech will really take once it is wrapped to the
    // panel: at large text one line of dialogue is two or three, and a box
    // sized by the number of *said* lines cut them off.
    int says = 0;
    for (int i = 0; i < g->saids; i++)
        says += wrapped(nullptr, 0, 0, room, INK, g->said[i]);
    if (says == 0) says = 1;

    const int height = 14 + line + 6 + says * (line + 4) + 10 +
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
        // as one speech rather than three separate remarks - and wrapped to
        // the panel, because what people say is written for meaning rather
        // than to a column width.
        char said[GG_LINE_MAX + 4];
        SDL_snprintf(said, sizeof said, "%s%s%s", i == 0 ? "\"" : " ",
                     g->said[i], i == g->saids - 1 ? "\"" : "");
        y = wrapped(ren, x, y, room, INK, said);
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

