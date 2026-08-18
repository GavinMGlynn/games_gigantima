## What this changes

<!-- One or two sentences. If it closes an issue, say "Closes #N". -->

## How you know it works

<!-- The project's own rule: verify on the real output. A test name, a `ctest`
     run, a `--shot` frame, a replay file. "It should work now" is not a
     verification. -->

## Checklist

- [ ] `ctest` is green
- [ ] Built with at least two of GCC, Clang and MSVC — they do not warn about
      the same things
- [ ] A test lands with it, named as the fact it pins, and I checked it by
      breaking that fact and watching it fail
- [ ] `docs/PROJECT_STATUS.md` and `docs/COMPLETION_PLAN.md` still tell the
      truth, and are updated in this same commit if they did not
- [ ] If it touches `src/core/`: no renderer, window, event, gamepad or audio
      type, no floating point, no wall-clock time
- [ ] If it regenerates the art: `assets/CREDITS.md` is regenerated with it
