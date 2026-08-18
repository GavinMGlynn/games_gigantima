// gg_edit.h - the map being edited, and everything that can be done to it.
//
// Deliberately separate from the editor's window. Every operation the mouse
// drives is a function here, over a plain document, with no renderer and no
// event in sight - so a test can author a whole map by making the same calls a
// person would make with a mouse, save it, and play it in the game. That is the
// item's own verification, and it is not a thing that can be checked by taking
// a picture of a window.
//
// The same split the rest of the project uses: gg_render draws, gg_game
// decides, and neither knows the other's business.
#ifndef GG_EDIT_H
#define GG_EDIT_H

#include "core/gg_common.h"
#include "core/gg_world.h"

typedef enum {
    GG_TOOL_TERRAIN,    // paint the ground
    GG_TOOL_PROP,       // put a thing on it
    GG_TOOL_ITEM,       // leave something lying about
    GG_TOOL_ACTOR,      // place a person
    GG_TOOL_SCHEDULE,   // give the selected person somewhere to be
    GG_TOOL_REGION,     // drag out a named area
    GG_TOOL_START,      // where a new game begins
    GG_TOOL_PORTAL,     // a way out, into another map
    GG_TOOL_COUNT
} gg_tool;

#define GG_EDIT_PATH_MAX 1024
#define GG_EDIT_SAY_MAX   128

// How many mistakes deep the editor remembers. Each step is a copy of the whole
// map - about 130 KB for the size of map this game ships - so thirty is a few
// megabytes, which is nothing, and deeper than anybody actually reaches for.
#define GG_EDIT_UNDO_MAX  30

typedef struct {
    gg_map map;
    bool   open;                       // a map is loaded
    bool   dirty;                      // changed since the last save
    char   path[GG_EDIT_PATH_MAX];     // where it came from, or "" for new

    gg_tool tool;

    // The brush: what each tool would place. One per tool rather than one
    // shared, so switching to props and back does not lose the terrain you
    // were painting with.
    int terrain, prop, item, art, region_kind;
    int item_count;

    // Which person is selected, for the schedule tool, and which of their
    // hours is being placed next.
    int actor;
    int sched_slot;

    // Where the portal tool's next way out leads. Set with gg_edit_link_to
    // rather than typed, because the editor has no text entry yet - a named
    // gap, and the reason the editor takes --link on its command line.
    char portal_to[GG_MAP_NAME_MAX];
    int  portal_x, portal_y;

    // A region being dragged out. Two corners, and the second follows the
    // mouse until it is let go.
    bool drag;
    int  drag_x, drag_y;

    char say[GG_EDIT_SAY_MAX];         // the last thing that happened

    // Undo, as whole snapshots of the map rather than as a list of what each
    // operation did. A map is a hundred kilobytes; an undo record per tool
    // would be eight kinds of record, each with its own way of being wrong,
    // and the first one anybody got wrong would corrupt somebody's map. This
    // is one shape, obviously correct, and thirty of them is a few megabytes.
    //
    // Held as pointers so a gg_editor can still sit on a stack: the snapshots
    // themselves are on the heap, taken when something is about to change and
    // dropped from the bottom when there are too many.
    gg_map *undo[GG_EDIT_UNDO_MAX];
    gg_map *redo[GG_EDIT_UNDO_MAX];
    int     undos, redos;

    // A stroke is one drag of the mouse and therefore one thing to undo, not
    // one per tile it crossed. See gg_edit_stroke.
    bool stroke, stroke_marked;
} gg_editor;

// --- the document ----------------------------------------------------------
// A blank map of grass. Returns false only if it could not be allocated.
bool gg_edit_new(gg_editor *e, int w, int h);
bool gg_edit_load(gg_editor *e, const char *path);
bool gg_edit_save(gg_editor *e, const char *path);
void gg_edit_close(gg_editor *e);

// --- the tools -------------------------------------------------------------
void gg_edit_tool(gg_editor *e, gg_tool t);

// Steps the current tool's brush. `by` is +1 or -1; it wraps.
void gg_edit_brush(gg_editor *e, int by);

// What the brush is, in words, for the palette to show.
const char *gg_edit_brush_name(const gg_editor *e);
const char *gg_tool_name(gg_tool t);

// --- taking it back --------------------------------------------------------
// Everything that changes the map remembers what it was first, so an editor
// without an undo - which is an editor people are afraid to try things in - is
// not what this is any more.
//
// A drag of the mouse is one thing to undo and not one per tile: the frontend
// says where a stroke begins and ends, and everything inside it is one step.
void gg_edit_stroke(gg_editor *e, bool begin);

// Puts the map back as it was before the last thing that changed it, or
// forward again. False when there is nothing to go back to.
bool gg_edit_undo(gg_editor *e);
bool gg_edit_redo(gg_editor *e);

// How many steps are remembered in each direction, for the status line and for
// a test that wants to say what it expects.
int gg_edit_undos(const gg_editor *e);
int gg_edit_redos(const gg_editor *e);

// --- acting on a tile ------------------------------------------------------
// The primary action - what a left click does with the current tool.
void gg_edit_apply(gg_editor *e, int x, int y);

// The secondary - what a right click does. Rubs out whatever the current tool
// places, so one tool is both the brush and the rubber and there is no
// separate erase mode to be stuck in.
void gg_edit_erase(gg_editor *e, int x, int y);

// Dragging, for the region tool. Anything else ignores these.
void gg_edit_drag_start(gg_editor *e, int x, int y);
void gg_edit_drag_end(gg_editor *e, int x, int y);

// Floods the run of ground joined to (x, y) with whatever the terrain brush
// holds, and returns how many tiles changed. Four-connected, and it matches on
// the terrain alone - a lake and the beach around it are different ground, and
// filling one should not eat the other.
//
// Only the ground tool fills. A flood of props or of people is not a thing
// anybody wants by accident, and there is nothing sensible for it to mean.
int gg_edit_fill(gg_editor *e, int x, int y);

// --- people ----------------------------------------------------------------
// The person at (x, y), or -1.
int gg_edit_actor_at(const gg_editor *e, int x, int y);

// Names the selected person. Ignored if nobody is selected.
void gg_edit_name_actor(gg_editor *e, const char *name);

// Renames the region under (x, y), or the last one drawn if nothing is there.
// Regions come out called "town 1" until somebody says otherwise, and a map
// whose places are all called "town 1" is a map nobody can write a story in.
bool gg_edit_name_region(gg_editor *e, int x, int y, const char *name);

// And the map itself, which is what the status band shows a player.
void gg_edit_name_map(gg_editor *e, const char *name);

// Where the portal tool's next way out will lead: a map's leaf name and a tile
// in it. Portals placed after this all go there until it is changed again.
void gg_edit_link_to(gg_editor *e, const char *map, int x, int y);

// --- checking --------------------------------------------------------------
// Everything wrong with the map that would matter to the game, written into
// `out`. Returns how many problems there are; zero means it is playable.
//
// The editor is where a map should be found to be broken, not the game.
#define GG_EDIT_PROBLEMS_MAX 8
int gg_edit_check(const gg_editor *e, char out[GG_EDIT_PROBLEMS_MAX][GG_EDIT_SAY_MAX]);

#endif // GG_EDIT_H
