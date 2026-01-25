# Shader Support Design Document

## Executive Summary

| Aspect | Decision |
|--------|----------|
| **Qt6 ShaderEffect compatibility** | Individual uniforms, NOT UBO blocks |
| **Zone data transfer** | Uniform arrays (4 x vec4[32]), NOT texture (Canvas can't do RGBA32F) |
| **Registry architecture** | Single instance in daemon, D-Bus to editor |
| **Qt6::ShaderTools** | Optional dependency, graceful degradation |
| **User custom shaders** | Enabled in v1, runtime compiled via `qsb` subprocess |
| **Error handling** | Deferred to next show(), no mid-drag window recreation |
| **Editor preview** | Static thumbnails only (v1), live preview in v2 |
| **Wayland support** | Match ZoneOverlay.qml flags exactly |

## Overview

This document describes the design for adding Shadertoy-style shader support to PlasmaZones zone overlays. Shaders provide customizable visual effects for zone rendering during window drag operations.

### Goals

1. **Per-layout shader assignment** - Each layout can have its own shader effect
2. **Shadertoy-like authoring** - Familiar API for users coming from Shadertoy
3. **Full-overlay rendering** - Single shader renders all zones (enables cross-zone effects)
4. **Editor integration** - Shader selection and preview in layout editor
5. **Performance conscious** - Graceful degradation, fallback to standard rendering
6. **Optional dependency** - Shader support compiles only if Qt6::ShaderTools is available

### Non-Goals

- Zone selector/layout popup does NOT use shaders (remains standard QML)
- Per-zone shader assignment (too complex, per-layout is sufficient)
- Real-time shader code editing in the editor (v1 uses file-based workflow)
- Shadertoy URL import (v2 feature)

### Coding Standards Compliance

All implementation MUST follow `.cursorrules` conventions:

- **SPDX headers** required on ALL new files (C++ and QML)
- **Qt6 string literals**: Use `QStringLiteral()` for constants, `QLatin1String()` for JSON keys
- **Namespace**: All C++ code in `namespace PlasmaZones { }`
- **QML registration**: ALL new QML files MUST be added to `qt_add_qml_module()` in CMakeLists.txt
- **Settings workflow**: New settings require updates to kcfg, ISettings, Settings.h, and Settings.cpp

---

## Architecture

### Key Design Decisions

1. **Single ShaderRegistry in Daemon** - Editor accesses shader list via D-Bus (matches existing architecture)
2. **Uniform array zone data** - Zone parameters passed as 4 vec4 arrays (rects, fillColors, borderColors, params) - limited to 32 zones but simpler than texture-based approach
3. **Individual uniforms for ShaderEffect** - Qt6 ShaderEffect doesn't support UBO blocks
4. **Optional Qt6::ShaderTools** - Graceful degradation if not installed
5. **Deferred shader fallback** - Errors don't trigger window recreation during active drag

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Layout Editor                                │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  PropertyPanel.qml                                           │   │
│  │  └─ ShaderSelector (ComboBox + Preview)                     │   │
│  │      • Lists shaders via D-Bus (SettingsAdaptor)            │   │
│  │      • Shows static preview thumbnail                        │   │
│  │      • Exposes shader-specific parameters                    │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Sets layout.shaderId via D-Bus
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         Data Model                                   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Layout                                                      │   │
│  │  ├─ QString shaderId        // "none", "glow", "plasma"...  │   │
│  │  ├─ QVariantMap shaderParams // shader-specific settings    │   │
│  │  └─ zones[]                                                  │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Serialized to layout.json
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       Runtime (Daemon)                               │
│  ┌──────────────────┐    ┌──────────────────────────────────┐      │
│  │  ShaderRegistry  │    │  OverlayService                   │      │
│  │  (singleton)     │    │  • Creates ShaderOverlay.qml      │      │
│  │  • Load shaders  │───►│  • Builds zone data QVariantList  │      │
│  │  • D-Bus exposed │    │  • Updates iTime each frame       │      │
│  │  • Metadata      │    │  • Deferred error handling        │      │
│  └──────────────────┘    └──────────────────────────────────┘      │
│           │                                                          │
│           │ availableShaders(), shaderInfo() via D-Bus              │
│           ▼                                                          │
│  ┌──────────────────┐                                               │
│  │  SettingsAdaptor │ ◄─── Editor queries shader list here         │
│  └──────────────────┘                                               │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     Rendering (QML)                                  │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  ShaderOverlay.qml (replaces ZoneOverlay.qml when shader)   │   │
│  │  ├─ ShaderEffect { fragmentShader: "*.qsb" }               │   │
│  │  │   ├─ Individual uniforms (NOT UBO block)                │   │
│  │  │   ├─ iTime, iResolution as properties                   │   │
│  │  │   └─ zones: QVariantList mapped to uniform arrays       │   │
│  │  └─ Repeater { ZoneLabel {} }  // Text overlay for numbers │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  ZoneOverlay.qml (unchanged - used when shaderId="none")    │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Data Model Changes

### Layout Class Extensions

```cpp
// layout.h additions
namespace PlasmaZones {

class Layout : public QObject {
    Q_OBJECT

    // Existing properties...

    // New shader properties
    Q_PROPERTY(QString shaderId READ shaderId WRITE setShaderId NOTIFY shaderIdChanged)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged)

public:
    // Getters
    QString shaderId() const { return m_shaderId; }
    QVariantMap shaderParams() const { return m_shaderParams; }

    // Setters (emit only on change per .cursorrules)
    void setShaderId(const QString &id);
    void setShaderParams(const QVariantMap &params);

    // Constants
    static constexpr int MaxZones = 32;  // Shader uniform buffer limit

Q_SIGNALS:
    void shaderIdChanged();
    void shaderParamsChanged();

private:
    QString m_shaderId = QStringLiteral("none");  // Default: no shader
    QVariantMap m_shaderParams;                    // Shader-specific parameters
};

} // namespace PlasmaZones
```

```cpp
// layout.cpp additions
void Layout::setShaderId(const QString &id)
{
    if (m_shaderId != id) {
        m_shaderId = id;
        Q_EMIT shaderIdChanged();
    }
}

void Layout::setShaderParams(const QVariantMap &params)
{
    if (m_shaderParams != params) {
        m_shaderParams = params;
        Q_EMIT shaderParamsChanged();
    }
}
```

### Layout JSON Serialization

```cpp
// In Layout::toJson()
QJsonObject Layout::toJson() const
{
    QJsonObject obj;
    // ... existing serialization ...

    obj[QLatin1String("shaderId")] = m_shaderId;
    obj[QLatin1String("shaderParams")] = QJsonObject::fromVariantMap(m_shaderParams);

    return obj;
}

// In Layout::fromJson() - with migration for existing layouts
void Layout::fromJson(const QJsonObject &obj)
{
    // ... existing deserialization ...

    // Shader properties with defaults for migration
    m_shaderId = obj[QLatin1String("shaderId")].toString(QStringLiteral("none"));
    m_shaderParams = obj[QLatin1String("shaderParams")].toObject().toVariantMap();
}
```

### Layout JSON Schema Extension

```json
{
  "id": "uuid-here",
  "name": "My Layout",
  "shaderId": "glow",
  "shaderParams": {
    "glowIntensity": 0.8,
    "pulseSpeed": 2.0,
    "glowColor": "#00ffff"
  },
  "zones": [...]
}
```

### Shader Metadata Schema

```json
// shaders/glow/metadata.json
{
  "id": "glow",
  "name": "Pulsing Glow",
  "description": "Soft glow effect that pulses when zones are highlighted",
  "author": "PlasmaZones",
  "version": "1.0",
  "parameters": [
    {
      "id": "glowIntensity",
      "name": "Glow Intensity",
      "type": "float",
      "default": 0.5,
      "min": 0.0,
      "max": 1.0,
      "maps_to": "customParams1_x"
    },
    {
      "id": "pulseSpeed",
      "name": "Pulse Speed",
      "type": "float",
      "default": 2.0,
      "min": 0.5,
      "max": 10.0,
      "maps_to": "customParams1_y"
    },
    {
      "id": "glowColor",
      "name": "Glow Color",
      "type": "color",
      "default": "#ffffff",
      "useZoneColor": true
    }
  ],
  "preview": "preview.png"
}
```

### Canonical metadata.json Schema

All metadata.json files (bundled and user) MUST follow this schema:

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "required": ["id", "name"],
  "properties": {
    "id": {
      "type": "string",
      "pattern": "^[a-z0-9-]+$",
      "description": "Unique shader ID (lowercase, alphanumeric, hyphens only)"
    },
    "name": {
      "type": "string",
      "description": "Human-readable display name"
    },
    "description": {
      "type": "string",
      "description": "Brief description of the shader effect"
    },
    "author": {
      "type": "string",
      "description": "Author name or identifier"
    },
    "version": {
      "type": "string",
      "pattern": "^[0-9]+\\.[0-9]+$",
      "description": "Shader version (e.g., '1.0')"
    },
    "preview": {
      "type": "string",
      "description": "Filename of preview image (128x80 PNG recommended)"
    },
    "parameters": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["id", "name", "type", "default", "maps_to"],
        "properties": {
          "id": {
            "type": "string",
            "description": "Parameter ID for internal use"
          },
          "name": {
            "type": "string",
            "description": "Human-readable parameter name"
          },
          "type": {
            "type": "string",
            "enum": ["float", "int", "bool", "color"],
            "description": "Parameter data type"
          },
          "default": {
            "description": "Default value (type depends on 'type' field)"
          },
          "min": {
            "type": "number",
            "description": "Minimum value (for float/int types)"
          },
          "max": {
            "type": "number",
            "description": "Maximum value (for float/int types)"
          },
          "maps_to": {
            "type": "string",
            "pattern": "^(customParams[12]_[xyzw]|customColor[12])$",
            "description": "Target uniform component (e.g., 'customParams1_x', 'customColor1')"
          },
          "useZoneColor": {
            "type": "boolean",
            "description": "If true, defaults to zone's color instead of 'default'"
          }
        }
      }
    }
  }
}
```

**Key requirements:**
- `id` MUST be unique and match the directory name
- `maps_to` MUST use underscore notation (`customParams1_x`) not dot notation
- Parameters are optional but if present, `maps_to` is required for each

---

## Shader System Design

### Qt Version Requirements

**Minimum: Qt 6.2** - Required for:
- `qt_add_shaders()` CMake function with BATCHABLE keyword
- Improved ShaderEffect compilation and error reporting
- `Qt.vector4d()` support in QML for uniform binding

**Note**: KDE Plasma 6 requires Qt 6.6+, so this minimum is conservative. The shader system uses standard Qt 6 features and should work with any Qt 6.x version, but Qt 6.2 is the earliest version with the `qt_add_shaders()` CMake function.

### Qt6 ShaderEffect Constraints

**CRITICAL**: Qt6 `ShaderEffect` does NOT support uniform buffer objects (UBOs) or `layout(std140)` blocks. All uniforms must be:

1. Declared as individual `uniform` statements
2. Bound via QML properties with matching names
3. Limited to types Qt can marshal: `float`, `vec2`, `vec3`, `vec4`, `mat4`, `sampler2D`

### Zone Data via Uniform Arrays

Zone data is passed as uniform arrays rather than textures because:

1. **QML Canvas limitation**: Canvas `getContext("2d")` only supports 8-bit channels, not RGBA32F
2. **Sufficient for 32 zones**: 4 vec4 arrays × 32 elements = 2KB, within uniform limits
3. **Simpler binding**: Direct QML property binding to shader uniforms
4. **No texture upload overhead**: Uniforms update faster than texture uploads

```glsl
// Zone data uniform arrays (max 32 zones)
uniform vec4 zoneRects[32];        // x, y, width, height (normalized 0-1)
uniform vec4 zoneFillColors[32];   // RGBA premultiplied alpha
uniform vec4 zoneBorderColors[32]; // RGBA
uniform vec4 zoneParams[32];       // borderRadius, borderWidth, isHighlighted, zoneNumber
```

**Data flow:**
```
C++ OverlayService          QML ShaderOverlay           GLSL Shader
─────────────────          ─────────────────           ───────────
buildZonesList() ────────► zones: [...] ──────────────► zoneRects[i]
  (QVariantList)              │                         zoneFillColors[i]
                              │ .map() transforms       zoneBorderColors[i]
                              ▼                         zoneParams[i]
                         zoneRects: [...]
                         zoneFillColors: [...]
```

**Trade-offs:**
- 2KB uniform data per frame (acceptable for overlay that runs during drag)
- Limited to 32 zones (sufficient for practical layouts)
- Array size is compile-time constant (can't dynamically grow)

### Uniform Structure for ShaderEffect

Shaders use individual uniforms compatible with Qt6 ShaderEffect:

```glsl
#version 440

// Qt-required uniforms (automatically provided by ShaderEffect)
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// Uniforms bound via QML properties
uniform float qt_Opacity;          // From ShaderEffect.opacity
uniform float iTime;               // Seconds since overlay shown
uniform float iTimeDelta;          // Frame delta time (clamped max 0.1s)
uniform vec2 iResolution;          // Viewport in LOGICAL pixels
uniform float iDevicePixelRatio;   // Screen.devicePixelRatio
uniform int iFrame;                // Frame counter
uniform int shaderFormatVersion;   // Currently 1 (for future compatibility)

// Zone metadata
uniform int zoneCount;             // Actual zone count (0 to 32)
uniform int highlightedCount;      // Number of highlighted zones

// Zone data as uniform arrays (max 32 zones)
uniform vec4 zoneRects[32];        // x, y, width, height (normalized 0-1)
uniform vec4 zoneFillColors[32];   // RGBA premultiplied alpha
uniform vec4 zoneBorderColors[32]; // RGBA
uniform vec4 zoneParams[32];       // borderRadius, borderWidth, isHighlighted, zoneNumber

// Shader-specific custom parameters (mapped from layout.shaderParams)
uniform vec4 customParams1;        // Maps to customParams1_x, _y, _z, _w in shaderParams
uniform vec4 customParams2;        // Maps to customParams2_x, _y, _z, _w in shaderParams
uniform vec4 customColor1;         // Maps to customColor1 (color string) in shaderParams
uniform vec4 customColor2;         // Maps to customColor2 (color string) in shaderParams
```

**Important Notes:**

1. **iResolution is in LOGICAL pixels** - Shader handles DPI internally:
   ```qml
   // In ShaderOverlay.qml
   property size iResolution: Qt.size(width, height)
   property real iDevicePixelRatio: Screen.devicePixelRatio
   ```

2. **Zone data accessed via uniform arrays**:
   ```glsl
   // Get zone i data (i = 0 to zoneCount-1)
   vec4 rect = zoneRects[i];
   vec4 fillColor = zoneFillColors[i];
   vec4 borderColor = zoneBorderColors[i];
   vec4 params = zoneParams[i];
   // params.x = borderRadius, params.y = borderWidth
   // params.z = isHighlighted (0 or 1), params.w = zoneNumber
   ```

3. **Coordinate system** - All coordinates in logical pixels, shader converts:
   ```glsl
   vec2 fragCoord = qt_TexCoord0 * iResolution;
   vec2 zonePos = rect.xy * iResolution;
   vec2 zoneSize = rect.zw * iResolution;
   ```

4. **Parameter mapping** - Shader parameters map to customParams uniforms:
   ```json
   // In metadata.json
   { "id": "glowIntensity", "maps_to": "customParams1_x" }
   ```
   The C++ code transforms this to `shaderParams["customParams1_x"] = value`

### Shadertoy-like API (User-Facing)

Bundled shaders use a Shadertoy-inspired API. A common include file provides helper functions:

**Zone helper functions (inlined in all shaders):**

These helper functions are included in the boilerplate prepended to user shaders and inlined in bundled shaders:

```glsl
// Zone data structure (unpacked from uniform arrays)
struct ZoneData {
    vec4 rect;           // x, y, width, height (normalized 0-1)
    vec4 fillColor;      // RGBA premultiplied alpha
    vec4 borderColor;    // RGBA
    float borderRadius;  // Logical pixels
    float borderWidth;   // Logical pixels
    float isHighlighted; // 0.0 or 1.0
    float zoneNumber;    // 1-based zone number
};

// Fetch zone data from uniform arrays
ZoneData getZone(int index) {
    ZoneData z;
    z.rect = zoneRects[index];
    z.fillColor = zoneFillColors[index];
    z.borderColor = zoneBorderColors[index];
    z.borderRadius = zoneParams[index].x;
    z.borderWidth = zoneParams[index].y;
    z.isHighlighted = zoneParams[index].z;
    z.zoneNumber = zoneParams[index].w;
    return z;
}

// Signed distance to rounded rectangle (negative = inside)
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Get signed distance to zone edge
float getZoneDistance(vec2 fragCoord, ZoneData zone) {
    vec2 zonePos = zone.rect.xy * iResolution;
    vec2 zoneSize = zone.rect.zw * iResolution;
    vec2 center = zonePos + zoneSize * 0.5;
    vec2 halfSize = zoneSize * 0.5;
    return sdRoundedBox(fragCoord - center, halfSize, zone.borderRadius);
}

// Find which zone contains point (-1 if none, returns topmost/last)
int getZoneAtPoint(vec2 fragCoord) {
    int result = -1;
    for (int i = 0; i < zoneCount; i++) {
        ZoneData z = getZone(i);
        if (getZoneDistance(fragCoord, z) < 0.0) {
            result = i;  // Last match wins (topmost z-order)
        }
    }
    return result;
}

// Check if point is inside zone
bool insideZone(vec2 fragCoord, ZoneData zone) {
    return getZoneDistance(fragCoord, zone) < 0.0;
}

// Default zone rendering with border
vec4 defaultZoneRender(vec2 fragCoord, ZoneData zone) {
    float dist = getZoneDistance(fragCoord, zone);
    
    if (dist > 0.0) {
        return vec4(0.0);  // Outside zone
    }
    
    // Border region
    if (dist > -zone.borderWidth) {
        return zone.borderColor;
    }
    
    // Fill region
    return zone.fillColor;
}

// Render all zones with default style
vec4 defaultRender(vec2 fragCoord) {
    vec4 result = vec4(0.0);
    
    for (int i = 0; i < zoneCount; i++) {
        ZoneData z = getZone(i);
        vec4 zoneColor = defaultZoneRender(fragCoord, z);
        
        // Alpha composite (back to front)
        result = mix(result, zoneColor, zoneColor.a);
    }
    
    return result;
}
```

**Example shader (glow/effect.frag):**

Note: Bundled shaders include all helper code inline. User shaders get boilerplate prepended automatically by ShaderCompiler.

```glsl
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// Qt uniforms
uniform float qt_Opacity;

// Shadertoy-compatible uniforms
uniform float iTime;
uniform vec2 iResolution;
uniform int shaderFormatVersion;

// Zone metadata
uniform int zoneCount;
uniform int highlightedCount;

// Zone data as uniform arrays (max 32 zones)
uniform vec4 zoneRects[32];        // x, y, width, height (normalized 0-1)
uniform vec4 zoneFillColors[32];   // RGBA premultiplied
uniform vec4 zoneBorderColors[32]; // RGBA
uniform vec4 zoneParams[32];       // borderRadius, borderWidth, isHighlighted, zoneNumber

// Custom parameters from layout.shaderParams
uniform vec4 customParams1;  // x=glowIntensity, y=pulseSpeed
uniform vec4 customColor1;   // Glow color override

// ============ HELPER FUNCTIONS (inlined) ============

struct ZoneData {
    vec4 rect;
    vec4 fillColor;
    vec4 borderColor;
    float borderRadius;
    float borderWidth;
    float isHighlighted;
    float zoneNumber;
};

ZoneData getZone(int index) {
    ZoneData z;
    z.rect = zoneRects[index];
    z.fillColor = zoneFillColors[index];
    z.borderColor = zoneBorderColors[index];
    z.borderRadius = zoneParams[index].x;
    z.borderWidth = zoneParams[index].y;
    z.isHighlighted = zoneParams[index].z;
    z.zoneNumber = zoneParams[index].w;
    return z;
}

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float getZoneDistance(vec2 fragCoord, ZoneData zone) {
    vec2 zonePos = zone.rect.xy * iResolution;
    vec2 zoneSize = zone.rect.zw * iResolution;
    vec2 center = zonePos + zoneSize * 0.5;
    vec2 halfSize = zoneSize * 0.5;
    return sdRoundedBox(fragCoord - center, halfSize, zone.borderRadius);
}

vec4 defaultZoneRender(vec2 fragCoord, ZoneData zone) {
    float dist = getZoneDistance(fragCoord, zone);
    if (dist > 0.0) return vec4(0.0);
    if (dist > -zone.borderWidth) return zone.borderColor;
    return zone.fillColor;
}

// ============ SHADER EFFECT ============

void main() {
    vec2 fragCoord = qt_TexCoord0 * iResolution;
    vec4 result = vec4(0.0);
    
    float glowIntensity = customParams1.x;
    float pulseSpeed = customParams1.y;
    
    for (int i = 0; i < zoneCount; i++) {
        ZoneData z = getZone(i);
        float dist = getZoneDistance(fragCoord, z);
        
        // Base zone rendering
        vec4 zoneColor = defaultZoneRender(fragCoord, z);
        
        // Add glow effect for highlighted zones
        if (z.isHighlighted > 0.5 && dist > 0.0 && dist < 50.0) {
            float pulse = 0.5 + 0.5 * sin(iTime * pulseSpeed);
            float glow = exp(-dist * 0.1) * glowIntensity * pulse;
            vec3 glowColor = customColor1.rgb;
            zoneColor.rgb += glow * glowColor;
            zoneColor.a = max(zoneColor.a, glow * 0.5);
        }
        
        result = mix(result, zoneColor, zoneColor.a);
    }
    
    fragColor = result * qt_Opacity;
}
```

### Helper Functions Summary

| Function | Description |
|----------|-------------|
| `ZoneData getZone(int index)` | Fetch zone data from uniform arrays |
| `float sdRoundedBox(vec2 p, vec2 b, float r)` | Signed distance to rounded rect |
| `float getZoneDistance(vec2 fragCoord, ZoneData zone)` | SDF to zone edge |
| `int getZoneAtPoint(vec2 fragCoord)` | Find zone at point (-1 if none) |
| `bool insideZone(vec2 fragCoord, ZoneData zone)` | Check if point in zone |
| `vec4 defaultZoneRender(vec2 fragCoord, ZoneData zone)` | Render single zone |
| `vec4 defaultRender(vec2 fragCoord)` | Render all zones with defaults |

---

## Shader Registry

### Directory Structure

```
/usr/share/plasmazones/shaders/     (system shaders - installed by package)
~/.local/share/plasmazones/shaders/ (user shaders - runtime compiled)

System shaders (read-only, pre-compiled):
shaders/
├── none/
│   └── metadata.json               (placeholder - no shader)
├── glow/
│   ├── metadata.json
│   ├── effect.frag                 (source - for reference only)
│   ├── effect.frag.qsb             (pre-compiled at package build)
│   └── preview.png                 (128x80 thumbnail)
├── neon/
│   └── ...
└── ...

User shaders (writable, runtime compiled):
~/.local/share/plasmazones/shaders/
├── my-effect/
│   ├── metadata.json               (required)
│   ├── effect.frag                 (required - user's shader code)
│   ├── effect.frag.qsb             (auto-generated on first use)
│   └── preview.png                 (optional)
└── another-effect/
    └── ...
```

### ShaderRegistry Class (Daemon-Only Singleton)

The `ShaderRegistry` is owned by the daemon and exposed to the editor via D-Bus. This ensures a single source of truth for available shaders. It loads both system shaders (pre-compiled) and user shaders (runtime-compiled).

```cpp
// shaderregistry.h
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "shadercompiler.h"
#include <QObject>
#include <QHash>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QFileSystemWatcher>
#include <QTimer>
#include <memory>

namespace PlasmaZones {

/**
 * @brief Registry of available shader effects
 * 
 * Singleton owned by Daemon, exposed to editor via SettingsAdaptor D-Bus.
 * Loads system shaders (pre-compiled .qsb) and user shaders (runtime-compiled).
 * Watches user shader directory for changes and auto-recompiles.
 */
class PLASMAZONES_EXPORT ShaderRegistry : public QObject {
    Q_OBJECT

public:
    struct ParameterInfo {
        QString id;
        QString name;
        QString type;            // "float", "color", "int", "bool"
        QString mapsTo;          // e.g., "customParams1_x" for QML property binding
        QVariant defaultValue;
        QVariant minValue;
        QVariant maxValue;
        bool useZoneColor = false;
    };

    struct ShaderInfo {
        QString id;
        QString name;
        QString description;
        QString author;
        QString version;
        QUrl shaderUrl;          // file:// or qrc:// URL to .qsb
        QString sourcePath;      // Path to .frag source (user shaders only)
        QString previewPath;     // Absolute path to preview.png
        QList<ParameterInfo> parameters;
        
        bool isUserShader = false;      // True for ~/.local/share shaders
        bool needsRecompile = false;    // Source newer than .qsb
        QString compilationError;       // Non-empty if compilation failed

        bool isValid() const { 
            return !id.isEmpty() && shaderUrl.isValid() && compilationError.isEmpty(); 
        }
    };

    explicit ShaderRegistry(QObject *parent = nullptr);
    ~ShaderRegistry() override = default;

    // Singleton access (created by Daemon)
    static ShaderRegistry *instance();

    // Get all available shaders (includes "none" placeholder)
    QList<ShaderInfo> availableShaders() const;

    // Get shader list as QVariantList for D-Bus/QML
    Q_INVOKABLE QVariantList availableShadersVariant() const;

    // Get specific shader info (returns invalid ShaderInfo if not found)
    ShaderInfo shader(const QString &id) const;

    // Get shader info as QVariantMap for D-Bus/QML
    Q_INVOKABLE QVariantMap shaderInfo(const QString &id) const;

    // Get shader .qsb URL (returns empty if not found or "none")
    Q_INVOKABLE QUrl shaderUrl(const QString &id) const;

    // Check if shaders are available (Qt6::ShaderTools was found at build)
    Q_INVOKABLE bool shadersEnabled() const;

    // Check if user can create custom shaders (qsb tool available)
    Q_INVOKABLE bool userShadersEnabled() const;

    // Get user shader directory path
    Q_INVOKABLE QString userShaderDirectory() const;

    // Open user shader directory in file manager
    Q_INVOKABLE void openUserShaderDirectory() const;

    // Validate shader parameters against schema
    bool validateParams(const QString &id, const QVariantMap &params) const;

    // Validate and coerce params, returning map with defaults for invalid values
    QVariantMap validateAndCoerceParams(const QString &id, const QVariantMap &params) const;

    // Get default parameters for a shader
    Q_INVOKABLE QVariantMap defaultParams(const QString &id) const;

    // Force recompile a user shader
    Q_INVOKABLE bool recompileShader(const QString &id);

    // Reload shader list (called on file changes, startup)
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void shadersChanged();
    void shaderCompilationStarted(const QString &shaderId);
    void shaderCompilationFinished(const QString &shaderId, bool success, const QString &error);

private Q_SLOTS:
    void onUserShaderDirChanged(const QString &path);
    void performDebouncedRefresh();

private:
    void loadSystemShaders();
    void loadUserShaders();
    void loadUserShaderFromDir(const QString &shaderDir);
    ShaderInfo loadShaderMetadata(const QString &shaderDir);
    bool validateParameterValue(const ParameterInfo &param, const QVariant &value) const;
    void setupFileWatcher();
    void ensureUserShaderDirExists();

    QHash<QString, ShaderInfo> m_shaders;
    bool m_shadersEnabled = false;
    std::unique_ptr<ShaderCompiler> m_compiler;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refreshTimer = nullptr;

    static ShaderRegistry *s_instance;
    static QString systemShaderDir();
    static QString userShaderDir();
};

} // namespace PlasmaZones
```

### D-Bus Exposure via SettingsAdaptor

The editor queries available shaders through the existing D-Bus settings interface:

```cpp
// settingsadaptor.h additions
class SettingsAdaptor : public QDBusAbstractAdaptor {
    // ... existing methods ...

    // Shader registry access (delegates to ShaderRegistry::instance())
public Q_SLOTS:
    // Shader discovery
    QVariantList AvailableShaders();
    QVariantMap ShaderInfo(const QString &shaderId);
    QVariantMap DefaultShaderParams(const QString &shaderId);
    
    // Shader system status
    bool ShadersEnabled();
    bool UserShadersEnabled();
    QString UserShaderDirectory();
    void OpenUserShaderDirectory();
    
    // Shader management
    void RefreshShaders();
    bool RecompileShader(const QString &shaderId);
};

// settingsadaptor.cpp
QVariantList SettingsAdaptor::AvailableShaders()
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->availableShadersVariant() : QVariantList();
}

QVariantMap SettingsAdaptor::ShaderInfo(const QString &shaderId)
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->shaderInfo(shaderId) : QVariantMap();
}

bool SettingsAdaptor::ShadersEnabled()
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->shadersEnabled() : false;
}

bool SettingsAdaptor::UserShadersEnabled()
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->userShadersEnabled() : false;
}

QString SettingsAdaptor::UserShaderDirectory()
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->userShaderDirectory() : QString();
}

void SettingsAdaptor::OpenUserShaderDirectory()
{
    auto *registry = ShaderRegistry::instance();
    if (registry) {
        registry->openUserShaderDirectory();
    }
}

void SettingsAdaptor::RefreshShaders()
{
    auto *registry = ShaderRegistry::instance();
    if (registry) {
        registry->refresh();
    }
}

bool SettingsAdaptor::RecompileShader(const QString &shaderId)
{
    auto *registry = ShaderRegistry::instance();
    return registry ? registry->recompileShader(shaderId) : false;
}
```

### Parameter Validation Implementation

```cpp
// shaderregistry.cpp
bool ShaderRegistry::validateParams(const QString &id, const QVariantMap &params) const
{
    const ShaderInfo info = shader(id);
    if (!info.isValid()) {
        return false;
    }

    for (const ParameterInfo &param : info.parameters) {
        if (params.contains(param.id)) {
            if (!validateParameterValue(param, params.value(param.id))) {
                qWarning() << "Invalid shader parameter:" << param.id
                           << "for shader:" << id;
                return false;
            }
        }
    }
    return true;
}

bool ShaderRegistry::validateParameterValue(const ParameterInfo &param,
                                            const QVariant &value) const
{
    if (param.type == QLatin1String("float")) {
        bool ok = false;
        double v = value.toDouble(&ok);
        if (!ok) return false;
        if (param.minValue.isValid() && v < param.minValue.toDouble()) return false;
        if (param.maxValue.isValid() && v > param.maxValue.toDouble()) return false;
    } else if (param.type == QLatin1String("int")) {
        bool ok = false;
        int v = value.toInt(&ok);
        if (!ok) return false;
        if (param.minValue.isValid() && v < param.minValue.toInt()) return false;
        if (param.maxValue.isValid() && v > param.maxValue.toInt()) return false;
    } else if (param.type == QLatin1String("color")) {
        QColor c(value.toString());
        if (!c.isValid()) return false;
    } else if (param.type == QLatin1String("bool")) {
        if (!value.canConvert<bool>()) return false;
    }
    return true;
}

QVariantMap ShaderRegistry::validateAndCoerceParams(const QString &id,
                                                     const QVariantMap &params) const
{
    QVariantMap result;
    const ShaderInfo info = shader(id);
    if (!info.isValid()) {
        return result;
    }

    for (const ParameterInfo &param : info.parameters) {
        if (params.contains(param.id) && validateParameterValue(param, params.value(param.id))) {
            result[param.id] = params.value(param.id);
        } else {
            result[param.id] = param.defaultValue;
        }
    }
    return result;
}

QVariantMap ShaderRegistry::defaultParams(const QString &id) const
{
    QVariantMap result;
    const ShaderInfo info = shader(id);
    for (const ParameterInfo &param : info.parameters) {
        result[param.id] = param.defaultValue;
    }
    return result;
}
```

---

## Layout Editor Integration

### PropertyPanel Changes

Add shader selection section to the layout properties (not zone properties):

```
┌─────────────────────────────────────────┐
│ Layout Properties                        │
├─────────────────────────────────────────┤
│ Name: [My Layout          ]             │
│                                         │
│ ─── Shader Effect ───────────────────── │
│                                         │
│ Effect: [▼ Pulsing Glow    ]           │
│         ┌──────────────────┐            │
│         │  ╭──╮  ╭──╮     │ ← Static   │
│         │  │░░│  │░░│     │   preview  │
│         │  ╰──╯  ╰──╯     │   (128x80) │
│         └──────────────────┘            │
│                                         │
│ ─── Effect Settings ─────────────────── │
│                                         │
│ Glow Intensity: [────●────] 0.5        │
│ Pulse Speed:    [──●──────] 2.0        │
│ Glow Color:     [■ #00ffff] [Use Zone] │
│                                         │
├─────────────────────────────────────────┤
│ Zone Properties (existing)              │
│ ...                                     │
└─────────────────────────────────────────┘
```

**NOTE**: v1 uses static preview thumbnails only. Live shader preview in editor is a v2 feature.

### Editor Shader Access via D-Bus

The editor does NOT have a local `ShaderRegistry`. It queries shaders via D-Bus from the daemon:

```cpp
// EditorController.cpp - query shaders via D-Bus

void EditorController::refreshAvailableShaders()
{
    // Query daemon's ShaderRegistry via SettingsAdaptor D-Bus
    QDBusInterface settingsIface(
        QStringLiteral("org.plasmazones"),
        QStringLiteral("/Settings"),
        QStringLiteral("org.plasmazones.Settings"),
        QDBusConnection::sessionBus()
    );
    
    if (!settingsIface.isValid()) {
        qWarning() << "Cannot query shaders: daemon D-Bus interface unavailable";
        m_availableShaders.clear();
        m_availableShaders.append(createNoneShaderEntry());
        Q_EMIT availableShadersChanged();
        return;
    }
    
    QDBusReply<QVariantList> reply = settingsIface.call(QStringLiteral("AvailableShaders"));
    if (reply.isValid()) {
        m_availableShaders = reply.value();
        Q_EMIT availableShadersChanged();
    } else {
        qWarning() << "D-Bus AvailableShaders call failed:" << reply.error().message();
    }
}

QVariantMap EditorController::getShaderInfo(const QString &shaderId) const
{
    QDBusInterface settingsIface(
        QStringLiteral("org.plasmazones"),
        QStringLiteral("/Settings"),
        QStringLiteral("org.plasmazones.Settings"),
        QDBusConnection::sessionBus()
    );
    
    if (settingsIface.isValid()) {
        QDBusReply<QVariantMap> reply = settingsIface.call(
            QStringLiteral("ShaderInfo"), shaderId);
        if (reply.isValid()) {
            return reply.value();
        }
    }
    return QVariantMap();
}
```

### New QML Components

**ShaderSelector.qml** - Shader dropdown with static preview
```qml
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    
    required property var availableShaders  // QVariantList from controller
    required property string currentShaderId
    
    signal shaderSelected(string shaderId)
    
    spacing: Kirigami.Units.smallSpacing
    
    // Shader dropdown
    ComboBox {
        id: shaderCombo
        Layout.fillWidth: true
        
        model: root.availableShaders
        textRole: "name"
        valueRole: "id"
        
        currentIndex: {
            for (var i = 0; i < model.length; i++) {
                if (model[i].id === root.currentShaderId) return i
            }
            return 0  // Default to "none"
        }
        
        onActivated: {
            root.shaderSelected(currentValue)
        }
    }
    
    // Static preview thumbnail
    Image {
        Layout.preferredWidth: 128
        Layout.preferredHeight: 80
        Layout.alignment: Qt.AlignHCenter
        
        visible: root.currentShaderId !== "none"
        
        source: {
            // Get preview path from current shader info
            for (var i = 0; i < root.availableShaders.length; i++) {
                if (root.availableShaders[i].id === root.currentShaderId) {
                    var path = root.availableShaders[i].previewPath
                    return path ? "file://" + path : ""
                }
            }
            return ""
        }
        
        fillMode: Image.PreserveAspectFit
        
        // Fallback for missing preview
        Rectangle {
            anchors.fill: parent
            visible: parent.status !== Image.Ready
            color: Kirigami.Theme.alternateBackgroundColor
            
            Kirigami.Icon {
                anchors.centerIn: parent
                source: "view-preview"
                width: Kirigami.Units.iconSizes.large
                height: width
            }
        }
    }
}
```

**ShaderParameterEditor.qml** - Dynamic parameter controls
```qml
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    
    required property var parameters     // List of parameter definitions from shader
    required property var currentParams  // Current values from layout.shaderParams
    
    signal parameterChanged(string key, var value)
    signal resetRequested()
    
    spacing: Kirigami.Units.smallSpacing
    
    Repeater {
        model: root.parameters
        
        delegate: RowLayout {
            required property var modelData
            
            Layout.fillWidth: true
            
            Label {
                text: modelData.name
                Layout.preferredWidth: 100
            }
            
            // Float slider
            Loader {
                Layout.fillWidth: true
                active: modelData.type === "float"
                
                sourceComponent: Slider {
                    from: modelData.min !== undefined ? modelData.min : 0
                    to: modelData.max !== undefined ? modelData.max : 1
                    value: root.currentParams[modelData.id] !== undefined 
                           ? root.currentParams[modelData.id] 
                           : modelData.default
                    
                    onMoved: root.parameterChanged(modelData.id, value)
                }
            }
            
            // Color button
            Loader {
                active: modelData.type === "color"
                
                sourceComponent: Rectangle {
                    width: 32
                    height: 32
                    color: root.currentParams[modelData.id] || modelData.default
                    border.width: 1
                    border.color: Kirigami.Theme.textColor
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: colorDialog.open()
                    }
                    
                    // Would need actual ColorDialog here
                }
            }
        }
    }
    
    Button {
        text: i18n("Reset to Defaults")
        icon.name: "edit-undo"
        onClicked: root.resetRequested()
    }
}
```

### Editor Controller Changes

```cpp
// EditorController.h additions

namespace PlasmaZones {

class EditorController : public QObject {
    Q_OBJECT

    // Shader properties for current layout
    Q_PROPERTY(QString currentShaderId READ currentShaderId
               WRITE setCurrentShaderId NOTIFY currentShaderIdChanged)
    Q_PROPERTY(QVariantMap currentShaderParams READ currentShaderParams
               WRITE setCurrentShaderParams NOTIFY currentShaderParamsChanged)
    Q_PROPERTY(QVariantList availableShaders READ availableShaders
               NOTIFY availableShadersChanged)
    Q_PROPERTY(QVariantList currentShaderParameters READ currentShaderParameters
               NOTIFY currentShaderParametersChanged)
    Q_PROPERTY(bool shadersEnabled READ shadersEnabled NOTIFY shadersEnabledChanged)

public:
    // Getters
    QString currentShaderId() const;
    QVariantMap currentShaderParams() const;
    QVariantList availableShaders() const { return m_availableShaders; }
    QVariantList currentShaderParameters() const;
    bool shadersEnabled() const { return m_shadersEnabled; }

    // Setters
    void setCurrentShaderId(const QString &id);
    void setCurrentShaderParams(const QVariantMap &params);

    // QML-invokable methods
    Q_INVOKABLE void setShaderParameter(const QString &key, const QVariant &value);
    Q_INVOKABLE void resetShaderParameters();
    Q_INVOKABLE void saveShaderAsDefault();
    Q_INVOKABLE void refreshAvailableShaders();

Q_SIGNALS:
    void currentShaderIdChanged();
    void currentShaderParamsChanged();
    void availableShadersChanged();
    void currentShaderParametersChanged();
    void shadersEnabledChanged();

private:
    // Cached from D-Bus query (NOT a local ShaderRegistry)
    QVariantList m_availableShaders;
    bool m_shadersEnabled = false;
    
    QVariantMap getShaderInfo(const QString &shaderId) const;
};

} // namespace PlasmaZones
```

```cpp
// EditorController.cpp shader implementations

void EditorController::setCurrentShaderId(const QString &id)
{
    if (!m_currentLayout) {
        return;
    }

    if (m_currentLayout->shaderId() != id) {
        auto *cmd = new UpdateLayoutShaderCommand(m_currentLayout, id);
        m_undoController->push(cmd);

        Q_EMIT currentShaderIdChanged();
        Q_EMIT currentShaderParametersChanged();
    }
}

void EditorController::setShaderParameter(const QString &key, const QVariant &value)
{
    if (!m_currentLayout) {
        return;
    }

    QVariantMap params = m_currentLayout->shaderParams();
    if (params.value(key) != value) {
        auto *cmd = new UpdateShaderParamCommand(m_currentLayout, key, value);
        m_undoController->push(cmd);
        Q_EMIT currentShaderParamsChanged();
    }
}

void EditorController::saveShaderAsDefault()
{
    if (!m_currentLayout) {
        return;
    }

    const QString shaderId = m_currentLayout->shaderId();
    const QVariantMap params = m_currentLayout->shaderParams();

    QJsonDocument doc(QJsonObject::fromVariantMap(params));
    const QString paramsJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    // Update global defaults via D-Bus
    QDBusInterface settingsIface(
        QStringLiteral("org.plasmazones"),
        QStringLiteral("/Settings"),
        QStringLiteral("org.plasmazones.Settings"),
        QDBusConnection::sessionBus()
    );
    
    if (settingsIface.isValid()) {
        settingsIface.call(QStringLiteral("SetDefaultShaderId"), shaderId);
        settingsIface.call(QStringLiteral("SetDefaultShaderParams"), paramsJson);
    }
}

QVariantList EditorController::currentShaderParameters() const
{
    if (!m_currentLayout) {
        return QVariantList();
    }
    
    QVariantMap info = getShaderInfo(m_currentLayout->shaderId());
    return info.value(QLatin1String("parameters")).toList();
}
```

### Undo/Redo Support

New commands needed:
- `UpdateLayoutShaderCommand` - Change shader selection
- `UpdateShaderParamCommand` - Change individual parameter
- `ResetShaderParamsCommand` - Reset all parameters to defaults

```cpp
// UpdateLayoutShaderCommand.h
class UpdateLayoutShaderCommand : public QUndoCommand {
public:
    UpdateLayoutShaderCommand(Layout *layout, const QString &newShaderId);
    
    void undo() override;
    void redo() override;
    int id() const override { return CommandId::UpdateLayoutShader; }
    
private:
    QPointer<Layout> m_layout;
    QString m_oldShaderId;
    QString m_newShaderId;
    QVariantMap m_oldParams;  // Preserve old params for undo
};
```

---

## Overlay Rendering Changes

### OverlayService Modifications

```cpp
// overlayservice.h additions

#include <QImage>
#include <QElapsedTimer>
#include <QTimer>
#include <QMutex>

namespace PlasmaZones {

class OverlayService : public IOverlayService {
    Q_OBJECT

    // ...existing members...

private:
    // Shader support
    QElapsedTimer m_shaderTimer;           // For iTime (shared across all monitors)
    std::atomic<qint64> m_lastFrameTime{0}; // For iTimeDelta (atomic for thread safety)
    std::atomic<int> m_frameCount{0};       // For iFrame
    QTimer *m_shaderUpdateTimer = nullptr;  // Animation timer
    QMutex m_shaderTimerMutex;              // Protects timer restart operations

    // Zone data for shaders (regenerated when zones/highlights change)
    bool m_zoneDataDirty = true;            // Rebuild zone data on next frame

    // Shader error state (deferred handling)
    bool m_shaderErrorPending = false;      // Error occurred, handle on next show()
    QString m_pendingShaderError;

    // Choose overlay type based on layout shader
    void createOverlayWindow(QScreen *screen);  // Modified
    bool useShaderOverlay() const;
    bool canUseShaders() const;  // Checks ShaderRegistry::instance()->shadersEnabled()

    // Zone data for shaders - builds QVariantList for uniform arrays
    QVariantList buildZonesList(QScreen *screen) const;
    void updateZonesForAllWindows();

    // Update shader uniforms each frame
    void updateShaderUniforms();

    // Animation control
    void startShaderAnimation();
    void stopShaderAnimation();

    // System events
    void handleSystemResumed();
    void handleShaderError(const QString &error);

public Q_SLOTS:
    // Called from QML when ShaderEffect reports error
    void onShaderError(const QString &errorLog);
};

} // namespace PlasmaZones
```

### Zone Data Generation for Uniform Arrays

Zone data is built as a QVariantList that QML maps to uniform arrays:

```cpp
// overlayservice.cpp

QVariantList OverlayService::buildZonesList(QScreen *screen) const
{
    QVariantList zones;
    
    if (!m_layout) {
        return zones;
    }
    
    const auto layoutZones = m_layout->zones();
    constexpr int maxZones = 32;
    
    if (layoutZones.size() > maxZones) {
        qWarning() << "Layout has" << layoutZones.size() << "zones, shader supports max"
                   << maxZones << "- extra zones will not render";
    }
    
    int highlightedCount = 0;
    const int zoneCount = qMin(layoutZones.size(), maxZones);
    
    for (int i = 0; i < zoneCount; ++i) {
        const Zone *zone = layoutZones.at(i);
        
        // Skip invalid zones
        if (zone->width() <= 0.0 || zone->height() <= 0.0) {
            continue;
        }
        
        QVariantMap zoneData;
        
        // Rect (normalized 0-1)
        zoneData[QLatin1String("x")] = zone->x();
        zoneData[QLatin1String("y")] = zone->y();
        zoneData[QLatin1String("width")] = zone->width();
        zoneData[QLatin1String("height")] = zone->height();
        
        // Fill color (RGBA premultiplied alpha)
        QColor fillColor = zone->useCustomColors() ? zone->highlightColor() 
                                                   : m_settings->highlightColor();
        qreal alpha = zone->useCustomColors() ? zone->activeOpacity() 
                                              : m_settings->activeOpacity();
        zoneData[QLatin1String("fillR")] = fillColor.redF() * alpha;
        zoneData[QLatin1String("fillG")] = fillColor.greenF() * alpha;
        zoneData[QLatin1String("fillB")] = fillColor.blueF() * alpha;
        zoneData[QLatin1String("fillA")] = alpha;
        
        // Border color (RGBA)
        QColor borderColor = zone->useCustomColors() ? zone->borderColor()
                                                     : m_settings->borderColor();
        zoneData[QLatin1String("borderR")] = borderColor.redF();
        zoneData[QLatin1String("borderG")] = borderColor.greenF();
        zoneData[QLatin1String("borderB")] = borderColor.blueF();
        zoneData[QLatin1String("borderA")] = borderColor.alphaF();
        
        // Params
        zoneData[QLatin1String("borderRadius")] = zone->useCustomColors() 
            ? zone->borderRadius() : m_settings->borderRadius();
        zoneData[QLatin1String("borderWidth")] = zone->useCustomColors()
            ? zone->borderWidth() : m_settings->borderWidth();
        
        // Highlight state
        bool isHighlighted = m_highlightedZoneIds.contains(zone->id().toString());
        zoneData[QLatin1String("isHighlighted")] = isHighlighted ? 1.0 : 0.0;
        if (isHighlighted) {
            highlightedCount++;
        }
        
        zoneData[QLatin1String("zoneNumber")] = zone->zoneNumber();
        zoneData[QLatin1String("name")] = zone->name();
        
        zones.append(zoneData);
    }
    
    return zones;
}

void OverlayService::updateZonesForAllWindows()
{
    m_zoneDataDirty = false;
    
    for (auto it = m_overlayWindows.begin(); it != m_overlayWindows.end(); ++it) {
        QScreen *screen = it.key();
        QQuickWindow *window = it.value();
        
        QVariantList zones = buildZonesList(screen);
        int highlightedCount = 0;
        for (const QVariant &z : zones) {
            if (z.toMap().value(QLatin1String("isHighlighted")).toDouble() > 0.5) {
                highlightedCount++;
            }
        }
        
        window->setProperty("zones", zones);
        window->setProperty("zoneCount", zones.size());
        window->setProperty("highlightedCount", highlightedCount);
    }
}
```

**QML binding in ShaderOverlay.qml:**
```qml
// zones property (QVariantList) is mapped to uniform arrays:
property var zoneRects: root.zones.map(z => Qt.vector4d(z.x, z.y, z.width, z.height))
property var zoneFillColors: root.zones.map(z => Qt.vector4d(z.fillR, z.fillG, z.fillB, z.fillA))
property var zoneBorderColors: root.zones.map(z => Qt.vector4d(z.borderR, z.borderG, z.borderB, z.borderA))
property var zoneParams: root.zones.map(z => Qt.vector4d(z.borderRadius, z.borderWidth, z.isHighlighted, z.zoneNumber))
```

### Multi-Monitor iTime Synchronization

All monitors share the same time base for synchronized effects:

```cpp
void OverlayService::show()
{
    // Handle any pending shader error from previous session
    if (m_shaderErrorPending) {
        qWarning() << "Previous shader error:" << m_pendingShaderError;
        m_shaderErrorPending = false;
        // Will fall back to standard overlay via useShaderOverlay()
    }
    
    m_visible = true;
    
    {
        QMutexLocker locker(&m_shaderTimerMutex);
        m_shaderTimer.start();
        m_lastFrameTime.store(0);
        m_frameCount.store(0);
    }
    
    m_zoneDataDirty = true;  // Rebuild zone data on next frame

    for (QScreen *screen : QGuiApplication::screens()) {
        createOverlayWindow(screen);
    }

    if (useShaderOverlay()) {
        updateZonesForAllWindows();  // Push zone data to uniform arrays
        startShaderAnimation();
    }
}

void OverlayService::updateShaderUniforms()
{
    qint64 currentTime;
    {
        QMutexLocker locker(&m_shaderTimerMutex);
        if (!m_shaderTimer.isValid()) {
            return;
        }
        currentTime = m_shaderTimer.elapsed();
    }
    
    const float iTime = currentTime / 1000.0f;

    // Calculate delta time with clamp (max 100ms prevents jumps after sleep)
    constexpr float maxDelta = 0.1f;
    const qint64 lastTime = m_lastFrameTime.exchange(currentTime);
    float iTimeDelta = qMin((currentTime - lastTime) / 1000.0f, maxDelta);

    // Prevent frame counter overflow (reset at 1 billion, ~193 days at 60fps)
    int frame = m_frameCount.fetch_add(1);
    if (frame > 1000000000) {
        m_frameCount.store(0);
    }

    // Update zone data for shaders if dirty (highlight changed, etc.)
    if (m_zoneDataDirty) {
        updateZonesForAllWindows();
    }

    // Update ALL shader overlay windows with synchronized time
    for (auto *window : m_overlayWindows) {
        QObject *rootItem = window->contentItem();
        if (rootItem) {
            rootItem->setProperty("iTime", iTime);
            rootItem->setProperty("iTimeDelta", iTimeDelta);
            rootItem->setProperty("iFrame", frame);
        }
    }
}

void OverlayService::startShaderAnimation()
{
    if (!m_shaderUpdateTimer) {
        m_shaderUpdateTimer = new QTimer(this);
        m_shaderUpdateTimer->setTimerType(Qt::PreciseTimer);
        connect(m_shaderUpdateTimer, &QTimer::timeout,
                this, &OverlayService::updateShaderUniforms);
    }

    const int frameRate = m_settings ? m_settings->shaderFrameRate() : 60;
    const int interval = 1000 / qBound(30, frameRate, 144);
    m_shaderUpdateTimer->start(interval);
}

void OverlayService::stopShaderAnimation()
{
    if (m_shaderUpdateTimer) {
        m_shaderUpdateTimer->stop();
    }
}

void OverlayService::handleSystemResumed()
{
    QMutexLocker locker(&m_shaderTimerMutex);
    if (m_visible && m_shaderTimer.isValid()) {
        m_shaderTimer.restart();
        m_lastFrameTime.store(0);
    }
}

void OverlayService::onShaderError(const QString &errorLog)
{
    qWarning() << "Shader error during overlay:" << errorLog;
    
    // Defer handling to next show() - don't recreate windows mid-drag
    m_shaderErrorPending = true;
    m_pendingShaderError = errorLog;
    
    // For current session, the ShaderOverlay falls back to transparent
    // (shader shows nothing on error, but window remains to prevent flicker)
}

bool OverlayService::canUseShaders() const
{
    auto *registry = ShaderRegistry::instance();
    return registry && registry->shadersEnabled();
}

bool OverlayService::useShaderOverlay() const
{
    if (!canUseShaders()) {
        return false;
    }
    if (!m_layout || m_layout->shaderId() == QLatin1String("none")) {
        return false;
    }
    if (m_shaderErrorPending) {
        return false;  // Previous error, fall back
    }
    if (m_settings && !m_settings->enableShaderEffects()) {
        return false;  // User disabled shaders
    }
    
    auto *registry = ShaderRegistry::instance();
    return registry && registry->shader(m_layout->shaderId()).isValid();
}
```

### New QML: ShaderOverlay.qml

```qml
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Zone overlay window with shader-based rendering.
 * 
 * Zone data is passed as uniform arrays (not texture) because:
 * 1. Canvas getContext("2d") only supports 8-bit channels, not RGBA32F
 * 2. For ≤32 zones, uniform arrays are efficient enough
 * 3. Direct property binding is simpler than texture upload
 */
Window {
    id: root

    // Properties set from C++ OverlayService
    required property url shaderSource
    required property var zones           // Zone data array [{x,y,w,h,colors...}, ...]
    required property int zoneCount
    required property int highlightedCount
    required property var shaderParams    // Custom shader parameters

    // Animated uniforms (updated by C++ via setProperty)
    property real iTime: 0
    property real iTimeDelta: 0
    property int iFrame: 0

    // Window flags - MUST match ZoneOverlay.qml for Wayland compatibility
    // Qt.platform.pluginName returns "wayland" or "xcb"
    flags: Qt.FramelessWindowHint |
           (Qt.platform.pluginName === "wayland" ? Qt.WindowDoesNotAcceptFocus : Qt.WindowStaysOnTopHint)
    color: "transparent"
    visible: false  // Controlled by C++ OverlayService

    // Main content
    Item {
        id: content
        anchors.fill: parent

        // Shader effect layer
        ShaderEffect {
            id: shaderEffect
            anchors.fill: parent
            visible: status !== ShaderEffect.Error

            fragmentShader: root.shaderSource

            // Shadertoy-compatible uniforms (LOGICAL pixels)
            property real iTime: root.iTime
            property real iTimeDelta: root.iTimeDelta
            property int iFrame: root.iFrame
            property size iResolution: Qt.size(root.width, root.height)
            property real iDevicePixelRatio: Screen.devicePixelRatio
            property int shaderFormatVersion: 1

            // Zone metadata counts
            property int zoneCount: root.zoneCount
            property int highlightedCount: root.highlightedCount

            // Zone data as uniform arrays (max 32 zones)
            // Each zone: vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params
            property var zoneRects: root.zones.map(z => Qt.vector4d(z.x, z.y, z.width, z.height))
            property var zoneFillColors: root.zones.map(z => Qt.vector4d(z.fillR, z.fillG, z.fillB, z.fillA))
            property var zoneBorderColors: root.zones.map(z => Qt.vector4d(z.borderR, z.borderG, z.borderB, z.borderA))
            property var zoneParams: root.zones.map(z => Qt.vector4d(z.borderRadius, z.borderWidth, z.isHighlighted, z.zoneNumber))

            // Shader-specific custom parameters (from layout.shaderParams via maps_to)
            property vector4d customParams1: internal.buildCustomParams1()
            property vector4d customParams2: internal.buildCustomParams2()
            property vector4d customColor1: internal.buildCustomColor1()
            property vector4d customColor2: internal.buildCustomColor2()

            onStatusChanged: {
                if (status === ShaderEffect.Error) {
                    console.error("ShaderOverlay: Shader error:", log)
                    // Report to C++ for deferred handling
                    // overlayService is set via engine->rootContext()->setContextProperty()
                    if (typeof overlayService !== "undefined") {
                        overlayService.onShaderError(log)
                    }
                }
            }
        }

        // Fallback when shader fails - show nothing (transparent)
        // The ShaderEffect.visible handles this automatically

        // Zone labels rendered on top of shader
        Repeater {
            model: root.zones

            ZoneLabel {
                required property var modelData
                required property int index

                // Position based on normalized zone rect
                x: (modelData.x !== undefined ? modelData.x : 0) * root.width
                y: (modelData.y !== undefined ? modelData.y : 0) * root.height
                width: (modelData.width !== undefined ? modelData.width : 0) * root.width
                height: (modelData.height !== undefined ? modelData.height : 0) * root.height

                zoneNumber: modelData.zoneNumber || (index + 1)
                zoneName: modelData.name || ""
                visible: root.showNumbers && width > 0 && height > 0
            }
        }
    }

    // Show numbers setting
    property bool showNumbers: true

    // Internal helper to build customParams from shaderParams using parameter schema
    QtObject {
        id: internal

        // Build customParams1 vec4 from shader parameters with maps_to: "customParams1_x/_y/_z/_w"
        // Note: maps_to uses underscores (QML property names), GLSL uses dots (vec4.x accessor)
        function buildCustomParams1() {
            var p = root.shaderParams || {}
            return Qt.vector4d(
                p.customParams1_x !== undefined ? p.customParams1_x : 0.5,
                p.customParams1_y !== undefined ? p.customParams1_y : 2.0,
                p.customParams1_z !== undefined ? p.customParams1_z : 0.0,
                p.customParams1_w !== undefined ? p.customParams1_w : 0.0
            )
        }

        function buildCustomParams2() {
            var p = root.shaderParams || {}
            return Qt.vector4d(
                p.customParams2_x !== undefined ? p.customParams2_x : 0.0,
                p.customParams2_y !== undefined ? p.customParams2_y : 0.0,
                p.customParams2_z !== undefined ? p.customParams2_z : 0.0,
                p.customParams2_w !== undefined ? p.customParams2_w : 0.0
            )
        }

        function buildCustomColor1() {
            var p = root.shaderParams || {}
            if (p.customColor1) {
                var c = Qt.color(p.customColor1)
                return Qt.vector4d(c.r, c.g, c.b, c.a)
            }
            // Default to theme highlight color
            return Qt.vector4d(
                Kirigami.Theme.highlightColor.r,
                Kirigami.Theme.highlightColor.g,
                Kirigami.Theme.highlightColor.b,
                Kirigami.Theme.highlightColor.a
            )
        }

        function buildCustomColor2() {
            var p = root.shaderParams || {}
            if (p.customColor2) {
                var c = Qt.color(p.customColor2)
                return Qt.vector4d(c.r, c.g, c.b, c.a)
            }
            return Qt.vector4d(1, 1, 1, 1)
        }
    }
}
```

### New QML: ZoneLabel.qml

```qml
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Zone number/name label overlay.
 * Used by ShaderOverlay to display zone identifiers on top of shader.
 */
Item {
    id: root

    property int zoneNumber: 1
    property string zoneName: ""

    // Centered label
    Label {
        anchors.centerIn: parent
        text: root.zoneName || root.zoneNumber.toString()
        color: Kirigami.Theme.textColor
        font.pixelSize: Math.min(parent.width, parent.height) * 0.3
        font.bold: true

        // Text outline for visibility on any background
        style: Text.Outline
        styleColor: Kirigami.Theme.backgroundColor
    }
}
```

### Conditional Overlay Selection

```cpp
void OverlayService::createOverlayWindow(QScreen *screen)
{
    QUrl qmlSource;
    
    if (useShaderOverlay()) {
        qmlSource = QUrl(QStringLiteral("qrc:/qt/qml/org/plasmazones/overlay/ShaderOverlay.qml"));
    } else {
        qmlSource = QUrl(QStringLiteral("qrc:/qt/qml/org/plasmazones/overlay/ZoneOverlay.qml"));
    }

    // IMPORTANT: Expose overlayService to QML context for error reporting
    // This allows ShaderOverlay.qml to call overlayService.onShaderError()
    m_engine->rootContext()->setContextProperty(
        QStringLiteral("overlayService"), this);

    QQmlComponent component(m_engine.get(), qmlSource);
    if (component.status() != QQmlComponent::Ready) {
        qWarning() << "Failed to load overlay QML:" << component.errorString();
        return;
    }

    QObject *obj = component.create();
    QQuickWindow *window = qobject_cast<QQuickWindow *>(obj);
    if (!window) {
        qWarning() << "Overlay QML did not create a Window";
        delete obj;
        return;
    }

    // Set screen and geometry
    window->setScreen(screen);
    window->setGeometry(screen->geometry());

    // Set initial properties
    if (useShaderOverlay()) {
        auto *registry = ShaderRegistry::instance();
        const QString shaderId = m_layout->shaderId();
        
        window->setProperty("shaderSource", registry->shaderUrl(shaderId));
        window->setProperty("zoneCount", m_layout->zoneCount());
        window->setProperty("shaderParams", QVariant::fromValue(m_layout->shaderParams()));
        window->setProperty("zones", buildZonesList(screen));
    } else {
        // Existing ZoneOverlay properties...
        window->setProperty("zones", buildZonesList(screen));
    }

    m_overlayWindows.insert(screen, window);
}
```

---

## Shader Compilation Pipeline

### Optional Qt6::ShaderTools Dependency

Shader support is **optional** - the application builds and runs without shaders if Qt6::ShaderTools is not installed.

```cmake
# Root CMakeLists.txt

# Check for optional shader support
find_package(Qt6 COMPONENTS ShaderTools QUIET)
if(Qt6ShaderTools_FOUND)
    message(STATUS "Qt6::ShaderTools found - shader effects ENABLED")
    set(PLASMAZONES_SHADERS_ENABLED ON)
else()
    message(WARNING "Qt6::ShaderTools not found - shader effects DISABLED")
    message(WARNING "Install qt6-shadertools-dev (or equivalent) to enable shader effects")
    set(PLASMAZONES_SHADERS_ENABLED OFF)
endif()

# Pass to source via compile definition
if(PLASMAZONES_SHADERS_ENABLED)
    add_compile_definitions(PLASMAZONES_SHADERS_ENABLED)
endif()
```

### Build-Time Compilation (Shipped Shaders)

Shaders are compiled at build time using `qt_add_shaders()`. The `BATCHABLE` keyword is required for ShaderEffect compatibility.

```cmake
# src/CMakeLists.txt - only if shaders enabled

if(PLASMAZONES_SHADERS_ENABLED)
    # Shader source files
    set(SHADER_SOURCES
        ${CMAKE_SOURCE_DIR}/shaders/glow/effect.frag
        ${CMAKE_SOURCE_DIR}/shaders/neon/effect.frag
        ${CMAKE_SOURCE_DIR}/shaders/glass/effect.frag
        ${CMAKE_SOURCE_DIR}/shaders/gradient/effect.frag
        ${CMAKE_SOURCE_DIR}/shaders/pulse/effect.frag
    )

    # Compile shaders for ShaderEffect (BATCHABLE required!)
    qt_add_shaders(plasmazonesd "overlay_shaders"
        BATCHABLE                        # Required for ShaderEffect compatibility
        PREFIX "/qt/qml/org/plasmazones/shaders"
        FILES
            ${SHADER_SOURCES}
    )

    # Link ShaderTools to daemon
    target_link_libraries(plasmazonesd PRIVATE Qt6::ShaderTools)
endif()
```

**CRITICAL**: The `BATCHABLE` keyword is essential! Without it, shaders won't work with Qt Quick's ShaderEffect component.

### Shader Include Files

The `zone_helpers.glsl` include file must be handled specially since GLSL doesn't have a standard include mechanism. Options:

**Option A: Preprocessor concatenation at build time**
```cmake
# Custom command to concatenate includes into each shader
function(preprocess_shader INPUT_FILE OUTPUT_FILE)
    file(READ ${CMAKE_SOURCE_DIR}/shaders/common/zone_helpers.glsl HELPERS)
    file(READ ${INPUT_FILE} SHADER_CODE)
    file(WRITE ${OUTPUT_FILE} "${HELPERS}\n${SHADER_CODE}")
endfunction()

# Preprocess each shader before compilation
foreach(SHADER_SRC ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    set(PROCESSED_SHADER ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME})
    preprocess_shader(${SHADER_SRC} ${PROCESSED_SHADER})
    list(APPEND PROCESSED_SHADERS ${PROCESSED_SHADER})
endforeach()
```

**Option B: Inline helpers in each shader** (simpler, chosen for v1)

Each bundled shader includes the helper functions directly. This is more verbose but avoids build complexity.

### ShaderRegistry Compile-Time Flag

```cpp
// shaderregistry.cpp

ShaderRegistry::ShaderRegistry(QObject *parent)
    : QObject(parent)
{
#ifdef PLASMAZONES_SHADERS_ENABLED
    m_shadersEnabled = true;
    loadSystemShaders();
    loadUserShaders();
#else
    m_shadersEnabled = false;
    qInfo() << "Shader effects disabled (Qt6::ShaderTools not available at build time)";
#endif
}

bool ShaderRegistry::shadersEnabled() const
{
    return m_shadersEnabled;
}
```

### Runtime Compilation (User Shaders)

Users can add custom shaders by placing `.frag` files in their user shader directory.

#### User Shader Directory Structure

```
~/.local/share/plasmazones/shaders/
├── my-custom-effect/
│   ├── metadata.json          # Required: shader info and parameters
│   ├── effect.frag            # Required: GLSL fragment shader source
│   ├── preview.png            # Optional: 128x80 thumbnail
│   └── effect.frag.qsb        # Auto-generated: compiled shader (cached)
└── another-shader/
    └── ...
```

#### Shader Compilation Workflow

```
┌──────────────────────────────────────────────────────────────────┐
│  User places .frag file in ~/.local/share/plasmazones/shaders/  │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  ShaderRegistry::refresh() detects new shader directory         │
│  (called on startup, file watcher, or manual refresh)           │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  Check if .qsb exists and is newer than .frag                   │
│  If yes → use cached .qsb                                        │
│  If no  → compile via ShaderCompiler                            │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  ShaderCompiler::compile()                                       │
│  1. Prepend zone_helpers.glsl boilerplate                       │
│  2. Run qsb tool as subprocess                                   │
│  3. Cache .qsb alongside .frag                                   │
│  4. Return success/error                                         │
└──────────────────────────────────────────────────────────────────┘
```

#### ShaderCompiler Class

```cpp
// shadercompiler.h
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QProcess>

namespace PlasmaZones {

/**
 * @brief Compiles user GLSL shaders to Qt's .qsb format
 * 
 * Uses the qsb tool from Qt6::ShaderTools to compile fragment shaders.
 * Wraps user code with standard boilerplate and zone helper functions.
 */
class ShaderCompiler : public QObject {
    Q_OBJECT

public:
    enum class Result {
        Success,
        CompilationError,
        QsbToolNotFound,
        InvalidInput,
        WriteError
    };

    explicit ShaderCompiler(QObject *parent = nullptr);

    /**
     * Compile a fragment shader to .qsb format
     * @param fragPath Path to .frag source file
     * @param outputPath Path for output .qsb file (typically same dir as source)
     * @return Compilation result
     */
    Result compile(const QString &fragPath, const QString &outputPath);

    /**
     * Compile with custom boilerplate prepended
     * @param userCode Raw user shader code
     * @param outputPath Path for output .qsb file
     * @return Compilation result
     */
    Result compileWithBoilerplate(const QString &userCode, const QString &outputPath);

    /**
     * Check if the qsb tool is available
     */
    bool isQsbAvailable() const;

    /**
     * Get the last error message (set on failure)
     */
    QString lastError() const { return m_lastError; }

    /**
     * Get the qsb tool path
     */
    static QString qsbToolPath();

Q_SIGNALS:
    void compilationStarted(const QString &shaderPath);
    void compilationFinished(const QString &shaderPath, Result result);

private:
    QString wrapWithBoilerplate(const QString &userCode) const;
    Result runQsb(const QString &inputPath, const QString &outputPath);

    QString m_lastError;
    QString m_boilerplate;  // Cached zone_helpers content
};

} // namespace PlasmaZones
```

#### ShaderCompiler Implementation

```cpp
// shadercompiler.cpp

#include "shadercompiler.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace PlasmaZones {

// Boilerplate prepended to all user shaders
static const char *SHADER_BOILERPLATE = R"(
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// Qt-required uniforms
uniform float qt_Opacity;

// Shadertoy-compatible uniforms
uniform float iTime;
uniform float iTimeDelta;
uniform vec2 iResolution;
uniform float iDevicePixelRatio;
uniform int iFrame;
uniform int shaderFormatVersion;  // Currently 1

// Zone metadata
uniform int zoneCount;
uniform int highlightedCount;

// Zone data as uniform arrays (max 32 zones)
uniform vec4 zoneRects[32];        // x, y, width, height (normalized 0-1)
uniform vec4 zoneFillColors[32];   // RGBA premultiplied alpha
uniform vec4 zoneBorderColors[32]; // RGBA
uniform vec4 zoneParams[32];       // borderRadius, borderWidth, isHighlighted, zoneNumber

// Custom parameters (from layout.shaderParams via maps_to)
uniform vec4 customParams1;        // Maps to customParams1_x, _y, _z, _w
uniform vec4 customParams2;        // Maps to customParams2_x, _y, _z, _w
uniform vec4 customColor1;         // Maps to customColor1 (color string)
uniform vec4 customColor2;         // Maps to customColor2 (color string)

// Zone data structure (unpacked from uniform arrays)
struct ZoneData {
    vec4 rect;           // x, y, width, height (normalized 0-1)
    vec4 fillColor;      // RGBA premultiplied alpha
    vec4 borderColor;    // RGBA
    float borderRadius;  // Logical pixels
    float borderWidth;   // Logical pixels
    float isHighlighted; // 0.0 or 1.0
    float zoneNumber;    // 1-based zone number
};

ZoneData getZone(int index) {
    ZoneData z;
    z.rect = zoneRects[index];
    z.fillColor = zoneFillColors[index];
    z.borderColor = zoneBorderColors[index];
    z.borderRadius = zoneParams[index].x;
    z.borderWidth = zoneParams[index].y;
    z.isHighlighted = zoneParams[index].z;
    z.zoneNumber = zoneParams[index].w;
    return z;
}

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float getZoneDistance(vec2 fragCoord, ZoneData zone) {
    vec2 zonePos = zone.rect.xy * iResolution;
    vec2 zoneSize = zone.rect.zw * iResolution;
    vec2 center = zonePos + zoneSize * 0.5;
    vec2 halfSize = zoneSize * 0.5;
    return sdRoundedBox(fragCoord - center, halfSize, zone.borderRadius);
}

int getZoneAtPoint(vec2 fragCoord) {
    int result = -1;
    for (int i = 0; i < zoneCount; i++) {
        ZoneData z = getZone(i);
        if (getZoneDistance(fragCoord, z) < 0.0) {
            result = i;
        }
    }
    return result;
}

vec4 defaultZoneRender(vec2 fragCoord, ZoneData zone) {
    float dist = getZoneDistance(fragCoord, zone);
    if (dist > 0.0) return vec4(0.0);
    if (dist > -zone.borderWidth) return zone.borderColor;
    return zone.fillColor;
}

vec4 defaultRender(vec2 fragCoord) {
    vec4 result = vec4(0.0);
    for (int i = 0; i < zoneCount; i++) {
        ZoneData z = getZone(i);
        vec4 zoneColor = defaultZoneRender(fragCoord, z);
        result = mix(result, zoneColor, zoneColor.a);
    }
    return result;
}

// ============ USER CODE BELOW ============
)";

ShaderCompiler::ShaderCompiler(QObject *parent)
    : QObject(parent)
    , m_boilerplate(QString::fromUtf8(SHADER_BOILERPLATE))
{
}

QString ShaderCompiler::qsbToolPath()
{
    // Try to find qsb in PATH
    QString qsbPath = QStandardPaths::findExecutable(QStringLiteral("qsb"));
    if (!qsbPath.isEmpty()) {
        return qsbPath;
    }
    
    // Try Qt installation directory
    qsbPath = QStandardPaths::findExecutable(QStringLiteral("qsb"),
        {QStringLiteral("/usr/lib/qt6/bin"), QStringLiteral("/usr/lib64/qt6/bin")});
    
    return qsbPath;
}

bool ShaderCompiler::isQsbAvailable() const
{
    return !qsbToolPath().isEmpty();
}

QString ShaderCompiler::wrapWithBoilerplate(const QString &userCode) const
{
    return m_boilerplate + userCode;
}

ShaderCompiler::Result ShaderCompiler::compile(const QString &fragPath, const QString &outputPath)
{
    QFile file(fragPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = QStringLiteral("Cannot open shader file: %1").arg(fragPath);
        return Result::InvalidInput;
    }
    
    QString userCode = QString::fromUtf8(file.readAll());
    return compileWithBoilerplate(userCode, outputPath);
}

ShaderCompiler::Result ShaderCompiler::compileWithBoilerplate(const QString &userCode,
                                                               const QString &outputPath)
{
    if (!isQsbAvailable()) {
        m_lastError = QStringLiteral("qsb tool not found. Install qt6-shadertools.");
        return Result::QsbToolNotFound;
    }
    
    // Write wrapped shader to temp file
    QTemporaryFile tempFile;
    tempFile.setFileTemplate(QDir::tempPath() + QStringLiteral("/qfz_shader_XXXXXX.frag"));
    if (!tempFile.open()) {
        m_lastError = QStringLiteral("Cannot create temporary file");
        return Result::WriteError;
    }
    
    QString wrappedCode = wrapWithBoilerplate(userCode);
    tempFile.write(wrappedCode.toUtf8());
    tempFile.close();
    
    return runQsb(tempFile.fileName(), outputPath);
}

ShaderCompiler::Result ShaderCompiler::runQsb(const QString &inputPath, const QString &outputPath)
{
    Q_EMIT compilationStarted(inputPath);
    
    QProcess qsb;
    QStringList args;
    args << QStringLiteral("--glsl") << QStringLiteral("100es,120,150");
    args << QStringLiteral("--hlsl") << QStringLiteral("50");
    args << QStringLiteral("--msl") << QStringLiteral("12");
    args << QStringLiteral("-b");  // Batchable for ShaderEffect
    args << QStringLiteral("-o") << outputPath;
    args << inputPath;
    
    qsb.start(qsbToolPath(), args);
    if (!qsb.waitForFinished(30000)) {  // 30 second timeout
        m_lastError = QStringLiteral("qsb compilation timed out");
        Q_EMIT compilationFinished(inputPath, Result::CompilationError);
        return Result::CompilationError;
    }
    
    if (qsb.exitCode() != 0) {
        m_lastError = QString::fromUtf8(qsb.readAllStandardError());
        Q_EMIT compilationFinished(inputPath, Result::CompilationError);
        return Result::CompilationError;
    }
    
    Q_EMIT compilationFinished(inputPath, Result::Success);
    return Result::Success;
}

} // namespace PlasmaZones
```

#### User Shader Template

When users create a new shader, they write only the `main()` function:

```glsl
// ~/.local/share/plasmazones/shaders/my-effect/effect.frag
// User writes ONLY this part - boilerplate is prepended automatically

void main() {
    vec2 fragCoord = qt_TexCoord0 * iResolution;
    vec4 result = vec4(0.0);
    
    for (int i = 0; i < zoneCount; i++) {
        ZoneData z = getZone(i);
        float dist = getZoneDistance(fragCoord, z);
        
        // Default zone rendering
        vec4 zoneColor = defaultZoneRender(fragCoord, z);
        
        // Add custom effect: rainbow border for highlighted zones
        if (z.isHighlighted > 0.5 && dist > -z.borderWidth && dist < 0.0) {
            float hue = fract(iTime * 0.5 + z.zoneNumber * 0.1);
            vec3 rainbow = vec3(
                abs(hue * 6.0 - 3.0) - 1.0,
                2.0 - abs(hue * 6.0 - 2.0),
                2.0 - abs(hue * 6.0 - 4.0)
            );
            zoneColor.rgb = clamp(rainbow, 0.0, 1.0);
        }
        
        result = mix(result, zoneColor, zoneColor.a);
    }
    
    fragColor = result * qt_Opacity;
}
```

#### User Shader Metadata

```json
// ~/.local/share/plasmazones/shaders/my-effect/metadata.json
{
  "id": "my-effect",
  "name": "Rainbow Borders",
  "description": "Animated rainbow effect on highlighted zone borders",
  "author": "Username",
  "version": "1.0",
  "parameters": [
    {
      "id": "speed",
      "name": "Animation Speed",
      "type": "float",
      "default": 0.5,
      "min": 0.1,
      "max": 5.0,
      "maps_to": "customParams1_x"
    }
  ]
}
```

#### File Watcher for Hot Reload

```cpp
// In ShaderRegistry - watch user shader directory for changes

void ShaderRegistry::setupFileWatcher()
{
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(userShaderDir());
    
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ShaderRegistry::onUserShaderDirChanged);
}

void ShaderRegistry::onUserShaderDirChanged(const QString &path)
{
    Q_UNUSED(path)
    
    // Debounce rapid changes (e.g., editor auto-save)
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(500);  // 500ms debounce
        connect(m_refreshTimer, &QTimer::timeout, this, &ShaderRegistry::refresh);
    }
    
    m_refreshTimer->start();
}

void ShaderRegistry::refresh()
{
    qInfo() << "Refreshing shader registry...";
    
    // Reload all shaders
    // IMPORTANT: Load order defines precedence - user shaders loaded AFTER system
    // shaders will OVERRIDE bundled shaders with the same ID
    m_shaders.clear();
    loadSystemShaders();   // Load bundled shaders first
    loadUserShaders();     // User shaders override bundled with same ID
    
    Q_EMIT shadersChanged();
}

// User shader precedence implementation:
// When loadUserShaderFromDir() inserts into m_shaders, it overwrites any
// existing entry with the same ID. Since user shaders are loaded after
// system shaders, user shaders take precedence.
//
// Example: If bundled "glow" shader exists and user creates ~/shaders/glow/,
// the user's version will be used instead of the bundled version.
```

#### Compilation Error Handling

```cpp
// In ShaderRegistry::loadUserShaders()

void ShaderRegistry::loadUserShaderFromDir(const QString &shaderDir)
{
    QDir dir(shaderDir);
    QString fragPath = dir.filePath(QStringLiteral("effect.frag"));
    QString qsbPath = dir.filePath(QStringLiteral("effect.frag.qsb"));
    
    if (!QFile::exists(fragPath)) {
        qWarning() << "User shader missing effect.frag:" << shaderDir;
        return;
    }
    
    // Check if recompilation needed
    QFileInfo fragInfo(fragPath);
    QFileInfo qsbInfo(qsbPath);
    
    bool needsCompile = !qsbInfo.exists() || 
                        fragInfo.lastModified() > qsbInfo.lastModified();
    
    if (needsCompile) {
        qInfo() << "Compiling user shader:" << fragPath;
        
        ShaderCompiler compiler;
        ShaderCompiler::Result result = compiler.compile(fragPath, qsbPath);
        
        if (result != ShaderCompiler::Result::Success) {
            qWarning() << "Failed to compile user shader:" << fragPath;
            qWarning() << "Error:" << compiler.lastError();
            
            // Store error for display in editor
            ShaderInfo info = loadShaderMetadata(shaderDir);
            info.compilationError = compiler.lastError();
            info.shaderUrl = QUrl();  // Invalid - won't be usable
            m_shaders.insert(info.id, info);
            return;
        }
    }
    
    // Load metadata and register shader
    ShaderInfo info = loadShaderMetadata(shaderDir);
    info.shaderUrl = QUrl::fromLocalFile(qsbPath);
    info.isUserShader = true;
    m_shaders.insert(info.id, info);
}
```

#### Security Considerations

User shaders run on the GPU and cannot directly access the filesystem or network. However, there are some risks:

| Risk | Mitigation |
|------|------------|
| GPU hang (infinite loop) | GPU driver timeout (typically 2-5 seconds); system recovers |
| Excessive GPU load | User's own machine; they control what they run |
| Malformed .qsb | `qsb` tool validates during compilation |
| Large texture allocation | Shaders can't allocate; only use provided uniforms |

**Trust Model**: User shaders are treated like any user-installed script (bash, Python). The user explicitly places files in their data directory and chooses to enable them. No additional sandboxing is required for v1.

**Future Enhancement** (v2): For shader sharing/community library, add:
- Shader signing with trusted keys
- Complexity analysis (loop bounds check)
- User consent dialog for untrusted sources

---

## User Shader Template

A template is provided to help users create custom shaders:

### Template Directory Structure

```
docs/examples/custom-shader-template/
├── metadata.json
├── effect.frag
└── README.md
```

### Template metadata.json

```json
{
  "id": "my-custom-shader",
  "name": "My Custom Shader",
  "description": "A custom shader effect for PlasmaZones",
  "author": "Your Name",
  "version": "1.0",
  "parameters": [
    {
      "id": "intensity",
      "name": "Effect Intensity",
      "type": "float",
      "default": 0.5,
      "min": 0.0,
      "max": 1.0,
      "maps_to": "customParams1_x"
    },
    {
      "id": "speed",
      "name": "Animation Speed",
      "type": "float",
      "default": 1.0,
      "min": 0.1,
      "max": 5.0,
      "maps_to": "customParams1_y"
    },
    {
      "id": "color",
      "name": "Effect Color",
      "type": "color",
      "default": "#00ffff",
      "maps_to": "customColor1"
    }
  ]
}
```

### Template effect.frag

```glsl
// My Custom Shader for PlasmaZones
// Copy this folder to ~/.local/share/plasmazones/shaders/my-custom-shader/
//
// Available uniforms (auto-prepended boilerplate):
//   float iTime             - Seconds since overlay shown
//   vec2 iResolution        - Viewport size in logical pixels  
//   int zoneCount           - Number of zones (0-32)
//   int highlightedCount    - Number of highlighted zones
//   vec4 zoneRects[32]      - Zone positions (normalized 0-1)
//   vec4 zoneFillColors[32] - Zone fill colors (RGBA)
//   vec4 zoneBorderColors[32] - Zone border colors (RGBA)
//   vec4 zoneParams[32]     - Zone params (borderRadius, borderWidth, isHighlighted, zoneNumber)
//   vec4 customParams1      - Your parameters from metadata.json (maps_to: customParams1_x/y/z/w)
//   vec4 customColor1       - Your color parameter (maps_to: customColor1)
//
// Available functions (auto-prepended boilerplate):
//   ZoneData getZone(int index)           - Get zone data struct
//   float getZoneDistance(vec2, ZoneData) - Signed distance to zone
//   int getZoneAtPoint(vec2 fragCoord)    - Which zone contains point (-1 if none)
//   vec4 defaultZoneRender(vec2, ZoneData) - Default zone fill/border rendering
//   vec4 defaultRender(vec2 fragCoord)    - Render all zones with default style

void main() {
    vec2 fragCoord = qt_TexCoord0 * iResolution;
    
    // Get parameters from customParams1 (mapped from metadata.json)
    float intensity = customParams1.x;
    float speed = customParams1.y;
    vec3 effectColor = customColor1.rgb;
    
    // Start with default zone rendering
    vec4 result = defaultRender(fragCoord);
    
    // Add your custom effect here!
    // Example: Add a subtle animated vignette
    vec2 uv = fragCoord / iResolution;
    float vignette = 1.0 - length(uv - 0.5) * intensity;
    result.rgb *= vignette;
    
    // Example: Tint highlighted zones
    for (int i = 0; i < zoneCount; i++) {
        ZoneData z = getZone(i);
        if (z.isHighlighted > 0.5) {
            float dist = getZoneDistance(fragCoord, z);
            if (dist < 0.0) {
                // Inside highlighted zone - add animated color tint
                float pulse = 0.5 + 0.5 * sin(iTime * speed);
                result.rgb = mix(result.rgb, effectColor, intensity * pulse * 0.3);
            }
        }
    }
    
    fragColor = result * qt_Opacity;
}
```

### Template README.md

```markdown
# Custom Shader Template for PlasmaZones

## Installation

1. Copy this folder to `~/.local/share/plasmazones/shaders/`
2. Rename the folder to your shader's ID (e.g., `my-awesome-effect`)
3. Edit `metadata.json` with your shader's name and parameters
4. Edit `effect.frag` with your shader code
5. The shader will appear in the Layout Editor's shader selector

## Development Workflow

1. Open the Layout Editor and select your shader
2. Edit `effect.frag` in your text editor
3. Save the file - PlasmaZones auto-recompiles and reloads
4. If there's an error, check the Layout Editor for details

## Available Uniforms

| Uniform | Type | Description |
|---------|------|-------------|
| `iTime` | float | Seconds since overlay shown |
| `iTimeDelta` | float | Frame delta time |
| `iResolution` | vec2 | Viewport size (logical pixels) |
| `iFrame` | int | Frame counter |
| `zoneCount` | int | Number of zones |
| `zoneRects[32]` | vec4[32] | Zone position/size (x,y,w,h normalized) |
| `zoneFillColors[32]` | vec4[32] | Zone fill colors (RGBA premultiplied) |
| `zoneBorderColors[32]` | vec4[32] | Zone border colors (RGBA) |
| `zoneParams[32]` | vec4[32] | Zone params (borderRadius, borderWidth, isHighlighted, zoneNumber) |
| `customParams1` | vec4 | Your float parameters |
| `customParams2` | vec4 | More float parameters |
| `customColor1` | vec4 | Your color parameter |
| `customColor2` | vec4 | Another color parameter |

## Tips

- Use `defaultRender()` as a starting point and modify the result
- Access zone data with `getZone(index)` - returns position, colors, etc.
- Check `zone.isHighlighted` to apply effects to active zones
- Use `getZoneDistance()` for distance-based effects (glow, borders)
- Keep performance in mind - overlay runs during window drag!
```

---

## Bundled Shader Library (v1)

### Minimal Set for Initial Release

| ID | Name | Description |
|----|------|-------------|
| `none` | None | Standard QML rendering (no shader) |
| `glow` | Pulsing Glow | Soft animated glow on highlighted zones |
| `neon` | Neon Borders | Bright neon-style borders with bloom |
| `glass` | Frosted Glass | Subtle frosted glass effect |
| `gradient` | Gradient Fill | Animated gradient backgrounds |
| `pulse` | Radar Pulse | Ripple effect from zone centers |

### Shader Design Guidelines

1. **Performance**: Target 60fps on integrated graphics
2. **Subtlety**: Effects should enhance, not distract
3. **Theming**: Respect zone colors, multiply don't replace
4. **Fallback**: Graceful degradation if uniforms missing
5. **Consistency**: All shaders use same zone rendering base

### Preview Thumbnail Requirements

Each shader directory must include a `preview.png`:

- **Size**: 128x80 pixels (16:10 aspect ratio to match typical layout)
- **Content**: Representative frame showing shader effect with 2-3 sample zones
- **Background**: Transparent or dark (#1a1a1a) to work with both light/dark themes
- **Generation**: Created manually or via offline render script (not auto-generated at runtime)
- **Fallback**: If missing, editor shows generic shader icon

```
shaders/glow/
├── metadata.json
├── effect.frag
├── effect.frag.qsb
└── preview.png      # 128x80, shows glow effect on sample zones
```

**QML fallback for missing preview:**
```qml
Image {
    source: shader.previewPath !== ""
            ? shader.previewPath
            : "qrc:/icons/shader-placeholder.svg"
    fillMode: Image.PreserveAspectFit
}
```

---

## Performance Considerations

### Frame Budget

- Target: 16.6ms frame time (60fps)
- Shader budget: <5ms (leave headroom for compositor)
- Zone data update: <1ms

### Optimization Strategies

1. **Uniform updates**: Only update zone buffer when zones/highlight changes
2. **iTime precision**: Use `mediump float` where sufficient
3. **Early discard**: `discard` pixels outside all zones early
4. **LOD**: Reduce effect complexity during window drag
5. **Adaptive quality**: Detect frame drops, reduce quality dynamically

### Fallback Behavior

```cpp
bool OverlayService::useShaderOverlay() const {
    // Use singleton ShaderRegistry::instance() - not a member pointer
    if (!canUseShaders()) {
        return false;
    }
    if (!m_layout || m_layout->shaderId() == QLatin1String("none")) {
        return false;
    }
    if (m_shaderErrorPending) {
        return false;  // Previous error, fall back
    }
    if (m_settings && !m_settings->enableShaderEffects()) {
        return false;  // User disabled shaders
    }
    if (m_performanceModeActive) {  // Auto-detected low performance
        return false;
    }
    
    auto *registry = ShaderRegistry::instance();
    return registry && registry->shader(m_layout->shaderId()).isValid();
}
```

---

## Settings Integration

Per `.cursorrules` settings workflow, each new setting requires updates to 4 files.

### Step 1: plasmazones.kcfg

```xml
<!-- plasmazones.kcfg additions - in new "Shaders" group -->
<group name="Shaders">
    <entry name="EnableShaderEffects" type="Bool">
        <label>Enable shader effects for zone overlays</label>
        <default>true</default>
    </entry>

    <entry name="ShaderQuality" type="Int">
        <label>Shader rendering quality (0=Low, 1=Medium, 2=High)</label>
        <default>1</default>
        <min>0</min>
        <max>2</max>
    </entry>

    <entry name="ShaderFrameRate" type="Int">
        <label>Target frame rate for shader animations</label>
        <default>60</default>
        <min>30</min>
        <max>144</max>
    </entry>

    <entry name="DefaultShaderId" type="String">
        <label>Default shader for new layouts</label>
        <default>none</default>
    </entry>

    <entry name="DefaultShaderParams" type="String">
        <label>Default shader parameters (JSON)</label>
        <default>{}</default>
    </entry>
</group>
```

### Step 2: interfaces.h (ISettings)

```cpp
// interfaces.h additions to ISettings
Q_SIGNALS:
    // ... existing signals ...
    void enableShaderEffectsChanged();
    void shaderQualityChanged();
    void shaderFrameRateChanged();
    void defaultShaderIdChanged();
    void defaultShaderParamsChanged();
```

### Step 3: settings.h

```cpp
// settings.h additions

// In Q_PROPERTY section:
Q_PROPERTY(bool enableShaderEffects READ enableShaderEffects
           WRITE setEnableShaderEffects NOTIFY enableShaderEffectsChanged)
Q_PROPERTY(int shaderQuality READ shaderQuality
           WRITE setShaderQuality NOTIFY shaderQualityChanged)
Q_PROPERTY(int shaderFrameRate READ shaderFrameRate
           WRITE setShaderFrameRate NOTIFY shaderFrameRateChanged)
Q_PROPERTY(QString defaultShaderId READ defaultShaderId
           WRITE setDefaultShaderId NOTIFY defaultShaderIdChanged)
Q_PROPERTY(QString defaultShaderParams READ defaultShaderParams
           WRITE setDefaultShaderParams NOTIFY defaultShaderParamsChanged)

// In public section:
bool enableShaderEffects() const { return m_enableShaderEffects; }
void setEnableShaderEffects(bool value);

int shaderQuality() const { return m_shaderQuality; }
void setShaderQuality(int value);

int shaderFrameRate() const { return m_shaderFrameRate; }
void setShaderFrameRate(int value);

QString defaultShaderId() const { return m_defaultShaderId; }
void setDefaultShaderId(const QString &value);

QString defaultShaderParams() const { return m_defaultShaderParams; }
void setDefaultShaderParams(const QString &value);

// In private section:
bool m_enableShaderEffects = true;
int m_shaderQuality = 1;  // Medium
int m_shaderFrameRate = 60;
QString m_defaultShaderId = QStringLiteral("none");
QString m_defaultShaderParams = QStringLiteral("{}");
```

### Step 4: settings.cpp

```cpp
// settings.cpp additions

// Setters (check value changed before emitting):
void Settings::setEnableShaderEffects(bool value)
{
    if (m_enableShaderEffects != value) {
        m_enableShaderEffects = value;
        Q_EMIT enableShaderEffectsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setShaderQuality(int value)
{
    value = qBound(0, value, 2);
    if (m_shaderQuality != value) {
        m_shaderQuality = value;
        Q_EMIT shaderQualityChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setShaderFrameRate(int value)
{
    value = qBound(30, value, 144);
    if (m_shaderFrameRate != value) {
        m_shaderFrameRate = value;
        Q_EMIT shaderFrameRateChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDefaultShaderId(const QString &value)
{
    if (m_defaultShaderId != value) {
        m_defaultShaderId = value;
        Q_EMIT defaultShaderIdChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDefaultShaderParams(const QString &value)
{
    if (m_defaultShaderParams != value) {
        m_defaultShaderParams = value;
        Q_EMIT defaultShaderParamsChanged();
        Q_EMIT settingsChanged();
    }
}

// In load():
void Settings::load()
{
    // ... existing load code ...

    KConfigGroup shaderGroup = config->group(QStringLiteral("Shaders"));
    m_enableShaderEffects = shaderGroup.readEntry("EnableShaderEffects", true);
    m_shaderQuality = shaderGroup.readEntry("ShaderQuality", 1);
    m_shaderFrameRate = shaderGroup.readEntry("ShaderFrameRate", 60);
    m_defaultShaderId = shaderGroup.readEntry("DefaultShaderId",
                                               QStringLiteral("none"));
    m_defaultShaderParams = shaderGroup.readEntry("DefaultShaderParams",
                                                   QStringLiteral("{}"));
}

// In save():
void Settings::save()
{
    // ... existing save code ...

    KConfigGroup shaderGroup = config->group(QStringLiteral("Shaders"));
    shaderGroup.writeEntry("EnableShaderEffects", m_enableShaderEffects);
    shaderGroup.writeEntry("ShaderQuality", m_shaderQuality);
    shaderGroup.writeEntry("ShaderFrameRate", m_shaderFrameRate);
    shaderGroup.writeEntry("DefaultShaderId", m_defaultShaderId);
    shaderGroup.writeEntry("DefaultShaderParams", m_defaultShaderParams);
}

// In reset():
void Settings::reset()
{
    // ... existing reset code ...

    m_enableShaderEffects = true;
    m_shaderQuality = 1;
    m_shaderFrameRate = 60;
    m_defaultShaderId = QStringLiteral("none");
    m_defaultShaderParams = QStringLiteral("{}");
}
```

### Editor Settings (Handled Separately)

Per `.cursorrules`, editor-specific settings go in EditorController, not Settings class:

```cpp
// EditorController.h additions
// NOTE: This controls whether to show the static preview thumbnail in ShaderSelector,
// NOT live shader preview (which is a v2 feature). Default true.
Q_PROPERTY(bool showShaderThumbnail READ showShaderThumbnail
           WRITE setShowShaderThumbnail NOTIFY showShaderThumbnailChanged)

private:
    bool m_showShaderThumbnail = true;  // Show static preview.png in selector

// EditorController.cpp - loadEditorSettings()/saveEditorSettings()
void EditorController::loadEditorSettings()
{
    // ... existing editor settings ...

    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    m_showShaderThumbnail = editorGroup.readEntry("ShowShaderThumbnail", true);
}

void EditorController::saveEditorSettings()
{
    // ... existing editor settings ...

    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    editorGroup.writeEntry("ShowShaderThumbnail", m_showShaderThumbnail);
}
```

### Global Defaults Workflow

```
┌─────────────────────────────────────────┐
│ Shader Effect Section                    │
├─────────────────────────────────────────┤
│ Effect: [▼ Pulsing Glow    ]            │
│ ...parameter controls...                 │
│                                          │
│ [Reset to Defaults] [Save as Default ▼] │
│                     ├─────────────────┤  │
│                     │ Save for this   │  │
│                     │ shader only     │  │
│                     ├─────────────────┤  │
│                     │ Save shader +   │  │
│                     │ params as global│  │
│                     └─────────────────┘  │
└─────────────────────────────────────────┘
```

**Inheritance chain:**
1. Layout-specific params (highest priority)
2. Global default params for this shader
3. Shader's built-in defaults (from metadata.json)

### KCM Integration

Add shader settings to the KDE System Settings module:

```
Appearance section:
├── Zone Colors (existing)
├── ...
└── Shader Effects
    ├── [✓] Enable shader effects
    ├── Quality: [▼ Medium]
    └── Frame rate: [60 ▼] fps
```

---

## Edge Cases and Error Handling

### Zone Data Edge Cases

| Case | Issue | Handling |
|------|-------|----------|
| Zero zones | `zoneCount = 0`, shader loop runs 0 times | Render transparent; shader handles gracefully |
| >32 zones | Exceeds uniform array size | Clamp to 32, log warning, extra zones ignored by shader |
| Zero-size zone | `width=0` or `height=0` causes SDF NaN | Skip zone in buildZonesList(), log warning |
| Negative positions | Zone partially off-screen | Allow - SDF math handles negative coords correctly |
| Overlapping zones | Multiple zones contain same point | `getZoneAtPoint()` returns highest z-order (last in list) |
| Zone ID changes | Zone removed/added during drag | Set `m_zoneDataDirty = true`, rebuild on next frame |

### Timing Edge Cases

| Case | Issue | Handling |
|------|-------|----------|
| iFrame overflow | `int` overflows at ~2 billion | Atomic reset to 0 at 1 billion (~193 days at 60fps) |
| System suspend | iTime jumps forward after resume | Mutex-protected timer restart on resume signal |
| Frame spike | iTimeDelta huge during frame drop | Clamp iTimeDelta to max 100ms via atomic exchange |
| Very long drag | iTime loses precision after hours | Acceptable - float32 has ~7 digits precision (~3.8 hours) |
| Timer/suspend race | Timer fires while restarting | QMutex protects `m_shaderTimer` restart operations |

### Platform Edge Cases

| Case | Issue | Handling |
|------|-------|----------|
| DPI scaling | Coordinates mismatch | Shader receives logical pixels + devicePixelRatio uniform |
| HDR displays | Color values may clip | v1: SDR only; v2: consider HDR-aware shaders |
| Multi-GPU | Shader compiled for wrong GPU | Qt RHI/.qsb handles cross-GPU portability |
| Wayland vs X11 | Different overlay mechanisms | Flags match ZoneOverlay.qml exactly; LayerShellQt handles Wayland |
| Software rendering | No GPU shader support | `ShaderEffect.status === Error` triggers transparent fallback |
| Qt6::ShaderTools missing | Build fails | Optional dependency; compile-time flag disables feature |

### Data Integrity Edge Cases

| Case | Issue | Handling |
|------|-------|----------|
| Invalid shaderParams JSON | Malformed or wrong types | `validateAndCoerceParams()` returns defaults for invalid |
| Missing shader .qsb | File deleted after install | `ShaderRegistry::shader()` returns invalid, fallback to "none" |
| Corrupt metadata.json | Shader metadata unreadable | Skip shader in registry, log error, continue |
| Unknown shader ID | Layout references deleted shader | Fall back to "none" shader on load |
| Param type mismatch | `"glowIntensity": "invalid"` | Coerce to default, log warning |
| D-Bus unavailable | Editor can't query shaders | Show "Shaders unavailable" message, disable controls |

### User Shader Edge Cases

| Case | Issue | Handling |
|------|-------|----------|
| qsb tool not installed | Can't compile user shaders | `userShadersEnabled()` returns false; system shaders still work |
| Malformed .frag syntax | Compilation fails | Store error in ShaderInfo.compilationError; show in UI |
| Missing metadata.json | Shader incomplete | Generate minimal metadata from directory name |
| User edits .frag during use | Shader needs recompile | File watcher triggers refresh; `needsRecompile` flag set |
| Large shader file | Slow compilation | 30-second timeout; show progress indicator |
| GPU hang from infinite loop | System freeze | GPU driver timeout (2-5s); user's responsibility |
| Shader references missing uniform | Compilation error | qsb reports error; shown in editor |
| User shader overwrites system ID | ID collision | User shader takes precedence (loaded after system) |
| Rapid file changes | Excessive recompilation | 500ms debounce on file watcher |
| User deletes shader dir | Layout references gone shader | Fall back to "none" on next overlay show |

### Concurrency Edge Cases

| Case | Issue | Handling |
|------|-------|----------|
| Rapid highlight changes | Texture rebuild race | Set dirty flag; rebuild at most once per frame |
| Layout switch during overlay | Shader changes mid-display | Queue change for next `show()` call |
| Screen added during overlay | New screen needs window | Handle `screenAdded` signal, create window |
| Screen removed during overlay | Window becomes invalid | Handle `screenRemoved` signal, destroy window |
| Resolution change mid-overlay | iResolution stale | QML binding auto-updates from window size |
| Shader error during drag | Window recreation causes flicker | Defer to next `show()`; ShaderEffect shows transparent |

### Shader Error Handling (Deferred Pattern)

```qml
// In ShaderOverlay.qml
ShaderEffect {
    id: shaderEffect
    visible: status !== ShaderEffect.Error  // Hide on error, show transparent

    onStatusChanged: {
        if (status === ShaderEffect.Error) {
            console.error("ShaderOverlay: Shader error:", log)
            // Report to C++ for DEFERRED handling (not immediate window recreation)
            if (typeof overlayService !== "undefined") {
                overlayService.onShaderError(log)
            }
        }
    }
}
```

```cpp
// In OverlayService - DEFERRED error handling
void OverlayService::onShaderError(const QString &errorLog)
{
    qWarning() << "Shader error during overlay:" << errorLog;
    
    // DON'T recreate windows now - we're mid-drag!
    // Just flag for next show() call
    m_shaderErrorPending = true;
    m_pendingShaderError = errorLog;
    
    // ShaderEffect.visible = false shows transparent, which is acceptable
    // Full fallback happens on next show() via useShaderOverlay() check
}

void OverlayService::show()
{
    // Check for deferred shader error
    if (m_shaderErrorPending) {
        qWarning() << "Falling back to standard overlay due to previous shader error";
        // useShaderOverlay() returns false when m_shaderErrorPending is true
    }
    
    // ... rest of show() logic ...
}
```

### DPI/Device Pixel Ratio Handling

```qml
// In ShaderOverlay.qml - use LOGICAL pixels, let shader handle DPI
ShaderEffect {
    // Pass logical resolution + DPI ratio to shader
    property size iResolution: Qt.size(width, height)  // Logical pixels
    property real iDevicePixelRatio: Screen.devicePixelRatio
    
    // Shader code converts when needed:
    // vec2 physicalCoord = fragCoord * iDevicePixelRatio;
    // float borderWidthPhysical = borderWidth * iDevicePixelRatio;
}
```

**Rationale**: Keeping coordinates in logical pixels matches QML's coordinate system. The shader converts to physical pixels only when needed (e.g., for consistent border widths across DPI settings).

---

## Implementation Phases

### Phase 1: Foundation
- [ ] Add optional Qt6::ShaderTools find in CMakeLists.txt
- [ ] Add `PLASMAZONES_SHADERS_ENABLED` compile definition
- [ ] Extend Layout class with `shaderId`, `shaderParams` properties
- [ ] Update Layout JSON serialization (with migration for existing layouts)
- [ ] Add shader settings to kcfg, ISettings, Settings.h, Settings.cpp

### Phase 2: ShaderCompiler
- [ ] Create `ShaderCompiler` class with qsb subprocess wrapper
- [ ] Implement `qsbToolPath()` detection across distros
- [ ] Implement `wrapWithBoilerplate()` with zone helper functions
- [ ] Add 30-second timeout for compilation
- [ ] Handle compilation errors with user-friendly messages

### Phase 3: ShaderRegistry & D-Bus
- [ ] Create `ShaderRegistry` class in daemon (singleton)
- [ ] Load system shaders (pre-compiled .qsb from qrc)
- [ ] Load user shaders (runtime compile .frag to .qsb)
- [ ] Add QFileSystemWatcher for user shader directory
- [ ] Add debounced refresh on file changes (500ms)
- [ ] Add D-Bus methods: `AvailableShaders()`, `ShaderInfo()`, `UserShaderDirectory()`
- [ ] Add `OpenUserShaderDirectory()` D-Bus method
- [ ] Add `RecompileShader()` D-Bus method

### Phase 4: Zone Data for Uniform Arrays
- [ ] Implement `buildZonesList()` in OverlayService
- [ ] Add `updateZonesForAllWindows()` for zone data updates
- [ ] Add `m_zoneDataDirty` flag for incremental updates
- [ ] Thread-safe iTime updates with atomics/mutex
- [ ] Handle system suspend/resume for timer

### Phase 5: ShaderOverlay.qml
- [ ] Create `ShaderOverlay.qml` with correct Wayland flags
- [ ] Create `ZoneLabel.qml` for text overlay
- [ ] Implement texture upload via Canvas
- [ ] Add shader error reporting to C++
- [ ] Register new QML files in CMakeLists.txt

### Phase 6: Bundled Shaders
- [ ] Create shader boilerplate with zone helpers
- [ ] Implement glow shader with customParams1/customColor1
- [ ] Implement neon, glass, gradient, pulse shaders
- [ ] Create 128x80 preview.png for each shader
- [ ] Add `qt_add_shaders()` with BATCHABLE to CMake

### Phase 7: Editor Integration
- [ ] Add `refreshAvailableShaders()` D-Bus query in EditorController
- [ ] Create `ShaderSelector.qml` with static preview
- [ ] Create `ShaderParameterEditor.qml` with dynamic controls
- [ ] Add "Open Shader Folder" button in PropertyPanel
- [ ] Add compilation error display in PropertyPanel
- [ ] Add "Recompile" button for user shaders
- [ ] Implement undo commands: `UpdateLayoutShaderCommand`, `UpdateShaderParamCommand`

### Phase 8: User Shader Experience
- [ ] Create user shader directory on first use
- [ ] Add example shader template to docs/examples/
- [ ] Show user vs system shader indicator in UI
- [ ] Show "needs recompile" indicator when source changed
- [ ] Add shader compilation progress feedback

### Phase 9: Polish & Testing
- [ ] Test shader fallback when Qt6::ShaderTools missing
- [ ] Test user shader compilation workflow
- [ ] Test file watcher hot-reload
- [ ] Test deferred error handling (no window thrashing)
- [ ] Test on Wayland and X11
- [ ] Test multi-monitor iTime synchronization
- [ ] Test with 32 zones (max)
- [ ] Performance profiling on integrated graphics
- [ ] Hide shader UI when `shadersEnabled() == false`
- [ ] Show "qsb not found" warning when user shaders disabled

### Future (v2)
- [ ] Live shader preview in editor (daemon WebSocket?)
- [ ] Import from Shadertoy URL
- [ ] Shader sharing/community library with signing
- [ ] Shader complexity validation (loop bounds, texture fetches)
- [ ] HDR-aware shader rendering

---

## File Changes Summary

### New Files

All new files MUST include SPDX headers:
```cpp
// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
```

```
src/
├── core/
│   ├── shaderregistry.h/.cpp       # Shader metadata loading, D-Bus exposure
│   └── shadercompiler.h/.cpp       # Runtime qsb compilation for user shaders
├── ui/
│   ├── ShaderOverlay.qml           # Full-screen shader overlay window
│   └── ZoneLabel.qml               # Zone number/name text (used by shader overlay)
├── editor/
│   ├── qml/
│   │   ├── ShaderSelector.qml      # Dropdown + static thumbnail preview
│   │   └── ShaderParameterEditor.qml  # Dynamic parameter controls
│   └── undo/commands/
│       ├── UpdateLayoutShaderCommand.h/.cpp
│       └── UpdateShaderParamCommand.h/.cpp
shaders/                             # System shaders (pre-compiled)
├── glow/
│   ├── metadata.json
│   ├── effect.frag                 # Source (includes helpers inline)
│   ├── effect.frag.qsb             # Pre-compiled at package build
│   └── preview.png                 # 128x80 thumbnail
├── neon/
│   └── ...
├── glass/
│   └── ...
├── gradient/
│   └── ...
└── pulse/
    └── ...

docs/examples/                       # User shader template
└── custom-shader-template/
    ├── metadata.json               # Template metadata
    ├── effect.frag                 # Template shader code
    └── README.md                   # Instructions for users
```

### Modified Files

```
src/core/layout.h/.cpp            - Add shaderId, shaderParams properties
src/core/interfaces.h             - Add shader signals to ISettings
src/daemon/overlayservice.h/.cpp  - Shader overlay, zone data, animation timer
src/dbus/settingsadaptor.h/.cpp   - Add shader registry D-Bus methods
src/config/settings.h/.cpp        - Add shader settings with full workflow
src/config/plasmazones.kcfg       - Add Shaders group with 5 entries
src/editor/EditorController.h/.cpp - Query shaders via D-Bus, not local registry
src/editor/qml/PropertyPanel.qml  - Add shader section
CMakeLists.txt                    - Optional Qt6::ShaderTools check
src/CMakeLists.txt                - Conditional shader compilation
```

### CMakeLists.txt Changes (CRITICAL)

**Root CMakeLists.txt:**
```cmake
# Core Qt6 components (ShaderTools is OPTIONAL)
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Quick
    QuickControls2
    Gui
    DBus
    Widgets
)

# OPTIONAL: Shader support (graceful degradation if missing)
find_package(Qt6 COMPONENTS ShaderTools QUIET)
if(Qt6ShaderTools_FOUND)
    message(STATUS "Qt6::ShaderTools found - shader effects ENABLED")
    set(PLASMAZONES_SHADERS_ENABLED ON)
    add_compile_definitions(PLASMAZONES_SHADERS_ENABLED)
else()
    message(WARNING "Qt6::ShaderTools not found - shader effects DISABLED")
    message(WARNING "Install qt6-shadertools (Arch), qt6-shader-baker (Fedora), or libqt6shadertools6 (Debian/Ubuntu)")
    set(PLASMAZONES_SHADERS_ENABLED OFF)
endif()
```

**src/CMakeLists.txt - Core Library:**
```cmake
# Add new source files to plasmazones_core_SRCS
set(plasmazones_core_SRCS
    # ... existing files ...
    core/shaderregistry.cpp
    core/shaderregistry.h
    core/shadercompiler.cpp
    core/shadercompiler.h
)
```

**src/CMakeLists.txt - Daemon with Conditional Shaders:**
```cmake
# Daemon QML module with overlay files
qt_add_qml_module(plasmazonesd
    URI org.plasmazones.overlay
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        ui/ZoneOverlay.qml
        ui/ZoneItem.qml
        ui/ShaderOverlay.qml     # Always include (degrades gracefully)
        ui/ZoneLabel.qml
    NO_RESOURCE_TARGET_PATH
)

# Conditional shader compilation
if(PLASMAZONES_SHADERS_ENABLED)
    # Compile bundled shaders with BATCHABLE for ShaderEffect
    qt_add_shaders(plasmazonesd "overlay_shaders"
        BATCHABLE
        PREFIX "/qt/qml/org/plasmazones/shaders"
        FILES
            ${CMAKE_SOURCE_DIR}/shaders/glow/effect.frag
            ${CMAKE_SOURCE_DIR}/shaders/neon/effect.frag
            ${CMAKE_SOURCE_DIR}/shaders/glass/effect.frag
            ${CMAKE_SOURCE_DIR}/shaders/gradient/effect.frag
            ${CMAKE_SOURCE_DIR}/shaders/pulse/effect.frag
    )
    
    target_link_libraries(plasmazonesd PRIVATE Qt6::ShaderTools)
endif()
```

**src/CMakeLists.txt - Editor QML Module:**

Per `.cursorrules` lines 545-559, ALL new QML files MUST be registered:

```cmake
qt_add_qml_module(plasmazones-editor
    URI org.plasmazones.editor
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        # ... existing files ...
        editor/qml/ShaderSelector.qml
        editor/qml/ShaderParameterEditor.qml
)
```

### Resource Path Consistency

All QML and shader resources use consistent paths:

| Resource Type | Path Pattern |
|---------------|--------------|
| Overlay QML | `qrc:/qt/qml/org/plasmazones/overlay/ShaderOverlay.qml` |
| Editor QML | `qrc:/qt/qml/org/plasmazones/editor/ShaderSelector.qml` |
| Compiled shaders | `qrc:/qt/qml/org/plasmazones/shaders/glow/effect.frag.qsb` |

**C++ code must use these full paths:**
```cpp
// Correct
QUrl shaderUrl = QUrl(QStringLiteral("qrc:/qt/qml/org/plasmazones/shaders/glow/effect.frag.qsb"));

// Incorrect - old pattern
QUrl shaderUrl = QUrl(QStringLiteral("qrc:/shaders/glow/effect.frag.qsb"));
```

---

## Design Decisions

1. **Hot-reload during drag?**
   - **Decision: No** - Shader changes do NOT hot-reload during active window drag
   - Rationale: Stability during drag operation, shader switch on next overlay show

2. **Multi-monitor consistency**
   - **Decision: Yes** - All monitors share the same `iTime` base
   - Rationale: Synchronized effects across screens look intentional, not buggy
   - Implementation: Single `QElapsedTimer` in `OverlayService`, atomic counters, mutex for restart

3. **Single ShaderRegistry in daemon**
   - **Decision: Editor uses D-Bus** - No duplicate registry in editor process
   - Rationale: Single source of truth, matches existing architecture pattern
   - Implementation: `SettingsAdaptor` exposes `AvailableShaders()`, `ShaderInfo()` D-Bus methods
   - Editor calls D-Bus to get shader list, no local file parsing

4. **Zone data via uniform arrays, not texture**
   - **Decision: Uniform arrays** - Zone params as 4 vec4[32] uniform arrays
   - Rationale: QML Canvas can't handle RGBA32F textures (only 8-bit channels); uniform arrays are simpler
   - Implementation: QVariantList built in C++, QML `.map()` transforms to uniform arrays
   - Trade-off: 2KB per frame acceptable for overlay that only runs during drag
   - Zone data rebuilt only when zones/highlights change, not every frame

5. **Editor preview approach**
   - **Decision: Static thumbnails only** - No live shader preview in editor (v1)
   - Rationale: Editor runs as separate process; would need full shader pipeline duplication
   - Implementation: Static `preview.png` images in shader directories
   - Future v2: Consider WebSocket to daemon for live preview frames

6. **Parameter persistence**
   - **Decision: Both** - Layout JSON stores per-layout params, Settings stores global defaults
   - Per-layout: `layout.shaderParams` in each layout's JSON file
   - Global defaults: New setting `defaultShaderParams` in KConfig
   - When creating new layout or resetting: inherit from global defaults
   - "Save as Default" button updates global defaults via D-Bus

7. **Shader error handling**
   - **Decision: Deferred handling** - Errors don't recreate windows mid-drag
   - During overlay: ShaderEffect shows nothing on error (transparent)
   - Error logged and flagged; on next `show()` call, falls back to ZoneOverlay.qml
   - Editor shows error when user selects shader via D-Bus validation
   - Rationale: Prevents window thrashing during active window drag operations

8. **Optional Qt6::ShaderTools dependency**
   - **Decision: Graceful degradation** - App builds and runs without shader support
   - Compile-time: `PLASMAZONES_SHADERS_ENABLED` definition
   - Runtime: `ShaderRegistry::shadersEnabled()` returns false
   - Settings UI: Shader options hidden when disabled
   - Rationale: Not all distros package Qt6::ShaderTools; don't break builds

9. **Screen refresh rate handling**
   - **Decision: Use settings value, bounded** - `shaderFrameRate` setting (default 60)
   - Timer interval = `1000 / qBound(30, shaderFrameRate, 144)`
   - Uses `Qt::PreciseTimer` for accurate timing
   - If display is slower than setting, Qt VSync prevents over-rendering

10. **Wayland/X11 compatibility**
    - **Decision: Match existing ZoneOverlay flags exactly**
    - Wayland: `Qt.WindowDoesNotAcceptFocus` for layer shell overlay
    - X11: `Qt.WindowStaysOnTopHint` for above-all rendering
    - Detection: `Qt.platform.pluginName === "wayland"`
    - LayerShellQt integration handled by existing infrastructure

11. **Shader format versioning**
    - **Decision: Version uniform for future compatibility**
    - All shaders receive `uniform int shaderFormatVersion = 1`
    - Shaders can check version and degrade if newer than expected
    - Breaking changes increment version; old shaders show warning

12. **User custom shaders in v1**
    - **Decision: Enabled** - Users can create custom shaders
    - User places `.frag` files in `~/.local/share/plasmazones/shaders/`
    - Runtime compilation via `qsb` subprocess (boilerplate auto-prepended)
    - File watcher for hot-reload during development
    - Compilation errors shown in editor UI with "Recompile" button
    - **Trust model**: Same as any user script; user explicitly installs shaders
    - **Security**: GPU-only execution, no filesystem/network access from shader

13. **qsb tool availability**
    - **Decision: Graceful degradation for user shaders**
    - `userShadersEnabled()` returns false if qsb not in PATH
    - System shaders (pre-compiled .qsb in package) still work
    - UI shows "Install qt6-shadertools to create custom shaders" hint
    - "Open Shader Folder" button visible regardless (for copying shaders)

---

## References

- [Qt 6 ShaderEffect Documentation](https://doc.qt.io/qt-6/qml-qtquick-shadereffect.html)
- [Qt Shader Tools](https://doc.qt.io/qt-6/qtshadertools-index.html)
- [Shadertoy](https://www.shadertoy.com/)
- [GLSL Signed Distance Functions](https://iquilezles.org/articles/distfunctions2d/)
- [Qt RHI Overview](https://doc.qt.io/qt-6/qrhi.html)

---

---

## Critical Fixes from v1.2-1.5 Reviews

This section documents the major architectural fixes applied in v1.3-1.5:

### 1. Qt6 ShaderEffect UBO Limitation (v1.3)

**Problem**: Original design used `layout(std140) uniform buf {}` block which Qt6 ShaderEffect does NOT support.

**Fix**: Changed to individual uniforms with zone data as uniform arrays.

### 2. Dual ShaderRegistry Architecture (v1.3)

**Problem**: Both daemon and editor had separate `ShaderRegistry` instances.

**Fix**: Single `ShaderRegistry` in daemon, exposed via D-Bus:
- `SettingsAdaptor` exposes `AvailableShaders()`, `ShaderInfo()`, `DefaultShaderParams()`, `RefreshShaders()`, etc.
- Editor queries D-Bus, caches results locally
- No shader file parsing in editor process

### 3. Shader Error Window Thrashing (v1.3)

**Problem**: Shader errors triggered immediate window recreation mid-drag.

**Fix**: Deferred error handling:
- Set `m_shaderErrorPending = true` on error
- ShaderEffect shows transparent (visible = false on error)
- Fallback to ZoneOverlay.qml happens on next `show()` call

### 4. QML Canvas RGBA32F Limitation (v1.5)

**Problem**: Original v1.3 design used QImage::Format_RGBA32FPx4 texture uploaded via Canvas, but QML Canvas `getContext("2d")` only supports 8-bit channels, losing floating-point precision.

**Fix**: Changed to uniform arrays instead of texture:
- Zone data passed as 4 `vec4` uniform arrays (zoneRects, zoneFillColors, zoneBorderColors, zoneParams)
- QML `zones` property is QVariantList, mapped to uniform arrays via `.map()`
- Removed Canvas intermediate step entirely
- 2KB uniform data acceptable for 32 zones during drag operation

### 4. Optional Qt6::ShaderTools Dependency

**Problem**: Build would fail if Qt6::ShaderTools not installed.

**Fix**: Graceful degradation:
- `find_package(Qt6 COMPONENTS ShaderTools QUIET)` (optional)
- Compile-time `PLASMAZONES_SHADERS_ENABLED` flag
- `ShaderRegistry::shadersEnabled()` returns false if missing
- Settings UI hides shader options when disabled

### 5. Wayland Overlay Compatibility

**Problem**: ShaderOverlay.qml had incorrect window flags for Wayland.

**Fix**: Copy exact flag pattern from ZoneOverlay.qml:
```qml
flags: Qt.FramelessWindowHint |
       (Qt.platform.pluginName === "wayland" ? Qt.WindowDoesNotAcceptFocus : Qt.WindowStaysOnTopHint)
```

### 6. Resource Path Consistency

**Problem**: Mixed `qrc:/shaders/` and `qrc:/ui/` paths didn't match `qt_add_qml_module` output.

**Fix**: Consistent `qrc:/qt/qml/org/plasmazones/...` paths:
- Shaders: `qrc:/qt/qml/org/plasmazones/shaders/glow/effect.frag.qsb`
- Overlay: `qrc:/qt/qml/org/plasmazones/overlay/ShaderOverlay.qml`

### 7. Thread Safety for iTime

**Problem**: `updateShaderUniforms()` could race with `handleSystemResumed()`.

**Fix**: Atomic counters + mutex:
- `std::atomic<qint64> m_lastFrameTime`
- `std::atomic<int> m_frameCount`
- `QMutex m_shaderTimerMutex` protects timer restart

### 8. CMake BATCHABLE Keyword

**Problem**: `qt_add_shaders()` called without `BATCHABLE` keyword.

**Fix**: Added `BATCHABLE` for ShaderEffect compatibility:
```cmake
qt_add_shaders(plasmazonesd "overlay_shaders"
    BATCHABLE  # Required for ShaderEffect!
    ...
)
```

---

## Revision History

| Date | Version | Changes |
|------|---------|---------|
| 2026-01-XX | 1.0 | Initial design document |
| 2026-01-XX | 1.1 | Added design decisions based on review |
| 2026-01-XX | 1.2 | Compliance updates per .cursorrules review:<br>- Qt6 string literal compliance (QStringLiteral, QLatin1String)<br>- Complete settings workflow (kcfg → ISettings → Settings.h → Settings.cpp)<br>- Namespace wrapping for all C++ code<br>- Memory management clarification (std::unique_ptr)<br>- QML registration requirements in CMakeLists.txt<br>- SPDX header requirements<br>- Editor settings separation (EditorController)<br>- Comprehensive edge cases section<br>- Parameter validation implementation<br>- DPI/devicePixelRatio handling<br>- iTime/iTimeDelta clamping for system suspend<br>- iFrame overflow handling<br>- Preview thumbnail requirements<br>- Shader animation pause during zone editing |
| 2026-01-XX | 1.3 | Major architectural fixes from technical review:<br>- **CRITICAL**: Replaced UBO block with individual uniforms (Qt6 ShaderEffect limitation)<br>- Planned texture-based zone data (32x4 image) - superseded in v1.5<br>- Single ShaderRegistry in daemon, D-Bus exposed to editor<br>- Deferred shader error handling (no window recreation mid-drag)<br>- Optional Qt6::ShaderTools dependency with compile-time flag<br>- Wayland overlay flags matching ZoneOverlay.qml exactly<br>- Consistent qrc:/ resource paths with qt_add_qml_module<br>- Thread-safe iTime with atomics and mutex<br>- CMake BATCHABLE keyword for qt_add_shaders()<br>- Removed live editor preview (v2 feature), static thumbnails only<br>- Added shader format versioning uniform |
| 2026-01-XX | 1.4 | Added user custom shaders to v1:<br>- ShaderCompiler class for runtime qsb compilation<br>- User shader directory with file watcher for hot-reload<br>- Shader boilerplate auto-prepended (zone helpers, uniforms)<br>- Compilation error display in editor with Recompile button<br>- Trust model: same as user scripts (user explicitly installs)<br>- "Open Shader Folder" and example template<br>- Graceful degradation when qsb tool not available |
| 2026-01-XX | 1.5 | Conflict resolution and consistency fixes:<br>- **CRITICAL**: Replaced texture-based zone data with uniform arrays (QML Canvas cannot handle RGBA32F)<br>- Removed all #include from shader examples (inlined helpers)<br>- Fixed ShaderRegistry access pattern (use instance() consistently)<br>- Added complete D-Bus methods: UserShadersEnabled, OpenUserShaderDirectory, RefreshShaders, RecompileShader<br>- Added canonical JSON Schema for metadata.json<br>- Standardized maps_to format (underscore notation: customParams1_x)<br>- Added overlayService context property documentation<br>- Replaced buildZonesList() for uniform array data flow<br>- Renamed previewShaderEnabled to showShaderThumbnail (clarifies static vs live)<br>- Added Qt 6.2+ requirement with fallback documentation<br>- User shader precedence clarification (loaded after system, overwrites same ID) |
