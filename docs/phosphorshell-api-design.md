# PhosphorShell — API Design Document

## Overview

PhosphorShell is a Qt6/C++20 library for creating Wayland layer-shell surfaces
with optional GPU-accelerated shader effects. It extracts and generalizes the
layer-shell and shader rendering code from PlasmaZones into a reusable library
that any Wayland Qt application can consume.

**License:** LGPL-2.1-or-later (KDE ecosystem standard for libraries)

**Namespace:** `PhosphorShell`

**Build:** In-tree library target within the PlasmaZones repository. Installed
alongside the PlasmaZones package. Can be split into a standalone repo later.

## Design Goals

1. **Zero Wayland types in public API** — consumers work with pure Qt types
2. **Shader pipeline independent of zone concepts** — generic uniform extension
3. **Shadertoy-compatible** — iTime, iMouse, iResolution, multipass, textures
4. **Library-first** — PlasmaZones becomes a consumer, not the sole user
5. **WM-pivot ready** — compositing foundation that a window manager can build on

## Non-Goals (Kept in PlasmaZones)

- Zone data types (ZoneData, ZoneRect, ZoneColor, ZoneDataSnapshot)
- Zone-specific UBO arrays (zoneRects, zoneFillColors, zoneBorderColors, zoneParams)
- OverlayService orchestration
- D-Bus adaptors
- Bundled zone shader themes (the 25 data/shaders/ directories)
- Labels texture rendering (ZoneLabelTextureBuilder)

---

## Public API

### 1. Layer Surface — `PhosphorShell::LayerSurface`

Drop-in layer-shell surface management. Attach to any QWindow before `show()`.

```cpp
#include <PhosphorShell/LayerSurface>

auto* window = new QQuickWindow;
auto* surface = PhosphorShell::LayerSurface::get(window);

surface->setLayer(PhosphorShell::LayerSurface::LayerOverlay);
surface->setAnchors(PhosphorShell::LayerSurface::AnchorAll);
surface->setExclusiveZone(-1);  // ignore other exclusive zones
surface->setKeyboardInteractivity(PhosphorShell::LayerSurface::KeyboardInteractivityNone);
surface->setScope("com.example.myoverlay");

window->show();
```

#### Enums

```cpp
enum Layer {
    LayerBackground = 0,
    LayerBottom      = 1,
    LayerTop         = 2,
    LayerOverlay     = 3,
};

enum KeyboardInteractivity {
    KeyboardInteractivityNone      = 0,
    KeyboardInteractivityExclusive = 1,
    KeyboardInteractivityOnDemand  = 2,
};

enum Anchor {
    AnchorNone   = 0,
    AnchorTop    = 1,
    AnchorBottom = 2,
    AnchorLeft   = 4,
    AnchorRight  = 8,
};
Q_DECLARE_FLAGS(Anchors, Anchor)
```

#### Properties (Q_PROPERTY, all with change signals)

| Property               | Type                   | Mutable | Notes |
|------------------------|------------------------|---------|-------|
| `layer`                | `Layer`                | Yes*    | *v2+ compositors only after show() |
| `anchors`              | `Anchors`              | Yes     | |
| `exclusiveZone`        | `int32_t`              | Yes     | -1 = ignore, 0 = respect, >0 = reserve |
| `keyboardInteractivity`| `KeyboardInteractivity`| Yes     | |
| `margins`              | `QMargins`             | Yes     | |
| `scope`                | `QString`              | No      | Must set before show() |
| `screen`               | `QScreen*`             | No      | Must set before show() |

#### Static Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `get(QWindow*)` | `LayerSurface*` | Get or create. Must call before first show() for creation. |
| `find(QWindow*)` | `LayerSurface*` | Retrieve existing, or nullptr. Never creates. |
| `isSupported()` | `bool` | True if compositor supports zwlr_layer_shell_v1. |

#### BatchGuard

RAII guard to suppress `propertiesChanged()` during multiple setter calls:

```cpp
{
    PhosphorShell::LayerSurface::BatchGuard batch(surface);
    surface->setLayer(PhosphorShell::LayerSurface::LayerOverlay);
    surface->setAnchors(PhosphorShell::LayerSurface::AnchorAll);
    surface->setMargins(QMargins(0, 0, 0, 0));
} // single propertiesChanged() emitted here
```

---

### 2. Shader Effect — `PhosphorShell::ShaderEffect`

QQuickItem that renders a fullscreen fragment shader via Qt RHI. This is the
generalized version of PlasmaZones' ZoneShaderItem, with zone-specific data
removed and replaced by a pluggable uniform extension system.

```cpp
#include <PhosphorShell/ShaderEffect>

// In QML:
// PhosphorShell.ShaderEffect {
//     anchors.fill: parent
//     shaderSource: "file:///path/to/effect.frag"
//     customParams1: Qt.vector4d(0.1, 0.5, 0.0, 1.0)
// }
```

#### Properties (Shadertoy-compatible)

| Property         | Type          | Description |
|------------------|---------------|-------------|
| `iTime`          | `qreal`       | Elapsed time (wrapped for float32 precision) |
| `iTimeDelta`     | `qreal`       | Frame delta time |
| `iFrame`         | `int`         | Frame counter |
| `iResolution`    | `QSizeF`      | Surface size in pixels |
| `iMouse`         | `QPointF`     | Cursor position (pixels) |
| `shaderSource`   | `QUrl`        | Path to fragment shader |
| `shaderParams`   | `QVariantMap` | Named parameter → value map |
| `status`         | `Status`      | Null / Loading / Ready / Error |
| `errorLog`       | `QString`     | Compilation error message |

#### Custom Parameter Slots

32 float slots in 8 vec4s, directly settable from QML or C++:

| Property | Type | Uniform |
|----------|------|---------|
| `customParams1..8` | `QVector4D` | `customParams[0..7]` in UBO |
| `customColor1..16` | `QVector4D` | `customColors[0..15]` in UBO |

#### Multipass Properties

| Property | Type | Description |
|----------|------|-------------|
| `bufferShaderPath` | `QString` | Single buffer pass shader |
| `bufferShaderPaths` | `QStringList` | Up to 4 buffer passes (A→B→C→D) |
| `bufferFeedback` | `bool` | Ping-pong: sample own previous frame |
| `bufferScale` | `qreal` | Buffer resolution scale (0.125–1.0) |
| `bufferWrap` | `QString` | "clamp" or "repeat" |
| `bufferWraps` | `QStringList` | Per-channel wrap modes |
| `bufferFilter` | `QString` | "nearest", "linear", or "mipmap" |
| `bufferFilters` | `QStringList` | Per-channel filter modes |

#### Texture Slots

| Property | Type | Binding | Description |
|----------|------|---------|-------------|
| `audioSpectrum` | `QVector<float>` | 6 | CAVA bar values (0–1) |
| `wallpaperTexture` | `QImage` | 11 | Desktop wallpaper image |
| `useWallpaper` | `bool` | — | Subscribe to wallpaper changes |
| `useDepthBuffer` | `bool` | 12 | Enable R32F depth buffer |

User textures (bindings 7–10) set via:

```cpp
void setUserTexture(int slot, const QImage& image);       // slot 0–3
void setUserTextureWrap(int slot, const QString& wrap);
```

---

### 3. Uniform Extension — `PhosphorShell::IUniformExtension`

Interface for appending custom data after the base UBO layout. This is how
PlasmaZones adds zone arrays without the library knowing about zones.

```cpp
#include <PhosphorShell/IUniformExtension>

class IUniformExtension {
public:
    virtual ~IUniformExtension() = default;

    /// Size in bytes of the extension region (must be std140-aligned)
    virtual int extensionSize() const = 0;

    /// Write extension data into the buffer at the given offset.
    /// Called on the render thread during prepare().
    virtual void write(char* buffer, int offset) const = 0;

    /// Whether the extension data has changed since the last write.
    /// The render node skips the extension upload when this returns false.
    virtual bool isDirty() const = 0;

    /// Mark as clean after a successful write.
    virtual void clearDirty() = 0;
};
```

#### PlasmaZones Usage

```cpp
// In PlasmaZones — not part of PhosphorShell
class ZoneUniformExtension : public PhosphorShell::IUniformExtension {
    // Appends: zoneRects[64], zoneFillColors[64], zoneBorderColors[64], zoneParams[64]
    int extensionSize() const override { return 64 * 4 * sizeof(float) * 4; } // 4096 bytes
    void write(char* buffer, int offset) const override { /* memcpy zone arrays */ }
    bool isDirty() const override { return m_dirty.load(); }
    void clearDirty() override { m_dirty.store(false); }
};

// Attach to a ShaderEffect:
effect->setUniformExtension(std::shared_ptr<PhosphorShell::IUniformExtension>(zoneExt));
```

---

### 4. Shader Registry — `PhosphorShell::ShaderRegistry`

Discovers, validates, and caches shader metadata from directories.

```cpp
#include <PhosphorShell/ShaderRegistry>

auto* registry = new PhosphorShell::ShaderRegistry(parent);
registry->addSearchPath("/usr/share/myapp/shaders");
registry->addSearchPath(userShaderDir);
registry->refresh();

auto shaders = registry->availableShaders();
auto info = registry->shader("electric-glow");
auto defaults = registry->defaultParams("electric-glow");
```

#### ShaderInfo

```cpp
struct ShaderInfo {
    QString id;
    QString name;
    QString description;
    QString author;
    QString version;
    QString category;
    QUrl shaderUrl;              // file:// to .frag
    QString sourcePath;
    QString vertexShaderPath;
    QStringList bufferShaderPaths;
    QString previewPath;         // preview.png
    QList<ParameterInfo> parameters;
    QMap<QString, QVariantMap> presets;
    bool isUserShader;
    bool isMultipass;
    bool useWallpaper;
    bool bufferFeedback;
    qreal bufferScale;
    QString bufferWrap;
    QStringList bufferWraps;
    QString bufferFilter;
    QStringList bufferFilters;
    bool useDepthBuffer;
};
```

#### ParameterInfo

```cpp
struct ParameterInfo {
    QString id;
    QString name;
    QString group;               // UI grouping
    QString type;                // "float", "color", "int", "bool", "image"
    int slot;                    // Uniform slot index
    QVariant defaultValue;
    QVariant minValue;
    QVariant maxValue;
    bool useZoneColor;           // Hint: consumer may want to bind to zone color
    QString wrap;                // Image wrap mode
    QString uniformName() const; // Slot → uniform name mapping
};
```

#### Key Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `addSearchPath(path)` | `void` | Register a shader search directory |
| `refresh()` | `void` | Rescan all search paths |
| `availableShaders()` | `QList<ShaderInfo>` | All discovered shaders |
| `shader(id)` | `ShaderInfo` | Lookup by ID |
| `shaderUrl(id)` | `QUrl` | Fragment shader URL |
| `defaultParams(id)` | `QVariantMap` | Default parameter values |
| `validateAndCoerceParams(id, params)` | `QVariantMap` | Validate + fill defaults |
| `translateParamsToUniforms(id, params)` | `QVariantMap` | Semantic IDs → uniform names |
| `presetParams(id, preset)` | `QVariantMap` | Named preset values |
| `shadersEnabled()` | `bool` | Qt6::ShaderTools available |

#### Change vs Current Design

The current `ShaderRegistry` is a singleton owned by the daemon with hardcoded
system/user paths. PhosphorShell's version:
- **Not a singleton** — consumers create their own instances
- **Explicit search paths** via `addSearchPath()` instead of hardcoded dirs
- **No wallpaper provider coupling** — wallpaper is a texture concern, not registry

Wallpaper provider (`IWallpaperProvider`) stays in PhosphorShell since it's useful
to any shader-rendering app, but moves out of ShaderRegistry into its own header.

---

### 5. Shader Include Resolver — `PhosphorShell::ShaderIncludeResolver`

Static utility for expanding `#include` directives in GLSL source.

```cpp
#include <PhosphorShell/ShaderIncludeResolver>

QString error;
QString expanded = PhosphorShell::ShaderIncludeResolver::expandIncludes(
    source, shaderDir, {systemIncludeDir}, &error);
```

Unchanged from current design. Max depth 10.

---

### 6. Wallpaper Provider — `PhosphorShell::IWallpaperProvider`

```cpp
#include <PhosphorShell/IWallpaperProvider>

class IWallpaperProvider {
public:
    virtual ~IWallpaperProvider() = default;
    virtual QString wallpaperPath() = 0;
};

/// Auto-detect: KDE, Hyprland, Sway, GNOME, fallback
std::unique_ptr<IWallpaperProvider> createWallpaperProvider();
```

---

## UBO Layout

### Base Uniforms (PhosphorShell)

```
Offset  Type        Name                    Notes
──────────────────────────────────────────────────────────
0       mat4        qt_Matrix               Scene graph transform
64      float       qt_Opacity              Scene graph opacity
68      float       iTime                   Wrapped elapsed time
72      float       iTimeDelta              Frame delta
76      int         iFrame                  Frame counter
80      vec2        iResolution             Surface size (px)
88      int         _reserved0              (was zoneCount — now in extension)
92      int         _reserved1              (was highlightedCount)
96      vec4        iMouse                  xy=px, zw=normalized
112     vec4        iDate                   year, month, day, seconds
128     vec4[8]     customParams            32 float parameter slots
256     vec4[16]    customColors            16 color parameter slots
512     vec4[4]     iChannelResolution      Buffer texture sizes
576     int         iAudioSpectrumSize      CAVA bar count (0=disabled)
580     int         _pad                    
584     int[2]      _pad2                   
592     vec4[4]     iTextureResolution      User texture sizes
656     float       iTimeHi                 Wrap offset for precision
660     float[3]    _pad3                   std140 struct alignment
──────────────────────────────────────────────────────────
672 bytes total (base)
```

### Extension Region (Consumer-Defined)

Starts at byte 672. PlasmaZones appends:

```
672     vec4[64]    zoneRects               x, y, w, h (normalized)
1696    vec4[64]    zoneFillColors          RGBA
2720    vec4[64]    zoneBorderColors        RGBA
3744    vec4[64]    zoneParams              borderRadius, borderWidth, zoneNumber, highlighted
4768    — end —
```

Total with PlasmaZones extension: 4768 bytes (fits in 8192 UBO).

---

## Texture Binding Slots

| Binding | Name           | Description |
|---------|----------------|-------------|
| 0       | (app-defined)  | PlasmaZones uses for labels texture |
| 1       | (reserved)     | Future use |
| 2–5     | iChannel0–3    | Multipass buffer textures |
| 6       | audioSpectrum  | CAVA FFT data |
| 7–10    | uTexture0–3    | User-supplied images |
| 11      | wallpaper      | Desktop wallpaper |
| 12      | depth          | R32F depth buffer (optional) |

---

## Shader Directory Convention

Each shader is a directory containing:

```
my-shader/
├── metadata.json      # Required: id, name, description, parameters, presets
├── zone.vert          # Vertex shader (passthrough)
├── effect.frag        # Main fragment shader
├── pass0.frag         # Optional: buffer pass A
├── pass1.frag         # Optional: buffer pass B
├── pass2.frag         # Optional: buffer pass C
├── pass3.frag         # Optional: buffer pass D
└── preview.png        # Optional: UI thumbnail
```

### metadata.json Schema

```json
{
    "id": "electric-glow",
    "name": "Electric Glow",
    "description": "Neon glow effect with animated pulses",
    "author": "fuddlesworth",
    "version": "1.0",
    "category": "Neon",
    "wallpaper": false,
    "depthBuffer": false,
    "multipass": false,
    "bufferFeedback": false,
    "bufferScale": 1.0,
    "bufferWrap": "clamp",
    "bufferFilter": "linear",
    "parameters": [
        {
            "id": "speed",
            "name": "Animation Speed",
            "type": "float",
            "slot": 0,
            "default": 0.1,
            "min": 0.01,
            "max": 0.5
        },
        {
            "id": "glowColor",
            "name": "Glow Color",
            "type": "color",
            "slot": 0,
            "default": "#00ffcc"
        }
    ],
    "presets": {
        "Calm": { "speed": 0.05, "glowColor": "#0066ff" },
        "Intense": { "speed": 0.4, "glowColor": "#ff0066" }
    }
}
```

---

## GLSL Include Library

PhosphorShell ships these include files, available to all shaders:

| File | Contents |
|------|----------|
| `common.glsl` | Shared uniforms, time helpers (timeSin/timeCos), hash functions |
| `multipass.glsl` | Buffer sampling helpers |
| `audio.glsl` | Audio spectrum processing |
| `depth.glsl` | Depth buffer utilities |
| `textures.glsl` | Texture sampling helpers |
| `wallpaper.glsl` | Wallpaper texture sampling |

---

## CMake Target

```cmake
# phosphorshell/CMakeLists.txt
project(PhosphorShell VERSION 0.1.0 LANGUAGES C CXX)

add_library(PhosphorShell SHARED
    src/layersurface.cpp
    src/shadereffect.cpp
    src/shaderregistry.cpp
    src/shaderincluderesolver.cpp
    src/rendernode_rhi.cpp
    src/wallpaperprovider.cpp
    src/qpa/layershellintegration.cpp
    src/qpa/layershellwindow.cpp
    src/qpa/phosphorshellplugin.cpp
    src/qpa/xdg_popup_stub.c
)

add_library(PhosphorShell::PhosphorShell ALIAS PhosphorShell)

target_link_libraries(PhosphorShell
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Quick
    PRIVATE Qt6::WaylandClient Qt6::GuiPrivate Qt6::ShaderTools
            Wayland::Client
)

target_include_directories(PhosphorShell
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
)

# Export macros
include(GenerateExportHeader)
generate_export_header(PhosphorShell
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorshell_export.h
)

# QPA plugin is built as a MODULE loaded at runtime
add_library(phosphorshell-qpa MODULE src/qpa/phosphorshellplugin.cpp ...)
```

### PlasmaZones Integration

```cmake
# In PlasmaZones top-level CMakeLists.txt:
add_subdirectory(phosphorshell)

# In src/CMakeLists.txt (daemon):
target_link_libraries(plasmazones PRIVATE PhosphorShell::PhosphorShell)
```

---

## Migration Path

### Phase 1: Extract Layer Surface (mechanical move)
- Move `src/core/layersurface.*` → `phosphorshell/src/`
- Move `src/core/qpa/*` → `phosphorshell/src/qpa/`
- Move `src/core/protocols/*.xml` → `phosphorshell/protocols/`
- Rename namespace `PlasmaZones` → `PhosphorShell` in moved files
- Rename `_pz_*` dynamic properties → `_ps_*`
- Rename export macro `PLASMAZONES_EXPORT` → `PHOSPHORSHELL_EXPORT`
- PlasmaZones `#include <PhosphorShell/LayerSurface>` — no behavior change

### Phase 2: Extract Shader Pipeline (requires generalization)
- Move `ShaderRegistry`, `ShaderIncludeResolver`, `ShaderUtils` → `phosphorshell/src/`
- Remove singleton pattern from ShaderRegistry → `addSearchPath()` API
- Move rendering base types → `phosphorshell/src/`
- Create `IUniformExtension` interface
- Split `ZoneShaderUniforms` into `BaseUniforms` (library) + zone extension (PlasmaZones)
- Move `ZoneShaderNodeRhi` → `phosphorshell/src/rendernode_rhi.*` (generalized)
- Create `PhosphorShell::ShaderEffect` (base QQuickItem without zone properties)
- PlasmaZones' `ZoneShaderItem` becomes a thin subclass that adds zone Q_PROPERTYs
  and creates a `ZoneUniformExtension`

### Phase 3: Extract Support Code
- Move `IWallpaperProvider` + implementations → `phosphorshell/src/`
- Move `data/shaders/*.glsl` (include files only) → `phosphorshell/shaders/`
- Themed shader directories stay in PlasmaZones `data/shaders/`

---

## Future Protocol Additions (Not in Initial Extraction)

| Feature | Protocol | Priority | Notes |
|---------|----------|----------|-------|
| Popups on layer surfaces | `get_popup` in wlr-layer-shell | Medium | Needed for panels with menus |
| Exclusive edge | v5 `set_exclusive_edge` | Low | Edge-specific exclusive zones |
| Error events | wlr-layer-shell | Medium | Needed for untrusted consumers |
| ext-layer-shell-v1 | Standardized successor | High (future) | When compositors adopt it |
| xdg-activation | Focus stealing prevention | Medium | For interactive surfaces |
