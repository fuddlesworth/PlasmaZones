# PlasmaZones Shader Guide

Create custom GPU-accelerated effects for zone overlays using GLSL shaders.

## Quick Start

1. Create a folder in `~/.local/share/plasmazones/shaders/my-effect/`
2. Add three files:
   - `metadata.json` - shader configuration
   - `effect.glsl` - fragment shader
   - `zone.vert.glsl` - vertex shader (can copy from existing)
3. Restart the daemon or it will auto-reload

## Shader Structure

### metadata.json

```json
{
    "id": "my-effect",
    "name": "My Effect",
    "description": "A custom zone overlay effect",
    "author": "Your Name",
    "version": "1.0",
    "renderType": "rendernode",
    "fragmentShader": "effect.glsl",
    "vertexShader": "zone.vert.glsl",
    "parameters": [
        {
            "id": "intensity",
            "name": "Effect Intensity",
            "group": "Appearance",
            "type": "float",
            "slot": 0,
            "default": 1.0,
            "min": 0.0,
            "max": 2.0
        },
        {
            "id": "mainColor",
            "name": "Main Color",
            "group": "Colors",
            "type": "color",
            "slot": 0,
            "default": "#ff6600"
        }
    ]
}
```

### Parameter Groups

For shaders with many parameters, you can organize them into collapsible groups using the optional `group` field:

```json
{
    "id": "glowSize",
    "name": "Glow Size",
    "group": "Glow Effects",
    "type": "float",
    "slot": 2,
    "default": 35.0,
    "min": 10.0,
    "max": 60.0
}
```

Parameters with the same `group` value will appear together in a collapsible section in the editor. The first group is expanded by default; others are collapsed to reduce clutter. Parameters without a `group` field are shown in a flat list (backward compatible).

### Parameter Slots

| Type | Slot Range | Uniform Mapping |
|------|------------|-----------------|
| `float` | 0-15 | `customParams[0].xyzw` (0-3), `customParams[1].xyzw` (4-7), `customParams[2].xyzw` (8-11), `customParams[3].xyzw` (12-15) |
| `color` | 0-3 | `customColors[0]` (0), `customColors[1]` (1), `customColors[2]` (2), `customColors[3]` (3) |
| `int` | 0-15 | Same as float |
| `bool` | 0-15 | Same as float (0.0 or 1.0) |

You have 16 float slots and 4 color slots. Slots are mapped to GLSL uniforms automatically.

## Fragment Shader Template

```glsl
// SPDX-License-Identifier: GPL-3.0-or-later
#version 330 core

in vec2 vTexCoord;
out vec4 fragColor;

// Required uniform block - DO NOT MODIFY STRUCTURE
layout(std140) uniform ZoneUniforms {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;           // Animation time in seconds
    float iTimeDelta;      // Time since last frame
    int iFrame;            // Frame counter
    vec2 iResolution;      // Window size in pixels
    int zoneCount;         // Number of zones
    int highlightedCount;  // Number of highlighted zones
    vec4 iMouse;        // xy = pixels, zw = normalized (0-1)
    vec4 customParams[4];  // [0-3], access as customParams[0].x for slot 0, etc.
    vec4 customColors[4];  // [0-3], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];       // Zone geometry: xy = position, zw = size (normalized 0-1)
    vec4 zoneFillColors[64];  // Fill colors (rgba)
    vec4 zoneBorderColors[64];// Border colors (rgba)
    vec4 zoneParams[64];      // x = borderRadius, y = borderWidth, z = isHighlighted (>0.5)
};

// Signed distance to rounded rectangle
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

vec4 renderZone(vec2 fragCoord, vec4 rect, vec4 fillColor, vec4 borderColor, vec4 params, bool isHighlighted) {
    float borderRadius = params.x;
    float borderWidth = params.y;

    // Convert normalized coords to pixels
    vec2 rectPos = rect.xy * iResolution;
    vec2 rectSize = rect.zw * iResolution;
    vec2 center = rectPos + rectSize * 0.5;
    vec2 p = fragCoord - center;

    // Signed distance to zone boundary
    float d = sdRoundedBox(p, rectSize * 0.5, borderRadius);

    vec4 result = vec4(0.0);

    // Fill (inside zone: d < 0)
    if (d < 0.0) {
        result = fillColor;
        if (isHighlighted) {
            // Make highlighted zones brighter
            result.rgb *= 1.3;
        }
    }

    // Border
    if (abs(d) < borderWidth) {
        float border = 1.0 - smoothstep(0.0, borderWidth, abs(d));
        result = mix(result, borderColor, border);
    }

    return result;
}

void main() {
    // Convert texture coords to pixel coords (flip Y)
    vec2 fragCoord = vec2(vTexCoord.x, 1.0 - vTexCoord.y) * iResolution;
    vec4 color = vec4(0.0);

    if (zoneCount == 0) {
        fragColor = vec4(0.0);
        return;
    }

    // Render each zone
    for (int i = 0; i < zoneCount && i < 64; i++) {
        vec4 rect = zoneRects[i];
        if (rect.z <= 0.0 || rect.w <= 0.0) continue;  // Skip zero-size zones

        bool isHighlighted = zoneParams[i].z > 0.5;
        vec4 zoneColor = renderZone(fragCoord, rect, zoneFillColors[i],
            zoneBorderColors[i], zoneParams[i], isHighlighted);

        // Alpha compositing
        float srcA = zoneColor.a;
        float dstA = color.a;
        float outA = srcA + dstA * (1.0 - srcA);
        if (outA > 0.0) {
            color.rgb = (zoneColor.rgb * srcA + color.rgb * dstA * (1.0 - srcA)) / outA;
        }
        color.a = outA;
    }

    fragColor = vec4(clamp(color.rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0) * qt_Opacity);
}
```

## Vertex Shader Template

Copy this as `zone.vert.glsl` - usually no modifications needed:

```glsl
// SPDX-License-Identifier: GPL-3.0-or-later
#version 330 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

out vec2 vTexCoord;

layout(std140) uniform ZoneUniforms {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    float iTimeDelta;
    int iFrame;
    vec2 iResolution;
    int zoneCount;
    int highlightedCount;
    vec4 iMouse;        // xy = pixels, zw = normalized (0-1)
    vec4 customParams[4];  // [0-3], access as customParams[0].x for slot 0, etc.
    vec4 customColors[4];  // [0-3], access as customColors[0] for color slot 0, etc.
    vec4 zoneRects[64];
    vec4 zoneFillColors[64];
    vec4 zoneBorderColors[64];
    vec4 zoneParams[64];
};

void main() {
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
```

## Using Your Parameters

Access your custom parameters in the fragment shader:

```glsl
// From metadata: "slot": 0 (maps to customParams[0].x)
float intensity = customParams[0].x;

// From metadata: "slot": 5 (maps to customParams[1].y)
float myParam5 = customParams[1].y;  // slot 5 = [5/4].[5%4] = [1].y

// From metadata: "slot": 0 (color, maps to customColors[0])
vec3 mainColor = customColors[0].rgb;
float colorAlpha = customColors[0].a;

// Animation using iTime
float pulse = sin(iTime * 2.0) * 0.5 + 0.5;
```

## Tips

**Animation:** Use `iTime` for smooth animations. Multiply for speed: `iTime * 2.0` is twice as fast.

**Mouse interaction:** `iMouse.xy` gives pixel coordinates, `iMouse.zw` gives normalized (0-1) coordinates.

**Highlighted zones:** Check `zoneParams[i].z > 0.5` to know if a zone is highlighted (mouse hovering).

**Performance:** Keep shader complexity reasonable. Avoid excessive loops or texture lookups.

**Debugging:** Check the journal for shader errors: `journalctl --user -u plasmazones -f`

**Hot reload:** Save your shader and the daemon will automatically reload it within a few seconds.

## Example: Pulsing Glow

Add a glowing pulse effect to highlighted zones:

```glsl
vec4 renderZone(...) {
    // ... standard zone rendering ...

    if (isHighlighted && d > 0.0 && d < 30.0) {
        // Pulsing glow outside the zone
        float pulse = sin(iTime * 3.0) * 0.3 + 0.7;
        float glow = exp(-d / 10.0) * pulse;
        result.rgb += customColors[0].rgb * glow;
        result.a = max(result.a, glow * 0.5);
    }

    return result;
}
```

## Built-in Shaders

Study the built-in shaders in `/usr/share/plasmazones/shaders/` (or your install prefix) for more examples:

| Shader | Techniques |
|--------|-----------|
| neon-glow | Bloom, glow, flickering |
| glass-morphism | Blur simulation, refraction |
| minimalist | Soft shadows, gradients, bevels |
| holographic | Iridescence, rainbow effects |
| gradient-mesh | Animated color gradients |
| liquid-warp | Distortion, fluid simulation |
| magnetic-field | Field lines, particles |
| particle-field | Noise-based particles |

## Troubleshooting

**Shader doesn't appear in editor:** Check `metadata.json` is valid JSON. Run `jq . metadata.json` to validate.

**Black screen or no effect:** Check journal for GLSL compilation errors. Common issues:
- Missing semicolons
- Type mismatches (int vs float)
- Accessing array out of bounds

**Effect looks wrong:** Remember Y is flipped in texture coordinates. Use `1.0 - vTexCoord.y` when converting to screen space.

**Changes not appearing:** The daemon watches for file changes but may take a few seconds. Try restarting: `systemctl --user restart plasmazones`
