// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "renderer.h"

#include "zoneshadercommon.h"
#include "zoneuniformextension.h"

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include <rhi/qrhi.h>

#include <private/qquickitem_p.h>

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <private/qquickshadereffectsource_p.h>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QUrl>

#include <iostream>
#include <memory>

namespace PlasmaZones::ShaderRender {
namespace {

// Build the labels texture used at binding 1 (uZoneLabels).  Mirrors
// the daemon's ZoneLabelTextureBuilder: zone number drawn at zone
// centre, white fill with a dark stroke so glyphs are legible
// regardless of the underlying shader colour.  Many shaders sample
// uZoneLabels for halos / chromatic aberration / glyph fills — if
// the binding is empty, those effects are silently absent.
QImage buildLabelsImage(const QVector<Zone>& zones, const QSize& resolution)
{
    if (zones.isEmpty() || resolution.width() <= 0 || resolution.height() <= 0) {
        QImage empty(1, 1, QImage::Format_RGBA8888);
        empty.fill(Qt::transparent);
        return empty;
    }
    QImage image(resolution, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    constexpr int kGridUnit = 8;
    for (const auto& z : zones) {
        const QRectF px(z.rect.x() * resolution.width(), z.rect.y() * resolution.height(),
                        z.rect.width() * resolution.width(), z.rect.height() * resolution.height());
        if (px.width() <= 0 || px.height() <= 0)
            continue;

        const QString text = QString::number(z.zoneNumber);
        const qreal fontPixelSize = qMax<qreal>(kGridUnit, qMin(px.width(), px.height()) * 0.25);
        QFont font;
        font.setPixelSize(static_cast<int>(fontPixelSize));
        font.setWeight(QFont::Bold);

        QPainterPath path;
        path.addText(0, 0, font, text);
        const QRectF textBounds = path.boundingRect();
        const QPointF center = px.center();
        path.translate(center.x() - textBounds.center().x(), center.y() - textBounds.center().y());

        const QPen outlinePen(QColor(0, 0, 0, 200), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(outlinePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawPath(path);
    }
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

// ShaderEffect only loads the fragment shader from its shaderSource
// URL.  Zone shaders ship a custom vertex shader too — the daemon's
// ZoneShaderItem handles it inside updatePaintNode, but reusing
// ZoneShaderItem here pulls in too much of the daemon.  Instead we
// subclass ShaderEffect with a minimal override that loads the
// vertex shader onto the render node the first time it appears.
//
// In addition, the daemon's ZoneShaderNodeRhi binds a labels-texture
// sampler at extra binding 1 (uZoneLabels).  Without that binding
// every shader that samples uZoneLabels reads garbage on Vulkan —
// and even on OpenGL the implementation defaults to 0, which makes
// glow/chromatic-aberration/text effects silently absent.  We
// reproduce the binding here using QRhi APIs against the base
// ShaderNodeRhi created by ShaderEffect.
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

    /// Pre-rendered zone-numbers image bound to binding 1 as
    /// uZoneLabels.  Owned by the item; the texture/sampler created
    /// from it live as long as this RenderEffect.
    void setLabelsImage(const QImage& image)
    {
        m_labelsImage = image.convertToFormat(QImage::Format_RGBA8888);
        m_labelsDirty = true;
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

            // Bind/upload labels texture at extra binding 1 once
            // the window's QRhi is reachable.  The daemon does this
            // inside ZoneShaderNodeRhi::prepare() via setExtraBinding
            // from a subclass; we drive it from here on the sync
            // thread using the public API.
            ensureLabelsBinding(rhiNode);
        }
        return node;
    }

private:
    void ensureLabelsBinding(PhosphorRendering::ShaderNodeRhi* rhiNode)
    {
        QQuickWindow* w = window();
        if (!w)
            return;
        QRhi* rhi = w->rhi();
        if (!rhi)
            return;

        const QSize target = m_labelsImage.size().isEmpty() ? QSize(1, 1) : m_labelsImage.size();

        if (!m_labelsTexture || m_labelsTexture->pixelSize() != target) {
            m_labelsTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, target));
            if (!m_labelsTexture->create()) {
                m_labelsTexture.reset();
                return;
            }
            if (!m_labelsSampler) {
                m_labelsSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
                if (!m_labelsSampler->create()) {
                    m_labelsSampler.reset();
                    return;
                }
            }
            rhiNode->setExtraBinding(1, m_labelsTexture.get(), m_labelsSampler.get());
            m_labelsDirty = true;
        }

        if (m_labelsDirty) {
            QImage src = m_labelsImage;
            if (src.isNull() || src.size().isEmpty()) {
                src = QImage(1, 1, QImage::Format_RGBA8888);
                src.fill(Qt::transparent);
            }
            QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
            batch->uploadTexture(m_labelsTexture.get(), src);
            // Defer commit to the node's command buffer at next prepare.
            // Push via the render control's command buffer if available.
            if (auto* cb = rhiNode->commandBuffer()) {
                cb->resourceUpdate(batch);
            } else {
                // No command buffer yet — queue without a target;
                // resource updates without a CB are released, so we
                // wait for the next sync.  Re-flag dirty.
                batch->release();
                return;
            }
            m_labelsDirty = false;
        }
    }

    QString m_vertexShaderPath;
    bool m_vertexLoaded = false;
    int m_zoneCountTotal = 0;
    int m_zoneCountHighlighted = 0;

    QImage m_labelsImage;
    bool m_labelsDirty = false;
    std::unique_ptr<QRhiTexture> m_labelsTexture;
    std::unique_ptr<QRhiSampler> m_labelsSampler;
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

    // The QML wrapper enables layer.enabled on ZoneShaderItem so the
    // shader renders to a private FBO.  Without this, the scene
    // graph's batch renderer's pass-tracking state desyncs against
    // the buffer passes the render node manages itself, and
    // multipass shaders silently produce nothing.  Some single-pass
    // shaders also rely on the FBO isolation (e.g. anything that
    // expects a clean clear before the fragment runs).  Mirror the
    // QML behaviour via QQuickItemPrivate.
    {
        auto* priv = QQuickItemPrivate::get(effect);
        priv->layer()->setEnabled(true);
        priv->layer()->setTextureMirroring(QQuickShaderEffectSource::NoMirroring);
    }

    auto zoneExt = std::make_shared<PlasmaZones::ZoneUniformExtension>();
    auto runtimeZones = toRuntimeZones(opts.zones);
    zoneExt->updateFromZones(runtimeZones);
    effect->setUniformExtension(zoneExt);

    // Initial zone counts.  highlightedCount and per-zone isHighlighted
    // flags are updated inside the frame loop so the demo cycles the
    // active zone — see the loop body for the cycling logic.
    effect->setZoneCounts(runtimeZones.size(), 0);

    // Pre-render the labels texture.  Many shaders sample
    // uZoneLabels for halo/chroma/text effects — leaving binding 1
    // empty makes those effects silently disappear.
    effect->setLabelsImage(buildLabelsImage(opts.zones, size));

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

    // Demo background: a subtle Plasma-Breeze-dark grey so the
    // shaders' alpha channel reads as something other than the
    // file viewer's checkerboard.  Most shaders render semi-
    // transparent content; with a transparent clear the result
    // looked black-on-black for non-vivid bands.  This colour was
    // picked to be visibly NOT black without dominating the
    // shader output.
    window->setColor(QColor(QStringLiteral("#31363b")));

    // ── Frame loop ───────────────────────────────────────────────
    QVector<float> spectrum;
    spectrum.reserve(AudioMock::kBarCount);

    const qreal frameInterval = 1.0 / opts.fps;
    qreal iTime = 0.0;

    // Zone-highlight schedule:
    //   slice 0:           every zone highlighted (the shader at
    //                      full vivid intensity — this is the
    //                      hero shot for static thumbnails)
    //   slices 1..N:       highlight zone N-1 only (cycle)
    //
    // Splitting into N+1 equal slices means the all-highlighted
    // intro and each individual zone get the same screen time —
    // for a 15s 4-zone clip that's ~3s per slice.
    const int numZones = runtimeZones.size();
    const int slicesTotal = numZones + 1;
    int lastSlice = -1;

    for (int i = 0; i < opts.frameCount; ++i) {
        effect->setIFrame(i);
        effect->setITime(iTime);
        effect->setITimeDelta(frameInterval);

        if (numZones > 0) {
            const int slice = std::min((i * slicesTotal) / std::max(opts.frameCount, 1), slicesTotal - 1);
            if (slice != lastSlice) {
                int highlightedCount = 0;
                if (slice == 0) {
                    for (int z = 0; z < numZones; ++z) {
                        runtimeZones[z].isHighlighted = true;
                    }
                    highlightedCount = numZones;
                } else {
                    const int activeZone = slice - 1;
                    for (int z = 0; z < numZones; ++z) {
                        runtimeZones[z].isHighlighted = (z == activeZone);
                    }
                    highlightedCount = 1;
                }
                zoneExt->updateFromZones(runtimeZones);
                effect->setZoneCounts(numZones, highlightedCount);
                lastSlice = slice;
            }
        }

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
