// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "renderer.h"

#include "zoneshadercommon.h"
#include "zoneuniformextension.h"

#include <PhosphorRendering/ShaderEffect.h>

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QRhi>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QUrl>

#include <iostream>
#include <memory>

namespace PlasmaZones::ShaderRender {
namespace {

// Mirror what ZoneShaderItem does with its zoneuniformextension —
// we don't subclass ShaderEffect here, just attach a freshly-built
// extension so the UBO has zone data.  Keeping the daemon's exact
// ZoneUniformExtension means GLSL written against the runtime UBO
// (zoneRects[64], zoneFillColors[64], etc.) rasterizes with the
// same byte layout.
QVector<PlasmaZones::ZoneData> toRuntimeZones(const QVector<Zone>& srcZones)
{
    QVector<PlasmaZones::ZoneData> out;
    out.reserve(srcZones.size());
    for (const auto& z : srcZones) {
        PlasmaZones::ZoneData d{};
        d.rect = z.rect;
        d.fillColor = z.fillColor;
        d.borderColor = z.borderColor;
        d.borderRadius = static_cast<float>(z.borderRadius);
        d.borderWidth = static_cast<float>(z.borderWidth);
        d.isHighlighted = z.isHighlighted;
        d.zoneNumber = z.zoneNumber;
        out.append(d);
    }
    return out;
}

void seedShaderEffect(PhosphorRendering::ShaderEffect& effect,
                      const ShaderMetadata& md,
                      const QSize& resolution)
{
    effect.setIResolution(QSizeF(resolution));
    effect.setShaderSource(QUrl::fromLocalFile(md.fragmentShader));

    if (md.multipass) {
        if (!md.bufferShaders.isEmpty()) {
            effect.setBufferShaderPaths(md.bufferShaders);
        } else if (!md.bufferShader.isEmpty()) {
            effect.setBufferShaderPath(md.bufferShader);
        }
        effect.setBufferFeedback(md.bufferFeedback);
        effect.setBufferScale(md.bufferScale);
        effect.setBufferWrap(md.bufferWrap);
        if (!md.bufferWraps.isEmpty()) effect.setBufferWraps(md.bufferWraps);
        effect.setBufferFilter(md.bufferFilter);
        if (!md.bufferFilters.isEmpty()) effect.setBufferFilters(md.bufferFilters);
    }
    effect.setUseDepthBuffer(md.depthBuffer);
    effect.setUseWallpaper(md.wallpaper);

    for (int i = 0; i < 8; ++i) {
        effect.setCustomParamAt(i, md.customParams[i]);
    }
    for (int i = 0; i < 16; ++i) {
        effect.setCustomColorAt(i, md.customColors[i]);
    }
    for (int i = 0; i < 4; ++i) {
        if (!md.userTextures[i].isEmpty()) {
            QImage img(md.userTextures[i]);
            if (!img.isNull()) {
                effect.setUserTexture(i, img);
                effect.setUserTextureWrap(i, md.userTextureWraps[i]);
            }
        }
    }
}

// Collect the standard shader include path candidates: the
// installed share-dir under .../plasmazones/shaders, then the
// in-tree libs/phosphor-rendering/shaders directory if present.
// The runtime resolves common.glsl / multipass.glsl / audio.glsl /
// textures.glsl / depth.glsl from these.
QStringList shaderIncludePaths()
{
    QStringList paths;
    const QStringList xdg = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString& dir : xdg) {
        paths.append(dir + QStringLiteral("/shaders"));
    }
    paths.append(QStringLiteral("/usr/share/plasmazones/shaders"));
    paths.append(QStringLiteral("data/shaders"));
    paths.append(QStringLiteral("libs/phosphor-rendering/shaders"));
    return paths;
}

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

int Renderer::render(const RenderOptions& opts)
{
    if (!QGuiApplication::instance()) {
        std::cerr << "Renderer::render: no QGuiApplication\n";
        return 1;
    }
    if (!opts.sink) {
        std::cerr << "Renderer::render: no sink\n";
        return 1;
    }

    // ── Offscreen surface ────────────────────────────────────────
    auto surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(QSurfaceFormat::defaultFormat());
    surface->create();
    if (!surface->isValid()) {
        std::cerr << "Renderer::render: failed to create QOffscreenSurface\n";
        return 1;
    }

    // Pick the RHI backend BEFORE the first QQuickWindow is
    // constructed — otherwise Qt locks in the platform default
    // and ignores the later call.  Vulkan is the most portable
    // for headless (lavapipe in CI, real GPUs locally); users can
    // override with QSG_RHI_BACKEND=opengl for boxes without a
    // Vulkan-capable Mesa.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);

    // ── Render control + window ──────────────────────────────────
    auto control = std::make_unique<QQuickRenderControl>();
    auto window = std::make_unique<QQuickWindow>(control.get());
    window->setColor(Qt::transparent);
    window->resize(opts.resolution);
    window->setGeometry(0, 0, opts.resolution.width(), opts.resolution.height());

    if (!control->initialize()) {
        std::cerr << "Renderer::render: QQuickRenderControl::initialize() failed. "
                     "Try VK_ICD_FILENAMES=lavapipe_icd.json or "
                     "QSG_RHI_BACKEND=opengl.\n";
        return 1;
    }

    // ── Build the scene ──────────────────────────────────────────
    using PhosphorRendering::ShaderEffect;
    auto* effect = new ShaderEffect(window->contentItem());
    effect->setSize(QSizeF(opts.resolution));
    effect->setShaderIncludePaths(shaderIncludePaths());

    // Attach a ZoneUniformExtension (GPL-licensed; we link as a
    // non-redistributable internal header — the tool itself is
    // GPL-3.0).  Pre-fills with zones, dirty so first frame uploads.
    auto zoneExt = std::make_shared<PlasmaZones::ZoneUniformExtension>();
    zoneExt->updateFromZones(toRuntimeZones(opts.zones));
    effect->setUniformExtension(zoneExt);

    seedShaderEffect(*effect, opts.metadata, opts.resolution);

    // ── Frame loop ───────────────────────────────────────────────
    QVector<float> spectrum;
    spectrum.reserve(AudioMock::kBarCount);

    const qreal frameInterval = 1.0 / opts.fps;
    qreal iTime = 0.0;
    int written = 0;

    for (int i = 0; i < opts.frameCount; ++i) {
        effect->setIFrame(i);
        effect->setITime(iTime);
        effect->setITimeDelta(frameInterval);

        if (opts.audio) {
            opts.audio->fillFrame(i, opts.fps, spectrum);
            effect->setAudioSpectrum(spectrum);
        }

        // TODO: Qt 6.6+ exposes QQuickRenderControl::beginFrame /
        // endFrame for explicit RHI command buffer management.  The
        // call-site below uses the higher-level polishAndSync +
        // render which works for simple ShaderEffects but doesn't
        // currently flush the multipass buffer ping-pong cleanly on
        // every render path.  Multipass + bufferFeedback shaders
        // (neon-city, voxel-terrain, paper, etc.) may need a few
        // warmup iterations OR explicit beginFrame/endFrame to
        // settle.  Verify against runtime output and switch to the
        // explicit API if needed.
        control->polishAndSync();
        control->render();

        // grabWindow returns an RGBA QImage.  Costly call (RHI
        // readback) but fine for a one-shot tool — ~40 fps render
        // throughput on a modest GPU.
        const QImage frame = window->grabWindow();
        if (frame.isNull()) {
            std::cerr << "Renderer::render: grabWindow returned null at frame " << i << "\n";
            return 1;
        }

        if (!opts.sink->writeFrame(frame)) {
            std::cerr << "Renderer::render: sink rejected frame " << i << "\n";
            return 1;
        }
        ++written;

        iTime += frameInterval;
    }

    if (!opts.sink->finalize()) {
        std::cerr << "Renderer::render: sink failed to finalize\n";
        return 1;
    }

    std::cout << "rendered " << written << " frames\n";
    return 0;
}

} // namespace PlasmaZones::ShaderRender
