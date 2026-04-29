// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "renderer.h"

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>
#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/ZoneShaderNodeRhi.h>
#include <PhosphorRendering/ZoneUniformExtension.h>

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

#include <QLoggingCategory>

#include <memory>
#include <optional>

namespace PlasmaZones::ShaderRender {

Q_LOGGING_CATEGORY(lcRenderer, "plasmazones.shader-render.renderer")

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

// ShaderEffect doesn't itself load a vertex shader (its node is the generic
// ShaderNodeRhi). Zone shaders ship a custom vertex shader and need a labels
// texture bound at sampler 1; both responsibilities live in
// PhosphorRendering::ZoneShaderNodeRhi. We override createShaderNode() so the
// scene graph gets a ZoneShaderNodeRhi instead of the plain one, then push
// vertex/labels/zone-counts through the node's public API from
// updatePaintNode (sync phase). The node uploads the labels texture inside
// its own prepare() against a live command buffer — no manual RHI plumbing
// from this side.
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

    /// Total zone count + currently-highlighted count. Zone-aware shaders
    /// gate their per-zone loops on these via common.glsl's zoneCount.
    void setZoneCounts(int total, int highlighted)
    {
        m_zoneCountTotal = total;
        m_zoneCountHighlighted = highlighted;
        update();
    }

    /// Pre-rendered zone-numbers image bound to sampler 1 as uZoneLabels. The
    /// underlying ZoneShaderNodeRhi takes ownership of the upload during its
    /// next prepare().
    void setLabelsImage(const QImage& image)
    {
        m_labelsImage = image.convertToFormat(QImage::Format_RGBA8888);
        m_labelsDirty = true;
        update();
    }

protected:
    PhosphorRendering::ShaderNodeRhi* createShaderNode() override
    {
        return new PhosphorRendering::ZoneShaderNodeRhi(this);
    }

    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override
    {
        QSGNode* node = PhosphorRendering::ShaderEffect::updatePaintNode(oldNode, data);
        if (!node)
            return node;
        auto* zoneNode = dynamic_cast<PhosphorRendering::ZoneShaderNodeRhi*>(node);
        if (!zoneNode)
            return node;

        // The scene graph can destroy and recreate the render node (e.g. on
        // releaseResources / window hide). Detecting a pointer change resets
        // the per-node "already pushed" flags so the new node gets the vertex
        // shader and labels image re-applied — without this, the new node
        // would render without them and the shader would silently misbehave.
        // m_labelsDirty is forced even with an empty image so the node's
        // 1×1 transparent fallback gets bound at slot 1 — leaving the slot
        // unbound would render the SRB pipeline without uZoneLabels.
        if (zoneNode != m_lastZoneNode) {
            m_vertexLoaded = false;
            m_vertexLoadFailed = false;
            m_labelsDirty = true;
            m_lastZoneNode = zoneNode;
        }

        if (!m_vertexLoaded && !m_vertexLoadFailed && !m_vertexShaderPath.isEmpty()) {
            if (zoneNode->loadVertexShader(m_vertexShaderPath)) {
                zoneNode->invalidateShader();
                m_vertexLoaded = true;
            } else {
                // Latch the failure so we don't retry every frame. The base
                // ShaderNodeRhi already logs the underlying load error; this
                // line just clarifies that the tool is giving up after one
                // attempt rather than spinning silently.
                qCWarning(lcRenderer) << "vertex shader load failed for" << m_vertexShaderPath
                                      << "— continuing with the default fullscreen vertex stage";
                m_vertexLoadFailed = true;
            }
        }
        zoneNode->setZoneCounts(m_zoneCountTotal, m_zoneCountHighlighted);
        if (m_labelsDirty) {
            zoneNode->setLabelsTexture(m_labelsImage);
            m_labelsDirty = false;
        }
        return node;
    }

private:
    QString m_vertexShaderPath;
    bool m_vertexLoaded = false;
    bool m_vertexLoadFailed = false;
    int m_zoneCountTotal = 0;
    int m_zoneCountHighlighted = 0;
    QImage m_labelsImage;
    bool m_labelsDirty = false;
    PhosphorRendering::ZoneShaderNodeRhi* m_lastZoneNode = nullptr;
};

QVector<PhosphorRendering::ZoneData> toRuntimeZones(const QVector<Zone>& srcZones)
{
    QVector<PhosphorRendering::ZoneData> out;
    out.reserve(srcZones.size());
    for (const auto& z : srcZones) {
        PhosphorRendering::ZoneData d{};
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

void seedShaderEffect(PhosphorRendering::ShaderEffect& effect, const ShaderMetadata& md, const QSize& /*resolution*/)
{
    // iResolution is driven from the item's width/height by ShaderEffect::
    // syncBasePropertiesToNode every frame, so seeding it here would be
    // overwritten before the first sync — drop the redundant call.
    effect.setShaderSource(QUrl::fromLocalFile(md.fragmentShader));

    if (md.multipass) {
        if (!md.bufferShaders.isEmpty()) {
            effect.setBufferShaderPaths(md.bufferShaders);
        } else if (!md.bufferShader.isEmpty()) {
            effect.setBufferShaderPath(md.bufferShader);
        } else {
            qCWarning(lcRenderer) << "shader" << md.id
                                  << "declares multipass=true but supplies no bufferShader/bufferShaders —"
                                  << "the multipass path will produce nothing";
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
    if (md.wallpaper) {
        // The daemon binds the actual wallpaper texture before the shader
        // runs; the tool has no compositor to ask. Forcing useWallpaper=false
        // pins previews to a deterministic no-wallpaper code path rather than
        // having shaders sample an unbound (= garbage on Vulkan / impl-default
        // 0 on OpenGL) wallpaper texture and read as a different image every
        // backend.
        qCWarning(lcRenderer) << "shader" << md.id << "declares wallpaper=true but tool doesn't bind a wallpaper —"
                              << "preview uses the no-wallpaper render path";
    }
    effect.setUseWallpaper(false);

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

// ── Helpers extracted from Renderer::render ────────────────────────────────

QSGRendererInterface::GraphicsApi pickGraphicsApi()
{
    const QByteArray backend = qgetenv("QSG_RHI_BACKEND").toLower();
    if (backend == "vulkan")
        return QSGRendererInterface::VulkanRhi;
    if (backend == "metal")
        return QSGRendererInterface::MetalRhi;
    if (backend == "d3d11" || backend == "d3d")
        return QSGRendererInterface::Direct3D11Rhi;
    if (!backend.isEmpty() && backend != "opengl") {
        qCWarning(lcRenderer) << "unknown QSG_RHI_BACKEND=" << backend << "— falling back to OpenGL";
    }
    return QSGRendererInterface::OpenGLRhi;
}

/// Owns the QRhi resources backing the offscreen render target. RAII via
/// member unique_ptrs — destruction order is RT → RPDesc → DS buffer → color.
struct OffscreenTarget
{
    std::unique_ptr<QRhiTexture> color;
    std::unique_ptr<QRhiRenderBuffer> depthStencil;
    std::unique_ptr<QRhiRenderPassDescriptor> renderPass;
    std::unique_ptr<QRhiTextureRenderTarget> target;
};

/// Allocate the offscreen target. Returns std::nullopt and logs on any RHI
/// allocation failure; render() bails out with a non-zero exit code.
std::optional<OffscreenTarget> buildOffscreenTarget(QRhi* rhi, const QSize& size)
{
    OffscreenTarget t;
    t.color.reset(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!t.color->create()) {
        qCWarning(lcRenderer) << "color texture create() failed";
        return std::nullopt;
    }
    t.depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    if (!t.depthStencil->create()) {
        qCWarning(lcRenderer) << "depth buffer create() failed";
        return std::nullopt;
    }
    QRhiColorAttachment colorAtt(t.color.get());
    QRhiTextureRenderTargetDescription rtDesc(colorAtt);
    rtDesc.setDepthStencilBuffer(t.depthStencil.get());
    t.target.reset(rhi->newTextureRenderTarget(rtDesc));
    t.renderPass.reset(t.target->newCompatibleRenderPassDescriptor());
    t.target->setRenderPassDescriptor(t.renderPass.get());
    if (!t.target->create()) {
        qCWarning(lcRenderer) << "texture render target create() failed";
        return std::nullopt;
    }
    return t;
}

/// Run a few sync/render cycles WITHOUT capture so the shader has time to
/// transition Loading → Ready before frame 0 is written. loadFragmentShader
/// runs inside the first updatePaintNode (during sync()), so capturing frame
/// 0 against a still-compiling shader yields the clear-colour.
void runWarmup(QQuickRenderControl* control, RenderEffect* effect)
{
    using PhosphorRendering::ShaderEffect;
    constexpr int kWarmupFrames = 3;
    for (int i = 0; i < kWarmupFrames; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        control->polishItems();
        control->beginFrame();
        control->sync();
        control->render();
        control->endFrame();
        if (effect->status() == ShaderEffect::Status::Ready)
            return;
    }
    if (effect->status() != ShaderEffect::Status::Ready) {
        qCWarning(lcRenderer) << "shader not Ready after" << kWarmupFrames
                              << "warmup frames (status=" << static_cast<int>(effect->status())
                              << ") — continuing anyway";
    }
}

/// Apply the highlight schedule for frame @p i and push the new state to the
/// uniform extension + zone counts when the slice changes.
///   slice 0:        every zone highlighted (the hero shot for thumbnails)
///   slices 1..N:    one zone highlighted at a time, cycling
///
/// Splitting into N+1 equal slices means the all-highlighted intro and each
/// individual zone get the same screen time. @p lastSlice is in/out so the
/// caller can short-circuit no-op transitions.
void applyHighlightSchedule(int i, int frameCount, QVector<PhosphorRendering::ZoneData>& zones,
                            PhosphorRendering::ZoneUniformExtension& zoneExt, RenderEffect& effect, int& lastSlice)
{
    const int numZones = zones.size();
    if (numZones == 0)
        return;
    const int slicesTotal = numZones + 1;
    const int slice = std::min((i * slicesTotal) / std::max(frameCount, 1), slicesTotal - 1);
    if (slice == lastSlice)
        return;
    int highlightedCount = 0;
    if (slice == 0) {
        for (auto& z : zones)
            z.isHighlighted = true;
        highlightedCount = numZones;
    } else {
        const int activeZone = slice - 1;
        for (int z = 0; z < numZones; ++z)
            zones[z].isHighlighted = (z == activeZone);
        highlightedCount = 1;
    }
    zoneExt.updateFromZones(zones);
    effect.setZoneCounts(numZones, highlightedCount);
    lastSlice = slice;
}

/// Submit the readback batch for the current frame and pull the pixel data.
/// Returns the (top-down) RGBA8888 frame on success; an empty QImage on
/// failure. Callers detect failure via QImage::isNull().
QImage captureFrame(QQuickRenderControl* control, QRhi* rhi, QRhiTexture* colorTex, const QSize& physicalSize, int i)
{
    QRhiReadbackResult readbackResult;
    QRhiReadbackDescription readbackDesc(colorTex);
    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture(readbackDesc, &readbackResult);
    QRhiCommandBuffer* cb = control->commandBuffer();
    if (!cb) {
        // Release the batch we never submitted so RHI bookkeeping doesn't
        // leak it — release() returns the batch to the pool without queueing.
        batch->release();
        qCWarning(lcRenderer) << "control has no command buffer on frame" << i;
        return {};
    }
    cb->resourceUpdate(batch);
    control->endFrame();
    if (readbackResult.data.isEmpty()) {
        qCWarning(lcRenderer) << "empty readback on frame" << i;
        return {};
    }
    QImage frame(reinterpret_cast<const uchar*>(readbackResult.data.constData()), physicalSize.width(),
                 physicalSize.height(), physicalSize.width() * 4, QImage::Format_RGBA8888);
    return flipVertical(frame);
}

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

int Renderer::render(const RenderOptions& opts)
{
    using PhosphorRendering::ShaderEffect;

    if (!QGuiApplication::instance()) {
        qCWarning(lcRenderer) << "no QGuiApplication";
        return 1;
    }
    if (!opts.sink) {
        qCWarning(lcRenderer) << "no sink";
        return 1;
    }

    const QSize size = opts.resolution;

    // Pick the RHI backend BEFORE the QQuickWindow is constructed.
    QQuickWindow::setGraphicsApi(pickGraphicsApi());

    auto control = std::make_unique<QQuickRenderControl>();
    auto window = std::make_unique<QQuickWindow>(control.get());

    // Demo background: a subtle Plasma-Breeze-dark grey so the shaders'
    // alpha channel reads as something other than the file viewer's
    // checkerboard. Most shaders render semi-transparent content; with a
    // transparent clear the result looks black-on-black for non-vivid
    // bands. Set once before warmup so every captured frame uses the same
    // clear colour.
    window->setColor(QColor(QStringLiteral("#31363b")));
    window->resize(size);

    // Initialize the render control first — sets up Qt's internal RHI, scene
    // graph, and render target allocation. After this, window->rhi() returns
    // a valid pointer to use for the offscreen target.
    if (!control->initialize()) {
        qCWarning(lcRenderer) << "QQuickRenderControl::initialize() failed —"
                              << "check that a GL-capable display session is available";
        return 1;
    }

    QRhi* rhi = window->rhi();
    if (!rhi) {
        qCWarning(lcRenderer) << "window->rhi() is null after initialize";
        return 1;
    }

    // Offscreen rendering — pin DPR to 1.0 so the texture matches the user's
    // --resolution exactly. Keeping the ambient session's DPR (often 2 on
    // HiDPI displays) would render at 4x the pixel count and force a CPU
    // SmoothTransformation downsample every frame. The window is never shown
    // so a non-display DPR is meaningless.
    constexpr qreal dpr = 1.0;
    const QSize physicalSize = size;

    auto target = buildOffscreenTarget(rhi, physicalSize);
    if (!target) {
        return 1;
    }
    QRhiTexture* colorTex = target->color.get();

    QQuickRenderTarget qrt = QQuickRenderTarget::fromRhiRenderTarget(target->target.get());
    qrt.setDevicePixelRatio(dpr);
    window->setRenderTarget(qrt);

    // ── Build the scene ──────────────────────────────────────────
    // Content item needs to inherit the window's pixel size explicitly under
    // our offscreen setup — otherwise children anchored to it get 0x0 and
    // the scene graph culls them.
    window->contentItem()->setSize(QSizeF(size));

    auto* effect = new RenderEffect(window->contentItem(), opts.metadata.vertexShader);
    effect->setSize(QSizeF(size));
    effect->setVisible(true);
    effect->setShaderIncludePaths(shaderIncludePaths());

    // The QML wrapper enables layer.enabled on ZoneShaderItem so the shader
    // renders to a private FBO. Without this, the scene graph's batch
    // renderer's pass-tracking state desyncs against the buffer passes the
    // render node manages itself, and multipass shaders silently produce
    // nothing. Some single-pass shaders also rely on the FBO isolation (e.g.
    // anything that expects a clean clear before the fragment runs). Mirror
    // the QML behaviour via QQuickItemPrivate.
    {
        auto* priv = QQuickItemPrivate::get(effect);
        priv->layer()->setEnabled(true);
        priv->layer()->setTextureMirroring(QQuickShaderEffectSource::NoMirroring);
    }

    auto zoneExt = std::make_shared<PhosphorRendering::ZoneUniformExtension>();
    auto runtimeZones = toRuntimeZones(opts.zones);
    zoneExt->updateFromZones(runtimeZones);
    effect->setUniformExtension(zoneExt);

    // Initial zone counts. highlightedCount and per-zone isHighlighted flags
    // are updated inside the frame loop so the demo cycles the active zone
    // — see applyHighlightSchedule().
    effect->setZoneCounts(runtimeZones.size(), 0);

    // Pre-render the labels texture. Many shaders sample uZoneLabels for
    // halo/chroma/text effects — an empty binding makes them silently absent.
    effect->setLabelsImage(buildLabelsImage(opts.zones, size));

    seedShaderEffect(*effect, opts.metadata, size);

    // Surface compile failures so a silent empty-frame loop isn't the first
    // sign something went wrong.
    QObject::connect(effect, &ShaderEffect::statusChanged, [effect]() {
        if (effect->status() == ShaderEffect::Status::Error) {
            qCWarning(lcRenderer) << "shader error:" << effect->errorLog();
        }
    });

    runWarmup(control.get(), effect);

    // ── Frame loop ───────────────────────────────────────────────
    QVector<float> spectrum;
    spectrum.reserve(AudioMock::kBarCount);

    const qreal frameInterval = 1.0 / opts.fps;
    int lastSlice = -1;

    for (int i = 0; i < opts.frameCount; ++i) {
        // Compute iTime from the frame index (not an accumulator) so long
        // renders don't drift — at fps=30 a 10-minute clip otherwise
        // accumulates ~0.06s of float error.
        effect->setIFrame(i);
        effect->setITime(static_cast<qreal>(i) / opts.fps);
        effect->setITimeDelta(frameInterval);

        applyHighlightSchedule(i, opts.frameCount, runtimeZones, *zoneExt, *effect, lastSlice);

        if (opts.audio) {
            opts.audio->fillFrame(i, opts.fps, spectrum);
            effect->setAudioSpectrum(spectrum);
        }

        // Drain the event queue so property-change signals, shader-compile
        // completion notifications, and scene-graph-dirty flags all
        // propagate before this frame's sync. Without this, setX() calls
        // above queue events that fire AFTER control->sync() reads the
        // item state, and the first frame always renders the prior value.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        control->polishItems();
        control->beginFrame();
        control->sync();
        control->render();

        const QImage flipped = captureFrame(control.get(), rhi, colorTex, physicalSize, i);
        if (flipped.isNull()) {
            return 1;
        }
        // Hand the unscaled frame at --resolution to the sink; the sink's
        // writeFrame() resizes to --output-size in one pass (PNG and ffmpeg
        // sinks both honour it). Doing the resize here would double-scale.
        if (!opts.sink->writeFrame(flipped)) {
            qCWarning(lcRenderer) << "sink rejected frame" << i;
            return 1;
        }
    }

    if (!opts.sink->finalize()) {
        qCWarning(lcRenderer) << "sink failed to finalize";
        return 1;
    }

    qCInfo(lcRenderer) << "rendered" << opts.frameCount << "frames";
    return 0;
}

} // namespace PlasmaZones::ShaderRender

#include "renderer.moc"
