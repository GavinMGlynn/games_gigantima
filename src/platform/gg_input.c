// gg_input.c - keyboard and gamepad handling.
#include "platform/gg_input.h"

// WSL2 has no kernel driver for an Xbox pad: it speaks GIP, a vendor-specific
// protocol that usbhid will not bind and this kernel ships no xpad for. SDL can
// still drive it through libusb from userspace, but only if its whitelist is
// waived - that whitelist exists to stop libusb stealing devices from kernel
// drivers, and under WSL there are none to steal. Scoped to WSL for exactly
// that reason: on real Linux this hint could take a device away from the driver
// already handling it.
static bool running_under_wsl(void) {
    SDL_IOStream *io = SDL_IOFromFile("/proc/sys/kernel/osrelease", "rb");
    if (!io) return false;

    char buf[128] = { 0 };
    const size_t n = SDL_ReadIO(io, buf, sizeof buf - 1);
    SDL_CloseIO(io);
    buf[n] = '\0';

    for (char *p = buf; *p; p++) *p = (char)SDL_tolower((unsigned char)*p);
    return SDL_strstr(buf, "microsoft") || SDL_strstr(buf, "wsl");
}

static void pad_open_first(gg_input *in) {
    if (in->pad) return;

    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) return;

    for (int i = 0; i < count && !in->pad; i++) {
        in->pad = SDL_OpenGamepad(ids[i]);
        if (!in->pad) continue;
        in->pad_id = ids[i];
        in->pad_rumbles = SDL_GetBooleanProperty(
            SDL_GetGamepadProperties(in->pad),
            SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
        const char *name = SDL_GetGamepadName(in->pad);
        SDL_Log("gigantima: gamepad connected: %s (rumble %s)",
                name ? name : "unnamed",
                in->no_rumble    ? "disabled" :
                in->pad_rumbles  ? "supported" : "not supported");
    }
    SDL_free(ids);
}

static void pad_close(gg_input *in) {
    if (!in->pad) return;
    // Stop any effect still running, or the pad is left buzzing after we exit.
    if (in->pad_rumbles) SDL_RumbleGamepad(in->pad, 0, 0, 0);
    SDL_CloseGamepad(in->pad);
    in->pad = nullptr;
    in->pad_id = 0;
    in->pad_rumbles = false;
    in->pad_dx = in->pad_dy = 0;
    SDL_Log("gigantima: gamepad disconnected");
}

void gg_input_init(gg_input *in, bool no_rumble) {
    SDL_zerop(in);
    in->no_rumble = no_rumble;

    if (running_under_wsl()) {
        SDL_SetHint("SDL_HIDAPI_LIBUSB_WHITELIST", "0");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    }

    // A missing gamepad subsystem is not fatal: this game is fully playable on
    // the keyboard, so it is a log line, not an error.
    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        pad_open_first(in);
        if (!in->pad) SDL_Log("gigantima: no gamepad detected (keyboard only)");
    } else {
        SDL_Log("gigantima: gamepad subsystem unavailable: %s", SDL_GetError());
    }
}

void gg_input_quit(gg_input *in) {
    pad_close(in);
}

void gg_input_rumble(gg_input *in, uint16_t low, uint16_t high, uint32_t ms) {
    if (!in->pad || !in->pad_rumbles || in->no_rumble) return;
    SDL_RumbleGamepad(in->pad, low, high, ms);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
void gg_input_bind(gg_input *in, const gg_settings *set) {
    SDL_memcpy(in->key, set->key, sizeof in->key);
    SDL_memcpy(in->alt, set->alt, sizeof in->alt);
}

// Which action a key is bound to, or GG_ACT_NONE.
//
// One table for every key, movement and verbs alike, because "rebindable keys"
// that cannot move the walk keys is a promise with a hole in it - and because
// the eight directions are actions like any other here.
static gg_action key_action(const gg_input *in, SDL_Scancode sc) {
    if (sc == SDL_SCANCODE_UNKNOWN) return GG_ACT_NONE;
    for (int a = 1; a < GG_ACT_COUNT; a++)
        if (in->key[a] == sc || in->alt[a] == sc) return (gg_action)a;
    return GG_ACT_NONE;
}

static bool key_direction(const gg_input *in, SDL_Scancode sc, int *dx, int *dy) {
    return gg_action_delta(key_action(in, sc), dx, dy);
}

bool gg_input_event(gg_input *in, const SDL_Event *ev) {
    switch (ev->type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        pad_open_first(in);
        return true;

    case SDL_EVENT_GAMEPAD_REMOVED:
        if (ev->gdevice.which == in->pad_id) pad_close(in);
        return true;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        // Each button is given both meanings at once. Which one is honoured is
        // decided later, by whichever drain the frontend calls for the screen
        // that is showing.
        switch (ev->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            in->latched = GG_ACT_TALK; in->nav_latched = GG_NAV_CHOOSE; return true;
        case SDL_GAMEPAD_BUTTON_EAST:
            in->latched = GG_ACT_WAIT; in->nav_latched = GG_NAV_BACK;   return true;
        case SDL_GAMEPAD_BUTTON_WEST:
            in->latched = GG_ACT_LOOK; return true;
        case SDL_GAMEPAD_BUTTON_NORTH:
            in->latched = GG_ACT_OPEN; in->nav_latched = GG_NAV_ERASE;  return true;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            in->latched = GG_ACT_CAST; return true;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            // The four face buttons are spoken for, and striking wants a
            // button of its own rather than a modifier.
            in->latched = GG_ACT_FIGHT; return true;
        case SDL_GAMEPAD_BUTTON_BACK:
            // The other menu button, and the pack is the other menu.
            in->latched = GG_ACT_PACK; return true;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            // The last button on a pad that nothing else wanted. Pressing the
            // stick opens a page you close with B, so catching it by accident
            // costs a keypress and nothing else.
            in->latched = GG_ACT_SHEET; return true;
        case SDL_GAMEPAD_BUTTON_START:
            in->pause_latched = true; return true;
        default: return false;
        }

    // The two triggers. The face buttons and the shoulders were all spoken for
    // and two verbs were left with no way to reach them at all: picking a thing
    // up off the ground, and reading the journal. A pad that cannot pick things
    // up cannot finish the story, which is not a controller anybody would call
    // supported.
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        bool *held = nullptr;
        gg_action verb = GG_ACT_NONE;
        if (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
            held = &in->trigger_left;
            verb = GG_ACT_GET;
        } else if (ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            held = &in->trigger_right;
            verb = GG_ACT_JOURNAL;
        } else {
            return false;              // a stick; sampled per tick, not here
        }

        // Hysteresis, so one pull is one action: it fires on the way past the
        // threshold and cannot fire again until it has fallen back to half of
        // it. A single threshold makes a finger resting on the edge fire every
        // time the axis jitters.
        if (!*held && ev->gaxis.value > GG_PAD_TRIGGER) {
            *held = true;
            in->latched = verb;
        } else if (*held && ev->gaxis.value < GG_PAD_TRIGGER / 2) {
            *held = false;
        }
        return true;
    }

    case SDL_EVENT_KEY_DOWN: {
        int dx, dy;
        if (key_direction(in, ev->key.scancode, &dx, &dy)) {
            if (!ev->key.repeat) {
                // A fresh press steps at once and restarts the delay; SDL's own
                // key repeat is ignored because its rate is a desktop text
                // setting, not a game one.
                in->kb_dx = dx;
                in->kb_dy = dy;
                in->repeat = 0;
                in->was_held = false;
            }
            return true;
        }
        if (ev->key.repeat) return false;

        const gg_action verb = key_action(in, ev->key.scancode);
        if (verb == GG_ACT_NONE) return false;
        in->latched = verb;
        return true;
    }

    case SDL_EVENT_KEY_UP: {
        int dx, dy;
        if (key_direction(in, ev->key.scancode, &dx, &dy)) {
            // Only clear if this is the key still being tracked; releasing an
            // older key must not cancel a newer one still held.
            if (in->kb_dx == dx && in->kb_dy == dy) in->kb_dx = in->kb_dy = 0;
            return true;
        }
        return false;
    }

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Per-tick sampling
// ---------------------------------------------------------------------------
void gg_input_tick(gg_input *in) {
    in->pad_dx = in->pad_dy = 0;
    if (in->pad) {
        const Sint16 ax = SDL_GetGamepadAxis(in->pad, SDL_GAMEPAD_AXIS_LEFTX);
        const Sint16 ay = SDL_GetGamepadAxis(in->pad, SDL_GAMEPAD_AXIS_LEFTY);
        if (ax < -GG_PAD_DEADZONE) in->pad_dx = -1;
        if (ax >  GG_PAD_DEADZONE) in->pad_dx =  1;
        if (ay < -GG_PAD_DEADZONE) in->pad_dy = -1;
        if (ay >  GG_PAD_DEADZONE) in->pad_dy =  1;

        if (SDL_GetGamepadButton(in->pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  in->pad_dx = -1;
        if (SDL_GetGamepadButton(in->pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) in->pad_dx =  1;
        if (SDL_GetGamepadButton(in->pad, SDL_GAMEPAD_BUTTON_DPAD_UP))    in->pad_dy = -1;
        if (SDL_GetGamepadButton(in->pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  in->pad_dy =  1;
    }

    // Keyboard wins when both are pushed, on the theory that a hand on the
    // keyboard is the more deliberate of the two.
    in->dx = in->kb_dx ? in->kb_dx : in->pad_dx;
    in->dy = in->kb_dy ? in->kb_dy : in->pad_dy;

    if (in->repeat > 0) in->repeat--;
    if (in->dx == 0 && in->dy == 0) {
        in->was_held = false;
        in->repeat = 0;
    }
}

static gg_action action_for(int dx, int dy) {
    if (dx < 0 && dy < 0) return GG_ACT_NW;
    if (dx > 0 && dy < 0) return GG_ACT_NE;
    if (dx < 0 && dy > 0) return GG_ACT_SW;
    if (dx > 0 && dy > 0) return GG_ACT_SE;
    if (dx < 0) return GG_ACT_W;
    if (dx > 0) return GG_ACT_E;
    if (dy < 0) return GG_ACT_N;
    if (dy > 0) return GG_ACT_S;
    return GG_ACT_NONE;
}

// True when the held direction should fire this tick: at once on a fresh
// press, then after GG_REPEAT_DELAY, then every GG_REPEAT_RATE. Shared by both
// drains so a menu cursor and a walking sprite move to the same rhythm.
static bool direction_fires(gg_input *in) {
    if (in->dx == 0 && in->dy == 0) return false;

    if (!in->was_held) {
        in->was_held = true;
        in->repeat = GG_REPEAT_DELAY;
        return true;
    }
    if (in->repeat == 0) {
        in->repeat = GG_REPEAT_RATE;
        return true;
    }
    return false;
}

gg_action gg_input_take(gg_input *in) {
    // One-shot actions jump the queue: they are rarer and more deliberate than
    // walking, and making the player wait out a repeat tick to talk feels
    // broken even though it is only 16 ms.
    if (in->latched != GG_ACT_NONE) {
        const gg_action a = in->latched;
        in->latched = GG_ACT_NONE;
        return a;
    }

    return direction_fires(in) ? action_for(in->dx, in->dy) : GG_ACT_NONE;
}

bool gg_input_take_pause(gg_input *in) {
    const bool pressed = in->pause_latched;
    in->pause_latched = false;
    return pressed;
}

void gg_input_forget(gg_input *in) {
    in->latched = GG_ACT_NONE;
    in->nav_latched = GG_NAV_NONE;
    in->pause_latched = false;
}

gg_nav gg_input_nav(gg_input *in) {
    if (gg_input_take_pause(in)) return GG_NAV_ACCEPT;

    if (in->nav_latched != GG_NAV_NONE) {
        const gg_nav n = in->nav_latched;
        in->nav_latched = GG_NAV_NONE;
        return n;
    }

    if (!direction_fires(in)) return GG_NAV_NONE;

    // Vertical wins over horizontal on a diagonal: a menu is a column, so a
    // stick pushed up and slightly left should still go up.
    if (in->dy < 0) return GG_NAV_UP;
    if (in->dy > 0) return GG_NAV_DOWN;
    if (in->dx < 0) return GG_NAV_LEFT;
    return GG_NAV_RIGHT;
}
