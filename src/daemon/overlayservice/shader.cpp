// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorSurfaces/SurfaceManager.h>
#include "../../core/logging.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../../core/utils.h"
#include "../../core/shaderregistry.h"
#include "../rendering/zonelabeltexturebuilder.h"
#include "pz_roles.h"

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QMutexLocker>
#include <QTimer>
#include <QImage>
#include <QGuiApplication>
#include <QPalette>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

// Parse shader params from JSON object. Returns empty map on parse error or invalid format.
// Logs parse errors on failure.
QVariantMap parseShaderParamsJson(const QString& json, const char* context)
{
    QVariantMap shaderParams;
    if (json.isEmpty()) {
        return shaderParams;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcOverlay) << context << "invalid shader params JSON:" << parseError.errorString();
        return shaderParams;
    }
    if (!doc.isObject()) {
        qCWarning(lcOverlay) << context << "shader params JSON is not an object";
        return shaderParams;
    }
    const QJsonObject o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        shaderParams.insert(it.key(), it.value().toVariant());
    }
    return shaderParams;
}

// parseZonesJson is defined in overlayservice_internal.h (shared inline)

// Build labels texture for shader preview zones. Uses settings for font when available.
QImage buildLabelsImageForPreviewZones(const QVariantList& zones, const QSize& size,
                                       const IZoneVisualizationSettings* settings)
{
    const LabelFontSettings lfs = extractLabelFontSettings(settings);
    QImage labelsImage = ZoneLabelTextureBuilder::build(zones, size, lfs.fontColor, true, lfs.backgroundColor,
                                                        lfs.fontFamily, lfs.fontSizeScale, lfs.fontWeight,
                                                        lfs.fontItalic, lfs.fontUnderline, lfs.fontStrikeout);
    if (labelsImage.isNull()) {
        labelsImage = QImage(1, 1, QImage::Format_ARGB32);
        labelsImage.fill(Qt::transparent);
    }
    return labelsImage;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Support Methods
// ═══════════════════════════════════════════════════════════════════════════════

bool OverlayService::canUseShaders() const
{
#ifdef PLASMAZONES_SHADERS_ENABLED
    return m_shaderRegistry && m_shaderRegistry->shadersEnabled();
#else
    return false;
#endif
}

bool OverlayService::useShaderForScreen(QScreen* screen) const
{
    if (!screen) {
        return false;
    }
    // Resolve to virtual screen ID when the physical screen has subdivisions,
    // so shader-type checks use the correct per-virtual-screen layout.
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    auto* mgr = m_screenManager;
    if (mgr && mgr->hasVirtualScreens(physId)) {
        // Check all virtual screens — if any uses a shader, return true.
        // This is used by initializeOverlay which creates per-virtual-screen windows.
        const QStringList vsIds = mgr->virtualScreenIdsFor(physId);
        for (const QString& vsId : vsIds) {
            if (useShaderForScreen(vsId)) {
                return true;
            }
        }
        return false;
    }
    return useShaderForScreen(physId);
}

bool OverlayService::anyScreenUsesShader() const
{
    if (!canUseShaders()) {
        return false;
    }
    if (m_settings && !m_settings->enableShaderEffects()) {
        return false;
    }
    for (const QString& screenId : m_screenStates.keys()) {
        if (useShaderForScreen(screenId)) {
            return true;
        }
    }
    return false;
}

bool OverlayService::useShaderForScreen(const QString& screenId) const
{
    if (!canUseShaders()) {
        return false;
    }
    if (m_settings && !m_settings->enableShaderEffects()) {
        return false;
    }
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);
    if (!screenLayout) {
        return false;
    }
    if (ShaderRegistry::isNoneShader(screenLayout->shaderId())) {
        return false;
    }

    // LayoutPreview mode requires standard QML overlay (ZonePreview can't be rendered in GLSL).
    // If any zone resolves to LayoutPreview mode, fall back to standard overlay for this screen.
    int globalMode = m_settings ? static_cast<int>(m_settings->overlayDisplayMode()) : 0;
    int layoutMode = screenLayout->overlayDisplayMode();
    for (const auto* zone : screenLayout->zones()) {
        int resolved =
            zone->overlayDisplayMode() >= 0 ? zone->overlayDisplayMode() : (layoutMode >= 0 ? layoutMode : globalMode);
        if (resolved == 1) { // OverlayDisplayMode::LayoutPreview
            return false;
        }
    }

    return m_shaderRegistry && m_shaderRegistry->shader(screenLayout->shaderId()).isValid();
}

void OverlayService::startShaderAnimation()
{
    if (!m_shaderUpdateTimer) {
        m_shaderUpdateTimer = new QTimer(this);
        m_shaderUpdateTimer->setTimerType(Qt::PreciseTimer);
        connect(m_shaderUpdateTimer, &QTimer::timeout, this, &OverlayService::updateShaderUniforms);
    }

    // Get frame rate from settings (default 60fps, bounded 30-144)
    const int frameRate = qBound(30, m_settings ? m_settings->shaderFrameRate() : 60, 144);
    // Use qRound for more accurate frame timing (e.g., 60fps -> 17ms not 16ms)
    const int interval = qRound(1000.0 / frameRate);
    m_shaderUpdateTimer->start(interval);

    // CAVA runs independently (started in setSettings / enableAudioVisualizerChanged).
    // Just sync config in case frame rate changed since CAVA was started.
    if (m_audioProvider && m_audioProvider->isRunning() && m_settings) {
        m_audioProvider->setFramerate(frameRate);
    }

    qCDebug(lcOverlay) << "Shader animation started at" << (1000 / interval) << "fps";
}

void OverlayService::stopShaderAnimation()
{
    // Don't stop CAVA here — it stays warm for instant audio data on next show().
    // Just clear the spectrum from overlay windows so they don't render stale data.
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* window = it_.value().overlayWindow;
        if (window) {
            writeQmlProperty(window, QStringLiteral("audioSpectrum"), QVariantList());
        }
    }
    if (m_shaderPreviewWindow) {
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("audioSpectrum"), QVariantList());
    }
    if (m_shaderUpdateTimer) {
        m_shaderUpdateTimer->stop();
        qCDebug(lcOverlay) << "Shader animation stopped";
    }
}

void OverlayService::onAudioSpectrumUpdated(const QVector<float>& spectrum)
{
    // Pass QVector<float> wrapped in QVariant to avoid per-element QVariant boxing.
    // ZoneShaderItem::setAudioSpectrum() detects and unwraps QVector<float> directly.
    const QVariant wrapped = QVariant::fromValue(spectrum);
    for (auto it = m_screenStates.cbegin(); it != m_screenStates.cend(); ++it) {
        auto* window = it.value().overlayWindow;
        if (window && useShaderForScreen(it.key())) {
            writeQmlProperty(window, QStringLiteral("audioSpectrum"), wrapped);
        }
    }
    // Shader preview (editor dialog) when visible and audio viz enabled
    if (m_shaderPreviewWindow && m_shaderPreviewWindow->isVisible() && m_settings
        && m_settings->enableAudioVisualizer()) {
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("audioSpectrum"), wrapped);
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

    // Keep iTime as double through the whole pipeline. ZoneShaderNodeRhi splits
    // it into iTime (wrapped) + iTimeHi at the final GPU upload — see
    // kShaderTimeWrap. Casting to float32 here would requantize the counter
    // before the wrap, reintroducing the freezing bug at long uptimes.
    const double iTime = static_cast<double>(currentTime) / 1000.0;

    // Calculate delta time with clamp (max 100ms prevents jumps after sleep/resume)
    constexpr float maxDelta = 0.1f;
    const qint64 lastTime = m_lastFrameTime.exchange(currentTime);
    float iTimeDelta = qMin(static_cast<float>(currentTime - lastTime) / 1000.0f, maxDelta);

    // Prevent frame counter overflow (reset at 1 billion, ~193 days at 60fps)
    int frame = m_frameCount.fetch_add(1);
    if (frame > 1000000000) {
        m_frameCount.store(0);
    }

    // Update zone data for shaders if dirty (highlight changed, layout changed, etc.)
    if (m_zoneDataDirty) {
        updateZonesForAllWindows();
    }

    // Update visible shader overlay windows with synchronized time.
    // Skip hidden non-shader windows kept alive across hide/show cycles —
    // they don't have iTime/iFrame properties and writing to hidden windows
    // is wasted work (60Hz property writes silently dropped by QML).
    for (auto it = m_screenStates.cbegin(); it != m_screenStates.cend(); ++it) {
        auto* window = it.value().overlayWindow;
        if (window && window->isVisible()) {
            writeQmlProperty(window, QStringLiteral("iTime"), static_cast<qreal>(iTime));
            writeQmlProperty(window, QStringLiteral("iTimeDelta"), static_cast<qreal>(iTimeDelta));
            writeQmlProperty(window, QStringLiteral("iFrame"), frame);
        }
    }
    // Update shader preview overlay (editor dialog) when visible
    if (m_shaderPreviewWindow && m_shaderPreviewWindow->isVisible()) {
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("iTime"), static_cast<qreal>(iTime));
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("iTimeDelta"), static_cast<qreal>(iTimeDelta));
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("iFrame"), frame);
    }
}

void OverlayService::showShaderPreview(int x, int y, int width, int height, const QString& screenId,
                                       const QString& shaderId, const QString& shaderParamsJson,
                                       const QString& zonesJson)
{
    if (width <= 0 || height <= 0) {
        qCWarning(lcOverlay) << "showShaderPreview: invalid size" << width << "x" << height;
        return;
    }
    if (ShaderRegistry::isNoneShader(shaderId)) {
        hideShaderPreview();
        return;
    }

    QScreen* screen = nullptr;
    if (!screenId.isEmpty()) {
        screen = resolveTargetScreen(m_screenManager, screenId);
    }
    if (!screen) {
        screen = Utils::findScreenAtPosition(x, y);
    }
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        qCWarning(lcOverlay) << "showShaderPreview: no screen available";
        return;
    }

    auto* registry = m_shaderRegistry;
    if (!registry || !registry->shadersEnabled()) {
        qCDebug(lcOverlay) << "showShaderPreview: shaders not available";
        return;
    }

    const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
    if (!info.isValid()) {
        qCWarning(lcOverlay) << "showShaderPreview: shader not found:" << shaderId;
        return;
    }

    const QVariantList zones = parseZonesJson(zonesJson, "showShaderPreview:");
    // Params arrive already translated to uniform names by the editor
    // (via EditorController::translateShaderParams → D-Bus translateParamsToUniforms).
    // Do NOT re-translate here — the keys are already uniform names like "customParams1_x".
    const QVariantMap shaderParams = parseShaderParamsJson(shaderParamsJson, "showShaderPreview:");

    if (!m_shaderPreviewWindow || m_shaderPreviewScreen != screen) {
        destroyShaderPreviewWindow();
        createShaderPreviewWindow(screen, screenId);
    }

    if (!m_shaderPreviewWindow) {
        return;
    }

    m_shaderPreviewScreen = screen;
    m_shaderPreviewShaderId = shaderId;
    m_shaderPreviewScreenId = screenId;

    auto* handle = m_shaderPreviewSurface ? m_shaderPreviewSurface->transport() : nullptr;
    if (!handle) {
        qCWarning(lcOverlay) << "showShaderPreview: no transport handle for preview surface"
                             << "— layer-shell may have been lost (compositor restart?)."
                             << "Destroying and recreating the window.";
        destroyShaderPreviewWindow();
        createShaderPreviewWindow(screen, screenId);
        if (!m_shaderPreviewSurface || !m_shaderPreviewWindow) {
            // Belt-and-braces: createShaderPreviewWindow sets both fields
            // atomically, but a future refactor that split them could leave
            // a non-null surface with a null window — `setWidth` on a null
            // window would crash. Guard both.
            return;
        }
        handle = m_shaderPreviewSurface->transport();
        if (!handle) {
            qCWarning(lcOverlay) << "showShaderPreview: recreated surface still has no transport — aborting";
            return;
        }
    }

    {
        // For virtual screens, margins are relative to the physical screen origin,
        // not the virtual screen origin (LayerShell positions within the physical output).
        const auto placement = layerPlacementAt(QPoint(x, y), screen->geometry());
        handle->setAnchors(placement.anchors);
        handle->setMargins(placement.margins);
    }

    // Set window size — position is controlled by layer-surface anchors + margins,
    // not by setX/setY which are no-ops on layer surfaces.
    m_shaderPreviewWindow->setWidth(width);
    m_shaderPreviewWindow->setHeight(height);

    // Shader properties — set all auxiliary props BEFORE shaderSource,
    // because setShaderSource() emits statusChanged() which cascades
    // through QML bindings and can trigger visibility changes before
    // buffer paths / zones / params are ready.
    // Note: applyShaderInfoToWindow sets shaderSource LAST internally.
    // We must set zones/labels BEFORE calling it so they're ready when
    // statusChanged fires. Set zones first, then call the helper.
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("zones"), zones);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("zoneCount"), zones.size());
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("highlightedCount"), 0);

    const QSize size(qMax(1, width), qMax(1, height));
    const QImage labelsImage = buildLabelsImageForPreviewZones(zones, size, m_settings);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("labelsTexture"), QVariant::fromValue(labelsImage));

    // applyShaderInfoToWindow sets shaderSource LAST (triggers statusChanged cascade).
    // Pass the preview's sub-rect + the containing physical screen so a
    // wallpaper-consuming shader samples the portion of the wallpaper that
    // actually sits behind the preview rect — mirrors the VS-cropping path
    // in overlay.cpp so preview and live overlay agree pixel-for-pixel.
    const QRect previewSubGeom(x, y, width, height);
    const QRect previewPhysGeom = screen->geometry();
    applyShaderInfoToWindow(m_shaderPreviewWindow, info, shaderParams, previewSubGeom, previewPhysGeom);

    // Start iTime animation for preview (shared timer with main overlay)
    // Must start m_shaderTimer - updateShaderUniforms() uses it and returns early if invalid
    ensureShaderTimerStarted(m_shaderTimer, m_shaderTimerMutex, m_lastFrameTime, m_frameCount);
    startShaderAnimation();

    m_shaderPreviewWindow->show();
    qCDebug(lcOverlay) << "showShaderPreview: x=" << x << "y=" << y << "size=" << width << "x" << height
                       << "shader=" << shaderId << "zones=" << zones.size();
}

void OverlayService::updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                                         const QString& zonesJson)
{
    if (!m_shaderPreviewWindow) {
        return;
    }

    if (width > 0 && height > 0) {
        QScreen* screen = m_shaderPreviewWindow->screen();
        if (screen) {
            // Size only — position is controlled by layer-surface anchors + margins.
            m_shaderPreviewWindow->setWidth(width);
            m_shaderPreviewWindow->setHeight(height);
            if (auto* handle = m_shaderPreviewSurface ? m_shaderPreviewSurface->transport() : nullptr) {
                // Margins are relative to the physical screen origin (LayerShell).
                // Anchors were baked in at attach; only margins mutate here.
                handle->setMargins(layerPlacementAt(QPoint(x, y), screen->geometry()).margins);
            }
        }
    }

    if (!zonesJson.isEmpty()) {
        const QVariantList zones = parseZonesJson(zonesJson, "updateShaderPreview:");
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("zones"), zones);
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("zoneCount"), zones.size());

        const int w = qMax(1, m_shaderPreviewWindow->width());
        const int h = qMax(1, m_shaderPreviewWindow->height());
        const QImage labelsImage = buildLabelsImageForPreviewZones(zones, QSize(w, h), m_settings);
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("labelsTexture"), QVariant::fromValue(labelsImage));
    }

    if (!shaderParamsJson.isEmpty()) {
        // Params arrive already translated to uniform names by the editor
        const QVariantMap shaderParams = parseShaderParamsJson(shaderParamsJson, "updateShaderPreview:");
        writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("shaderParams"), QVariant::fromValue(shaderParams));
    }
}

void OverlayService::hideShaderPreview()
{
    destroyShaderPreviewWindow();
}

void OverlayService::createShaderPreviewWindow(QScreen* screen, const QString& screenId)
{
    if (m_shaderPreviewSurface) {
        return;
    }

    QImage placeholder(1, 1, QImage::Format_ARGB32);
    placeholder.fill(Qt::transparent);
    QVariantMap initProps;
    initProps.insert(QStringLiteral("labelsTexture"), QVariant::fromValue(placeholder));
    initProps.insert(QStringLiteral("isShaderOverlay"), true);

    // Unique-per-instance scope to avoid compositor-side rate limiting when the
    // editor rapidly opens/closes the Shader Settings dialog. Routes through
    // makePerInstanceRole so the per-instance prefix is guaranteed by
    // construction to start with PzRoles::ShaderPreview's base — even though
    // the SurfaceAnimator deliberately doesn't register a config for this
    // role (editor-controlled imperative show/hide), keeping construction
    // uniform across every per-instance role keeps a future migration cheap.
    const QString scopeId = screenId.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(screen) : screenId;
    const auto role =
        PzRoles::makePerInstanceRole(PzRoles::ShaderPreview, scopeId, m_surfaceManager->nextScopeGeneration());

    auto* surface = createLayerSurface({.qmlUrl = QUrl(QStringLiteral("qrc:/ui/RenderNodeOverlay.qml")),
                                        .screen = screen,
                                        .role = role,
                                        .windowType = "shader preview overlay",
                                        .windowProperties = initProps});
    if (!surface) {
        return;
    }

    m_shaderPreviewSurface = surface;
    m_shaderPreviewWindow = surface->window();
    m_shaderPreviewScreen = screen;
    // Surface starts in State::Hidden (warmed) — caller flips visible later.
}

void OverlayService::destroyShaderPreviewWindow()
{
    if (m_shaderPreviewSurface) {
        // Disconnect so no signals (e.g. geometryChanged) are delivered to a window we're tearing down.
        if (m_shaderPreviewScreen && m_shaderPreviewWindow) {
            disconnect(m_shaderPreviewScreen, nullptr, m_shaderPreviewWindow, nullptr);
        }
        m_shaderPreviewSurface->deleteLater();
        m_shaderPreviewSurface = nullptr;
        m_shaderPreviewWindow = nullptr;
    }
    m_shaderPreviewScreen = nullptr;
    m_shaderPreviewShaderId.clear();
    m_shaderPreviewScreenId.clear();
    // Stop shader timer only if main overlay is also not visible
    if (!m_visible && m_shaderUpdateTimer && m_shaderUpdateTimer->isActive()) {
        stopShaderAnimation();
    }
}

} // namespace PlasmaZones
