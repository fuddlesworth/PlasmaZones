// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audiomock.h"
#include "encoder.h"
#include "layoutloader.h"
#include "metadataloader.h"

#include <QSize>
#include <QString>
#include <QVector>

namespace PlasmaZones::ShaderRender {

struct RenderOptions
{
    ShaderMetadata metadata; ///< Parsed metadata; fragment/buffer paths absolute.
    /// Absolute path of the pack's metadata.json. The renderer derives the
    /// pack directory from THIS (not from metadata.fragmentShader, which a
    /// pack may point into a subdirectory) when it re-parses the pack via
    /// ShaderRegistry::parsePackMetadata for the p_<id> param preamble.
    QString metadataPath;
    QVector<Zone> zones; ///< Normalized 0-1 rects + colors (loadLayoutZones).
    QSize resolution; ///< Render target size (passed to the shader as iResolution).
    int frameCount = 150;
    int fps = 30;
    AudioMock* audio = nullptr; ///< Per-frame spectrum source. Borrowed.
    FrameSink* sink = nullptr; ///< Per-frame output destination. The sink owns the
                               ///< output-size resize (PNG and ffmpeg sinks both
                               ///< honour --output-size); the renderer hands it
                               ///< unscaled frames at @c resolution. Borrowed.

    /// Pin one zone as the only highlighted zone for every frame. 0 (the
    /// default) keeps the cycling demo schedule (slice 0 = all highlighted,
    /// slices 1..N = one zone at a time). Any value in [1, zones.size()]
    /// disables the cycle and renders every frame with @c stillHighlightZone
    /// as the sole highlighted zone — useful when the renderer is driving
    /// thumbnail capture and the caller wants a deterministic hero shot.
    int stillHighlightZone = 0;
};

/**
 * @brief Headless offscreen renderer for PlasmaZones shaders.
 *
 * Boots a Qt Quick scene under QQuickRenderControl with a single
 * ShaderEffect filling the surface, configures it from the loaded
 * metadata + layout, and clocks N frames out of it.
 *
 * Each frame: advance iTime / iFrame, push a fresh audio spectrum
 * if the shader cares, render, grab the framebuffer, hand to the
 * FrameSink.
 */
class Renderer
{
public:
    Renderer();
    ~Renderer();

    /**
     * @return 0 on success, non-zero on failure. Diagnostics go to
     *         stderr; the sink already writes whatever it produced.
     */
    int render(const RenderOptions& opts);
};

} // namespace PlasmaZones::ShaderRender
