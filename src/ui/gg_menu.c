// gg_menu.c - the vertical list widget.
#include "ui/gg_menu.h"
#include "gfx/gg_font.h"

#include <stdarg.h>

// Row metrics. A row is two lines tall whether or not it has a detail line, so
// a list does not jump about as rows gain and lose their second line.
#define ROW_H     42
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
            const SDL_FRect bar = { (float)(cx - MENU_W / 2), (float)(y - 5),
                                    (float)MENU_W, (float)(ROW_H - 8) };
            SDL_RenderFillRect(ren, &bar);
            SDL_SetRenderDrawColor(ren, 217, 145, 63, 150);
            SDL_RenderRect(ren, &bar);
        }

        const SDL_Color c = !it->enabled ? GREY : (here ? AMBER : INK);
        gg_font_center(ren, cx, y, c, it->label);

        if (it->detail[0])
            gg_font_center(ren, cx, y + gg_font_height() - 2,
                           it->enabled ? DIM : GREY, it->detail);
        y += ROW_H;
    }
    return y;
}
