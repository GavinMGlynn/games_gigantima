// gg_font.c - drawing text from the baked glyph sheet.
#include "gfx/gg_font.h"
#include "platform/gg_paths.h"

#include <stdarg.h>
#include <SDL3_image/SDL_image.h>

// One texture per renderer. Two is all this game opens - the world window and
// the debug window - so a fixed pair beats a hash table.
#define MAX_RENDERERS 4

static struct {
    SDL_Renderer *ren;
    SDL_Texture  *tex;
} g_font[MAX_RENDERERS];

static SDL_Texture *texture_for(SDL_Renderer *ren) {
    for (int i = 0; i < MAX_RENDERERS; i++)
        if (g_font[i].ren == ren) return g_font[i].tex;
    return nullptr;
}

bool gg_font_init(SDL_Renderer *ren) {
    if (texture_for(ren)) return true;

    int slot = -1;
    for (int i = 0; i < MAX_RENDERERS; i++)
        if (!g_font[i].ren) { slot = i; break; }
    if (slot < 0) {
        SDL_Log("gigantima: too many renderers for the font cache");
        return false;
    }

    SDL_Texture *tex = IMG_LoadTexture(ren, gg_asset_path("atlas_font.png"));
    if (!tex) {
        SDL_Log("gigantima: cannot load atlas_font.png: %s", SDL_GetError());
        return false;
    }
    // Nearest, always. The glyphs were rasterised without antialiasing on
    // purpose; letting the GPU smooth them undoes exactly that decision.
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    g_font[slot].ren = ren;
    g_font[slot].tex = tex;
    return true;
}

void gg_font_quit_renderer(SDL_Renderer *ren) {
    for (int i = 0; i < MAX_RENDERERS; i++) {
        if (g_font[i].ren != ren) continue;
        SDL_DestroyTexture(g_font[i].tex);
        g_font[i].ren = nullptr;
        g_font[i].tex = nullptr;
    }
}

void gg_font_quit(void) {
    for (int i = 0; i < MAX_RENDERERS; i++) {
        SDL_DestroyTexture(g_font[i].tex);
        g_font[i].ren = nullptr;
        g_font[i].tex = nullptr;
    }
}

static int advance_of(unsigned char ch) {
    if (ch < GG_FONT_FIRST || ch > GG_FONT_LAST) return GG_FONT_ADV[0];
    return GG_FONT_ADV[ch - GG_FONT_FIRST];
}

int gg_font_width(const char *text) {
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '\n') break;
        w += advance_of(*p);
    }
    return w;
}

void gg_font_draw(SDL_Renderer *ren, int x, int y, SDL_Color c, const char *text) {
    SDL_Texture *tex = texture_for(ren);
    if (!tex || !text) return;

    SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex, c.a);

    const int x0 = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '\n') {
            x = x0;
            y += GG_FONT_CELL_H;
            continue;
        }
        if (*p < GG_FONT_FIRST || *p > GG_FONT_LAST) continue;

        const int idx = *p - GG_FONT_FIRST;
        const SDL_FRect src = {
            (float)((idx % GG_FONT_COLS) * GG_FONT_CELL_W),
            (float)((idx / GG_FONT_COLS) * GG_FONT_CELL_H),
            (float)GG_FONT_CELL_W, (float)GG_FONT_CELL_H,
        };
        const SDL_FRect dst = { (float)x, (float)y,
                                (float)GG_FONT_CELL_W, (float)GG_FONT_CELL_H };
        SDL_RenderTexture(ren, tex, &src, &dst);
        x += advance_of(*p);
    }
}

void gg_font_printf(SDL_Renderer *ren, int x, int y, SDL_Color c,
                    const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    gg_font_draw(ren, x, y, c, buf);
}

void gg_font_center(SDL_Renderer *ren, int cx, int y, SDL_Color c, const char *text) {
    gg_font_draw(ren, cx - gg_font_width(text) / 2, y, c, text);
}
