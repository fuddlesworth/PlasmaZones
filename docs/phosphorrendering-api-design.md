# PhosphorRendering — API Design Document

## Overview

PhosphorRendering is a Qt6/C++20 library for GPU-accelerated fragment shader
rendering via Qt RHI. It generalizes PlasmaZones' zone shader pipeline into a
reusable rendering engine that any Qt Quick application can consume.

**License:** LGPL-2.1-or-later
**Namespace:** `PhosphorRendering`
**Depends on:** PhosphorShell (for BaseUniforms, IUniformExtension, ShaderIncludeResolver)

## Dependency Graph

```
PhosphorShell              PhosphorRendering              PlasmaZones
(layer-shell, registry,    (RHI rendering pipeline,       (zones, overlays,
 BaseUniforms, GLSL)        multipass, textures)            tiling, D-Bus)
        │                          │                            │
        └──── PUBLIC link ─────────┘                            │
                                   └──── PUBLIC link ───────────┘
```

A WM/shell uses PhosphorRendering on its own compositor surfaces without
needing layer-shell. A standalone shader wallpaper app uses both.

---

## Design Principles

1. **No consumer-specific types** — no zones, no labels, no PlasmaZones concepts
2. **Extension via composition** — IUniformExtension for custom UBO data,
   ITextureProvider for custom texture bindings
3. **Shadertoy-compatible** — iTime, iMouse, iResolution, multipass, textures
4. **Thread-safe render node** — atomic dirty flags, safe item invalidation
5. **Partial UBO uploads** — dirty region tracking reduces GPU bandwidth

---

## Public API

### 1. ShaderEffect — `PhosphorRendering::ShaderEffect`

QQuickItem that renders a fullscreen fragment shader. This is the primary
consumer-facing class — drop it into QML and point it at a .frag file.

```cpp
#include <PhosphorRendering/ShaderEffect>
```

#### Q_PROPERTY (Shadertoy-compatible)

| Property | Type | Description |
|----------|------|-------------|
| `shaderSource` | `QUrl` | Fragment shader path (required) |
| `iTime` | `qreal` | Elapsed time (double precision, wrapped internally) |
| `iTimeDelta` | `qreal` | Frame delta |
| `iFrame` | `int` | Frame counter |
| `iResolution` | `QSizeF` | Surface size (pixels) |
| `iMouse` | `QPointF` | Cursor position |
| `status` | `Status` | Null / Loading / Ready / Error |
| `errorLog` | `QString` | Compilation error |

#### Q_PROPERTY (Custom parameters)

| Property | Type | Description |
|----------|------|-------------|
| `customParams1..8` | `QVector4D` | 32 float slots (8 vec4s) |
| `customColor1..16` | `QColor` | 16 color slots |

#### Q_PROPERTY (Multipass)

| Property | Type | Description |
|----------|------|-------------|
| `bufferShaderPath` | `QString` | Single buffer pass shader |
| `bufferShaderPaths` | `QStringList` | Up to 4 passes (A→B→C→D) |
| `bufferFeedback` | `bool` | Ping-pong: sample own previous frame |
| `bufferScale` | `qreal` | Buffer resolution (0.125–1.0) |
| `bufferWrap` | `QString` | "clamp" or "repeat" |
| `bufferWraps` | `QStringList` | Per-channel wrap modes |
| `bufferFilter` | `QString` | "nearest", "linear", "mipmap" |
| `bufferFilters` | `QStringList` | Per-channel filter modes |

#### Q_PROPERTY (Textures)

| Property | Type | Description |
|----------|------|-------------|
| `audioSpectrum` | `QVector<float>` | CAVA bar values at binding 6 |
| `wallpaperTexture` | `QImage` | Desktop wallpaper at binding 11 |
| `useWallpaper` | `bool` | Subscribe to wallpaper changes |
| `useDepthBuffer` | `bool` | Enable R32F depth at binding 12 |

#### C++ Extension Points

```cpp
/// Attach a uniform extension (appends data after BaseUniforms in the UBO).
void setUniformExtension(std::shared_ptr<PhosphorShell::IUniformExtension> ext);

/// Set user texture (bindings 7–10, slot 0–3).
void setUserTexture(int slot, const QImage& image);
void setUserTextureWrap(int slot, const QString& wrap);

/// Set include paths for shader #include resolution.
void setShaderIncludePaths(const QStringList& paths);

/// Force shader reload.
Q_INVOKABLE void reloadShader();
```

#### QML Usage

```qml
import PhosphorRendering

ShaderEffect {
    anchors.fill: parent
    shaderSource: "file:///path/to/effect.frag"
    customParams1: Qt.vector4d(0.1, 0.5, 0.0, 1.0)
    customColor1: "#ff6600"
}
```

#### PlasmaZones Subclass Example

```cpp
class ZoneShaderItem : public PhosphorRendering::ShaderEffect {
    Q_OBJECT
    Q_PROPERTY(QVariantList zones READ zones WRITE setZones NOTIFY zonesChanged)
    Q_PROPERTY(QImage labelsTexture READ labelsTexture WRITE setLabelsTexture ...)
    Q_PROPERTY(int hoveredZoneIndex ...)

public:
    ZoneShaderItem(QQuickItem* parent = nullptr) : ShaderEffect(parent) {
        m_zoneExtension = std::make_shared<ZoneUniformExtension>();
        setUniformExtension(m_zoneExtension);
    }

    void setZones(const QVariantList& zones) {
        // Parse zones → ZoneRect/ZoneColor arrays
        // Write to m_zoneExtension
        // m_zoneExtension->markDirty()
    }

private:
    std::shared_ptr<ZoneUniformExtension> m_zoneExtension;
};
```

---

### 2. ShaderNodeRhi — `PhosphorRendering::ShaderNodeRhi`

QSGRenderNode that does the actual RHI draw calls. Created by ShaderEffect
in updatePaintNode(). Consumers rarely interact with this directly — it's
public for advanced use cases (custom QQuickItems that need shader rendering
without ShaderEffect's property system).

```cpp
#include <PhosphorRendering/ShaderNodeRhi>
```

#### Core Interface

```cpp
class PHOSPHORRENDERING_EXPORT ShaderNodeRhi : public QSGRenderNode
{
public:
    explicit ShaderNodeRhi(QQuickItem* item);
    ~ShaderNodeRhi() override;

    // ── QSGRenderNode overrides ─────────────────────────────────────
    StateFlags changedStates() const override;
    RenderingFlags flags() const override;
    QRectF rect() const override;
    void prepare() override;
    void render(const RenderState* state) override;
    void releaseResources() override;

    // ── Item lifecycle ──────────────────────────────────────────────
    /// Mark item as destroyed (atomic; render thread checks before access).
    void invalidateItem();

    // ── Shader loading ──────────────────────────────────────────────
    bool loadVertexShader(const QString& path);
    bool loadFragmentShader(const QString& path);
    void setVertexShaderSource(const QString& source);
    void setFragmentShaderSource(const QString& source);
    void setShaderIncludePaths(const QStringList& paths);
    bool isShaderReady() const;
    QString shaderError() const;
    void invalidateShader();

    // ── Timing ──────────────────────────────────────────────────────
    void setTime(double time);       // Double precision; split to iTime + iTimeHi
    void setTimeDelta(float delta);
    void setFrame(int frame);

    // ── Scene ───────────────────────────────────────────────────────
    void setResolution(float width, float height);
    void setMousePosition(const QPointF& pos);

    // ── Custom parameters (32 floats in 8 vec4s) ────────────────────
    void setCustomParams(int index, const QVector4D& params);  // 0–7
    void setCustomColor(int index, const QColor& color);       // 0–15

    // ── Textures ────────────────────────────────────────────────────
    void setAudioSpectrum(const QVector<float>& spectrum);
    void setUserTexture(int slot, const QImage& image);
    void setUserTextureWrap(int slot, const QString& wrap);
    void setWallpaperTexture(const QImage& image);
    void setUseWallpaper(bool use);
    void setUseDepthBuffer(bool use);

    // ── Multipass ───────────────────────────────────────────────────
    void setBufferShaderPath(const QString& path);
    void setBufferShaderPaths(const QStringList& paths);
    void setBufferFeedback(bool enable);
    void setBufferScale(qreal scale);
    void setBufferWrap(const QString& wrap);
    void setBufferWraps(const QStringList& wraps);
    void setBufferFilter(const QString& filter);
    void setBufferFilters(const QStringList& filters);

    // ── Uniform extension ───────────────────────────────────────────
    void setUniformExtension(std::shared_ptr<PhosphorShell::IUniformExtension> ext);

    // ── Consumer texture bindings ───────────────────────────────────
    /// Reserve a texture binding slot for consumer use.
    /// The consumer provides the QRhiTexture and QRhiSampler; the node
    /// includes them in the SRB layout. Binding must not conflict with
    /// built-in bindings (0–12).
    void setExtraBinding(int binding, QRhiTexture* texture, QRhiSampler* sampler);
    void removeExtraBinding(int binding);

    // ── Dirty flag control ──────────────────────────────────────────
    void invalidateUniforms();
};
```

#### Texture Binding Layout (Fixed)

| Binding | Name | Managed by |
|---------|------|------------|
| 0 | UBO | ShaderNodeRhi (BaseUniforms + IUniformExtension) |
| 1 | (reserved) | Consumer via setExtraBinding (PZ: labels texture) |
| 2–5 | iChannel0–3 | ShaderNodeRhi (multipass buffers) |
| 6 | audioSpectrum | ShaderNodeRhi |
| 7–10 | uTexture0–3 | ShaderNodeRhi (user textures) |
| 11 | wallpaper | ShaderNodeRhi (if useWallpaper) |
| 12 | depth | ShaderNodeRhi (if useDepthBuffer) |
| 13+ | (consumer) | Consumer via setExtraBinding |

Binding 1 is reserved but not managed by the library — consumers that need
it (PlasmaZones for zone labels) use setExtraBinding(1, ...).

#### Partial UBO Upload Regions

The render node uses dirty flags to upload only changed UBO regions:

| Flag | Region | Size | Trigger |
|------|--------|------|---------|
| `timeDirty` | iTime, iTimeDelta, iFrame | 12 bytes | Every frame |
| `timeHiDirty` | iTimeHi | 4 bytes | ~every 17 min |
| `sceneDirty` | iResolution → end of struct | Full scene | Resolution/zone change |
| `extensionDirty` | IUniformExtension region | Extension size | Extension::isDirty() |

First upload is always full (entire UBO). Subsequent uploads use regions.

---

### 3. ShaderCompiler — `PhosphorRendering::ShaderCompiler`

Static utility for GLSL → SPIR-V compilation with include resolution and
caching. Extracted from the internal detail:: namespace.

```cpp
#include <PhosphorRendering/ShaderCompiler>
```

```cpp
class PHOSPHORRENDERING_EXPORT ShaderCompiler
{
public:
    struct Result {
        QShader shader;
        bool success = false;
        QString error;
    };

    /// Compile GLSL fragment/vertex shader to QShader (SPIR-V + GLSL variants).
    /// Cached by source hash; second call with same source returns immediately.
    static Result compile(const QString& source, QShader::Stage stage);

    /// Load shader from file, expand #includes, then compile.
    /// @param path        Path to .frag or .vert file
    /// @param includePaths Directories to search for #include directives
    static Result compileFromFile(const QString& path, const QStringList& includePaths);

    /// Clear the in-memory compilation cache (e.g. on hot-reload).
    static void clearCache();

    /// Bake target list: SPIR-V 1.0, GLSL 330, GLSL ES 300/310/320.
    static QList<QShaderBaker::GeneratedShader> bakeTargets();
};
```

---

### 4. BufferPassManager — `PhosphorRendering::BufferPassManager`

Encapsulates multipass buffer rendering (single-pass ping-pong and multi-buffer
chaining). Internal to ShaderNodeRhi but exposed for consumers that need direct
control.

```cpp
#include <PhosphorRendering/BufferPassManager>
```

```cpp
class PHOSPHORRENDERING_EXPORT BufferPassManager
{
public:
    explicit BufferPassManager(QRhi* rhi);
    ~BufferPassManager();

    // ── Configuration ───────────────────────────────────────────────
    void setMode(Mode mode);  // None, Single, Multi
    enum class Mode { None, Single, Multi };

    void setFeedback(bool enable);         // Ping-pong (single mode only)
    void setScale(qreal scale);            // 0.125–1.0
    void setWrap(int channel, const QString& wrap);
    void setFilter(int channel, const QString& filter);

    // ── Shader management ───────────────────────────────────────────
    void setShaderPath(const QString& path);           // Single mode
    void setShaderPaths(const QStringList& paths);     // Multi mode (1–4)
    void setIncludePaths(const QStringList& paths);

    // ── Render lifecycle ────────────────────────────────────────────

    /// Create/resize FBO textures to match the given output size.
    void ensureTargets(QSize outputSize);

    /// Record buffer passes into the command buffer.
    /// Called during prepare(), BEFORE the scene graph's main render pass.
    void recordPasses(QRhiCommandBuffer* cb,
                      QRhiBuffer* ubo,
                      QRhiTexture* audioTex,
                      QRhiTexture* wallpaperTex,
                      QRhiTexture* depthTex,
                      const std::array<QRhiTexture*, 4>& userTextures,
                      QRhiTexture* extraBinding1Tex,  // nullptr if unused
                      int frame);

    /// Output texture for the image pass to sample as iChannel0.
    QRhiTexture* outputTexture() const;

    /// Depth texture (R32F) if depth buffer is enabled, nullptr otherwise.
    QRhiTexture* depthTexture() const;

    /// Per-channel resolution for UBO iChannelResolution[i].
    QSize channelResolution(int channel) const;

    // ── Status ──────────────────────────────────────────────────────
    bool isReady() const;
    QString error() const;
};
```

---

## UBO Layout

The UBO is `sizeof(BaseUniforms) + extension->extensionSize()` bytes.

### Base Region (PhosphorShell::BaseUniforms, 672 bytes)

```
Offset  Type        Name                    Managed by
──────────────────────────────────────────────────────────
0       mat4        qt_Matrix               ShaderNodeRhi (from scene graph)
64      float       qt_Opacity              ShaderNodeRhi (from scene graph)
68      float       iTime                   ShaderNodeRhi (wrapped)
72      float       iTimeDelta              ShaderNodeRhi
76      int         iFrame                  ShaderNodeRhi
80      vec2        iResolution             ShaderNodeRhi
88      int         appField0               Consumer (via extension or direct)
92      int         appField1               Consumer (via extension or direct)
96      vec4        iMouse                  ShaderNodeRhi
112     vec4        iDate                   ShaderNodeRhi (system clock)
128     vec4[8]     customParams            ShaderNodeRhi (from ShaderEffect)
256     vec4[16]    customColors            ShaderNodeRhi (from ShaderEffect)
512     vec4[4]     iChannelResolution      ShaderNodeRhi (from BufferPassManager)
576     int         iAudioSpectrumSize      ShaderNodeRhi
580     int         iFlipBufferY            ShaderNodeRhi (always 1)
584     int[2]      _pad                    (std140 alignment)
592     vec4[4]     iTextureResolution      ShaderNodeRhi
656     float       iTimeHi                 ShaderNodeRhi (wrap offset)
660     float[3]    _pad                    (std140 struct alignment)
──────────────────────────────────────────────────────────
672 bytes total
```

### Extension Region (Consumer-Defined, starts at byte 672)

Written by IUniformExtension::write() on the render thread. Example for
PlasmaZones:

```
672     vec4[64]    zoneRects               ZoneUniformExtension
1696    vec4[64]    zoneFillColors          ZoneUniformExtension
2720    vec4[64]    zoneBorderColors        ZoneUniformExtension
3744    vec4[64]    zoneParams              ZoneUniformExtension
4768    — end —
```

---

## GLSL Contract

PhosphorRendering's common.glsl declares ShaderUniforms (672 bytes, no
extensions). Consumers declare their own UBO block that extends it:

**PhosphorShell's `common.glsl`** (generic, 672 bytes):
```glsl
layout(std140, binding = 0) uniform ShaderUniforms {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    // ... all BaseUniforms fields ...
    float iTimeHi;
};
```

**PlasmaZones' `common.glsl`** (extends with zones):
```glsl
layout(std140, binding = 0) uniform ZoneUniforms {
    // ── BaseUniforms (672 bytes) ──
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    // ... all BaseUniforms fields ...
    float iTimeHi;
    // ── Zone extension ──
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};
```

Both are valid — the GPU doesn't care about the block name, only the layout.

---

## CMake Target

```cmake
# phosphorrendering/CMakeLists.txt
set(PHOSPHORRENDERING_VERSION "0.1.0")

add_library(PhosphorRendering SHARED
    src/shadereffect.cpp
    src/shadernoderhicore.cpp        # Core prepare/render/release
    src/shadernoderhipipeline.cpp    # SRB + pipeline construction
    src/shadernoderhiuniforms.cpp    # UBO sync + texture uploads
    src/shadernoderhisetters.cpp     # Property setters
    src/shadercompiler.cpp
    src/bufferpassmanager.cpp
)
add_library(PhosphorRendering::PhosphorRendering ALIAS PhosphorRendering)

target_link_libraries(PhosphorRendering
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        PhosphorShell::PhosphorShell
    PRIVATE
        Qt6::GuiPrivate
        Qt6::ShaderToolsPrivate
)
```

### PlasmaZones Integration

```cmake
target_link_libraries(plasmazones_rendering PRIVATE
    PhosphorRendering::PhosphorRendering
)
```

---

## Directory Structure

```
phosphorrendering/
├── CMakeLists.txt
├── include/
│   └── PhosphorRendering/
│       ├── PhosphorRendering.h         # Umbrella header
│       ├── ShaderEffect               # Forwarding header
│       ├── ShaderEffect.h             # QQuickItem
│       ├── ShaderNodeRhi              # Forwarding header
│       ├── ShaderNodeRhi.h            # QSGRenderNode
│       ├── ShaderCompiler             # Forwarding header
│       ├── ShaderCompiler.h           # Static compiler utility
│       ├── BufferPassManager          # Forwarding header
│       └── BufferPassManager.h        # Multipass orchestrator
└── src/
    ├── shadereffect.cpp
    ├── shadernoderhicore.cpp
    ├── shadernoderhipipeline.cpp
    ├── shadernoderhiuniforms.cpp
    ├── shadernoderhisetters.cpp
    ├── shadercompiler.cpp
    ├── bufferpassmanager.cpp
    └── internal.h                     # RhiConstants, cache, macros
```

---

## Migration Path

### Phase 2a: Create phosphorrendering/ with ShaderCompiler + BufferPassManager
- Mechanical extraction of detail:: namespace → ShaderCompiler
- Extract buffer pass logic → BufferPassManager
- PlasmaZones still owns ZoneShaderNodeRhi but delegates to BufferPassManager

### Phase 2b: Extract ShaderNodeRhi
- Generalize ZoneShaderNodeRhi → PhosphorRendering::ShaderNodeRhi
- Replace zone-specific UBO writes with IUniformExtension::write()
- Replace labels texture (binding 1) with setExtraBinding()
- PlasmaZones creates a thin wrapper that configures the base node

### Phase 2c: Extract ShaderEffect
- Generalize ZoneShaderItem → PhosphorRendering::ShaderEffect
- Zone Q_PROPERTYs removed; zone parsing stays in PlasmaZones subclass
- Thread-safe snapshot system generalized to IUniformExtension

### Phase 2d: PlasmaZones becomes a consumer
- ZoneShaderItem : PhosphorRendering::ShaderEffect
- ZoneUniformExtension : PhosphorShell::IUniformExtension
- ZoneShaderNodeBase deleted (replaced by ShaderNodeRhi)
- rendering_macros.h deleted (setters simplified with indexed API)

---

## Key Design Decisions

### Indexed setters vs named setters

**Old (PlasmaZones):** `setCustomParams1()` through `setCustomParams8()` — 40 virtual methods.

**New (PhosphorRendering):** `setCustomParams(int index, const QVector4D& v)` — 2 methods.

QML still gets named properties on ShaderEffect (customParams1..8) for
ergonomic binding, but internally they delegate to the indexed API.

### Binding 1 is reserved, not managed

PlasmaZones needs binding 1 for zone labels. Other consumers may need it for
something else (e.g. a compositor's cursor texture). Rather than making the
library manage binding 1, we reserve it and let consumers provide their own
texture via setExtraBinding(1, ...).

This avoids a dummy 1×1 texture upload on every frame for consumers that
don't use binding 1.

### appField0/appField1 in BaseUniforms

These two int fields at offsets 88–95 are consumer-defined. PlasmaZones uses
them for zoneCount/highlightedCount. A WM might use them for workspace count
and active workspace index. The library writes 0 by default; consumers set
them via IUniformExtension or ShaderNodeRhi's appField accessors.

### ShaderCompiler is static, not a service

Shader compilation is stateless (source → QShader). The cache is an
optimization, not state. Making it static keeps the API simple and avoids
lifecycle questions about when to create/destroy a compiler instance.

### BufferPassManager owns its RHI resources

The buffer manager creates and owns its FBO textures, render targets, and
pipelines. ShaderNodeRhi queries outputTexture() for the image pass SRB.
This clean ownership boundary makes it testable in isolation.
