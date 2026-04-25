// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "renderer.h"

#include "zoneshadercommon.h"
#include "zoneuniformextension.h"

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include <rhi/qrhi.h>

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QUrl>

#include <iostream>
#include <memory>

namespace PlasmaZones::ShaderRender {
namespace {

// ShaderEffect only loads the fragment shader from its shaderSource
// URL.  Zone shaders ship a custom vertex shader too — the daemon's
// ZoneShaderItem handles it inside updatePaintNode, but reusing
// ZoneShaderItem here pulls in too much of the daemon.  Instead we
// subclass ShaderEffect with a minimal override that loads the
// vertex shader onto the render node the first time it appears.
class RenderEffect : public PhosphorRendering::ShaderEffect
{
public:
    explicit RenderEffect(QQuickItem* parent, const QString& vertPath)
        : PhosphorRendering::ShaderEffect(parent), m_vertexShaderPath(vertPath)
    {
    }

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override
    {
        QSGNode* node = PhosphorRendering::ShaderEffect::updatePaintNode(oldNode, data);
        // Once the parent has (re-)created the render node, push our
        // vertex shader in.  This mirrors what ZoneShaderItem does
        // in its own updatePaintNode — vertex path → loadVertexShader.
        if (node && !m_vertexLoaded && !m_vertexShaderPath.isEmpty()) {
            auto* rhiNode = dynamic_cast<PhosphorRendering::ShaderNodeRhi*>(node);
            if (rhiNode) {
                if (rhiNode->loadVertexShader(m_vertexShaderPath)) {
                    rhiNode->invalidateShader();
                    m_vertexLoaded = true;
                }
            }
        }
        return node;
    }

private:
    QString m_vertexShaderPath;
    bool m_vertexLoaded = false;
};


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

// Flip a QImage vertically.  RHI texture readback delivers rows
// bottom-up; we need top-down for normal image consumers and the
// ffmpeg pipe.
QImage flipVertical(const QImage& src)
{
    QImage flipped(src.size(), src.format());
    const int h = src.height();
    const qsizetype bpl = src.bytesPerLine();
    for (int y = 0; y < h; ++y) {
        std::memcpy(flipped.scanLine(y), src.constScanLine(h - 1 - y), bpl);
    }
    return flipped;
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

    const QSize size = opts.resolution;

    // ── Set up OpenGL context for an offscreen RHI ──────────────
    // Don't force a profile or version — let the platform pick
    // something compatible with its driver.  Setting CoreProfile
    // 3.3 fails on NVIDIA Wayland's EGL path here.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);

    QOpenGLContext glContext;
    glContext.setFormat(fmt);
    if (!glContext.create()) {
        std::cerr << "Renderer::render: QOpenGLContext::create() failed. "
                     "Check that an OpenGL-capable session is available.\n";
        return 1;
    }

    QOffscreenSurface offscreenSurface;
    offscreenSurface.setFormat(glContext.format());
    offscreenSurface.create();
    if (!offscreenSurface.isValid()) {
        std::cerr << "Renderer::render: offscreen surface isn't valid\n";
        return 1;
    }
    if (!glContext.makeCurrent(&offscreenSurface)) {
        std::cerr << "Renderer::render: makeCurrent on offscreen surface failed\n";
        return 1;
    }

    // ── Build the QRhi on top of our own context ─────────────────
    QRhiGles2InitParams rhiParams;
    rhiParams.fallbackSurface = &offscreenSurface;
    rhiParams.shareContext = &glContext;

    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &rhiParams));
    if (!rhi) {
        std::cerr << "Renderer::render: QRhi::create(OpenGLES2) failed\n";
        return 1;
    }

    // ── Offscreen render target: colour texture + render pass ───
    std::unique_ptr<QRhiTexture> colorTex(rhi->newTexture(
        QRhiTexture::RGBA8, size, 1,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!colorTex->create()) {
        std::cerr << "Renderer::render: colour texture create() failed\n";
        return 1;
    }

    // Depth/stencil for depthBuffer-enabled shaders.  Shaders that
    // don't request it still get one — harmless.
    std::unique_ptr<QRhiRenderBuffer> dsBuf(rhi->newRenderBuffer(
        QRhiRenderBuffer::DepthStencil, size, 1));
    if (!dsBuf->create()) {
        std::cerr << "Renderer::render: depth buffer create() failed\n";
        return 1;
    }

    QRhiColorAttachment colorAtt(colorTex.get());
    QRhiTextureRenderTargetDescription rtDesc(colorAtt);
    rtDesc.setDepthStencilBuffer(dsBuf.get());

    std::unique_ptr<QRhiTextureRenderTarget> textureRT(
        rhi->newTextureRenderTarget(rtDesc));
    std::unique_ptr<QRhiRenderPassDescriptor> rpDesc(
        textureRT->newCompatibleRenderPassDescriptor());
    textureRT->setRenderPassDescriptor(rpDesc.get());
    if (!textureRT->create()) {
        std::cerr << "Renderer::render: texture render target create() failed\n";
        return 1;
    }

    // ── Quick window / render control ───────────────────────────
    //
    // Pick the RHI backend BEFORE the QQuickWindow is constructed.
    // OpenGL matches the QRhi backend we built above; mixing them
    // produces a silent failure in initialize().
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);

    auto control = std::make_unique<QQuickRenderControl>();
    auto window = std::make_unique<QQuickWindow>(control.get());

    // Hand the window our RHI + texture render target BEFORE
    // initialize().  Otherwise initialize() tries to create its
    // own RHI against a window surface that doesn't exist under
    // this offscreen setup and fails.
    window->setGraphicsDevice(QQuickGraphicsDevice::fromRhi(rhi.get()));
    window->setRenderTarget(
        QQuickRenderTarget::fromRhiRenderTarget(textureRT.get()));
    window->setColor(Qt::transparent);
    window->resize(size);
    window->setGeometry(0, 0, size.width(), size.height());

    if (!control->initialize()) {
        std::cerr << "Renderer::render: QQuickRenderControl::initialize() failed\n";
        return 1;
    }

    // ── Build the scene ──────────────────────────────────────────
    using PhosphorRendering::ShaderEffect;
    auto* effect = new RenderEffect(window->contentItem(),
                                    opts.metadata.vertexShader);
    effect->setSize(QSizeF(size));
    effect->setShaderIncludePaths(shaderIncludePaths());

    auto zoneExt = std::make_shared<PlasmaZones::ZoneUniformExtension>();
    zoneExt->updateFromZones(toRuntimeZones(opts.zones));
    effect->setUniformExtension(zoneExt);

    seedShaderEffect(*effect, opts.metadata, size);

    // Surface shader-load / compile status — the default stderr
    // for a failed compile is silent unless we poll.
    QObject::connect(effect, &ShaderEffect::statusChanged, [effect]() {
        if (effect->status() == ShaderEffect::Status::Error) {
            std::cerr << "shader error: "
                      << effect->errorLog().toStdString() << "\n";
        }
    });

    // Warmup: the shader's loadFragmentShader call happens inside
    // the first updatePaintNode, which runs during sync().  Do a
    // couple of no-capture render cycles to move the status from
    // Loading → Ready before the real frame loop starts.  Without
    // this, frame 0 captures a blank clear-colour image because the
    // shader hasn't finished compiling by the time we read back.
    auto warmup = [&]() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        control->polishItems();
        control->beginFrame();
        control->sync();
        control->render();
        control->endFrame();
    };
    for (int i = 0; i < 3; ++i) {
        warmup();
        if (effect->status() == ShaderEffect::Status::Ready) break;
    }
    if (effect->status() != ShaderEffect::Status::Ready) {
        std::cerr << "warning: shader not Ready after warmup (status="
                  << static_cast<int>(effect->status()) << "). Continuing anyway.\n";
    }

    std::cerr << "shader: " << opts.metadata.fragmentShader.toStdString() << "\n";
    std::cerr << "zones:  " << opts.zones.size() << "\n";
    std::cerr << "effect size: " << effect->width() << "x" << effect->height() << "\n";
    std::cerr << "window size: " << window->width() << "x" << window->height() << "\n";

    // Tint the clear colour so we can distinguish "shader rendered
    // nothing" (card stays tinted) from "window didn't render at
    // all" (readback is all zeros) when debugging.
    window->setColor(QColor::fromRgbF(0.04f, 0.04f, 0.10f, 1.0f));

    // ── Frame loop ───────────────────────────────────────────────
    QVector<float> spectrum;
    spectrum.reserve(AudioMock::kBarCount);

    const qreal frameInterval = 1.0 / opts.fps;
    qreal iTime = 0.0;

    for (int i = 0; i < opts.frameCount; ++i) {
        effect->setIFrame(i);
        effect->setITime(iTime);
        effect->setITimeDelta(frameInterval);

        if (opts.audio) {
            opts.audio->fillFrame(i, opts.fps, spectrum);
            effect->setAudioSpectrum(spectrum);
        }

        // Drain the event queue so property-change signals, shader-
        // compile completion notifications, and scene-graph-dirty
        // flags all propagate before we render this frame.  Without
        // this, setX() calls above queue events that fire AFTER
        // control->sync() reads the item state, and the first frame
        // always renders the prior value.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        if (i == 0) {
            std::cerr << "frame 0 effect status: " << static_cast<int>(effect->status()) << "\n";
        }

        control->polishItems();
        control->beginFrame();
        control->sync();
        control->render();

        // Submit a texture readback in this frame.  Completed on
        // endFrame(), at which point readbackResult.data holds
        // frame.width * frame.height * 4 bytes of RGBA.
        QRhiReadbackResult readbackResult;
        QRhiReadbackDescription readbackDesc(colorTex.get());
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture(readbackDesc, &readbackResult);
        // There's no direct "attach this batch to the frame" on
        // render control, so rely on rhi::resourceUpdate via a
        // separate submission.  QRhi defers the readback until the
        // GPU catches up; endFrame() here flushes the render
        // commands, and the readback commits on the next frame's
        // batch submission.  To work synchronously in a one-shot
        // loop, we immediately beginOffscreenFrame + submit the
        // readback batch + endOffscreenFrame.
        control->endFrame();

        QRhiCommandBuffer* cb = nullptr;
        if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
            std::cerr << "Renderer::render: beginOffscreenFrame failed on frame "
                      << i << "\n";
            return 1;
        }
        cb->resourceUpdate(batch);
        rhi->endOffscreenFrame();

        if (readbackResult.data.isEmpty()) {
            std::cerr << "Renderer::render: empty readback on frame " << i << "\n";
            return 1;
        }

        QImage frame(reinterpret_cast<const uchar*>(readbackResult.data.constData()),
                     size.width(), size.height(),
                     size.width() * 4,
                     QImage::Format_RGBA8888);
        // GL textures are bottom-up; flip into a normal top-down
        // image before handing to the sink.
        const QImage out = flipVertical(frame);

        if (!opts.sink->writeFrame(out)) {
            std::cerr << "Renderer::render: sink rejected frame " << i << "\n";
            return 1;
        }

        iTime += frameInterval;
    }

    if (!opts.sink->finalize()) {
        std::cerr << "Renderer::render: sink failed to finalize\n";
        return 1;
    }

    std::cout << "rendered " << opts.frameCount << " frames\n";
    return 0;
}

} // namespace PlasmaZones::ShaderRender
