// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationpreviewcontroller.h"

#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "core/types/cavaoptions.h"
#include "phosphor_i18n.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderItemConfig.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/AnimationUniformExtension.h>
#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QRectF>
#include <QVector2D>
#include <QVector4D>

#include <QGuiApplication>

namespace PlasmaZones {

namespace {

/// The stand-in scene canvas. One fixed logical size for every composed
/// image: the shaders only ever see it through a sampler, so the pane's
/// own size never enters into it.
constexpr QSize kSceneSize(1024, 576);

/// Paint a faux window mirroring PZCommon.DecorationPreviewCard's anatomy
/// (the same ratios: 0.2 title bar with a bold caption, four text rules at
/// 0.055 height and 35% text colour, the saturated accent block), so the
/// composed scene textures — the desktop endpoints and the strip columns —
/// show the SAME stand-in window the live card classes stage. Two subjects
/// that read as different applications made the previews look unrelated.
/// @p dimmed marks an outgoing subject (the old tab) so the two sides of a
/// cross-fade stay tellable apart mid-blend.
void paintFauxWindow(QPainter& p, const QRectF& r, const QString& title,
                     const AnimationPreviewController::SceneColors& colors, bool dimmed = false)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    // Metric base: the height the card WOULD have at its native 22:14
    // aspect for this width, clamped to the actual height. Sizing details
    // off the raw height made a wide-but-short desktop window's bar and
    // rules visibly chunkier than the live card's; off this base, any
    // aspect keeps the card's element scale.
    const qreal base = qMin(r.height(), r.width() * 14.0 / 22.0);
    const qreal rad = qMax(2.0, base * 0.03);
    // Colours come from the live card's own Kirigami roles (handed over by
    // the pane via setSceneColors); the QPalette forms below are only the
    // degraded fallback for a host that never called it, and the accent's
    // fallback is Breeze's positive green since QPalette has no such role.
    const QPalette pal = QGuiApplication::palette();
    QColor body = colors.body.isValid() ? colors.body : pal.color(QPalette::Active, QPalette::Window);
    QColor bar = colors.titleBar.isValid() ? colors.titleBar : pal.color(QPalette::Active, QPalette::Highlight);
    const QColor titleText =
        colors.titleText.isValid() ? colors.titleText : pal.color(QPalette::Active, QPalette::HighlightedText);
    QColor rule = colors.rule.isValid() ? colors.rule : pal.color(QPalette::Active, QPalette::WindowText);
    QColor accent = colors.accent.isValid() ? colors.accent : QColor(39, 174, 96);
    if (dimmed) {
        body = body.darker(112);
        bar = bar.darker(125);
        accent = accent.darker(125);
    }
    p.setBrush(body);
    p.drawRoundedRect(r, rad, rad);
    // Title bar — rounded top corners, square bottom edge, like the card's.
    const QRectF barRect(r.x(), r.y(), r.width(), qMax(2.0, base * 0.2));
    p.setBrush(bar);
    p.drawRoundedRect(barRect, rad, rad);
    p.drawRect(barRect.adjusted(0, barRect.height() / 2.0, 0, 0));
    // Caption, with the card's same drop-out rule below a legible bar.
    if (barRect.height() >= 10.0 && !title.isEmpty()) {
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(qMax(4, qRound(barRect.height() * 0.5)));
        p.setFont(f);
        p.setPen(titleText);
        const qreal inset = barRect.height() * 0.4;
        p.drawText(barRect.adjusted(inset, 0.0, -inset, 0.0), Qt::AlignVCenter | Qt::AlignLeft, title);
        p.setPen(Qt::NoPen);
    }
    // Text rules and accent block, at the card's proportions.
    const qreal margin = base * 0.06;
    const qreal gap = base * 0.05;
    const qreal contentW = r.width() - 2.0 * margin;
    rule.setAlphaF(0.35f);
    qreal y = barRect.bottom() + margin;
    for (int i = 0; i < 4; ++i) {
        const qreal h = qMax(1.0, base * 0.055);
        const QRectF line(r.x() + margin, y, contentW * (i % 2 == 0 ? 0.92 : 0.64), h);
        if (line.bottom() > r.bottom() - margin) {
            break;
        }
        p.setBrush(rule);
        p.drawRoundedRect(line, h / 2.0, h / 2.0);
        y += h + gap;
    }
    const qreal accentH = qMax(2.0, base * 0.16);
    if (y + accentH <= r.bottom() - margin * 0.5) {
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(r.x() + margin, y, contentW * 0.45, accentH), accentH * 0.15, accentH * 0.15);
    }
}

/// The wallpaper scaled onto the scene canvas, or a flat dark ground when
/// no wallpaper resolves — the same degraded backdrop the QML panes show.
QImage sceneGround()
{
    QImage ground(kSceneSize, QImage::Format_RGBA8888);
    const QImage wp = PhosphorShaders::ShaderRegistry::loadWallpaperImage();
    if (wp.isNull()) {
        ground.fill(QColor(40, 42, 46));
        return ground;
    }
    QPainter p(&ground);
    p.drawImage(ground.rect(), wp);
    return ground;
}

/// Resolve @p packId to a previewable effect: registered and valid. Also
/// fills @p includePaths with each search path's
/// `shared/` dir and falls back to the shared `animation.vert` when the pack
/// declares no vertex shader — the exact resolution SurfaceAnimator's runLeg
/// performs before attaching a real leg, so the preview compiles against the
/// same sources a daemon transition would.
bool resolvePreviewEffect(PhosphorAnimationShaders::AnimationShaderRegistry* registry, const QString& packId,
                          PhosphorAnimationShaders::AnimationShaderEffect& effect, QStringList& includePaths)
{
    if (!registry || packId.isEmpty() || !registry->hasEffect(packId)) {
        return false;
    }
    effect = registry->effect(packId);
    if (!effect.isValid()) {
        return false;
    }
    const QStringList searchPaths = registry->searchPaths();
    for (const QString& sp : searchPaths) {
        const QString sharedDir = sp + QStringLiteral("/shared");
        if (QDir(sharedDir).exists()) {
            includePaths.append(sharedDir);
            if (effect.vertexShaderPath.isEmpty()) {
                const QString sharedVert = sharedDir + QStringLiteral("/animation.vert");
                if (QFile::exists(sharedVert)) {
                    effect.vertexShaderPath = sharedVert;
                }
            }
        }
    }
    return true;
}

} // namespace

AnimationPreviewController::AnimationPreviewController(PhosphorAnimationShaders::AnimationShaderRegistry* registry,
                                                       ISettings* settings, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
    , m_settings(settings)
{
    // A pack installed or edited while a preview is open must recompose it;
    // the pane's invokable-based bindings key on previewRevision for that.
    // Unlike the decoration controller there is no palette watch: animation
    // parameters are used as declared, with no theme-colour resolution step.
    if (m_registry) {
        connect(m_registry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
                &AnimationPreviewController::bumpPreviewRevision);
    }
    if (m_settings) {
        // Same standing-request dance as the decoration controller: follow
        // the visualizer setting while a host wants capture, and republish
        // the property first so the pane's notice updates either way.
        connect(m_settings, &ISettings::enableAudioVisualizerChanged, this, [this]() {
            Q_EMIT audioVisualizerEnabledChanged();
            if (!m_captureRequested) {
                return;
            }
            if (audioVisualizerEnabled()) {
                startAudioCapture();
            } else {
                stopCapture();
            }
        });
    }
}

AnimationPreviewController::~AnimationPreviewController()
{
    // Halt the external CAVA process directly rather than leaving it to the
    // provider's destruction: no signal emission while being torn down.
    if (m_audio && m_audio->isRunning()) {
        m_audio->stop();
    }
}

void AnimationPreviewController::bumpPreviewRevision()
{
    ++m_previewRevision;
    Q_EMIT previewRevisionChanged();
}

QVariantMap AnimationPreviewController::packInfo(const QString& packId) const
{
    QVariantMap info;
    if (!m_registry || packId.isEmpty() || !m_registry->hasEffect(packId)) {
        return info;
    }
    const PhosphorAnimationShaders::AnimationShaderEffect effect = m_registry->effect(packId);
    info.insert(QStringLiteral("valid"), effect.isValid());
    info.insert(QStringLiteral("audio"), effect.useAudio);
    info.insert(QStringLiteral("fboExtentSurface"),
                effect.fboExtentKind == PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Surface);
    // The subject class the pane should stage. An appearance-capable pack
    // (appliesTo includes "appearance", or a universal empty appliesTo)
    // previews the appearance leg — that is the leg the daemon actually
    // runs it for. Otherwise the first declared transition class wins.
    QString eventClass = QStringLiteral("appearance");
    const QStringList& classes = effect.appliesTo;
    if (!classes.isEmpty() && !classes.contains(QLatin1String("appearance"))) {
        for (const char* cls : {"desktop", "strip", "tab", "geometry", "move"}) {
            if (classes.contains(QLatin1String(cls))) {
                eventClass = QLatin1String(cls);
                break;
            }
        }
    }
    info.insert(QStringLiteral("eventClass"), eventClass);
    return info;
}

bool AnimationPreviewController::configurePreviewItem(QQuickItem* item, const QString& packId,
                                                      const QVariantMap& friendlyParams) const
{
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem) {
        return false;
    }
    PhosphorAnimationShaders::AnimationShaderEffect effect;
    QStringList includePaths;
    if (!resolvePreviewEffect(m_registry, packId, effect, includePaths)) {
        return false;
    }
    // Extension BEFORE static config, mirroring attachShaderToAnchor's
    // ordering rationale: the first prepare() must allocate the UBO with the
    // extension's trailing bytes or the shader reads garbage past
    // sizeof(BaseUniforms) until the next allocation cycle.
    shaderItem->setUniformExtension(std::make_shared<PhosphorAnimation::AnimationUniformExtension>());
    PhosphorAnimationLayer::applyEffectStaticConfig(shaderItem, effect, includePaths);
    // Show-leg seed. The pane's clock drives iTime 0→1 (show) then 1→0 with
    // isReversed flipped (hide), the exact per-leg drive a real transition
    // gets; seeding the show start keeps the first paint from flashing an
    // end-state frame.
    shaderItem->setITime(0.0);
    shaderItem->setIsReversed(false);
    updatePreviewParams(item, packId, friendlyParams);
    return true;
}

void AnimationPreviewController::updatePreviewParams(QQuickItem* item, const QString& packId,
                                                     const QVariantMap& friendlyParams) const
{
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem || !m_registry || packId.isEmpty() || !m_registry->hasEffect(packId)) {
        return;
    }
    const PhosphorAnimationShaders::AnimationShaderEffect effect = m_registry->effect(packId);
    if (!effect.isValid()) {
        return;
    }
    const QVariantMap translated =
        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(effect, friendlyParams);
    if (!translated.isEmpty()) {
        shaderItem->setShaderParams(translated);
    }
}

void AnimationPreviewController::syncPreviewGeometry(QQuickItem* item, bool surfaceExtent, qreal cardX, qreal cardY,
                                                     qreal cardW, qreal cardH, qreal fieldW, qreal fieldH,
                                                     bool paddedCapture) const
{
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem || cardW <= 0.0 || cardH <= 0.0) {
        return;
    }
    // iResolution AFTER the pane's width/height bindings have run for this
    // geometry (the pane calls this from geometry-change handlers), so it
    // wins over ShaderEffect::geometryChange's auto-reset to item bounds —
    // the same ordering syncShaderGeometryNow relies on. Anchor extent
    // reports the card, surface extent the field, matching production.
    shaderItem->setIResolution(surfaceExtent ? QSizeF(fieldW, fieldH) : QSizeF(cardW, cardH));
    const auto ext =
        std::dynamic_pointer_cast<PhosphorAnimation::AnimationUniformExtension>(shaderItem->uniformExtension());
    if (!ext) {
        return;
    }
    ext->setIAnchorSize(QSizeF(cardW, cardH));
    // Texture rect: identity when the capture holds the bare card, the
    // card's sub-rect when the capture is the padded canvas (the geometry /
    // move framing — the same fold kwin's padded capture carries), so
    // surfaceColor / anchorRemap resolve card-space samples on both
    // framings. Mirrors syncShaderGeometryNow's shaderContentRect math.
    if (paddedCapture && fieldW > 0.0 && fieldH > 0.0) {
        ext->setIAnchorRectInTexture(QVector4D(static_cast<float>(cardX / fieldW), static_cast<float>(cardY / fieldH),
                                               static_cast<float>(cardW / fieldW), static_cast<float>(cardH / fieldH)));
    } else {
        ext->setIAnchorRectInTexture(QVector4D(0.0f, 0.0f, 1.0f, 1.0f));
    }
    ext->setIAnchorPosInFbo(surfaceExtent ? QPointF(cardX, cardY) : QPointF(0.0, 0.0));
    // The preview field stands in for the wl_surface: the card's field-local
    // position plus the field's size, so a fly-in's closest-edge math and a
    // slide's clearance run against the pane the user is looking at rather
    // than the whole settings window.
    if (fieldW > 0.0 && fieldH > 0.0) {
        ext->setISurfaceScreenPos(QVector4D(static_cast<float>(cardX), static_cast<float>(cardY),
                                            static_cast<float>(fieldW), static_cast<float>(fieldH)));
    }
}

void AnimationPreviewController::driveTransitionState(QQuickItem* item, const QVariantMap& state) const
{
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem) {
        return;
    }
    const auto ext =
        std::dynamic_pointer_cast<PhosphorAnimation::AnimationUniformExtension>(shaderItem->uniformExtension());
    if (!ext) {
        return;
    }
    const auto vec4At = [&state](const char* key, bool* ok) -> QVector4D {
        const auto it = state.constFind(QLatin1String(key));
        *ok = it != state.constEnd();
        return *ok ? it->value<QVector4D>() : QVector4D();
    };
    const auto rectAsVec4 = [&state](const char* key, bool* ok) -> QVector4D {
        const auto it = state.constFind(QLatin1String(key));
        *ok = it != state.constEnd();
        if (!*ok) {
            return {};
        }
        const QRectF r = it->toRectF();
        return QVector4D(static_cast<float>(r.x()), static_cast<float>(r.y()), static_cast<float>(r.width()),
                         static_cast<float>(r.height()));
    };
    bool has = false;
    QVector4D v = vec4At("switchDelta", &has);
    if (has) {
        ext->setISwitchDelta(v);
    }
    v = vec4At("stripMotion", &has);
    if (has) {
        ext->setIStripMotion(v);
    }
    v = rectAsVec4("stripRect", &has);
    if (has) {
        ext->setIStripRect(v);
    }
    v = rectAsVec4("fromRect", &has);
    if (has) {
        ext->setIFromRect(v);
    }
    v = rectAsVec4("toRect", &has);
    if (has) {
        ext->setIToRect(v);
    }
    if (const auto it = state.constFind(QLatin1String("stripAxis")); it != state.constEnd()) {
        ext->setIStripAxis(it->value<QVector2D>());
    }
    if (const auto it = state.constFind(QLatin1String("oldWindowOpacity")); it != state.constEnd()) {
        ext->setIOldWindowOpacity(static_cast<float>(it->toDouble()));
    }
    if (const auto it = state.constFind(QLatin1String("hasOldWindow")); it != state.constEnd()) {
        ext->setIHasOldWindow(it->toBool());
    }
    v = rectAsVec4("iconRect", &has);
    if (has) {
        ext->setIIconRect(v);
    }
}

void AnimationPreviewController::driveMoveState(QQuickItem* item, qreal x, qreal y, qreal w, qreal h, qreal dtMs)
{
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem) {
        return;
    }
    const auto ext =
        std::dynamic_pointer_cast<PhosphorAnimation::AnimationUniformExtension>(shaderItem->uniformExtension());
    if (!ext) {
        return;
    }
    const QRectF frame(x, y, w, h);
    // (Re)initialise on the first call for an item, on a subject jump
    // (loop wrap teleports the card back to its start), or when the pane
    // rebuilt its shader item.
    const QPointF origin = frame.topLeft();
    const bool jumped =
        !m_moveSimItem || (origin - m_moveLastOrigin).manhattanLength() > qMax(frame.width(), frame.height());
    if (m_moveSimItem != item || jumped) {
        m_moveSimItem = item;
        // Grip at the titlebar's centre — where a real drag holds the
        // window — so the sheet hangs and swings below the grab the way it
        // does on screen; a centre grip lags symmetrically and reads as a
        // rigid slide.
        const QPointF grip(frame.center().x(), frame.top() + frame.height() * 0.06);
        ShaderInternal::initMeshSim(m_moveMesh, frame, grip, MeshSimParams{});
        m_moveTrail.fill(origin);
        m_moveTrailAccumMs = 0.0;
    }
    // Trail ring: same 15 ms step and newest-first order the compositor
    // records (ShaderTransition::kTrailStepMs); uploaded as offsets
    // against the current origin, exactly as paint_shader_window does.
    m_moveTrailAccumMs += qMax(0.0, dtMs);
    constexpr double kTrailStepMs = 15.0;
    while (m_moveTrailAccumMs >= kTrailStepMs) {
        m_moveTrailAccumMs -= kTrailStepMs;
        for (int i = int(m_moveTrail.size()) - 1; i > 0; --i) {
            m_moveTrail[i] = m_moveTrail[i - 1];
        }
        m_moveTrail[0] = origin;
    }
    QVector<QVector2D> trail;
    trail.reserve(int(m_moveTrail.size()));
    for (const QPointF& p : m_moveTrail) {
        trail.append(QVector2D(float(p.x() - origin.x()), float(p.y() - origin.y())));
    }
    ext->setIMoveTrail(trail);

    // Wobble lattice: the same spring integrator the compositor runs
    // (mesh_sim is a plain-Qt port of KWin wobblywindows), stepped with
    // the pane's frame delta, deflections uploaded per node.
    ShaderInternal::stepMeshSim(m_moveMesh, frame, qMax(0.0, dtMs));
    QVector<QVector2D> mesh;
    mesh.reserve(MeshSim::kCount);
    for (int n = 0; n < MeshSim::kCount; ++n) {
        const QPointF d = m_moveMesh.position[n] - m_moveMesh.origin[n];
        mesh.append(QVector2D(float(d.x()), float(d.y())));
    }
    ext->setIMoveMesh(mesh);
    m_moveLastOrigin = origin;
}

void AnimationPreviewController::bindClassTextures(QQuickItem* item, const QString& eventClass) const
{
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem) {
        return;
    }
    // Slot map mirrors the UBO-branch sampler aliases in the shared
    // transition includes: uFromDesktop=1, uToDesktop=2, uStrip=1,
    // uOldWindow=3.
    if (eventClass == QLatin1String("desktop")) {
        shaderItem->setUserTexture(1, desktopFromImage());
        shaderItem->setUserTexture(2, desktopToImage());
    } else if (eventClass == QLatin1String("strip")) {
        shaderItem->setUserTexture(1, stripSceneImage());
    } else if (eventClass == QLatin1String("tab") || eventClass == QLatin1String("geometry")
               || eventClass == QLatin1String("move")) {
        shaderItem->setUserTexture(3, oldWindowImage());
    }
}

void AnimationPreviewController::setSceneColors(const QColor& titleBar, const QColor& titleText, const QColor& body,
                                                const QColor& rule, const QColor& accent)
{
    const SceneColors next{titleBar, titleText, body, rule, accent};
    if (next == m_sceneColors) {
        return;
    }
    m_sceneColors = next;
    m_desktopFromCache = QImage();
    m_desktopToCache = QImage();
    m_stripSceneCache = QImage();
    m_oldWindowCache = QImage();
}

QImage AnimationPreviewController::desktopFromImage() const
{
    // The "from" endpoint: the windows scene — two faux windows over the
    // wallpaper, both at the card's native 22:14 aspect so the subjects
    // match the live card exactly. Cached until the theme colours move;
    // the wallpaper resolver is a snapshot anyway (see the decoration
    // controller's wallpaper note).
    if (!m_desktopFromCache.isNull()) {
        return m_desktopFromCache;
    }
    QImage img = sceneGround();
    QPainter p(&img);
    const QSizeF s = img.size();
    const auto cardRect = [&s](qreal x, qreal y, qreal w) {
        return QRectF(s.width() * x, s.height() * y, s.width() * w, s.width() * w * 14.0 / 22.0);
    };
    paintFauxWindow(p, cardRect(0.08, 0.12, 0.46), PhosphorI18n::tr("Sample Window"), m_sceneColors);
    paintFauxWindow(p, cardRect(0.58, 0.34, 0.33), PhosphorI18n::tr("Another Window"), m_sceneColors);
    m_desktopFromCache = img;
    return m_desktopFromCache;
}

QImage AnimationPreviewController::desktopToImage() const
{
    // The "to" endpoint. A bare wallpaper is the PEEK contract's TO
    // exactly; for a switch it reads as arriving on an empty desktop,
    // which keeps the two endpoints visibly distinct in either event.
    if (m_desktopToCache.isNull()) {
        m_desktopToCache = sceneGround();
    }
    return m_desktopToCache;
}

QImage AnimationPreviewController::stripSceneImage() const
{
    // A scrolling-strip scene: a row of columns over the wallpaper, the
    // middle one widest — what a real capture of the strip layer holds.
    // Columns are tall by nature; the painter's 22:14-normalized metric
    // base keeps their chrome at card scale regardless.
    if (!m_stripSceneCache.isNull()) {
        return m_stripSceneCache;
    }
    QImage img = sceneGround();
    QPainter p(&img);
    const QSizeF s = img.size();
    const double topY = s.height() * 0.10;
    const double h = s.height() * 0.78;
    paintFauxWindow(p, QRectF(s.width() * 0.02, topY, s.width() * 0.24, h), QString(), m_sceneColors);
    paintFauxWindow(p, QRectF(s.width() * 0.28, topY, s.width() * 0.44, h), PhosphorI18n::tr("Sample Window"),
                    m_sceneColors);
    paintFauxWindow(p, QRectF(s.width() * 0.74, topY, s.width() * 0.24, h), QString(), m_sceneColors);
    m_stripSceneCache = img;
    return m_stripSceneCache;
}

QImage AnimationPreviewController::oldWindowImage() const
{
    // The outgoing side of a tab switch / geometry cross-fade: one faux
    // window filling the frame (oldColor samples it card-relative), in the
    // dimmed variant so old and new stay tellable apart mid-fade.
    if (!m_oldWindowCache.isNull()) {
        return m_oldWindowCache;
    }
    QImage img(kSceneSize, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    QPainter p(&img);
    paintFauxWindow(p, QRectF(QPointF(0, 0), QSizeF(img.size())), PhosphorI18n::tr("Previous Tab"), m_sceneColors,
                    /*dimmed=*/true);
    m_oldWindowCache = img;
    return m_oldWindowCache;
}

QString AnimationPreviewController::wallpaperPath() const
{
    return PhosphorShaders::ShaderRegistry::wallpaperPath();
}

bool AnimationPreviewController::audioVisualizerEnabled() const
{
    return m_settings && m_settings->enableAudioVisualizer();
}

QVariant AnimationPreviewController::audioSpectrumVariant() const
{
    return QVariant::fromValue(m_spectrum);
}

void AnimationPreviewController::startAudioCapture()
{
    // Remembered even when the request cannot be honoured right now
    // (visualizer off, CAVA missing), so the setting turning on later
    // resumes capture for a preview that is still open.
    m_captureRequested = true;
    if (m_audio && m_audio->isRunning()) {
        return;
    }
    if (!audioVisualizerEnabled()) {
        return;
    }
    if (!PhosphorAudio::CavaSpectrumProvider::isCavaInstalled()) {
        qCDebug(lcCore) << "Animation preview: CAVA not available, audio-reactive preview disabled";
        return;
    }
    if (!m_audio) {
        m_audio = std::make_unique<PhosphorAudio::CavaSpectrumProvider>();
        connect(m_audio.get(), &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated, this,
                [this](const QVector<float>& spectrum) {
                    // Unconditional emit — a capture-rate stream of float
                    // vectors, where comparing the whole vector to spare a
                    // signal costs more than it saves.
                    m_spectrum = spectrum;
                    Q_EMIT audioSpectrumChanged();
                });
    }
    m_audio->setOptions(m_settings ? cavaOptionsFromSettings(m_settings) : PhosphorAudio::SpectrumOptions{});
    m_audio->start();
}

void AnimationPreviewController::stopAudioCapture()
{
    m_captureRequested = false;
    stopCapture();
}

void AnimationPreviewController::stopCapture()
{
    if (m_audio && m_audio->isRunning()) {
        m_audio->stop();
    }
    if (!m_spectrum.isEmpty()) {
        m_spectrum.clear();
        Q_EMIT audioSpectrumChanged();
    }
}

} // namespace PlasmaZones
