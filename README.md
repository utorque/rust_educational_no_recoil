# Pre-Computed Cursor Compensation System — Technical Analysis

This document describes the architecture of a Lua script running on a **peripheral input scripting platform** (e.g. Logitech G-Hub / LGS). It intercepts hardware events and injects synthetic mouse movements to counteract predictable, repetitive cursor drift caused by a sustained action.

---

## Platform API

The script operates inside a sandboxed Lua runtime exposed by the peripheral driver. Available primitives:

| API call | Purpose |
|---|---|
| `MoveMouseRelative(x, y)` | Moves the cursor by (x, y) pixels relative to current position |
| `IsMouseButtonPressed(n)` | Returns `true` if mouse button `n` is currently held |
| `EnablePrimaryMouseButtonEvents(bool)` | Enables/disables forwarding of the primary button event to the OS |
| `IsModifierPressed("lctrl")` | Reads keyboard modifier state |
| `PressKey / ReleaseKey / PressAndReleaseKey` | Synthesises keyboard input |
| `PressMouseButton / ReleaseMouseButton` | Synthesises mouse button events |
| `GetRunningTime()` | Returns milliseconds elapsed since script start (used for timing) |
| `OutputLogMessage(str)` | Writes to the driver debug log |
| `OnEvent(event, arg)` | Entry point: called by the driver on every hardware event |

---

## High-level structure

The system is split into five sections.

### 1. Configuration block

Every supported **action profile** has a pair of variables:
- `<PROFILE>_<SLOT>` — the side-button number (4 or 5) that activates this profile, or `nil` to leave it unbound.
- `<PROFILE>_<SLOT>_<MODIFIER>` — boolean flags for optional **modifier states** that affect compensation behaviour (e.g. optical magnification level, barrel attachment type).

Two global scale parameters:
- `SENSITIVITY` — input sensitivity scalar (default 0.4).
- `FOV` — field-of-view scalar (default 78), affects the ratio between abstract movement units and screen pixels.

The system supports 13 distinct profiles, each bindable to 2 independent slots — 26 possible bindings total.

---

### 2. Offset data tables

For each profile, three pre-recorded arrays are stored:
- `<PROFILE>_OFFSET_X` — horizontal cursor delta to apply per event step (abstract units).
- `<PROFILE>_OFFSET_Y` — vertical cursor delta per step (typically negative = downward pull).
- `<PROFILE>_RPM` — rate of the sustained action in cycles per minute, used to derive inter-step timing.
- `<PROFILE>_BULLETS` — length of the offset arrays (number of steps for which compensation data exists).

The Y-offsets are large negative values (e.g. `−1.35` per step), meaning the script continuously pulls the cursor downward to cancel an upward drift pattern.

---

### 3. Scale multiplier pre-computation

Before `OnEvent` is ever invoked, raw offsets are converted into final pixel-delta values accounting for:

- **View magnification** — each magnification level has a multiplier converting abstract units to screen pixels:
  - No magnification: ×1.0
  - Low magnification (e.g. ×1.2–×1.7, profile-dependent)
  - High magnification level 1 (e.g. ×6.75–×9.75)
  - High magnification level 2 (e.g. ×13.5–×15.5)
  - Reduced-magnification mode (e.g. ×0.8–×0.9)
- **Output modifier** — optional modifier flags slightly adjust timing or movement (typically ×0.9 or ×1.0).
- **Screen multiplier formula** (computed once at startup):
  ```lua
  screenMultiplier = -0.03 * (SENSITIVITY * 3) * (FOV / 100)
  ```
  Maps abstract recoil units to actual pixel deltas for the user's specific sensitivity and FOV.

Results are stored in precomputed arrays (e.g. `N1_PROFILE_C_X`, `N1_PROFILE_C_Y`, `N1_PROFILE_AT`, `N1_PROFILE_ST`) so no floating-point work occurs during the hot loop.

---

### 4. Main event loop — `OnEvent`

The driver calls `OnEvent(event, arg)` on every hardware button event.

#### Toggle mechanism
When a configured side-button (4 or 5) is **pressed**, a boolean flag `kickback` is flipped (`kickback = not kickback`), acting as an on/off toggle for that profile's compensation. A log message (`<PROFILE>_MACRO-ON / OFF`) is emitted.

#### Compensation loop (per profile)
Once `kickback == true`, the script waits for **both** a secondary trigger (button 3, e.g. activating a modifier mode) and a primary trigger (button 1, initiating the sustained action) to be held simultaneously. Then:

1. Waits ~5 ms for the first event step to register.
2. Iterates through the precomputed delta arrays in a `for` loop — one iteration per step.
3. For each step:
   - Calls `Smoothing(steps, dx, dy)` to move the cursor.
   - Waits for the inter-step delay derived from the action rate (`ST` value).
   - Checks if either trigger was released — exits immediately if so.
4. After all steps are exhausted, enters a `repeat...until` loop that keeps re-applying the last step's delta as long as the primary trigger is held.

**Modifier-state branch**: If `lctrl` is held, the script applies the magnified-mode offsets directly, bypassing the `StandMultiplier` (×1.89) that normally accounts for the standing/upright state. This simulates reduced drift in a crouched or stabilised posture.

#### The `Smoothing` function
```lua
function Smoothing(steps, dx, dy)
    x_ = 0; y_ = 0; t_ = 0
    for d = 1, steps do
        xI = round(d * dx / steps)
        yI = round(d * dy / steps)
        tI = d * steps / steps  -- always equals d
        MoveMouseRelative(round(xI - x_), round(yI - y_))
        sasd2441(tI - t_)
        x_ = xI; y_ = yI; t_ = tI
    end
end
```
Linearly interpolates the total `(dx, dy)` movement across `steps` sub-steps. Each sub-step calls `MoveMouseRelative` with the incremental delta, producing a smooth continuous drag rather than a single instantaneous jump. `steps` is derived from `AT` (attack time in ms), so longer intervals produce finer interpolation.

#### Single-event rapid-fire mode
For profiles that represent **discrete, non-auto actions** (i.e. the action does not repeat automatically when the trigger is held), the script additionally synthesises a **Pause/Break key press** before each step. This converts a continuously held primary trigger into a stream of individual trigger events at the profile's maximum rate — effectively simulating rapid repeated activation.

---

### 5. Sequential input macro

A secondary, independent feature. When a designated button is pressed:
1. Presses a configured interaction key (`E`) and waits 250 ms.
2. Moves the cursor 50 px diagonally to a target UI element.
3. Clicks once, then releases the interaction key.
4. Types a pre-configured 4-digit numeric code with 40 ms delays between each keystroke.

This automates the entry of a numeric passcode into an on-screen input interface.

---

## Summary

The system implements a **pre-computed, per-step cursor drift compensation loop** that:

1. Pre-calculates exact pixel deltas for every step of every profile, scaled by the user's sensitivity, FOV, and active modifier flags.
2. Waits for both a secondary and a primary trigger to be held simultaneously before activating.
3. Replays the delta sequence in real time — one entry per step — with precise inter-step timing derived from each profile's action rate.
4. Exits immediately when either trigger is released.

The `Smoothing` function ensures each delta is delivered as a gradual interpolated movement rather than an instantaneous jump, making the synthetic motion closely resemble organic human input.
