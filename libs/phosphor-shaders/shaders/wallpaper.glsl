// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop wallpaper texture binding (slot 11).
// Include from effect.frag or pass*.frag with:
//   #include <wallpaper.glsl>
//
// Opt-in: set "wallpaper": true in your shader's metadata.json.
// The pipeline will automatically capture and supply the desktop
// wallpaper image as uWallpaper.
//
// Use textureSize(uWallpaper, 0) to get the wallpaper resolution.
// The texture is 1x1 transparent when no wallpaper is available.

#ifndef PHOSPHORSHADERS_WALLPAPER_GLSL
#define PHOSPHORSHADERS_WALLPAPER_GLSL

layout(binding = 11) uniform sampler2D uWallpaper;

// Compute aspect-correct UV for wallpaper sampling.
// Centers the wallpaper to fill the given area, cropping overflow.
vec2 wallpaperUv(vec2 fragCoord, vec2 screenResolution) {
    vec2 wpSize = vec2(textureSize(uWallpaper, 0));
    vec2 uv = fragCoord / max(screenResolution, vec2(1.0));

    float wpAspect = wpSize.x / max(wpSize.y, 1.0);
    float scrAspect = screenResolution.x / max(screenResolution.y, 1.0);

    if (wpAspect > scrAspect) {
        float scale = scrAspect / wpAspect;
        uv.x = (uv.x - 0.5) * scale + 0.5;
    } else {
        float scale = wpAspect / scrAspect;
        uv.y = (uv.y - 0.5) * scale + 0.5;
    }
    return uv;
}

#endif // PHOSPHORSHADERS_WALLPAPER_GLSL
