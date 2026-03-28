// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../cavaservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/zone.h"
#include "../../core/utils.h"
#include "../../core/shaderregistry.h"
#include "../rendering/zonelabeltexturebuilder.h"
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlContext>
#include <QMutexLocker>
#include <QTimer>
#include <QImage>
#include <QGuiApplication>
#include <QPalette>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include "../../core/layersurface.h"

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
    const QColor labelFontColor = settings ? settings->labelFontColor() : QColor(Qt::white);
    QColor backgroundColor = Qt::black;
    if (settings) {
        backgroundColor = QGuiApplication::palette().color(QPalette::Active, QPalette::Base);
    }
    const QString fontFamily = settings ? settings->labelFontFamily() : QString();
    const qreal fontSizeScale = settings ? settings->labelFontSizeScale() : 1.0;
    const int fontWeight = settings ? settings->labelFontWeight() : QFont::Bold;
    const bool fontItalic = settings ? settings->labelFontItalic() : false;
    const bool fontUnderline = settings ? settings->labelFontUnderline() : false;
    const bool fontStrikeout = settings ? settings->labelFontStrikeout() : false;
    QImage labelsImage =
        ZoneLabelTextureBuilder::build(zones, size, labelFontColor, true, backgroundColor, fontFamily, fontSizeScale,
                                       fontWeight, fontItalic, fontUnderline, fontStrikeout);
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
    auto* registry = ShaderRegistry::instance();
    return registry && registry->shadersEnabled();
#else
    return false;
#endif
}

bool OverlayService::useShaderForScreen(QScreen* screen) const
{
    if (!canUseShaders()) {
        return false;
    }
    if (m_settings && !m_settings->enableShaderEffects()) {
        return false;
    }
    Layout* screenLayout = resolveScreenLayout(screen);
    if (!screenLayout || ShaderRegistry::isNoneShader(screenLayout->shaderId())) {
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

    auto* registry = ShaderRegistry::instance();
    return registry && registry->shader(screenLayout->shaderId()).isValid();
}

bool OverlayService::anyScreenUsesShader() const
{
    if (!canUseShaders()) {
        return false;
    }
    if (m_settings && !m_settings->enableShaderEffects()) {
        return false;
    }
    for (auto* screen : m_overlayWindows.keys()) {
        if (useShaderForScreen(screen)) {
            return true;
        }
    }
    return false;
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
    if (m_cavaService && m_cavaService->isRunning() && m_settings) {
        m_cavaService->setFramerate(frameRate);
    }

    qCDebug(lcOverlay) << "Shader animation started at" << (1000 / interval) << "fps";
}

void OverlayService::stopShaderAnimation()
{
    // Don't stop CAVA here — it stays warm for instant audio data on next show().
    // Just clear the spectrum from overlay windows so they don't render stale data.
    for (auto* window : std::as_const(m_overlayWindows)) {
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
    for (auto it = m_overlayWindows.cbegin(); it != m_overlayWindows.cend(); ++it) {
        auto* window = it.value();
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

    const float iTime = currentTime / 1000.0f;

    // Calculate delta time with clamp (max 100ms prevents jumps after sleep/resume)
    constexpr float maxDelta = 0.1f;
    const qint64 lastTime = m_lastFrameTime.exchange(currentTime);
    float iTimeDelta = qMin((currentTime - lastTime) / 1000.0f, maxDelta);

    // Prevent frame counter overflow (reset at 1 billion, ~193 days at 60fps)
    int frame = m_frameCount.fetch_add(1);
    if (frame > 1000000000) {
        m_frameCount.store(0);
    }

    // Update zone data for shaders if dirty (highlight changed, layout changed, etc.)
    if (m_zoneDataDirty) {
        updateZonesForAllWindows();
    }

    // Update ALL shader overlay windows with synchronized time
    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
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
        screen = Utils::findScreenByIdOrName(screenId);
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

    auto* registry = ShaderRegistry::instance();
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
        createShaderPreviewWindow(screen);
    }

    if (!m_shaderPreviewWindow) {
        return;
    }

    m_shaderPreviewScreen = screen;
    m_shaderPreviewShaderId = shaderId;
    m_shaderPreviewWindow->setScreen(screen);
    m_shaderPreviewWindow->setGeometry(x, y, width, height);

    if (auto* layerSurface = LayerSurface::get(m_shaderPreviewWindow)) {
        const QRect screenGeom = screen->geometry();
        const int localX = x - screenGeom.x();
        const int localY = y - screenGeom.y();
        layerSurface->setAnchors(LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorLeft));
        layerSurface->setMargins(QMargins(localX, localY, 0, 0));
    }

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

    // applyShaderInfoToWindow sets shaderSource LAST (triggers statusChanged cascade)
    applyShaderInfoToWindow(m_shaderPreviewWindow, info, shaderParams);

    // Start iTime animation for preview (shared timer with main overlay)
    // Must start m_shaderTimer - updateShaderUniforms() uses it and returns early if invalid
    {
        QMutexLocker locker(&m_shaderTimerMutex);
        if (!m_shaderTimer.isValid()) {
            m_shaderTimer.start();
            m_lastFrameTime.store(0);
            m_frameCount.store(0);
        }
    }
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
            m_shaderPreviewWindow->setGeometry(x, y, width, height);
            if (auto* layerSurface = LayerSurface::get(m_shaderPreviewWindow)) {
                const QRect screenGeom = screen->geometry();
                const int localX = x - screenGeom.x();
                const int localY = y - screenGeom.y();
                layerSurface->setMargins(QMargins(localX, localY, 0, 0));
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

void OverlayService::createShaderPreviewWindow(QScreen* screen)
{
    if (m_shaderPreviewWindow) {
        return;
    }

    m_engine->rootContext()->setContextProperty(QStringLiteral("overlayService"), this);

    QImage placeholder(1, 1, QImage::Format_ARGB32);
    placeholder.fill(Qt::transparent);
    QVariantMap initProps;
    initProps.insert(QStringLiteral("labelsTexture"), QVariant::fromValue(placeholder));

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/RenderNodeOverlay.qml")), screen,
                                   "shader preview overlay", initProps);
    if (!window) {
        qCWarning(lcOverlay) << "Failed to create shader preview overlay window";
        return;
    }

    window->setProperty("isShaderOverlay", true);

    if (auto* layerSurface = LayerSurface::get(window)) {
        layerSurface->setScreen(screen);
        layerSurface->setLayer(LayerSurface::LayerOverlay);
        layerSurface->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityNone);
        layerSurface->setScope(QStringLiteral("plasmazones-shader-preview"));
        layerSurface->setExclusiveZone(-1);
    }

    m_shaderPreviewWindow = window;
    m_shaderPreviewScreen = screen;
    window->setVisible(false);
}

void OverlayService::destroyShaderPreviewWindow()
{
    if (m_shaderPreviewWindow) {
        m_shaderPreviewWindow->close();
        m_shaderPreviewWindow->deleteLater();
        m_shaderPreviewWindow = nullptr;
    }
    m_shaderPreviewScreen = nullptr;
    m_shaderPreviewShaderId.clear();
    // Stop shader timer only if main overlay is also not visible
    if (!m_visible && m_shaderUpdateTimer && m_shaderUpdateTimer->isActive()) {
        stopShaderAnimation();
    }
}

} // namespace PlasmaZones
