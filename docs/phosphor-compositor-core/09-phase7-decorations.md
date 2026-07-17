// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

# Phase 7: Custom SSD + Window Tabbing + Shading + Effects

## Deliverables

- Compositor-drawn server-side decorations (title bar, buttons, resize handles)
- Window tabbing (multiple toplevels in one frame, tab bar, tab switching)
- Window shading (collapse to title bar height, animated)
- Effects pipeline: dual-kawase blur, box shadows, SDF rounded corners, inactive dim
- Zone-aware decoration theming (highlight active zone)
- Decoration state driven by rules + phosphor-theme

## Class Hierarchy

```
DecorationRenderer
├── creates DecorationFrame per SSD toplevel
├── owns ButtonAssets (icon textures for close/maximize/minimize)
├── reads theme from phosphor-theme (colors, metrics, radii)
└── renders decorations as SceneRect/SceneBuffer nodes

DecorationFrame
├── SceneTree node (parent of toplevel's SceneSurface)
├── TitleBar (SceneRect background + SceneBuffer title text + buttons)
├── Borders (top, left, right, bottom SceneRects for resize handles)
├── ShadowFrame (nine-patch or blurred SceneBuffer)
├── geometry tracks toplevel size + decoration offsets
└── state: normal, maximized (no borders), shaded (collapsed)

TitleBar
├── height (from theme, typically 30-36px)
├── title text (rendered via QStaticText → QImage → SceneBuffer)
├── icon (from appId → XDG icon theme lookup)
├── buttons: close, maximize, minimize, shade, keep-above
├── button layout (configurable: left/right/hidden)
└── active/inactive color state

TabGroup
├── QList<WindowEntry*> members (ordered by tab position)
├── WindowEntry* activeTab (the visible one)
├── DecorationFrame shared (one frame for the whole group)
├── TabBar (row of tab labels above/within title bar)
└── input routing: clicks on tabs switch active

TabBar
├── SceneTree (row of TabButton items)
├── TabButton per member (label + close button)
├── drag-to-reorder support
├── drop-to-add (drag window onto tab bar → join group)
└── double-click to detach tab

WindowShade
├── state: Unshaded | Shading | Shaded | Unshading
├── animation: height lerp from full → titleBar-only
├── surface clip: SceneSurface hidden when fully shaded
└── triggered by: double-click title bar, shortcut, or rule

EffectsPipeline
├── BlurEffect (dual-kawase, configurable radius)
├── ShadowEffect (nine-patch or Gaussian, per-window)
├── RoundedCornersEffect (SDF clip at 4 corners)
├── DimInactiveEffect (opacity multiply on inactive windows)
└── hooks into OutputRenderer draw pass

BlurEffect
├── BlurPass: downsample chain + upsample chain (dual-kawase)
├── per-surface blur region (from surface opaque region inverse)
├── background texture: capture scene behind surface (only the region behind blur surfaces, not full scene)
├── cached blur result per surface (reused if background unchanged; invalidated on damage overlap)
└── strength configurable (radius 10-80px → 4-8 passes)

ShadowEffect
├── ShadowParams (radius, spread, offsetX, offsetY, color)
├── NinePatchShadow (pre-rendered, stretched)
├── OR BlurredRectShadow (dynamic, exact size)
└── rendered behind decoration frame, outside window bounds
```

## File Map

```
libs/phosphor-compositor-core/src/decorations/
├── CMakeLists.txt
├── decoration_renderer.h
├── decoration_renderer.cpp
├── decoration_frame.h
├── decoration_frame.cpp
├── title_bar.h
├── title_bar.cpp
├── title_buttons.h             — Close/max/min button rendering + hit-test
├── title_buttons.cpp
├── border.h                    — Resize borders (hit-test → resize edges)
├── border.cpp
├── tab_group.h
├── tab_group.cpp
├── tab_bar.h
├── tab_bar.cpp
├── window_shade.h
├── window_shade.cpp
├── decoration_theme.h          — Reads from phosphor-theme
├── decoration_theme.cpp

libs/phosphor-compositor-core/src/effects/
├── CMakeLists.txt
├── effects_pipeline.h
├── effects_pipeline.cpp
├── blur_effect.h
├── blur_effect.cpp
├── shadow_effect.h
├── shadow_effect.cpp
├── rounded_corners_effect.h
├── rounded_corners_effect.cpp
├── dim_inactive_effect.h
├── dim_inactive_effect.cpp
├── shaders/
│   ├── kawase_down.frag.glsl   — Downsample pass
│   ├── kawase_up.frag.glsl     — Upsample pass
│   ├── shadow.frag.glsl        — Gaussian shadow
│   ├── rounded_clip.frag.glsl  — SDF corner clip
│   └── passthrough.vert.glsl   — Shared vertex shader
```

## Server-Side Decoration Architecture

### Decoration Frame ↔ Scene Graph

```
windowLayer (SceneTree)
└── toplevel group (SceneTree)  ← DecorationFrame owns this
    ├── shadow (SceneBuffer, z=-2)        — behind everything
    ├── borders (SceneTree, z=-1)         — resize handle hit targets
    │   ├── topBorder (SceneRect)
    │   ├── leftBorder (SceneRect)
    │   ├── rightBorder (SceneRect)
    │   └── bottomBorder (SceneRect)
    ├── titleBar (SceneTree, z=0)
    │   ├── background (SceneRect)        — title bar fill color
    │   ├── icon (SceneBuffer, 16×16)
    │   ├── title (SceneBuffer, rendered text)
    │   ├── tabBar (SceneTree, if tabbed) — tab labels
    │   └── buttons (SceneTree)
    │       ├── close (SceneBuffer)
    │       ├── maximize (SceneBuffer)
    │       └── minimize (SceneBuffer)
    └── clientSurface (SceneSurface, z=1) — the actual window content
```

### Geometry Calculation

```cpp
struct DecorationMetrics {
    int titleBarHeight = 32;
    int borderWidth = 4;       // resize border width (invisible grab area)
    int shadowRadius = 16;     // shadow extends beyond frame
    int cornerRadius = 10;     // rounded corner radius
};

QRect DecorationFrame::windowGeometry(QSize clientSize) const {
    // Frame geometry = client + decorations
    return QRect(
        -m_metrics.borderWidth,
        -m_metrics.titleBarHeight,
        clientSize.width() + 2 * m_metrics.borderWidth,
        clientSize.height() + m_metrics.titleBarHeight + m_metrics.borderWidth
    );
}

QRect DecorationFrame::totalBounds(QSize clientSize) const {
    // Including shadow
    QRect frame = windowGeometry(clientSize);
    return frame.adjusted(
        -m_metrics.shadowRadius, -m_metrics.shadowRadius,
        m_metrics.shadowRadius, m_metrics.shadowRadius
    );
}
```

### Button Hit-Test + Actions

```cpp
enum class TitleButton { Close, Maximize, Minimize, Shade, KeepAbove, Spacer };

struct ButtonLayout {
    QList<TitleButton> left;   // e.g., {Menu}
    QList<TitleButton> right;  // e.g., {Minimize, Maximize, Close}
};

void TitleBar::handleClick(QPointF localPos, uint32_t button) {
    auto* hit = hitTestButton(localPos);
    if (!hit) {
        if (button == BTN_LEFT) {
            // Title bar click: start interactive move
            m_toplevel->startInteractiveMove();
        }
        return;
    }

    switch (hit->type) {
    case TitleButton::Close:
        m_toplevel->requestClose();
        break;
    case TitleButton::Maximize:
        m_toplevel->toggleMaximized();
        break;
    case TitleButton::Minimize:
        m_windowManager->minimize(m_toplevel);
        break;
    case TitleButton::Shade:
        m_windowShade->toggle();
        break;
    case TitleButton::KeepAbove:
        m_windowManager->toggleKeepAbove(m_toplevel);
        break;
    }
}
```

### Resize Handle Edge Detection

```cpp
Qt::Edges DecorationFrame::hitTestEdge(QPointF pos) const {
    Qt::Edges edges;
    QRectF frame = frameGeometry();
    int grab = m_metrics.borderWidth + 4;  // extra grab pixels for usability

    if (pos.x() < frame.left() + grab) edges |= Qt::LeftEdge;
    if (pos.x() > frame.right() - grab) edges |= Qt::RightEdge;
    if (pos.y() < frame.top() + grab) edges |= Qt::TopEdge;
    if (pos.y() > frame.bottom() - grab) edges |= Qt::BottomEdge;

    return edges;
}
```

## Window Tabbing

### Tab Group Data Structure

```cpp
class TabGroup {
public:
    void addTab(WindowEntry* entry, int insertIndex = -1);
    void removeTab(WindowEntry* entry);
    void activateTab(WindowEntry* entry);
    void moveTab(int fromIndex, int toIndex);

    WindowEntry* activeTab() const;
    int tabCount() const;
    int indexOf(WindowEntry* entry) const;
    const QList<WindowEntry*>& tabs() const;

    // The shared decoration frame
    DecorationFrame* frame() const;

private:
    QList<WindowEntry*> m_tabs;
    int m_activeIndex = 0;
    std::unique_ptr<DecorationFrame> m_frame;
};
```

### Tab Lifecycle

```
Creating a tab group:
  1. User drags window A's title bar onto window B's title bar
  2. OR shortcut: "Add to tab group" while both focused
  3. Create TabGroup containing [A, B]
  4. A becomes active tab (its surface visible)
  5. B's surface hidden (SceneSurface::setVisible(false))
  6. Shared DecorationFrame shows tab bar with [A | B] labels

Switching tabs:
  1. Click on tab label in TabBar
  2. TabGroup::activateTab(clicked)
  3. Previous active surface → hidden
  4. New active surface → visible + keyboard focus
  5. Update DecorationFrame title/icon to match new active
  6. Animate crossfade (optional, via phosphor-animation)

Detaching a tab:
  1. Drag tab label away from TabBar (or double-click)
  2. TabGroup::removeTab(entry)
  3. Entry gets its own DecorationFrame
  4. If group has 1 tab remaining → dissolve group, remove TabBar
  5. Detached window placed at cursor position
```

### Tab Bar Rendering

```
TabBar layout (within TitleBar area):
  ┌────────────────────────────────────────────────────────────┐
  │ [icon] [TabA ×] [TabB ×] [TabC ×]       [_] [□] [×] │
  └────────────────────────────────────────────────────────────┘

- Active tab: highlighted background, bold text
- Inactive tabs: muted background, normal text
- Each tab has a small close button (×)
- Overflow: scroll arrows or ellipsis if too many tabs
- Drop target highlight: gap indicator between tabs during drag
```

## Window Shading

### State Machine

```
                    ┌────────────────┐
                    │  UNSHADED      │  (normal, full window visible)
                    └───────┬────────┘
                            │ trigger: double-click title / shortcut / rule
                            │ start animation: height → titleBarHeight
                            ▼
                    ┌────────────────┐
                    │  SHADING       │  (animating collapse)
                    │                │  clip surface height decreasing
                    │                │  duration: ~200ms (ease-out-cubic)
                    └───────┬────────┘
                            │ animation complete
                            ▼
                    ┌────────────────┐
                    │  SHADED        │  (only title bar visible)
                    │                │  client surface fully clipped
                    │                │  window occupies titleBarHeight only
                    └───────┬────────┘
                            │ trigger: same actions
                            │ start animation: height → full
                            ▼
                    ┌────────────────┐
                    │  UNSHADING     │  (animating expand)
                    │                │  clip surface height increasing
                    └───────┬────────┘
                            │ animation complete
                            ▼
                    ┌────────────────┐
                    │  UNSHADED      │
                    └────────────────┘
```

### Implementation

```cpp
class WindowShade {
public:
    enum class State { Unshaded, Shading, Shaded, Unshading };

    void toggle();
    void shade();
    void unshade();
    State state() const;

private:
    void startAnimation(State target);
    void onAnimationFrame(double progress);

    State m_state = State::Unshaded;
    WindowEntry* m_entry;
    DecorationFrame* m_frame;

    // Animation via phosphor-animation
    QVariantAnimation m_animation;
    int m_fullHeight;      // original client height
    int m_currentHeight;   // animated current visible height
};

void WindowShade::onAnimationFrame(double progress) {
    // progress: 0.0 → 1.0
    int titleBarHeight = m_frame->theme()->titleBarHeight();

    // m_currentHeight tracks the total window height (title bar + visible client area)
    if (m_state == Shading) {
        m_currentHeight = titleBarHeight + (m_fullHeight - titleBarHeight) * (1.0 - progress);
    } else {  // Unshading
        m_currentHeight = titleBarHeight + (m_fullHeight - titleBarHeight) * progress;
    }

    // Visible client height = total height minus the title bar (drawn by compositor)
    int visibleClientHeight = m_currentHeight - titleBarHeight;

    // Clip the client surface: 0 pixels when fully shaded, full height when unshaded
    m_entry->sceneSurface()->setClipRect(QRect(0, 0, m_entry->width(), visibleClientHeight));

    // Update frame geometry (window shrinks)
    m_frame->updateGeometry(QSize(m_entry->width(), m_currentHeight));

    // Mark damaged
    m_frame->markDamaged();
}
```

## Effects Pipeline

### Integration with OutputRenderer

```
Normal render pass:
  1. BlurEffect::captureBackground (render scene to offscreen, excluding blur surfaces)
  2. For each surface (back to front):
     a. ShadowEffect::drawShadow (if enabled for this surface)
     b. RoundedCornersEffect::beginClip (set up stencil/alpha mask)
     c. Draw surface texture (normal quad pipeline)
     d. RoundedCornersEffect::endClip
  3. If surface has blur region:
     a. BlurEffect::apply (sample captured background through blur)
     b. Composite blurred background under surface
  4. DimInactiveEffect: multiply opacity for inactive windows
```

### Dual-Kawase Blur Algorithm

```
Input: backgroundTexture (scene behind transparent surface)
Output: blurredTexture

Parameters:
  - radius: controls blur strength (maps to iteration count)
  - iterations: number of down/up passes (typically 4-8)

Algorithm:
  1. Downsample chain:
     for i in 0..iterations:
       render backgroundTexture (or previous level) at half resolution
       using kawase_down shader (samples 5 texels in cross pattern)
       → produces tex[i] at resolution / 2^(i+1)

  2. Upsample chain:
     for i in iterations-1..0:
       render tex[i+1] upsampled to tex[i] resolution
       using kawase_up shader (samples 8 texels in diamond pattern)
       → blends with tex[i] for detail preservation

  3. Result: full-resolution blurred texture
```

```glsl
// kawase_down.frag.glsl
#version 450
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D inputTexture;
layout(std140, binding = 0) uniform Params {
    vec2 halfPixel;  // 0.5 / textureSize
};

void main() {
    vec4 sum = texture(inputTexture, vTexCoord) * 4.0;
    sum += texture(inputTexture, vTexCoord - halfPixel);
    sum += texture(inputTexture, vTexCoord + halfPixel);
    sum += texture(inputTexture, vTexCoord + vec2(halfPixel.x, -halfPixel.y));
    sum += texture(inputTexture, vTexCoord - vec2(halfPixel.x, -halfPixel.y));
    fragColor = sum / 8.0;
}
```

```glsl
// kawase_up.frag.glsl
#version 450
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D inputTexture;
layout(std140, binding = 0) uniform Params {
    vec2 halfPixel;
};

void main() {
    vec4 sum = vec4(0.0);
    sum += texture(inputTexture, vTexCoord + vec2(-halfPixel.x * 2.0, 0.0));
    sum += texture(inputTexture, vTexCoord + vec2(-halfPixel.x, halfPixel.y)) * 2.0;
    sum += texture(inputTexture, vTexCoord + vec2(0.0, halfPixel.y * 2.0));
    sum += texture(inputTexture, vTexCoord + vec2(halfPixel.x, halfPixel.y)) * 2.0;
    sum += texture(inputTexture, vTexCoord + vec2(halfPixel.x * 2.0, 0.0));
    sum += texture(inputTexture, vTexCoord + vec2(halfPixel.x, -halfPixel.y)) * 2.0;
    sum += texture(inputTexture, vTexCoord + vec2(0.0, -halfPixel.y * 2.0));
    sum += texture(inputTexture, vTexCoord + vec2(-halfPixel.x, -halfPixel.y)) * 2.0;
    fragColor = sum / 12.0;
}
```

### Shadow Rendering

```cpp
class ShadowEffect {
public:
    struct Params {
        int radius = 12;        // blur radius for shadow
        int spread = 0;         // expand shadow beyond window bounds
        QPoint offset{0, 4};    // shadow offset (down for natural lighting)
        QColor color{0, 0, 0, 128}; // shadow color + alpha
    };

    // Pre-generate shadow nine-patch for common sizes
    void generateNinePatch(const Params& params, int cornerRadius);

    // Draw shadow behind window (before window content)
    void drawShadow(QRhiCommandBuffer* cb, const QRect& windowRect, const Params& params);

private:
    // Nine-patch: 9 pre-rendered quads stretched to fit any window size
    // Only regenerated when params change (not per-frame)
    struct NinePatch {
        QRhiTexture* texture;   // full shadow at template size
        int cornerSize;         // size of unstretched corners
    };
    QHash<Params, NinePatch> m_cache;
};
```

### SDF Rounded Corners

```glsl
// rounded_clip.frag.glsl — applied as final clip on each window
#version 450
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D windowTexture;
layout(std140, binding = 0) uniform Params {
    vec4 windowRect;   // x, y, width, height
    float radius;
    float smoothing;   // anti-alias width (typically 1.0)
};

float roundedBoxSDF(vec2 center, vec2 halfSize, float radius) {
    vec2 q = abs(center) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    vec4 color = texture(windowTexture, vTexCoord);

    vec2 pixelPos = vTexCoord * windowRect.zw;  // position within window
    vec2 center = pixelPos - windowRect.zw * 0.5;
    vec2 halfSize = windowRect.zw * 0.5;

    float dist = roundedBoxSDF(center, halfSize, radius);
    float alpha = 1.0 - smoothstep(-smoothing, smoothing, dist);

    fragColor = color * alpha;
}
```

## Theme Integration

```cpp
class DecorationTheme {
public:
    // Colors
    QColor activeTitleBarColor() const;
    QColor inactiveTitleBarColor() const;
    QColor activeTitleTextColor() const;
    QColor inactiveTitleTextColor() const;
    QColor borderColor() const;
    QColor buttonHoverColor() const;
    QColor buttonPressColor() const;
    QColor closeButtonColor() const;  // usually red

    // Metrics
    int titleBarHeight() const;
    int borderWidth() const;
    int cornerRadius() const;
    int buttonSize() const;
    int buttonSpacing() const;
    QFont titleFont() const;

    // Button layout
    ButtonLayout buttonLayout() const;

    // Shadow params
    ShadowEffect::Params shadowParams() const;

    // Zone highlight
    QColor zoneHighlightColor() const;
    int zoneHighlightWidth() const;

private:
    // Reads from phosphor-theme settings
    PhosphorTheme::ThemeProvider* m_theme;
};
```

## Verification

1. Windows have title bars with working close/maximize/minimize buttons
2. Drag title bar → window moves (interactive move via xdg_toplevel)
3. Drag border → window resizes (interactive resize)
4. Double-click title bar → window shades (collapses to title bar height)
5. Drag window A onto window B's title bar → tab group forms
6. Click tab → switches active content
7. Drag tab out → detaches into separate window
8. Transparent terminal (e.g., kitty with opacity 0.8) → blurred background visible
9. All windows have shadows (visible on desktop/each other)
10. Rounded corners on all windows (no sharp edges)
11. Inactive windows are slightly dimmed
12. Maximized windows: no borders, no rounded corners, no shadow (fullscreen-like)
13. Unit tests:
    - `test_decoration_geometry` — frame size calculation from client size
    - `test_button_hit_test` — click positions map to correct buttons
    - `test_edge_hit_test` — resize edge detection at all 8 edges/corners
    - `test_tab_group` — add/remove/switch/reorder tabs
    - `test_window_shade_animation` — state transitions, clip rect values
    - `test_blur_pipeline` — downsample/upsample produces expected texture sizes
    - `test_rounded_corners_sdf` — SDF produces correct alpha at corner pixels
