// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 4: Input Pipeline + Seat Management

## Deliverables

- libinput event processing (pointer, keyboard, touch, tablet, switch, gesture)
- libxkbcommon keyboard state (keymap compilation, modifier tracking, key repeat)
- wl_seat server-side protocol (wl_pointer, wl_keyboard, wl_touch)
- Focus management (keyboard + pointer focus targets)
- Configurable focus policy (click-to-focus, focus-follows-mouse, sloppy)
- Cursor rendering (hardware DRM plane preferred, software fallback)
- Pointer constraints + relative pointer protocols
- `CompositorShortcutBackend` implementing `PhosphorShortcuts::IBackend`

## Class Hierarchy

```
InputManager
├── owns LibinputContext (wrapper around libinput)
├── owns Seat (Wayland seat state)
├── owns KeyRepeatTimer
├── owns GestureRecognizer
└── dispatches events to Seat

LibinputContext
├── libinput* handle
├── opened via Session::openDevice
├── fd integrated into wayland event loop
└── translates libinput_event → internal InputEvent types

Seat
├── owns Pointer (focus surface, position, button state)
├── owns Keyboard (focus surface, xkb state, keymap)
├── owns Touch (active touch points, focus surfaces)
├── owns Tablet (tool tracking, surface mapping)
├── manages focus lifecycle (enter/leave events)
└── serializes events to wl_pointer/wl_keyboard/wl_touch resources

Pointer
├── QPointF position (global compositor space)
├── Surface* focusSurface (topmost surface under cursor)
├── QPointF focusLocalPosition (surface-local coords)
├── uint32_t buttonState (bitmask of held buttons)
├── CursorState (current cursor image/hotspot)
└── constraintState (confined/locked region)

Keyboard
├── xkb_context* / xkb_keymap* / xkb_state*
├── Surface* focusSurface
├── QSet<uint32_t> pressedKeys
├── ModifierState (depressed, latched, locked, group)
├── keymapFd (shared with clients via wl_keyboard.keymap)
└── repeatInfo (rate, delay)

Cursor
├── CursorImage (from client set_cursor or compositor default)
├── hotspot QPoint
├── HardwareCursor (DRM cursor plane assignment)
├── SoftwareCursor (scene graph node fallback)
└── visibility state

FocusPolicy (strategy pattern)
├── ClickToFocusPolicy (default)
├── FocusFollowsMousePolicy
└── SloppyFocusPolicy

CompositorShortcutBackend : PhosphorShortcuts::IBackend
├── registered key sequences
├── intercepts keys before client delivery
└── emits activated(id) on match
```

## File Map

```
libs/phosphor-compositor-core/src/input/
├── CMakeLists.txt
├── input_manager.h
├── input_manager.cpp
├── libinput_context.h
├── libinput_context.cpp
├── input_event.h               — Internal event types (PointerMotion, KeyPress, etc.)
├── seat.h
├── seat.cpp
├── pointer.h
├── pointer.cpp
├── keyboard.h
├── keyboard.cpp
├── touch.h
├── touch.cpp
├── tablet.h
├── tablet.cpp
├── cursor.h
├── cursor.cpp
├── hardware_cursor.h           — DRM cursor plane management
├── hardware_cursor.cpp
├── focus_policy.h              — IFocusPolicy + implementations
├── focus_policy.cpp
├── key_repeat.h                — Timer-based key repeat
├── key_repeat.cpp
├── compositor_shortcut_backend.h
├── compositor_shortcut_backend.cpp

libs/phosphor-compositor-core/src/protocols/
├── wl_seat_protocol.h          — wl_seat/wl_pointer/wl_keyboard/wl_touch server
├── wl_seat_protocol.cpp
├── pointer_constraints.h       — zwp_pointer_constraints_v1
├── pointer_constraints.cpp
├── relative_pointer.h          — zwp_relative_pointer_v1
└── relative_pointer.cpp
```

## Input Event Flow

```
libinput fd readable
    │
    ▼
LibinputContext::dispatch()
    │ libinput_dispatch() + process events
    │
    ├─ LIBINPUT_EVENT_POINTER_MOTION
    │   → InputEvent::PointerMotion{dx, dy, unaccelDx, unaccelDy, time}
    │
    ├─ LIBINPUT_EVENT_POINTER_BUTTON
    │   → InputEvent::PointerButton{button, state, time}
    │
    ├─ LIBINPUT_EVENT_POINTER_SCROLL_*
    │   → InputEvent::PointerAxis{source, axis, value, discrete, time}
    │
    ├─ LIBINPUT_EVENT_KEYBOARD_KEY
    │   → InputEvent::Key{keycode, state, time}
    │
    ├─ LIBINPUT_EVENT_TOUCH_*
    │   → InputEvent::Touch{type, slot, x, y, time}
    │
    └─ LIBINPUT_EVENT_TABLET_*
        → InputEvent::Tablet{...}
    │
    ▼
InputManager::processEvent(InputEvent)
    │
    ├─ Shortcut check (CompositorShortcutBackend::tryConsume)
    │   if consumed: stop here
    │
    ├─ Focus policy evaluation (for pointer/touch)
    │   → may change keyboard focus
    │
    └─ Route to Seat
        │
        ▼
Seat::handlePointerMotion / handleKey / handleTouch
    │
    ├─ Update internal state
    ├─ Hit-test scene graph (for pointer/touch)
    ├─ Compute surface-local coordinates
    ├─ Send enter/leave if focus changed
    └─ Serialize to wl_pointer/wl_keyboard/wl_touch events
```

## Keyboard State Machine

```
                     ┌─────────────────────────┐
                     │ XKB State               │
                     │                         │
 key down ─────────→ │ xkb_state_update_key()  │
                     │ → new modifiers?         │
                     │   yes: send wl_keyboard  │
                     │         .modifiers event │
                     │                         │
 key up ──────────→  │ xkb_state_update_key()  │
                     │ → modifiers changed?     │
                     │   yes: send modifiers    │
                     └─────────────────────────┘

Key Repeat:
  key down (repeatable key):
    1. Start repeat timer: delay = m_repeatDelay ms
    2. After delay: send wl_keyboard.key(repeat)
    3. Start interval timer: period = 1000/m_repeatRate ms
    4. Every period: send wl_keyboard.key(repeat)
  key up:
    1. Cancel repeat timer for that key
```

```cpp
class KeyRepeat {
public:
    void keyDown(uint32_t keycode, uint32_t time);
    void keyUp(uint32_t keycode);
    void setRate(int rate, int delay);  // rate: keys/sec, delay: ms

private:
    struct ActiveRepeat {
        uint32_t keycode;
        std::unique_ptr<QTimer> timer;  // owned; initial delay → then interval
    };
    QHash<uint32_t, ActiveRepeat> m_active;
    int m_rate = 25;   // keys/sec
    int m_delay = 600; // ms
    Keyboard* m_keyboard;
};
```

## Focus Management

### Pointer Focus

```cpp
// m_focusSurface is QPointer<Surface> — auto-nulls if surface is destroyed

void Pointer::updateFocus(QPointF globalPos) {
    m_position = globalPos;

    // Hit-test scene graph: topmost input-accepting surface under cursor
    SceneNode* hit = m_sceneGraph->hitTest(globalPos, HitTestFlag::InputRegionOnly);
    Surface* newFocus = hit ? hit->asSurface()->surface() : nullptr;

    if (newFocus != m_focusSurface.data()) {
        // Leave old surface (QPointer guarantees null-safety if destroyed)
        if (m_focusSurface) {
            sendLeave(m_focusSurface.data());
        }
        // Enter new surface
        m_focusSurface = newFocus;
        if (m_focusSurface) {
            m_focusLocalPosition = globalToSurfaceLocal(globalPos, m_focusSurface.data());
            sendEnter(m_focusSurface.data(), m_focusLocalPosition);
        }
    } else if (m_focusSurface) {
        // Same surface — send motion
        m_focusLocalPosition = globalToSurfaceLocal(globalPos, m_focusSurface.data());
        sendMotion(m_focusLocalPosition);
    }
}
```

### Keyboard Focus (click-to-focus)

```cpp
void ClickToFocusPolicy::onPointerButton(Pointer* pointer, uint32_t button, bool pressed) {
    if (!pressed || button != BTN_LEFT) return;

    Surface* clicked = pointer->focusSurface();
    if (!clicked) return;

    // Find the toplevel owning this surface
    XdgToplevel* toplevel = findToplevelForSurface(clicked);
    if (!toplevel) return;

    // Activate (set keyboard focus + raise)
    m_windowManager->activate(toplevel);
}
```

### Focus-Follows-Mouse Policy

```cpp
void FocusFollowsMousePolicy::onPointerMotion(Pointer* pointer) {
    Surface* surface = pointer->focusSurface();
    if (!surface) return;

    XdgToplevel* toplevel = findToplevelForSurface(surface);
    if (!toplevel) return;

    if (toplevel != m_keyboard->focusToplevel()) {
        // Optional raise delay (configurable)
        if (m_raiseDelay > 0) {
            m_raiseTimer.start(m_raiseDelay);
            m_pendingRaise = toplevel;  // QPointer<XdgToplevel>: safe if destroyed before timer fires
        }
        m_keyboard->setFocus(toplevel->surface());
    }
}
```

## Cursor Rendering

```
Cursor source priority:
  1. Client-set cursor (wl_pointer.set_cursor from focused client)
  2. Compositor default (arrow, resize handles, etc.)
  3. Hidden (client set null cursor)

Rendering path:
  1. Try hardware cursor (DRM cursor plane):
     - Upload cursor image to cursor BO (64x64 or 128x128 max)
     - Set cursor plane position via atomic properties
     - Zero CPU/GPU cost per frame
  2. If hardware fails (image too large, format unsupported, no cursor plane):
     - Render cursor as a SceneBuffer node in the scene graph
     - Re-rendered every frame the cursor moves (adds to damage)
```

```cpp
class HardwareCursor {
public:
    bool trySet(const QImage& image, QPoint hotspot, DrmOutput* output);
    void setPosition(QPoint pos, DrmOutput* output);
    void hide(DrmOutput* output);
    bool isActive() const;

private:
    gbm_bo* m_cursorBo = nullptr;
    uint32_t m_cursorFbId = 0;
    QSize m_maxSize;  // from DRM caps (typically 64x64 or 256x256)
};
```

## CompositorShortcutBackend

```cpp
class CompositorShortcutBackend : public PhosphorShortcuts::IBackend {
    Q_OBJECT
public:
    explicit CompositorShortcutBackend(QObject* parent = nullptr);

    void registerShortcut(const QString& id, const QKeySequence& defaultSeq,
                          const QKeySequence& currentSeq, const QString& description,
                          bool persistent) override;
    void updateShortcut(const QString& id, const QKeySequence& defaultSeq,
                        const QKeySequence& newTrigger) override;
    void unregisterShortcut(const QString& id) override;
    void flush() override;

    /// Called by InputManager before routing key to client.
    /// Returns true if key event was consumed by a shortcut.
    /// Only the compositor and daemon (via PluginAPI) may register shortcuts.
    /// Wayland clients cannot register compositor-level shortcuts through this backend.
    bool tryConsume(uint32_t keycode, bool pressed, uint32_t modifiers);

private:
    struct Binding {
        QString id;
        QKeySequence sequence;
        bool persistent;
    };
    QHash<QString, Binding> m_bindings;

    // Precomputed lookup: (modifiers, keycode) → binding id
    QHash<QPair<uint32_t, uint32_t>, QString> m_lookup;
    void rebuildLookup();
};
```

## Pointer Constraints Protocol

```
zwp_pointer_constraints_v1:
  - lock_pointer(surface, region, lifetime)
    → Hide cursor, report only relative deltas
    → Lifetime: oneshot (unlock on leave) or persistent
  - confine_pointer(surface, region, lifetime)
    → Cursor stays within region (clamp position)
    → Region in surface-local coords

State machine:
  [Inactive] → surface gains pointer focus → [Active]
  [Active] → surface loses pointer focus → [Inactive] (oneshot: destroyed)
  [Active] → client destroys constraint → [Destroyed]
```

## Verification

1. Mouse cursor visible, moves smoothly, responds to acceleration settings
2. Keyboard input reaches terminal — typing works
3. Key repeat: hold a key, see repeated characters at correct rate
4. Click window → it gains focus and raises to top
5. Alt+Tab (shortcut) → switches between windows without reaching clients
6. Pointer confinement: FPS game grabs pointer successfully
7. Hardware cursor: verify DRM cursor plane used (check `drm_info` or GPU profiler)
8. Multi-output: cursor crosses between monitors at the correct edge
9. Unit tests:
   - `test_keyboard_xkb` — keymap compilation, modifier state after key sequences
   - `test_pointer_focus` — hit-test returns correct surface
   - `test_key_repeat` — timer fires at correct intervals
   - `test_shortcut_backend` — register binding, verify tryConsume returns true
   - `test_focus_policy` — click-to-focus vs follow-mouse behavior
