// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 6: Layer Shell + Session Lock + Supporting Protocols

## Deliverables

- wlr-layer-shell-unstable-v1 server implementation
- ext-session-lock-v1 server implementation
- wl_data_device_manager v3 (clipboard + drag-and-drop)
- zwp_primary_selection_device_manager_v1
- xdg-decoration-unstable-v1 (SSD/CSD negotiation)
- wp-fractional-scale-v1
- wp-viewporter
- wp-presentation-time
- wp-tearing-control-v1
- ext-idle-notify-v1
- ext-idle-inhibit-v1 (zwp_idle_inhibit_manager_v1)
- ext-foreign-toplevel-list-v1
- Shell process launch + privileged client handling

## Class Hierarchy

```
LayerShellGlobal
├── creates LayerSurface per client request
├── manages exclusive zone reservations
└── computes usable area per output

LayerSurface
├── Surface* surface (the wl_surface + layer role)
├── Layer enum (Background, Bottom, Top, Overlay)
├── Anchor edges (top, bottom, left, right)
├── ExclusiveZone (pixels reserved from output edge)
├── Margin (top, right, bottom, left)
├── SceneNode in appropriate layer of scene graph
└── KeyboardInteractivity (none, exclusive, on-demand)

SessionLockManager
├── owns active SessionLock (at most one)
├── manages lock surface per output
└── gates all input routing when locked

SessionLock
├── locked state (once locked, stays locked until unlock/destroy)
├── LockSurface per output (covers entire output)
└── disables keyboard focus to all non-lock surfaces

DataDeviceManager
├── creates DataDevice per seat
├── manages DataSource lifecycle
├── coordinates selection (clipboard) ownership
└── coordinates drag-and-drop flows

DataDevice
├── current selection (DataSource from clipboard owner)
├── current DnD state (if drag in progress)
└── sends events to focused client

PrimarySelectionManager
├── mirrors DataDeviceManager for primary selection (middle-click paste)
└── separate selection ownership from clipboard

ClipboardBridge
├── coordinates selection events between focused surfaces
├── MIME type negotiation
└── fd-based data transfer

DragAndDrop
├── DragSource (origin surface + data source)
├── DragIcon (optional surface rendered at cursor)
├── DropTarget (surface under cursor during drag)
├── state machine: idle → started → entered → dropped / cancelled

XdgDecorationManager
├── negotiates server-side vs client-side decoration per toplevel
└── responds with mode: server | client

FractionalScaleManager
├── sends preferred_scale to surfaces on output enter
└── updates on output change / window move between outputs

Viewporter
├── wp_viewport per surface
├── source rect (crop) + destination size (scale)
└── applied during texture sampling in renderer

PresentationTime
├── wp_presentation per surface
├── fires presentation feedback after DRM page-flip
├── reports actual presentation timestamp + refresh interval

TearingControl
├── per-surface tearing preference (vsync | async)
├── async: skip waiting for vblank on page-flip
└── only valid for fullscreen surfaces on direct scanout path

IdleManager
├── tracks user activity (input events reset timer)
├── creates IdleNotification per client request
└── fires idle/resume events at configured timeout

IdleInhibitManager
├── tracks inhibitors (active surface → prevent idle)
├── any active inhibitor prevents idle notifications
└── inhibitor destroyed on surface unmap

ForeignToplevelList
├── ext-foreign-toplevel-list-v1 global
├── advertises all managed toplevels to subscribing clients (task managers)
└── sends title, appId, states (maximized, minimized, activated, fullscreen)
```

## File Map

```
libs/phosphor-compositor-core/src/protocols/
├── layer_shell.h
├── layer_shell.cpp
├── layer_surface.h
├── layer_surface.cpp
├── session_lock.h
├── session_lock.cpp
├── data_device.h
├── data_device.cpp
├── data_source.h
├── data_source.cpp
├── drag_and_drop.h
├── drag_and_drop.cpp
├── primary_selection.h
├── primary_selection.cpp
├── xdg_decoration.h
├── xdg_decoration.cpp
├── fractional_scale.h
├── fractional_scale.cpp
├── viewporter.h
├── viewporter.cpp
├── presentation_time.h
├── presentation_time.cpp
├── tearing_control.h
├── tearing_control.cpp
├── idle_notify.h
├── idle_notify.cpp
├── idle_inhibit.h
├── idle_inhibit.cpp
├── foreign_toplevel.h
├── foreign_toplevel.cpp

libs/phosphor-compositor-core/src/shell/
├── CMakeLists.txt
├── shell_launcher.h            — spawns phosphor-shell process
├── shell_launcher.cpp
├── privileged_client.h         — identifies shell as privileged
└── privileged_client.cpp
```

## Layer Shell

### Exclusive Zone Algorithm

```
For each output, compute usable area by subtracting exclusive zones:

usableArea = output.geometry

For each layer surface (sorted by creation order):
  if surface.exclusiveZone > 0:
    if anchored to TOP:
      usableArea.top += surface.exclusiveZone + surface.margin.top + surface.margin.bottom
    if anchored to BOTTOM:
      usableArea.bottom -= surface.exclusiveZone + surface.margin.top + surface.margin.bottom
    if anchored to LEFT:
      usableArea.left += surface.exclusiveZone + surface.margin.left + surface.margin.right
    if anchored to RIGHT:
      usableArea.right -= surface.exclusiveZone + surface.margin.left + surface.margin.right

Clamp: usableArea width/height must remain >= MinUsableSize (e.g. 100×100).
If an exclusive zone would reduce below minimum, reject it (set effective exclusive zone to 0).

Remaining usableArea = available space for toplevels (maximize geometry, placement bounds)
```

### Layer Surface Positioning

```cpp
QRect LayerSurface::computeGeometry(const QRect& outputGeo, const QRect& usableArea) const {
    QRect result;

    // Width: if anchored left AND right, fill between margins. Otherwise use requested size.
    if ((m_anchor & Anchor::Left) && (m_anchor & Anchor::Right)) {
        result.setLeft(outputGeo.left() + m_margin.left());
        result.setRight(outputGeo.right() - m_margin.right());
    } else {
        result.setWidth(m_desiredSize.width());
        if (m_anchor & Anchor::Left) {
            result.moveLeft(outputGeo.left() + m_margin.left());
        } else if (m_anchor & Anchor::Right) {
            result.moveRight(outputGeo.right() - m_margin.right());
        } else {
            result.moveLeft(outputGeo.center().x() - result.width() / 2);
        }
    }

    // Height: symmetric logic for top/bottom anchors
    // ... (same pattern as width) ...

    return result;
}
```

### Layer Surface Mapping into Scene Graph

```
Scene graph integration:
  Background layer → layerSurfaces with Layer::Background
  Bottom layer     → layerSurfaces with Layer::Bottom
  (window layer is between)
  Top layer        → layerSurfaces with Layer::Top
  Overlay layer    → layerSurfaces with Layer::Overlay

Each layer surface creates a SceneSurface node in the appropriate tree.
Keyboard interactivity:
  - None: never receives keyboard focus
  - Exclusive: steals keyboard focus when mapped (modals, app launchers)
  - OnDemand: can receive focus via click but doesn't steal
```

## Session Lock

### State Machine

```
                    ┌────────────────┐
                    │  UNLOCKED      │  (normal operation)
                    └───────┬────────┘
                            │ client sends lock request
                            ▼
                    ┌────────────────┐
                    │  LOCKING       │  (waiting for lock surfaces on all outputs)
                    └───────┬────────┘
                            │ all outputs have lock surfaces committed
                            ▼
                    ┌────────────────┐
                    │  LOCKED        │  (screen locked)
                    │                │  — all non-lock input blocked
                    │                │  — all non-lock surfaces hidden
                    │                │  — lock surfaces rendered fullscreen
                    └───────┬────────┘
                            │ client sends unlock_and_destroy
                            ▼
                    ┌────────────────┐
                    │  UNLOCKED      │  (resume normal operation)
                    └────────────────┘
```

### Security Requirements

- Only ONE lock can be active at a time (reject subsequent lock requests)
- Once locked, ALL input goes ONLY to lock surfaces (even if they crash)
- If lock client disconnects while locked: keep showing last lock surface (don't unlock)
- If lock client disconnects during LOCKING (before all outputs covered): render solid black on uncovered outputs, remain locked
- Kill switch: magic key combo (Ctrl+Alt+Backspace ×3) as emergency unlock (disabled by default; explicit opt-in required in config with security warning)

## Clipboard (wl_data_device)

### Selection Flow

```
Client A                  Compositor                Client B
(owner)                                            (requester)
  │                          │                        │
  │ set_selection(source) ──→│                        │
  │                          │ store source reference │
  │                          │                        │
  │                          │ (Client B gains focus) │
  │                          │──→ selection(offer)    │
  │                          │  (sends MIME types)    │
  │                          │                        │
  │                          │     ←── receive(mime, fd) │
  │  ←── send(mime, fd) ─────│                        │
  │ (write data to fd)       │                        │
  │ (close fd)               │                        │
```

### Drag and Drop State Machine

```
  [Idle]
    │ pointer button down + client starts drag
    ▼
  [Started]
    │ pointer enters a surface
    ▼
  [Entered]
    │ pointer moves → send motion events to drop target
    │ pointer leaves surface → send leave, go to [Started]
    │ pointer button released:
    ├─ if target accepted → [Dropped]
    └─ if target rejected → [Cancelled]

  [Dropped]
    │ target calls finish → [Finished]
    │ data transfer happens via fd

  [Cancelled]
    │ source destroyed, DnD ends
```

## XDG Decoration Negotiation

```cpp
void XdgDecorationManager::handleGetToplevelDecoration(XdgToplevel* toplevel) {
    // Our compositor always prefers server-side decorations
    // But respects client preference for CSD-only apps
    auto clientPref = toplevel->decorationPreference();

    if (clientPref == DecorationMode::ClientSide) {
        // Client explicitly wants CSD (e.g., GNOME apps, Electron)
        // Respect it — send mode = client_side
        sendMode(toplevel, DecorationMode::ClientSide);
        toplevel->setDecorationMode(DecorationMode::ClientSide);
    } else {
        // Client prefers SSD or has no preference → we draw decorations
        sendMode(toplevel, DecorationMode::ServerSide);
        toplevel->setDecorationMode(DecorationMode::ServerSide);
    }
}
```

## Shell Process Integration

```
Compositor                              phosphor-shell
    │                                        │
    │ 1. Fork/exec phosphor-shell            │
    │    (pass WAYLAND_DISPLAY via env +     │
    │     privileged token via sealed memfd) │
    │                                        │
    │                  ←─────────────────────│ 2. Connect as Wayland client
    │                                        │    (present token for auth)
    │ 3. Mark client as privileged           │
    │    (layer-shell overlay access,        │
    │     compositor-private protocol)       │
    │                                        │
    │                  ←─────────────────────│ 4. Create layer surfaces:
    │                                        │    - Panel (Top, exclusive zone)
    │                                        │    - Launcher (Overlay, on-demand)
    │                                        │    - Notifications (Top)
    │                                        │
    │ 5. Shell surfaces positioned           │
    │    per layer-shell protocol            │
    │                                        │
```

```cpp
class ShellLauncher : public QObject {
    Q_OBJECT
public:
    explicit ShellLauncher(QObject* parent = nullptr);

    void launchShell();

private Q_SLOTS:
    void onShellDisconnected();  // restart after delay

private:
    void generatePrivilegedToken();
    void spawnProcess();
    void onShellConnected(wl_client* client);

    QProcess m_shellProcess;
    QString m_token;  // single-use: invalidated after first successful auth

    // Token is passed via a sealed memfd (not environment variable) to prevent
    // /proc/<pid>/environ leakage. The fd is passed to the child via SCM_RIGHTS
    // on the Wayland socket or as an inherited fd (closed after fork).
};
```

## Fractional Scale

```cpp
void FractionalScaleManager::onSurfaceEnteredOutput(Surface* surface, IOutput* output) {
    double preferredScale = output->scale();
    // Convert to fractional scale wire format (scale * 120)
    uint32_t wireScale = qRound(preferredScale * 120.0);
    wp_fractional_scale_v1_send_preferred_scale(surface->fractionalScaleResource(), wireScale);
}
```

## Presentation Time

```cpp
void PresentationTime::onPageFlipComplete(DrmOutput* output, uint32_t secHi, uint32_t secLo,
                                           uint32_t nsec, uint32_t refreshNsec) {
    // Take ownership of the list to avoid iterator invalidation during wl_resource_destroy
    auto feedbackList = std::exchange(m_pendingFeedback[output], {});
    for (auto* feedback : feedbackList) {
        wp_presentation_feedback_send_presented(
            feedback->resource,
            secHi, secLo, nsec,
            refreshNsec,
            0,  // seq_hi
            output->pageFlipSequence(),  // seq_lo (monotonic frame counter)
            WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
            WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK |
            WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION
        );
        wl_resource_destroy(feedback->resource);
    }
}
```

## Idle Management

```cpp
class IdleManager {
public:
    void onInputEvent() {
        m_lastActivity = std::chrono::steady_clock::now();
        if (m_idle) {
            m_idle = false;
            for (auto* notification : m_notifications) {
                if (notification->fired) {
                    ext_idle_notification_v1_send_resumed(notification->resource);
                    notification->fired = false;
                }
            }
        }
    }

    void checkIdle() {
        auto elapsed = std::chrono::steady_clock::now() - m_lastActivity;
        for (auto* notification : m_notifications) {
            if (!notification->fired && elapsed >= notification->timeout) {
                // Check inhibitors
                if (hasActiveInhibitor()) continue;
                ext_idle_notification_v1_send_idled(notification->resource);
                notification->fired = true;
            }
        }
    }

private:
    bool hasActiveInhibitor() const {
        for (auto* inhibitor : m_inhibitors) {
            if (inhibitor->surface && inhibitor->surface->isMapped()) {
                return true;
            }
        }
        return false;
    }
};
```

## Data Flow: Layer Surface Lifecycle

```
Shell Process              Compositor                       Scene Graph
     │                         │                               │
     │ get_layer_surface(      │                               │
     │   surface, output,      │                               │
     │   layer=Top,            │                               │
     │   namespace="panel") ──→│                               │
     │                         │ Create LayerSurface           │
     │                         │ Compute geometry from         │
     │                         │   anchor + exclusive zone     │
     │                         │                               │
     │  ←── configure(w, h) ──│                               │
     │                         │                               │
     │ ack_configure + commit ─→│                              │
     │ (with buffer)           │ Insert SceneSurface          │
     │                         │   into topLayer ────────────→ │ (z-order: above windows)
     │                         │                               │
     │                         │ Recalculate usableArea        │
     │                         │ Notify toplevels of new       │
     │                         │   maximized geometry          │
     │                         │                               │
```

## Verification

1. Shell panel appears at top edge with exclusive zone (windows don't overlap it)
2. Second panel (dock) at bottom — both exclusive zones respected
3. Clipboard: copy in one app, paste in another
4. Primary selection: select text, middle-click paste in terminal
5. DnD: drag file from file manager → text editor accepts drop
6. Session lock: screen blanks, only lock surface receives input
7. Fullscreen game with tearing control: no vsync stutter (if supported)
8. `wlr-randr` shows fractional scale applied (e.g., 1.25×)
9. OBS task manager shows foreign toplevel list
10. Unit tests:
    - `test_layer_shell_exclusive_zone` — usable area computation
    - `test_layer_shell_positioning` — anchor combinations → correct geometry
    - `test_session_lock_blocks_input` — locked state rejects non-lock input
    - `test_clipboard_transfer` — MIME negotiation + fd data transfer
    - `test_dnd_state_machine` — full drag lifecycle
    - `test_idle_notify` — timer fires, inhibitor prevents
    - `test_xdg_decoration` — negotiation sends correct mode
    - `test_fractional_scale` — preferred scale sent on output enter
