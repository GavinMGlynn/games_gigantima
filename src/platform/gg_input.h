// gg_input.h - keyboard and gamepad, funnelled into one stream of actions.
//
// A turn-based game wants edges, not levels: holding a direction should step
// once, pause, then repeat, the way a roguelike does - not fire sixty moves a
// second. So this module owns the repeat timer, and hands the simulation one
// gg_action per call.
//
// Both devices are read every frame and OR-ed, so a keyboard and a pad can be
// used interchangeably, even mid-turn, without either stamping on the other.
#ifndef GG_INPUT_H
#define GG_INPUT_H

#include "core/gg_common.h"
#include "core/gg_game.h"

// Travel before a stick counts as a direction, and pull before a trigger
// counts as a press. Generous: this is a walking game, not a twitch one.
#define GG_PAD_DEADZONE 12000
#define GG_PAD_TRIGGER   8000

// Held-direction repeat, in ticks at GG_TICK_HZ. The first step is immediate,
// then nothing for GG_REPEAT_DELAY, then one step every GG_REPEAT_RATE.
//
// GG_REPEAT_RATE must not be shorter than GG_STEP_TICKS. At 5 against a slide
// of 8 the next move landed while the last was still sliding, and
// gg_actor_move_to re-anchors the interpolation on the tile just left - so the
// sprite teleported backwards a third of a tile on every step of a held walk.
// Matching them exactly makes held movement one continuous glide.
#define GG_REPEAT_DELAY 14
#define GG_REPEAT_RATE  GG_STEP_TICKS

// Menu navigation. The pad's face buttons feed this and the world's action
// stream at the same time; the frontend drains whichever suits the screen that
// is showing, and calls gg_input_forget on the way between screens so a press
// meant for a menu cannot swing a sword the instant the world comes back.
typedef enum {
    GG_NAV_NONE = 0,
    GG_NAV_UP,
    GG_NAV_DOWN,
    GG_NAV_LEFT,
    GG_NAV_RIGHT,
    GG_NAV_CHOOSE,      // A
    GG_NAV_BACK,        // B
    GG_NAV_ERASE,       // Y - rubs out a letter while naming a journey
    GG_NAV_ACCEPT,      // Start - "that will do"; opens the pause menu in play
} gg_nav;

typedef struct {
    // Direction currently held, as a tile delta. Both zero means nothing held.
    int dx, dy;

    // Edge-triggered actions latched since the last drain. These are one-shot
    // and never repeat, because "talk" firing sixty times is never wanted.
    gg_action latched;
    gg_nav    nav_latched;

    // Start, which is the one button that means something on every screen:
    // "pause" in the world, "that will do" in a menu. Kept apart from the two
    // streams above so that whichever one is being drained can read it.
    bool pause_latched;

    int repeat;              // ticks until the held direction steps again
    bool was_held;

    SDL_Gamepad   *pad;
    SDL_JoystickID pad_id;
    bool           pad_rumbles;
    bool           no_rumble;

    // Keyboard and pad tracked apart so releasing one does not clear the other.
    int kb_dx, kb_dy;
    int pad_dx, pad_dy;
} gg_input;

void gg_input_init(gg_input *in, bool no_rumble);
void gg_input_quit(gg_input *in);

// Feed every SDL event here. Returns true if the event was consumed.
bool gg_input_event(gg_input *in, const SDL_Event *ev);

// Once per tick, before asking for actions: samples the pad's analog state and
// advances the repeat timer.
void gg_input_tick(gg_input *in);

// The action for this tick, or GG_ACT_NONE. Call once per tick while the world
// is showing, and never in the same tick as gg_input_nav: both advance the
// held-direction timer, so calling both would eat every other step.
gg_action gg_input_take(gg_input *in);

// The menu command for this tick, or GG_NAV_NONE. Call once per tick while a
// menu is showing, instead of gg_input_take.
//
// Held directions repeat on the same timer the world uses, so running the
// cursor down a long list of journeys feels like walking a long corridor.
gg_nav gg_input_nav(gg_input *in);

// Whether Start has been pressed since the last ask. Safe to call alongside
// gg_input_take, because it touches no timer.
bool gg_input_take_pause(gg_input *in);

// Drops everything latched but not yet drained. Call when changing screens: a
// button pressed for the screen being left has no business firing on the one
// being entered.
void gg_input_forget(gg_input *in);

// Short rumble, ignored when there is no pad or the player disabled it.
void gg_input_rumble(gg_input *in, uint16_t low, uint16_t high, uint32_t ms);

#endif // GG_INPUT_H
