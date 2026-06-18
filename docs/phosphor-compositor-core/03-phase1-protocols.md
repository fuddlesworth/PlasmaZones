// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 1: Core Wayland Protocols + Scene Graph

## Deliverables

- Server-side protocol implementations: wl_compositor, wl_subcompositor, wl_shm, wl_output, xdg_wm_base/xdg_surface/xdg_toplevel/xdg_popup
- Scene graph with typed nodes
- Surface state machine (double-buffered pending/current)
- Damage tracking per-output
- Frame callback dispatch

## Class Hierarchy

```
SceneNode (abstract base)
├── SceneTree          — non-leaf grouping node
├── SceneSurface       — wraps a wl_surface (has buffer, damage, etc.)
├── SceneRect          — solid-color rectangle (backgrounds, borders)
└── SceneBuffer        — raw texture node (for SSD icons, cursor)

SceneOutput
├── references a SceneTree (the root of the scene for this output)
├── owns DamageTracker
└── owns FrameScheduler

Surface (wl_surface implementation)
├── PendingState       — accumulated before commit
├── CurrentState       — applied on commit
├── SubsurfaceList     — ordered children
└── Role (union/variant)
    ├── XdgToplevel
    ├── XdgPopup
    ├── LayerSurface (Phase 6)
    └── SubsurfaceRole

XdgToplevel
├── title, appId
├── states (maximized, fullscreen, activated, resizing, tiled_*)
├── minSize, maxSize
├── pendingConfigures (serial queue)
└── parent (for transient stacking)

XdgPopup
├── parent XdgSurface
├── positioner result (computed geometry)
└── grab state
```

## File Map

```
libs/phosphor-compositor-core/src/
├── scene/
│   ├── CMakeLists.txt
│   ├── scene_node.h              — Base SceneNode with transform, visibility, parent
│   ├── scene_node.cpp
│   ├── scene_tree.h              — Non-leaf container node
│   ├── scene_tree.cpp
│   ├── scene_surface.h           — Surface node (references Surface)
│   ├── scene_surface.cpp
│   ├── scene_rect.h              — Solid color rectangle
│   ├── scene_rect.cpp
│   ├── scene_output.h            — Per-output scene state + damage
│   ├── scene_output.cpp
│   ├── scene_graph.h             — Top-level: root tree + output list
│   ├── scene_graph.cpp
│   ├── damage_tracker.h          — Per-output damage accumulation
│   └── damage_tracker.cpp
├── protocols/
│   ├── CMakeLists.txt
│   ├── wayland_generated/        — wayland-scanner output (build dir, gitignored)
│   ├── surface.h                 — Surface class (wl_surface server-side)
│   ├── surface.cpp
│   ├── surface_state.h           — PendingState / CurrentState structs
│   ├── compositor_global.h       — wl_compositor global + surface factory
│   ├── compositor_global.cpp
│   ├── subcompositor.h           — wl_subcompositor + wl_subsurface
│   ├── subcompositor.cpp
│   ├── shm.h                     — wl_shm + wl_shm_pool + wl_buffer (SHM)
│   ├── shm.cpp
│   ├── output_global.h           — wl_output global per IOutput
│   ├── output_global.cpp
│   ├── xdg_shell.h               — xdg_wm_base global
│   ├── xdg_shell.cpp
│   ├── xdg_surface.h             — xdg_surface (role container)
│   ├── xdg_surface.cpp
│   ├── xdg_toplevel.h            — xdg_toplevel
│   ├── xdg_toplevel.cpp
│   ├── xdg_popup.h               — xdg_popup + positioner
│   └── xdg_popup.cpp

include/PhosphorCompositorCore/
├── SceneGraph.h                  — Public scene graph API
├── Surface.h                     — Public surface type
├── XdgToplevel.h                 — Public toplevel type
└── XdgPopup.h                    — Public popup type
```

## Surface State Machine

```
                  ┌────────────────┐
                  │  UNMAPPED      │  (no buffer committed, no role, or role destroyed)
                  └───────┬────────┘
                          │ first commit with buffer + role assigned
                          ▼
                  ┌────────────────┐
                  │  MAPPED        │  (visible, receives input, gets frame callbacks)
                  └───────┬────────┘
                          │ null buffer committed OR role requests unmap
                          ▼
                  ┌────────────────┐
                  │  UNMAPPED      │  (hidden, no input, no frame callbacks)
                  └────────────────┘
```

### Commit Semantics (Double-Buffered)

```cpp
struct PendingState {
    PendingState() {
        pixman_region32_init(&damage);
        pixman_region32_init(&opaqueRegion);
        pixman_region32_init(&inputRegion);
    }
    ~PendingState() {
        pixman_region32_fini(&damage);
        pixman_region32_fini(&opaqueRegion);
        pixman_region32_fini(&inputRegion);
    }
    PendingState(const PendingState&) = delete;
    PendingState& operator=(const PendingState&) = delete;
    PendingState(PendingState&&) = delete;
    PendingState& operator=(PendingState&&) = delete;

    std::optional<BufferRef> buffer;         // wl_surface.attach
    pixman_region32_t damage;                // wl_surface.damage / damage_buffer
    pixman_region32_t opaqueRegion;          // wl_surface.set_opaque_region
    pixman_region32_t inputRegion;           // wl_surface.set_input_region
    std::optional<int> bufferTransform;      // wl_surface.set_buffer_transform
    std::optional<int> bufferScale;          // wl_surface.set_buffer_scale
    std::optional<QPoint> bufferOffset;      // wl_surface.offset (v5+)
    QList<wl_resource*> frameCallbacks;    // wl_surface.frame
    // subsurface-specific:
    std::optional<QPoint> subsurfacePosition;
    bool subsurfaceSync = true;
};

struct CurrentState {
    CurrentState() {
        pixman_region32_init(&opaqueRegion);
        pixman_region32_init(&inputRegion);
    }
    ~CurrentState() {
        pixman_region32_fini(&opaqueRegion);
        pixman_region32_fini(&inputRegion);
    }
    CurrentState(const CurrentState&) = delete;
    CurrentState& operator=(const CurrentState&) = delete;
    CurrentState(CurrentState&&) = delete;
    CurrentState& operator=(CurrentState&&) = delete;

    BufferRef buffer;                        // Current attached buffer
    pixman_region32_t opaqueRegion;
    pixman_region32_t inputRegion;
    int bufferTransform = 0;
    int bufferScale = 1;
    QPoint bufferOffset;
};
```

**Commit algorithm:**
1. If role is subsurface in sync mode: cache pending, don't apply. Applied when parent commits.
2. Apply pending → current (only dirty fields)
3. Clear pending state
4. If buffer changed: update scene node texture reference, mark damaged
5. If first buffer with role: transition UNMAPPED → MAPPED, insert into scene graph
6. If null buffer: transition MAPPED → UNMAPPED, remove from scene graph
7. Accumulate frame callbacks for output
8. Mark overlapping outputs as damaged

## Scene Graph Design

### Node Properties (all nodes)

```cpp
class SceneNode {
public:
    virtual ~SceneNode() = default;
    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;
    SceneNode(SceneNode&&) = delete;
    SceneNode& operator=(SceneNode&&) = delete;

protected:
    SceneNode() = default;

private:
    SceneTree* m_parent = nullptr;
    QPointF m_position;         // relative to parent
    bool m_visible = true;
    int m_zIndex = 0;           // sorting within parent's children

    // Computed (cached, invalidated on tree mutation):
    QRectF m_globalBounds;      // position in output-space
    bool m_occluded = false;    // fully covered by opaque regions above
};
```

### Tree Structure

```
SceneGraph (root)
├── backgroundLayer (SceneTree, z=0)
│   └── [wallpaper SceneRect per output]
├── bottomLayer (SceneTree, z=1)  — layer-shell bottom
├── windowLayer (SceneTree, z=2)
│   ├── XdgToplevel A (SceneTree)
│   │   ├── decoration (SceneTree) — SSD title bar, border
│   │   ├── mainSurface (SceneSurface)
│   │   └── subsurfaces (SceneSurface...)
│   ├── XdgToplevel B (SceneTree)
│   │   └── ...
│   └── popups (SceneTree, above all toplevels in their parent's stacking)
├── topLayer (SceneTree, z=3)     — layer-shell top
├── overlayLayer (SceneTree, z=4) — layer-shell overlay
└── cursorLayer (SceneTree, z=5)  — hardware cursor fallback
```

### Damage Tracking

```cpp
class DamageTracker {
public:
    /// Add damage in output-local coordinates
    void addDamage(pixman_region32_t* region);

    /// Add damage for a surface commit (surface-local → output-local transform)
    void addSurfaceDamage(SceneSurface* surface, pixman_region32_t* surfaceDamage);

    /// Add damage for a node move (old bounds union new bounds)
    void addMoveDamage(const QRectF& oldBounds, const QRectF& newBounds);

    /// Compute accumulated damage for the current frame, accounting for buffer age.
    /// @param bufferAge How many frames old the backbuffer is (EGL_EXT_buffer_age)
    /// @param[out] result Caller-owned region; init'd by caller, populated by this method
    void
    accumulatedDamage(int bufferAge, pixman_region32_t* result) const;

    /// Mark frame rendered — rotate damage history
    void
    frameRendered();

    DamageTracker(int outputWidth, int outputHeight) {
        for (auto& h : m_history) pixman_region32_init(&h);
        pixman_region32_init(&m_currentDamage);
        pixman_region32_init_rect(&m_fullOutputRegion, 0, 0, outputWidth, outputHeight);
    }
    ~DamageTracker() {
        for (auto& h : m_history) pixman_region32_fini(&h);
        pixman_region32_fini(&m_currentDamage);
        pixman_region32_fini(&m_fullOutputRegion);
    }
    DamageTracker(const DamageTracker&) = delete;
    DamageTracker& operator=(const DamageTracker&) = delete;
    DamageTracker(DamageTracker&&) = delete;
    DamageTracker& operator=(DamageTracker&&) = delete;

private:
    // Ring buffer of per-frame damage (for buffer age accumulation)
    static constexpr int HistorySize = 4;
    pixman_region32_t m_history[HistorySize];
    int m_historyIndex = 0;

    // Current frame's damage (union of all addDamage calls this frame)
    pixman_region32_t m_currentDamage;

    // Full output region (initialized from output dimensions, used for full-repaint fallback)
    pixman_region32_t m_fullOutputRegion;
};
```

**Algorithm: compute render damage for this frame**
```
accumulatedDamage(bufferAge, result*):
    if bufferAge == 0 or bufferAge > HistorySize + 1:
        pixman_region32_copy(result, &m_fullOutputRegion)  // unknown or too old — must repaint everything
        return
    pixman_region32_copy(result, &m_currentDamage)
    for i in 1..bufferAge-1:
        idx = (m_historyIndex - i + HistorySize) % HistorySize  // always positive
        pixman_region32_union(result, result, &m_history[idx])
```

## Protocol Implementation Details

### wl_compositor (v6)

```
Globals registered: wl_compositor v6
Requests handled:
  - create_surface → allocate Surface, return wl_resource
  - create_region → allocate Region, return wl_resource

wl_surface requests:
  - attach(buffer, x, y)     → pending.buffer = buffer, pending.offset = (x,y)
  - damage(x, y, w, h)       → pending.damage union rect (surface coords)
  - damage_buffer(x,y,w,h)   → pending.damage union rect (buffer coords, transformed to surface)
  - frame(callback)           → pending.frameCallbacks.append(callback)
  - set_opaque_region(region) → pending.opaqueRegion = region
  - set_input_region(region)  → pending.inputRegion = region
  - set_buffer_transform(t)   → pending.bufferTransform = t
  - set_buffer_scale(s)       → pending.bufferScale = s
  - offset(x, y) [v5]        → pending.bufferOffset = (x, y)
  - commit                    → apply pending state (see commit algorithm above)
  - destroy                   → unmap if mapped, destroy resource
```

### xdg_toplevel State Machine

```
                  configure(serial, width, height, states[])
Client ←─────────────────────────────────────────────── Compositor
         (sent on: map, resize, state change, output enter)

                  ack_configure(serial)
Client ───────────────────────────────────────────────→ Compositor
         (client acknowledges a specific configure)

                  commit (with new buffer matching acked configure)
Client ───────────────────────────────────────────────→ Compositor
         (surface applied at the acked geometry)
```

**Configure queue:**
- Compositor queues configure events (each with a serial)
- Client acks them out-of-order or in-order
- On commit, compositor applies state from the LAST acked configure
- Unacked configures are considered "pending" — geometry isn't final yet

```cpp
struct PendingConfigure {
    uint32_t serial;
    QSize size;               // 0,0 = client decides
    QList<uint32_t> states; // XDG_TOPLEVEL_STATE_*
};

class XdgToplevel {
    // ...
    static constexpr int MaxPendingConfigures = 32;
    QList<PendingConfigure> m_pendingConfigures;
    uint32_t m_lastAckedSerial = 0;

    void sendConfigure(QSize size, QList<uint32_t> states);
    void ackConfigure(uint32_t serial);
    void applyOnCommit();  // called from surface commit

    // If m_pendingConfigures exceeds MaxPendingConfigures, close the surface
    // (protocol error: client is not acking configures)
};
```

### xdg_popup + Positioner

**Positioner computation algorithm:**
```
Input:
  - anchorRect (parent-surface-local rect)
  - anchorEdge (top/bottom/left/right/center)
  - gravity (which direction popup grows from anchor point)
  - size (popup desired size)
  - offset (additional pixel offset)
  - constraintAdjustments (flip, slide, resize)
  - parentBounds (usable area of output relative to parent)

Algorithm:
  1. Compute unconstrained position from anchor point + gravity
  2. Add offset
  3. Check if popup fits within parentBounds
  4. If not, apply constraint adjustments in order:
     a. FLIP: mirror anchor edge and gravity
     b. SLIDE: shift popup along constraint axis to fit
     c. RESIZE: shrink popup to fit
  5. Return final (x, y, width, height)
```

## Frame Scheduling

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ Client       │     │ Compositor   │     │ DRM/Output   │
│              │     │              │     │              │
│  commit() ──────→  │ mark damage  │     │              │
│              │     │              │     │              │
│              │     │              │  ←── pageflip done │
│              │     │ render frame ─────→ │ submit      │
│              │     │              │     │              │
│  ←────────────────  │ frame callback │   │              │
│ (can render  │     │ (done event) │     │              │
│  next frame) │     │              │     │              │
└──────────────┘     └──────────────┘     └──────────────┘
```

For Phase 1 (headless): a 60Hz QTimer triggers frame dispatch instead of DRM page-flip events.

## Verification

1. Build with headless backend
2. `WAYLAND_DISPLAY=... foot` → terminal opens (no visible rendering yet, but protocol handshake works)
3. `WAYLAND_DISPLAY=... weston-simple-shm` → client allocates SHM buffer, commits, receives frame callback
4. Unit tests:
   - `test_surface_commit` — verify pending→current state transition
   - `test_subsurface_sync` — verify parent-driven commit for sync subsurfaces
   - `test_xdg_toplevel_configure` — send configure, verify ack, verify state on commit
   - `test_xdg_positioner` — compute popup position with various constraint adjustments
   - `test_damage_tracker` — add damage, verify accumulation with buffer age
   - `test_scene_graph` — insert/remove nodes, verify z-ordering, verify global bounds computation
