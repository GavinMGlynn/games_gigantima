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
static bool key_direction(SDL_Scancode sc, int *dx, int *dy) {
    switch (sc) {
    // Arrows, WASD and the numeric keypad. The keypad matters: it is the only
    // layout that makes the four diagonals reachable as single keys, which a
    // grid game with eight-way movement genuinely wants.
    case SDL_SCANCODE_UP:    case SDL_SCANCODE_W: case SDL_SCANCODE_KP_8:
        *dx = 0;  *dy = -1; return true;
    case SDL_SCANCODE_DOWN:  case SDL_SCANCODE_S: case SDL_SCANCODE_KP_2:
        *dx = 0;  *dy = 1;  return true;
    case SDL_SCANCODE_LEFT:  case SDL_SCANCODE_A: case SDL_SCANCODE_KP_4:
        *dx = -1; *dy = 0;  return true;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: case SDL_SCANCODE_KP_6:
        *dx = 1;  *dy = 0;  return true;
    case SDL_SCANCODE_KP_7: *dx = -1; *dy = -1; return true;
    case SDL_SCANCODE_KP_9: *dx = 1;  *dy = -1; return true;
    case SDL_SCANCODE_KP_1: *dx = -1; *dy = 1;  return true;
    case SDL_SCANCODE_KP_3: *dx = 1;  *dy = 1;  return true;
    default: return false;
    }
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
        switch (ev->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: in->latched = GG_ACT_TALK;  return true;
        case SDL_GAMEPAD_BUTTON_WEST:  in->latched = GG_ACT_LOOK;  return true;
        case SDL_GAMEPAD_BUTTON_NORTH: in->latched = GG_ACT_OPEN;  return true;
        case SDL_GAMEPAD_BUTTON_EAST:  in->latched = GG_ACT_WAIT;  return true;
        default: return false;
        }

    case SDL_EVENT_KEY_DOWN: {
        int dx, dy;
        if (key_direction(ev->key.scancode, &dx, &dy)) {
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

        switch (ev->key.scancode) {
        case SDL_SCANCODE_T:      in->latched = GG_ACT_TALK; return true;
        case SDL_SCANCODE_L:      in->latched = GG_ACT_LOOK; return true;
        case SDL_SCANCODE_O:      in->latched = GG_ACT_OPEN; return true;
        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_PERIOD:
        case SDL_SCANCODE_KP_5:   in->latched = GG_ACT_WAIT; return true;
        default: return false;
        }
    }

    case SDL_EVENT_KEY_UP: {
        int dx, dy;
        if (key_direction(ev->key.scancode, &dx, &dy)) {
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

gg_action gg_input_take(gg_input *in) {
    // One-shot actions jump the queue: they are rarer and more deliberate than
    // walking, and making the player wait out a repeat tick to talk feels
    // broken even though it is only 16 ms.
    if (in->latched != GG_ACT_NONE) {
        const gg_action a = in->latched;
        in->latched = GG_ACT_NONE;
        return a;
    }

    if (in->dx == 0 && in->dy == 0) return GG_ACT_NONE;

    if (!in->was_held) {
        in->was_held = true;
        in->repeat = GG_REPEAT_DELAY;
        return action_for(in->dx, in->dy);
    }
    if (in->repeat == 0) {
        in->repeat = GG_REPEAT_RATE;
        return action_for(in->dx, in->dy);
    }
    return GG_ACT_NONE;
}
