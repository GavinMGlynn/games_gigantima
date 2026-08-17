// gg_menu.c - the vertical list widget.
#include "ui/gg_menu.h"
#include "gfx/gg_font.h"

#include <stdarg.h>

// Row metrics. A row is two lines tall whether or not it has a detail line, so
// a list does not jump about as rows gain and lose their second line.
// Whether the text is larger than the size this layout was drawn around. At
// that size two lines a row will not fit a screenful, so a row puts its detail
// beside its label instead of under it - which is the difference between a
// menu that grows and a menu that overlaps itself.
static bool large_text(void) { return gg_font_height() > GG_FONT_CELL_H; }

// A row: two lines of text and a little air, or one line and a little air when
// the text is large. Derived from the font rather than fixed.
static int row_h(void) {
    return large_text() ? gg_font_height() + 10 : gg_font_height() * 2 + 8;
}
#define MENU_W    420

static const SDL_Color INK   = { 226, 216, 190, 255 };
static const SDL_Color DIM   = { 138, 132, 116, 255 };
static const SDL_Color GREY  = {  92,  88,  78, 255 };
static const SDL_Color AMBER = { 217, 145,  63, 255 };

void gg_menu_reset(gg_menu *m, const char *title) {
    SDL_zerop(m);
    if (title) SDL_strlcpy(m->title, title, sizeof m->title);
}

int gg_menu_add(gg_menu *m, bool enabled, const char *label,
                const char *detail, ...) {
    if (m->n >= GG_MENU_MAX) return -1;

    gg_menu_item *it = &m->item[m->n];
    SDL_zerop(it);
    it->enabled = enabled;
    SDL_strlcpy(it->label, label ? label : "", sizeof it->label);

    if (detail) {
        va_list ap;
        va_start(ap, detail);
        SDL_vsnprintf(it->detail, sizeof it->detail, detail, ap);
        va_end(ap);
    }
    return m->n++;
}

void gg_menu_move(gg_menu *m, int dy) {
    if (m->n == 0 || dy == 0) return;

    const int step = dy > 0 ? 1 : -1;
    // At most one lap: if nothing else is enabled the cursor comes back to
    // where it started rather than spinning.
    for (int tries = 0; tries < m->n; tries++) {
        m->cursor = (m->cursor + step + m->n) % m->n;
        if (m->item[m->cursor].enabled) return;
    }
}

void gg_menu_select(gg_menu *m, int index) {
    if (m->n == 0) return;
    m->cursor = gg_clampi(index, 0, m->n - 1);
    if (!m->item[m->cursor].enabled) gg_menu_move(m, 1);
}

int gg_menu_chosen(const gg_menu *m) {
    if (m->n == 0 || m->cursor < 0 || m->cursor >= m->n) return -1;
    return m->item[m->cursor].enabled ? m->cursor : -1;
}

int gg_menu_draw(const gg_menu *m, SDL_Renderer *ren, int cx, int top) {
    int y = top;

    if (m->title[0]) {
        gg_font_center(ren, cx, y, AMBER, m->title);
        y += gg_font_height() * 2;
    }

    for (int i = 0; i < m->n; i++) {
        const gg_menu_item *it = &m->item[i];
        const bool here = (i == m->cursor) && it->enabled;

        if (here) {
            // A filled bar rather than a ">" marker: at this font size a
            // single glyph is easy to lose, and the bar also shows how wide
            // the row's hit area is.
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 217, 145, 63, 40);
            // Wide enough for what is on the row: at large text a label and
            // its detail sit side by side and run past the usual width.
            const int want = gg_font_width(it->label) +
                             (it->detail[0] ? gg_font_width(it->detail) +
                                              gg_font_height() : 0) + 40;
            const int w = want > MENU_W ? want : MENU_W;
            const SDL_FRect bar = { (float)(cx - w / 2), (float)(y - 5),
                                    (float)w, (float)(row_h() - 8) };
            SDL_RenderFillRect(ren, &bar);
            SDL_SetRenderDrawColor(ren, 217, 145, 63, 150);
            SDL_RenderRect(ren, &bar);
        }

        const SDL_Color c = !it->enabled ? GREY : (here ? AMBER : INK);
        if (large_text() && it->detail[0]) {
            // Label and detail on one line, the label bright and the detail
            // dim, centred as a pair.
            const int lw = gg_font_width(it->label);
            const int dw = gg_font_width(it->detail);
            const int gap = gg_font_height() / 2;
            const int x = cx - (lw + gap + dw) / 2;
            gg_font_draw(ren, x, y, c, it->label);
            gg_font_draw(ren, x + lw + gap, y, it->enabled ? DIM : GREY,
                         it->detail);
        } else {
            gg_font_center(ren, cx, y, c, it->label);
            if (it->detail[0])
                gg_font_center(ren, cx, y + gg_font_height() - 2,
                               it->enabled ? DIM : GREY, it->detail);
        }
        y += row_h();
    }
    return y;
}
