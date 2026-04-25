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
#include <QQmlComponent>
#include <QQmlEngine>
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
//
// Derived from QQuickItem via ShaderEffect; Q_OBJECT + a default
// constructor make it QML-registerable so we can instantiate the
// scene via a QQmlComponent rather than imperatively — matches the
// scene-graph assembly the daemon's ZoneShaderItem uses.
class RenderEffect : public PhosphorRendering::ShaderEffect
{
    Q_OBJECT
public:
    explicit RenderEffect(QQuickItem* parent = nullptr)
        : PhosphorRendering::ShaderEffect(parent)
    {
    }
    explicit RenderEffect(QQuickItem* parent, const QString& vertPath)
        : PhosphorRendering::ShaderEffect(parent)
        , m_vertexShaderPath(vertPath)
    {
    }

    void setVertexShaderPath(const QString& p)
    {
        m_vertexShaderPath = p;
    }

    /// Set the zoneCount / highlightedCount values that zone-aware
    /// shaders key off via common.glsl's `zoneCount` uniform.  The
    /// daemon's ZoneShaderItem does this inside its own
    /// updatePaintNode after syncing zone data.  Zero skips the
    /// draw path in most shaders, so this MUST be set for zone
    /// shaders to render anything.
    void setZoneCounts(int total, int highlighted)
    {
        m_zoneCountTotal = total;
        m_zoneCountHighlighted = highlighted;
        update();
    }

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override
    {
        QSGNode* node = PhosphorRendering::ShaderEffect::updatePaintNode(oldNode, data);
        if (!node)
            return node;
        auto* rhiNode = dynamic_cast<PhosphorRendering::ShaderNodeRhi*>(node);
        if (rhiNode) {
            if (!m_vertexLoaded && !m_vertexShaderPath.isEmpty()) {
                if (rhiNode->loadVertexShader(m_vertexShaderPath)) {
                    rhiNode->invalidateShader();
                    m_vertexLoaded = true;
                }
            }
            // Zone-aware shaders gate rendering on zoneCount > 0.
            // ShaderEffect's base updatePaintNode doesn't push this
            // through — the daemon's ZoneShaderItem pushes it via
            // setAppField0/1 inside its own override.  Mirror that.
            rhiNode->setAppField0(m_zoneCountTotal);
            rhiNode->setAppField1(m_zoneCountHighlighted);
        }
        return node;
    }

private:
    QString m_vertexShaderPath;
    bool m_vertexLoaded = false;
    int m_zoneCountTotal = 0;
    int m_zoneCountHighlighted = 0;
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

void seedShaderEffect(PhosphorRendering::ShaderEffect& effect, const ShaderMetadata& md, const QSize& resolution)
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
        if (!md.bufferWraps.isEmpty())
            effect.setBufferWraps(md.bufferWraps);
        effect.setBufferFilter(md.bufferFilter);
        if (!md.bufferFilters.isEmpty())
            effect.setBufferFilters(md.bufferFilters);
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

    // Pick the RHI backend BEFORE the QQuickWindow is constructed.
    // OpenGL is the most portable for headless use — NVIDIA /
    // Mesa both provide working GL drivers under Wayland-less
    // setups too via EGL / GBM.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);

    auto control = std::make_unique<QQuickRenderControl>();
    auto window = std::make_unique<QQuickWindow>(control.get());

    window->setColor(Qt::transparent);
    window->resize(size);
    window->setGeometry(0, 0, size.width(), size.height());

    // Initialize the render control first — this sets up Qt's
    // internal RHI, scene graph, and render target allocation.
    // After this, window->rhi() returns a valid pointer we use to
    // build the texture we want the scene to paint into.
    if (!control->initialize()) {
        std::cerr << "Renderer::render: QQuickRenderControl::initialize() failed. "
                     "Check that a GL-capable display session is available.\n";
        return 1;
    }

    QRhi* rhi = window->rhi();
    if (!rhi) {
        std::cerr << "Renderer::render: window->rhi() is null after initialize\n";
        return 1;
    }

    // QQuickWindow scales logical item sizes by devicePixelRatio
    // when rendering.  The texture (physical pixel size) has to
    // match that or the scene graph renders at a tile size the
    // wrong dimensions from what the RT expects.  Detect the
    // window's DPR and size the texture accordingly; we downscale
    // at readback time to hit the user's requested output size.
    const qreal dpr = window->devicePixelRatio() > 0 ? window->devicePixelRatio() : 1.0;
    const QSize physicalSize(static_cast<int>(std::ceil(size.width() * dpr)),
                             static_cast<int>(std::ceil(size.height() * dpr)));

    // ── Offscreen render target: texture + depth + render pass ──
    std::unique_ptr<QRhiTexture> colorTex(rhi->newTexture(
        QRhiTexture::RGBA8, physicalSize, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!colorTex->create()) {
        std::cerr << "Renderer::render: colour texture create() failed\n";
        return 1;
    }
    std::unique_ptr<QRhiRenderBuffer> dsBuf(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, physicalSize, 1));
    if (!dsBuf->create()) {
        std::cerr << "Renderer::render: depth buffer create() failed\n";
        return 1;
    }
    QRhiColorAttachment colorAtt(colorTex.get());
    QRhiTextureRenderTargetDescription rtDesc(colorAtt);
    rtDesc.setDepthStencilBuffer(dsBuf.get());
    std::unique_ptr<QRhiTextureRenderTarget> textureRT(rhi->newTextureRenderTarget(rtDesc));
    std::unique_ptr<QRhiRenderPassDescriptor> rpDesc(textureRT->newCompatibleRenderPassDescriptor());
    textureRT->setRenderPassDescriptor(rpDesc.get());
    if (!textureRT->create()) {
        std::cerr << "Renderer::render: texture render target create() failed\n";
        return 1;
    }
    QQuickRenderTarget qrt = QQuickRenderTarget::fromRhiRenderTarget(textureRT.get());
    qrt.setDevicePixelRatio(dpr);
    window->setRenderTarget(qrt);

    // ── Build the scene ──────────────────────────────────────────
    using PhosphorRendering::ShaderEffect;

    // Content item needs to inherit the window's pixel size
    // explicitly under our offscreen setup — otherwise children
    // anchored to it get 0x0 and the scene graph culls them.
    window->contentItem()->setSize(QSizeF(size));

    auto* effect = new RenderEffect(window->contentItem(), opts.metadata.vertexShader);
    effect->setSize(QSizeF(size));
    effect->setVisible(true);
    effect->setShaderIncludePaths(shaderIncludePaths());

    auto zoneExt = std::make_shared<PlasmaZones::ZoneUniformExtension>();
    const auto runtimeZones = toRuntimeZones(opts.zones);
    zoneExt->updateFromZones(runtimeZones);
    effect->setUniformExtension(zoneExt);

    // Push zone counts into appField0/appField1, which is where
    // common.glsl maps `zoneCount` / `highlightedCount`.  Zone-
    // aware shaders gate their entire draw path on zoneCount > 0
    // — without this, they early-return vec4(0) before drawing.
    int highlightedCount = 0;
    for (const auto& z : runtimeZones)
        if (z.isHighlighted)
            ++highlightedCount;
    effect->setZoneCounts(runtimeZones.size(), highlightedCount);

    seedShaderEffect(*effect, opts.metadata, size);

    // Surface compile failures so a silent empty-frame loop isn't
    // the first sign something went wrong.
    QObject::connect(effect, &ShaderEffect::statusChanged, [effect]() {
        if (effect->status() == ShaderEffect::Status::Error) {
            std::cerr << "shader error: " << effect->errorLog().toStdString() << "\n";
        }
    });

    // Warmup: loadFragmentShader runs inside the first
    // updatePaintNode (during sync()), so frame 0 would capture a
    // still-compiling shader's clear-colour output.  Do a couple of
    // no-capture render cycles to move status Loading → Ready.
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
        if (effect->status() == ShaderEffect::Status::Ready)
            break;
    }
    if (effect->status() != ShaderEffect::Status::Ready) {
        std::cerr << "warning: shader not Ready after warmup (status=" << static_cast<int>(effect->status())
                  << "). Continuing anyway.\n";
    }

    // Transparent clear so the shader's alpha channel passes through
    // in the output (most shaders render semi-transparent content
    // on top of the zone fills).
    window->setColor(Qt::transparent);

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

        control->polishItems();
        control->beginFrame();
        control->sync();
        control->render();

        QRhiReadbackResult readbackResult;
        QRhiReadbackDescription readbackDesc(colorTex.get());
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture(readbackDesc, &readbackResult);
        QRhiCommandBuffer* cb = control->commandBuffer();
        if (!cb) {
            std::cerr << "Renderer::render: control has no command buffer on frame " << i << "\n";
            return 1;
        }
        cb->resourceUpdate(batch);
        control->endFrame();

        if (readbackResult.data.isEmpty()) {
            std::cerr << "Renderer::render: empty readback on frame " << i << "\n";
            return 1;
        }

        QImage frame(reinterpret_cast<const uchar*>(readbackResult.data.constData()), physicalSize.width(),
                     physicalSize.height(), physicalSize.width() * 4, QImage::Format_RGBA8888);
        const QImage flipped = flipVertical(frame);
        QImage out = flipped;
        if (flipped.size() != size) {
            out = flipped.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }

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

#include "renderer.moc"
