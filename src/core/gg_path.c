// gg_path.c - A* over the tile grid.
#include "core/gg_path.h"

bool gg_path_init(gg_pathfinder *pf, int w, int h) {
    SDL_zerop(pf);
    if (w <= 0 || h <= 0) return false;

    const size_t n = (size_t)w * (size_t)h;
    pf->gcost = SDL_calloc(n, sizeof *pf->gcost);
    pf->came  = SDL_calloc(n, sizeof *pf->came);
    pf->stamp = SDL_calloc(n, sizeof *pf->stamp);
    pf->fcost = SDL_calloc(n, sizeof *pf->fcost);
    pf->heap  = SDL_calloc(n, sizeof *pf->heap);
    if (!pf->gcost || !pf->came || !pf->stamp || !pf->fcost || !pf->heap) {
        gg_path_free(pf);
        return false;
    }
    pf->w = w;
    pf->h = h;
    // Stamps start at zero and cells are zeroed, so the first search must use
    // a generation no cell can already carry.
    pf->gen = 0;
    return true;
}

void gg_path_free(gg_pathfinder *pf) {
    if (!pf) return;
    SDL_free(pf->gcost);
    SDL_free(pf->came);
    SDL_free(pf->stamp);
    SDL_free(pf->fcost);
    SDL_free(pf->heap);
    SDL_zerop(pf);
}

// --- binary min-heap over cell indices, ordered by fcost -------------------
// Ties break on the cell index. Not cosmetic: two nodes with equal f must come
// out in the same order every run, or a seeded world stops being reproducible.
static bool heap_less(const gg_pathfinder *pf, int32_t a, int32_t b) {
    if (pf->fcost[a] != pf->fcost[b]) return pf->fcost[a] < pf->fcost[b];
    return a < b;
}

static void heap_push(gg_pathfinder *pf, int32_t cell) {
    int i = pf->heapn++;
    pf->heap[i] = cell;
    while (i > 0) {
        const int parent = (i - 1) / 2;
        if (!heap_less(pf, pf->heap[i], pf->heap[parent])) break;
        const int32_t t = pf->heap[i];
        pf->heap[i] = pf->heap[parent];
        pf->heap[parent] = t;
        i = parent;
    }
}

static int32_t heap_pop(gg_pathfinder *pf) {
    const int32_t top = pf->heap[0];
    pf->heap[0] = pf->heap[--pf->heapn];
    int i = 0;
    for (;;) {
        const int l = 2 * i + 1, r = l + 1;
        int best = i;
        if (l < pf->heapn && heap_less(pf, pf->heap[l], pf->heap[best])) best = l;
        if (r < pf->heapn && heap_less(pf, pf->heap[r], pf->heap[best])) best = r;
        if (best == i) break;
        const int32_t t = pf->heap[i];
        pf->heap[i] = pf->heap[best];
        pf->heap[best] = t;
        i = best;
    }
    return top;
}

// ---------------------------------------------------------------------------
// The search
// ---------------------------------------------------------------------------
// Fixed order, and it must stay fixed: it decides which of two equally good
// paths is taken, and that has to be the same on every machine and every run.
static const int DX[8] = {  0,  0, -1,  1, -1,  1, -1,  1 };
static const int DY[8] = { -1,  1,  0,  0, -1, -1,  1,  1 };

bool gg_path_next_step(gg_pathfinder *pf, gg_passable_fn passable, void *ctx,
                       int sx, int sy, int tx, int ty, int budget,
                       int *nx, int *ny) {
    if (!pf->gcost) return false;
    if (sx < 0 || sy < 0 || sx >= pf->w || sy >= pf->h) return false;
    if (sx == tx && sy == ty) return false;          // already there

    pf->gen++;
    pf->heapn = 0;

    const int32_t start = (int32_t)(sy * pf->w + sx);
    const bool target_on_map = tx >= 0 && ty >= 0 && tx < pf->w && ty < pf->h;
    const int32_t goal = target_on_map ? (int32_t)(ty * pf->w + tx) : -1;

    pf->gcost[start] = 0;
    pf->came[start]  = -1;
    pf->stamp[start] = pf->gen;
    pf->fcost[start] = gg_path_heuristic(sx, sy, tx, ty);
    heap_push(pf, start);

    // The closest cell reached, by heuristic. If the goal turns out to be
    // walled off or the budget runs out, the step heads here instead - which
    // is what makes a blocked actor edge around an obstacle rather than stand
    // still and give up.
    int32_t best = start;
    uint32_t best_h = pf->fcost[start];
    bool found = false;
    int expanded = 0;

    while (pf->heapn > 0 && expanded < budget) {
        const int32_t cur = heap_pop(pf);
        // Stale heap entry: this cell was reached again more cheaply after it
        // was pushed. Cheaper to skip it here than to sift the heap.
        if (pf->fcost[cur] > pf->gcost[cur] +
                             gg_path_heuristic(cur % pf->w, cur / pf->w, tx, ty))
            continue;

        if (cur == goal) { found = true; best = cur; break; }
        expanded++;

        const int cx = cur % pf->w, cy = cur / pf->w;
        for (int d = 0; d < 8; d++) {
            const int ax = cx + DX[d], ay = cy + DY[d];
            if (ax < 0 || ay < 0 || ax >= pf->w || ay >= pf->h) continue;

            const int32_t next = (int32_t)(ay * pf->w + ax);
            const bool is_goal = next == goal;

            // The goal may be occupied - walking up to somebody is a legal
            // path - but nothing else on the way may be.
            if (!is_goal && !passable(ctx, ax, ay)) continue;

            const bool diagonal = DX[d] != 0 && DY[d] != 0;
            if (diagonal) {
                // No cutting corners. Without this an actor slips diagonally
                // between two walls that meet at a point, which looks like
                // walking through the join and lets it leave a sealed room.
                if (!passable(ctx, cx + DX[d], cy) ||
                    !passable(ctx, cx, cy + DY[d])) continue;
            }

            const uint16_t step = (uint16_t)(diagonal ? GG_STEP_DIAG : GG_STEP_ORTHO);
            const uint16_t g = (uint16_t)(pf->gcost[cur] + step);

            if (pf->stamp[next] == pf->gen && g >= pf->gcost[next]) continue;

            pf->stamp[next] = pf->gen;
            pf->gcost[next] = g;
            pf->came[next]  = cur;

            const uint32_t h = gg_path_heuristic(ax, ay, tx, ty);
            pf->fcost[next] = g + h;
            heap_push(pf, next);

            if (h < best_h) { best_h = h; best = next; }
        }
    }

    if (!found && best == start) return false;       // boxed in completely
    if (found) best = goal;

    // Walk the chain back to the start; the step we want is the cell whose
    // predecessor *is* the start.
    int32_t node = best;
    int guard = pf->w * pf->h;
    while (pf->came[node] != start && pf->came[node] != -1 && guard-- > 0)
        node = pf->came[node];
    if (pf->came[node] != start) return false;

    *nx = node % pf->w;
    *ny = node / pf->w;
    return true;
}
