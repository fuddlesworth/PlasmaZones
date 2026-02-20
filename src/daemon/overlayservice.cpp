// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice.h"
#include "cavaservice.h"
#include "windowthumbnailservice.h"
#include "../config/configdefaults.h"
#include "../core/zoneselectorlayout.h"

#include "../core/layout.h"
#include "../core/layoutmanager.h"
#include "../core/zone.h"
#include "../core/constants.h"
#include "../core/layoututils.h"
#include "../core/platform.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/utils.h"
#include "../core/shaderregistry.h"
#include "rendering/zonelabeltexturebuilder.h"

#include <KColorScheme>

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QQuickItem>
#include <QKeyEvent>
#include <QPointer>
#include <QTimer>
#include <QMutexLocker>
#include "../core/logging.h"
#include <KLocalizedContext>
#include <cmath>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <LayerShellQt/Window>
#include <LayerShellQt/Shell>

namespace PlasmaZones {

// Fallback config when ISettings* is null (e.g. during teardown).
// Uses ConfigDefaults to stay in sync with the .kcfg single source of truth.
static ZoneSelectorConfig defaultZoneSelectorConfig()
{
    return {
        ConfigDefaults::position(),
        ConfigDefaults::layoutMode(),
        ConfigDefaults::sizeMode(),
        ConfigDefaults::maxRows(),
        ConfigDefaults::previewWidth(),
        ConfigDefaults::previewHeight(),
        ConfigDefaults::previewLockAspect(),
        ConfigDefaults::gridColumns(),
        ConfigDefaults::triggerDistance()
    };
}

namespace {

// Parse zones from JSON array. Returns empty list on parse error or invalid format.
// Logs parse errors on failure.
QVariantList parseZonesJson(const QString& json, const char* context)
{
    QVariantList zones;
    if (json.isEmpty()) {
        return zones;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcOverlay) << context << "invalid zones JSON:" << parseError.errorString();
        return zones;
    }
    if (!doc.isArray()) {
        qCWarning(lcOverlay) << context << "zones JSON is not an array";
        return zones;
    }
    for (const QJsonValue& v : doc.array()) {
        if (v.isObject()) {
            QVariantMap m;
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it) {
                m.insert(it.key(), it.value().toVariant());
            }
            zones.append(m);
        }
    }
    return zones;
}

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

// Build labels texture for shader preview zones. Uses settings for font when available.
QImage buildLabelsImageForPreviewZones(const QVariantList& zones,
                                       const QSize& size,
                                       const IZoneVisualizationSettings* settings)
{
    const QColor labelFontColor = settings ? settings->labelFontColor() : QColor(Qt::white);
    QColor backgroundColor = Qt::black;
    if (settings) {
        KColorScheme scheme(QPalette::Active, KColorScheme::View);
        backgroundColor = scheme.background(KColorScheme::NormalBackground).color();
    }
    const QString fontFamily = settings ? settings->labelFontFamily() : QString();
    const qreal fontSizeScale = settings ? settings->labelFontSizeScale() : 1.0;
    const int fontWeight = settings ? settings->labelFontWeight() : QFont::Bold;
    const bool fontItalic = settings ? settings->labelFontItalic() : false;
    const bool fontUnderline = settings ? settings->labelFontUnderline() : false;
    const bool fontStrikeout = settings ? settings->labelFontStrikeout() : false;
    QImage labelsImage = ZoneLabelTextureBuilder::build(zones, size, labelFontColor, true, backgroundColor,
                                                        fontFamily, fontSizeScale, fontWeight, fontItalic,
                                                        fontUnderline, fontStrikeout);
    if (labelsImage.isNull()) {
        labelsImage = QImage(1, 1, QImage::Format_ARGB32);
        labelsImage.fill(Qt::transparent);
    }
    return labelsImage;
}

// Set a QML property, falling back to setProperty if needed
void writeQmlProperty(QObject* object, const QString& name, const QVariant& value)
{
    if (!object) {
        return;
    }

    QQmlProperty prop(object, name);
    if (prop.isValid()) {
        prop.write(value);
    } else {
        object->setProperty(name.toUtf8().constData(), value);
    }
}

// Push label font settings from IZoneVisualizationSettings to a QML window
void writeFontProperties(QObject* window, const IZoneVisualizationSettings* settings)
{
    if (!window || !settings) {
        return;
    }
    writeQmlProperty(window, QStringLiteral("labelFontColor"), settings->labelFontColor());
    writeQmlProperty(window, QStringLiteral("fontFamily"), settings->labelFontFamily());
    writeQmlProperty(window, QStringLiteral("fontSizeScale"), settings->labelFontSizeScale());
    writeQmlProperty(window, QStringLiteral("fontWeight"), settings->labelFontWeight());
    writeQmlProperty(window, QStringLiteral("fontItalic"), settings->labelFontItalic());
    writeQmlProperty(window, QStringLiteral("fontUnderline"), settings->labelFontUnderline());
    writeQmlProperty(window, QStringLiteral("fontStrikeout"), settings->labelFontStrikeout());
}

// Convert ZoneSelectorPosition to LayerShellQt anchors
LayerShellQt::Window::Anchors getAnchorsForPosition(ZoneSelectorPosition pos)
{
    switch (pos) {
    case ZoneSelectorPosition::TopLeft:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft);
    case ZoneSelectorPosition::Top:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::TopRight:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::Left:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorTop
                                             | LayerShellQt::Window::AnchorBottom);
    case ZoneSelectorPosition::Right:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorRight | LayerShellQt::Window::AnchorTop
                                             | LayerShellQt::Window::AnchorBottom);
    case ZoneSelectorPosition::BottomLeft:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft);
    case ZoneSelectorPosition::Bottom:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::BottomRight:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorRight);
    default:
        // Default to top anchors
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
    }
}

// Clean up all windows in a QHash map
template<typename K>
void cleanupWindowMap(QHash<K, QQuickWindow*>& windowMap)
{
    for (auto* window : std::as_const(windowMap)) {
        if (window) {
            QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
            window->close();
            window->deleteLater();
        }
    }
    windowMap.clear();
}

// Center an OSD/layer window on screen using LayerShellQt margins
void centerLayerWindowOnScreen(QQuickWindow* window, const QRect& screenGeom, int osdWidth, int osdHeight)
{
    if (!window) {
        return;
    }
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        const int hMargin = qMax(0, (screenGeom.width() - osdWidth) / 2);
        const int vMargin = qMax(0, (screenGeom.height() - osdHeight) / 2);
        layerWindow->setAnchors(LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
            | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layerWindow->setMargins(QMargins(hMargin, vMargin, hMargin, vMargin));
    }
}

// Result of OSD window preparation
struct OsdWindowSetup {
    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 16.0 / 9.0;

    explicit operator bool() const { return window != nullptr; }
};

// Calculate OSD size and center window
void sizeAndCenterOsd(QQuickWindow* window, const QRect& screenGeom, qreal aspectRatio)
{
    constexpr int osdWidth = 280;
    const int osdHeight = static_cast<int>(200 / aspectRatio) + 80;
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    centerLayerWindowOnScreen(window, screenGeom, osdWidth, osdHeight);
}

// The zone model doesn't know about overlay highlights (keyboard/hover),
// so we patch isHighlighted here before passing to shaders
QVariantList patchZonesWithHighlight(const QVariantList& zones, QQuickWindow* window)
{
    if (!window) {
        return zones;
    }
    const QString hid = window->property("highlightedZoneId").toString();
    const QVariantList hids = window->property("highlightedZoneIds").toList();

    QVariantList out;
    for (const QVariant& z : zones) {
        QVariantMap m = z.toMap();
        const QString id = m.value(QLatin1String("id")).toString();
        bool hi = (!id.isEmpty() && id == hid);
        if (!hi) {
            for (const QVariant& v : hids) {
                if (v.toString() == id) {
                    hi = true;
                    break;
                }
            }
        }
        m[QLatin1String("isHighlighted")] = hi;
        out.append(m);
    }
    return out;
}

QQuickItem* findQmlItemByName(QQuickItem* item, const QString& objectName)
{
    if (!item) {
        return nullptr;
    }

    if (item->objectName() == objectName) {
        return item;
    }

    const auto children = item->childItems();
    for (auto* child : children) {
        if (auto* found = findQmlItemByName(child, objectName)) {
            return found;
        }
    }

    return nullptr;
}

void updateZoneSelectorComputedProperties(QQuickWindow* window, QScreen* screen, const ZoneSelectorConfig& config,
                                          ISettings* settings, const ZoneSelectorLayout& layout)
{
    if (!window || !screen) {
        return;
    }

    const QRect screenGeom = screen->geometry();
    const int screenWidth = screenGeom.width();
    const int indicatorWidth = layout.indicatorWidth;

    // Compute previewScale
    const qreal previewScale = screenWidth > 0 ? static_cast<qreal>(indicatorWidth) / screenWidth : 0.09375;
    writeQmlProperty(window, QStringLiteral("previewScale"), previewScale);

    // Compute positionIsVertical from per-screen config
    const auto pos = static_cast<ZoneSelectorPosition>(config.position);
    writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                     (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));

    // Compute scaled zone appearance values (from global settings - not per-screen)
    if (settings) {
        const int zonePadding = settings->zonePadding();
        const int zoneBorderWidth = settings->borderWidth();
        const int zoneBorderRadius = settings->borderRadius();

        const int scaledPadding = std::max(1, qRound(zonePadding * previewScale));
        const int scaledBorderWidth = std::max(1, qRound(zoneBorderWidth * previewScale * 2));
        const int scaledBorderRadius = std::max(2, qRound(zoneBorderRadius * previewScale * 2));

        writeQmlProperty(window, QStringLiteral("scaledPadding"), scaledPadding);
        writeQmlProperty(window, QStringLiteral("scaledBorderWidth"), scaledBorderWidth);
        writeQmlProperty(window, QStringLiteral("scaledBorderRadius"), scaledBorderRadius);
    }
}

void applyZoneSelectorLayout(QQuickWindow* window, const ZoneSelectorLayout& layout)
{
    if (!window) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("indicatorWidth"), layout.indicatorWidth);
    writeQmlProperty(window, QStringLiteral("indicatorHeight"), layout.indicatorHeight);
    writeQmlProperty(window, QStringLiteral("indicatorSpacing"), layout.indicatorSpacing);
    writeQmlProperty(window, QStringLiteral("containerPadding"), layout.containerPadding);
    writeQmlProperty(window, QStringLiteral("containerPaddingSide"), layout.paddingSide);
    writeQmlProperty(window, QStringLiteral("containerTopMargin"), layout.containerTopMargin);
    writeQmlProperty(window, QStringLiteral("containerSideMargin"), layout.containerSideMargin);
    writeQmlProperty(window, QStringLiteral("labelTopMargin"), layout.labelTopMargin);
    writeQmlProperty(window, QStringLiteral("labelHeight"), layout.labelHeight);
    writeQmlProperty(window, QStringLiteral("labelSpace"), layout.labelSpace);
    writeQmlProperty(window, QStringLiteral("layoutColumns"), layout.columns);
    writeQmlProperty(window, QStringLiteral("layoutRows"), layout.rows);
    writeQmlProperty(window, QStringLiteral("totalRows"), layout.totalRows);
    writeQmlProperty(window, QStringLiteral("contentWidth"), layout.contentWidth);
    writeQmlProperty(window, QStringLiteral("contentHeight"), layout.contentHeight);
    writeQmlProperty(window, QStringLiteral("scrollContentHeight"), layout.scrollContentHeight);
    writeQmlProperty(window, QStringLiteral("scrollContentWidth"), layout.scrollContentWidth);
    writeQmlProperty(window, QStringLiteral("needsScrolling"), layout.needsScrolling);
    writeQmlProperty(window, QStringLiteral("needsHorizontalScrolling"), layout.needsHorizontalScrolling);
    // Explicitly set containerWidth/Height after contentWidth/Height to ensure they update
    writeQmlProperty(window, QStringLiteral("containerWidth"), layout.containerWidth);
    writeQmlProperty(window, QStringLiteral("containerHeight"), layout.containerHeight);
    writeQmlProperty(window, QStringLiteral("barWidth"), layout.barWidth);
    writeQmlProperty(window, QStringLiteral("barHeight"), layout.barHeight);
}

void applyZoneSelectorGeometry(QQuickWindow* window, QScreen* screen, const ZoneSelectorLayout& layout,
                               ZoneSelectorPosition pos)
{
    if (!window || !screen) {
        return;
    }

    const QRect screenGeom = screen->geometry();

    // Calculate base positions - window positioned at screen edges
    // QML handles internal margins within the window
    const int centeredX = screenGeom.x() + (screenGeom.width() - layout.barWidth) / 2;
    const int centeredY = screenGeom.y() + (screenGeom.height() - layout.barHeight) / 2;
    const int rightX = screenGeom.x() + screenGeom.width() - layout.barWidth;
    const int bottomY = screenGeom.y() + screenGeom.height() - layout.barHeight;

    switch (pos) {
    case ZoneSelectorPosition::TopLeft:
        window->setX(screenGeom.x());
        window->setY(screenGeom.y());
        break;
    case ZoneSelectorPosition::Top:
        window->setX(centeredX);
        window->setY(screenGeom.y());
        break;
    case ZoneSelectorPosition::TopRight:
        window->setX(rightX);
        window->setY(screenGeom.y());
        break;
    case ZoneSelectorPosition::Left:
        window->setX(screenGeom.x());
        window->setY(centeredY);
        break;
    case ZoneSelectorPosition::Right:
        window->setX(rightX);
        window->setY(centeredY);
        break;
    case ZoneSelectorPosition::BottomLeft:
        window->setX(screenGeom.x());
        window->setY(bottomY);
        break;
    case ZoneSelectorPosition::Bottom:
        window->setX(centeredX);
        window->setY(bottomY);
        break;
    case ZoneSelectorPosition::BottomRight:
        window->setX(rightX);
        window->setY(bottomY);
        break;
    default:
        // Fall back to Top position for invalid values
        window->setX(centeredX);
        window->setY(screenGeom.y());
        break;
    }
    window->setWidth(layout.barWidth);
    window->setHeight(layout.barHeight);
}

void updateZoneSelectorWindowLayout(QQuickWindow* window, QScreen* screen, const ZoneSelectorConfig& config,
                                    ISettings* settings, int layoutCount)
{
    if (!window || !screen) {
        return;
    }

    const ZoneSelectorLayout layout = computeZoneSelectorLayout(config, screen, layoutCount);

    // Set positionIsVertical before layout properties; QML anchors depend on it for
    // containerWidth/Height, so it has to be correct before we apply the layout.
    const auto pos = static_cast<ZoneSelectorPosition>(config.position);
    writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                     (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));

    applyZoneSelectorLayout(window, layout);

    // Update computed properties that depend on layout and settings
    updateZoneSelectorComputedProperties(window, screen, config, settings, layout);

    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setAnchors(getAnchorsForPosition(pos));
    }

    applyZoneSelectorGeometry(window, screen, layout, pos);
}

} // namespace

OverlayService::OverlayService(QObject* parent)
    : IOverlayService(parent)
    , m_engine(std::make_unique<QQmlEngine>()) // No parent - unique_ptr manages lifetime
{
    // Set up i18n for QML (makes i18n() available in QML)
    KLocalizedContext* localizedContext = new KLocalizedContext(m_engine.get());
    m_engine->rootContext()->setContextObject(localizedContext);

    // Connect to screen changes (with safety check for early initialization)
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, &OverlayService::handleScreenAdded);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OverlayService::handleScreenRemoved);
    } else {
        qCWarning(lcOverlay) << "Created before QGuiApplication - screen signals not connected";
    }

    // Connect to system sleep/resume via logind to restart shader timer after wake
    // This prevents large iTimeDelta jumps when system resumes from sleep
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"), this, SLOT(onPrepareForSleep(bool)));

    // Reset shader error state on construction (fresh start after reboot)
    m_pendingShaderError.clear();

    m_cavaService = std::make_unique<CavaService>(this);
    connect(m_cavaService.get(), &CavaService::spectrumUpdated,
            this, &OverlayService::onAudioSpectrumUpdated);
}

bool OverlayService::isVisible() const
{
    return m_visible;
}

bool OverlayService::isZoneSelectorVisible() const
{
    return m_zoneSelectorVisible;
}

OverlayService::~OverlayService()
{
    // Disconnect from QGuiApplication first so we don't get screen-related callbacks
    // while we're destroying windows.
    if (qGuiApp) {
        disconnect(qGuiApp, nullptr, this, nullptr);
    }

    // Clean up all window types before engine is destroyed
    // (takes C++ ownership to prevent QML GC interference)
    cleanupWindowMap(m_zoneSelectorWindows);
    cleanupWindowMap(m_overlayWindows);
    cleanupWindowMap(m_layoutOsdWindows);
    cleanupWindowMap(m_navigationOsdWindows);

    // Process pending deletions before destroying the QML engine.
    // All deleteLater() calls must complete while the engine is still valid.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // Now m_engine (unique_ptr) will be destroyed safely
    // since all QML objects have been properly cleaned up
}

QQuickWindow* OverlayService::createQmlWindow(const QUrl& qmlUrl,
                                              QScreen* screen,
                                              const char* windowType,
                                              const QVariantMap& initialProperties)
{
    if (!screen) {
        qCWarning(lcOverlay) << "Screen is null for" << windowType;
        return nullptr;
    }

    QQmlComponent component(m_engine.get(), qmlUrl);

    if (component.isError()) {
        qCWarning(lcOverlay) << "Failed to load" << windowType << "QML:" << component.errors();
        return nullptr;
    }

    if (component.status() != QQmlComponent::Ready) {
        qCWarning(lcOverlay) << windowType << "QML component not ready, status:" << component.status();
        return nullptr;
    }

    QObject* obj = initialProperties.isEmpty() ? component.create()
                                               : component.createWithInitialProperties(initialProperties);
    if (!obj) {
        qCWarning(lcOverlay) << "Failed to create" << windowType << "window:" << component.errors();
        return nullptr;
    }

    auto* window = qobject_cast<QQuickWindow*>(obj);
    if (!window) {
        qCWarning(lcOverlay) << "Created object is not a QQuickWindow for" << windowType;
        obj->deleteLater();
        return nullptr;
    }

    // Take C++ ownership so QML's GC doesn't delete the window
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);

    // Set the screen before configuring LayerShellQt
    window->setScreen(screen);

    return window;
}

void OverlayService::show()
{
    if (m_visible) {
        return;
    }

    // Check if we should show on all monitors or just the cursor's screen
    bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();

    QScreen* cursorScreen = nullptr;
    if (!showOnAllMonitors) {
        // Find the screen containing the cursor
        cursorScreen = QGuiApplication::screenAt(QCursor::pos());
        if (!cursorScreen) {
            // Fallback to primary screen if cursor position detection fails
            cursorScreen = Utils::primaryScreen();
        }
        // If the cursor's screen has PlasmaZones disabled, don't show overlay at all
        if (cursorScreen && m_settings && m_settings->isMonitorDisabled(Utils::screenIdentifier(cursorScreen))) {
            return;
        }
    }

    initializeOverlay(cursorScreen);
}

void OverlayService::showAtPosition(int cursorX, int cursorY)
{
    // Check if we should show on all monitors or just the cursor's screen
    bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();

    QScreen* cursorScreen = nullptr;
    if (!showOnAllMonitors) {
        // Find the screen containing the cursor using provided coordinates
        // This works on Wayland where QCursor::pos() doesn't work
        cursorScreen = Utils::findScreenAtPosition(cursorX, cursorY);
        if (!cursorScreen) {
            // Fallback to primary screen if no screen contains the cursor position
            cursorScreen = Utils::primaryScreen();
        }
        // If the cursor's screen has PlasmaZones disabled, don't show overlay at all
        if (cursorScreen && m_settings && m_settings->isMonitorDisabled(Utils::screenIdentifier(cursorScreen))) {
            return;
        }
    }

    if (m_visible) {
        // Already visible: when single-monitor mode, switch overlay if cursor moved to different screen (#136)
        if (!showOnAllMonitors && cursorScreen && m_currentOverlayScreen != cursorScreen) {
            initializeOverlay(cursorScreen);
        }
        return;
    }

    initializeOverlay(cursorScreen);
}

void OverlayService::initializeOverlay(QScreen* cursorScreen)
{
    // Determine if we should show on all monitors (cursorScreen == nullptr means all)
    const bool showOnAllMonitors = (cursorScreen == nullptr);

    m_visible = true;
    m_currentOverlayScreen = showOnAllMonitors ? nullptr : cursorScreen;

    // Initialize shader timing (shared across all monitors for synchronized effects)
    {
        QMutexLocker locker(&m_shaderTimerMutex);
        m_shaderTimer.start();
        m_lastFrameTime.store(0);
        m_frameCount.store(0);
    }
    m_zoneDataDirty = true; // Rebuild zone data on next frame

    // When single-monitor mode, hide overlay on screens we're switching away from (#136)
    if (!showOnAllMonitors) {
        for (auto* screen : m_overlayWindows.keys()) {
            if (screen != cursorScreen) {
                if (auto* window = m_overlayWindows.value(screen)) {
                    window->hide();
                }
            }
        }
    }

    for (auto* screen : Utils::allScreens()) {
        // Skip screens that aren't the cursor's screen when single-monitor mode is enabled
        if (!showOnAllMonitors && screen != cursorScreen) {
            continue;
        }
        // Skip monitors where PlasmaZones is disabled
        if (m_settings && m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
            continue;
        }

        if (!m_overlayWindows.contains(screen)) {
            createOverlayWindow(screen);
        }
        if (auto* window = m_overlayWindows.value(screen)) {
            assertWindowOnScreen(window, screen);
            qCDebug(lcOverlay) << "initializeOverlay: screen=" << screen->name()
                               << "screenGeom=" << screen->geometry()
                               << "availGeom=" << ScreenManager::actualAvailableGeometry(screen)
                               << "windowScreen=" << (window->screen() ? window->screen()->name() : QStringLiteral("null"));
            updateOverlayWindow(screen);
            window->show();
        }
    }

    // Check if we need to recreate windows - this handles the case where windows
    // were created before shaders were ready (e.g., at startup after reboot)
    // Check per-screen: each monitor's layout may differ in shader usage
    QList<QScreen*> screensToRecreate;

    for (auto* screen : Utils::allScreens()) {
        if (!m_overlayWindows.contains(screen)) {
            continue;
        }
        auto* window = m_overlayWindows.value(screen);
        if (!window) {
            continue;
        }

        // Use isShaderOverlay property set at creation time (more reliable than shaderSource
        // which can be set on non-shader windows by updateOverlayWindow())
        const bool windowIsShader = window->property("isShaderOverlay").toBool();
        const bool shouldUseShader = useShaderForScreen(screen);
        if (windowIsShader != shouldUseShader) {
            screensToRecreate.append(screen);
            qCDebug(lcOverlay) << "Overlay window type mismatch detected for screen" << screen->name()
                               << "(window is shader:" << windowIsShader << "should be:" << shouldUseShader << ")";
        }
    }

    // Recreate only the windows with type mismatch
    if (!screensToRecreate.isEmpty()) {
        for (QScreen* screen : screensToRecreate) {
            destroyOverlayWindow(screen);
        }
        for (QScreen* screen : screensToRecreate) {
            if (!m_settings || !m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
                createOverlayWindow(screen);
                updateOverlayWindow(screen);
                if (auto* window = m_overlayWindows.value(screen)) {
                    assertWindowOnScreen(window, screen);
                    window->show();
                }
            }
        }
    }

    if (anyScreenUsesShader()) {
        updateZonesForAllWindows(); // Push initial zone data
        startShaderAnimation();
    }

    Q_EMIT visibilityChanged(true);
}

void OverlayService::hide()
{
    if (!m_visible) {
        return;
    }

    m_visible = false;
    m_currentOverlayScreen = nullptr;

    // Stop shader animation
    stopShaderAnimation();

    // Invalidate shader timer
    {
        QMutexLocker locker(&m_shaderTimerMutex);
        m_shaderTimer.invalidate();
    }

    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            window->hide();
        }
    }

    m_pendingShaderError.clear();

    Q_EMIT visibilityChanged(false);
}

void OverlayService::toggle()
{
    if (m_visible) {
        hide();
    } else {
        show();
    }
}

void OverlayService::updateLayout(Layout* layout)
{
    setLayout(layout);
    if (m_visible) {
        updateGeometries();

        // Flash zones to indicate layout change if enabled
        if (m_settings && m_settings->flashZonesOnSwitch()) {
            for (auto* window : std::as_const(m_overlayWindows)) {
                if (window) {
                    QMetaObject::invokeMethod(window, "flash");
                }
            }
        }

        // Shader state management - MUST be outside flashZonesOnSwitch block
        // to ensure shader animations work regardless of flash setting
        if (anyScreenUsesShader()) {
            // Ensure shader timing + updates continue after layout switch
            {
                QMutexLocker locker(&m_shaderTimerMutex);
                if (!m_shaderTimer.isValid()) {
                    m_shaderTimer.start();
                    m_lastFrameTime.store(0);
                    m_frameCount.store(0);
                }
            }
            m_zoneDataDirty = true;
            updateZonesForAllWindows();
            if (!m_shaderUpdateTimer || !m_shaderUpdateTimer->isActive()) {
                startShaderAnimation();
            }
        } else {
            stopShaderAnimation();
        }
    }
}

void OverlayService::updateSettings(ISettings* settings)
{
    setSettings(settings);

    // Hide overlay and zone selector on monitors that are now disabled
    if (m_settings) {
        for (auto* screen : m_overlayWindows.keys()) {
            if (m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
                if (auto* window = m_overlayWindows.value(screen)) {
                    window->hide();
                }
            }
        }
        for (auto* screen : m_zoneSelectorWindows.keys()) {
            if (m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
                if (auto* window = m_zoneSelectorWindows.value(screen)) {
                    window->hide();
                }
            }
        }
    }

    if (m_visible) {
        for (auto* screen : m_overlayWindows.keys()) {
            if (m_settings && m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
                continue;
            }
            updateOverlayWindow(screen);
        }
    }

    // Keep zone selector windows in sync with settings changes (position, layout, sizing).
    // Without this, changing settings while the selector is visible can leave stale geometry
    // and anchors, causing corrupted rendering or incorrect window sizing.
    // Skip disabled monitors.
    if (!m_zoneSelectorWindows.isEmpty()) {
        for (auto* screen : m_zoneSelectorWindows.keys()) {
            if (m_settings && m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
                continue;
            }
            updateZoneSelectorWindow(screen);
        }
    }

    // If the selector was visible but got disabled via settings, hide it immediately.
    if (m_zoneSelectorVisible && m_settings && !m_settings->zoneSelectorEnabled()) {
        hideZoneSelector();
    }
}

void OverlayService::updateGeometries()
{
    for (auto* screen : m_overlayWindows.keys()) {
        updateOverlayWindow(screen);
    }
}

void OverlayService::highlightZone(const QString& zoneId)
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    // Update the highlightedZoneId property on all overlay windows
    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            writeQmlProperty(window, QStringLiteral("highlightedZoneId"), zoneId);
            // Clear multi-zone highlighting when using single zone
            writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), QVariantList());
        }
    }
}

void OverlayService::highlightZones(const QStringList& zoneIds)
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    // Update the highlightedZoneIds property on all overlay windows
    QVariantList zoneIdList;
    for (const QString& zoneId : zoneIds) {
        zoneIdList.append(zoneId);
    }

    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), zoneIdList);
            // Clear single zone highlighting when using multi-zone
            writeQmlProperty(window, QStringLiteral("highlightedZoneId"), QString());
        }
    }
}

void OverlayService::clearHighlight()
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    // Clear the highlight on all overlay windows
    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            writeQmlProperty(window, QStringLiteral("highlightedZoneId"), QString());
            writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), QVariantList());
        }
    }
}

void OverlayService::setLayout(Layout* layout)
{
    if (m_layout != layout) {
        m_layout = layout;
        // Mark zone data as dirty when layout changes to ensure shader overlay updates
        m_zoneDataDirty = true;
    }
}

void OverlayService::setSettings(ISettings* settings)
{
    if (m_settings != settings) {
        // Disconnect from old settings signals
        if (m_settings) {
            disconnect(m_settings, &ISettings::settingsChanged, this, nullptr);
            disconnect(m_settings, &ISettings::enableShaderEffectsChanged, this, nullptr);
            disconnect(m_settings, &ISettings::enableAudioVisualizerChanged, this, nullptr);
            disconnect(m_settings, &ISettings::audioSpectrumBarCountChanged, this, nullptr);
            disconnect(m_settings, &ISettings::shaderFrameRateChanged, this, nullptr);
        }

        m_settings = settings;

        // Connect to new settings signals
        if (m_settings) {
            auto refreshZoneSelectors = [this]() {
                for (QScreen* screen : m_zoneSelectorWindows.keys()) {
                    updateZoneSelectorWindow(screen);
                }
            };
            connect(m_settings, &ISettings::settingsChanged, this, refreshZoneSelectors);

            connect(m_settings, &ISettings::enableShaderEffectsChanged, this, [this]() {
                // When shader effects setting changes, recreate overlay windows if visible
                // to switch between shader and non-shader overlay types
                if (m_visible) {
                    // Check if we were using shaders before the setting changed
                    // (shader timer running indicates we were using shader overlay)
                    const bool wasUsingShader = m_shaderUpdateTimer && m_shaderUpdateTimer->isActive();
                    const bool shouldUseShader = anyScreenUsesShader();

                    // Only recreate if the overlay type actually needs to change
                    if (wasUsingShader != shouldUseShader) {
                        qCInfo(lcOverlay) << "Shader effects setting changed, recreating overlay windows"
                                           << "(was:" << wasUsingShader << "now:" << shouldUseShader << ")";

                        // Stop shader animation if it was running
                        if (wasUsingShader) {
                            stopShaderAnimation();
                        }

                        // Store current visibility state
                        const bool wasVisible = m_visible;

                        // Recreate all overlay windows (each gets correct type per-screen)
                        const auto screens = m_overlayWindows.keys();
                        for (QScreen* screen : screens) {
                            destroyOverlayWindow(screen);
                        }

                        // Recreate windows with correct type per-screen
                        for (QScreen* screen : screens) {
                            if (!m_settings || !m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
                                createOverlayWindow(screen);
                                updateOverlayWindow(screen);
                                if (wasVisible && m_overlayWindows.value(screen)) {
                                    m_overlayWindows.value(screen)->show();
                                }
                            }
                        }

                        // Start shader animation if any screen needs it
                        if (shouldUseShader && wasVisible) {
                            updateZonesForAllWindows(); // Push initial zone data
                            startShaderAnimation();
                        }
                    }
                }
            });

            connect(m_settings, &ISettings::enableAudioVisualizerChanged, this, [this]() {
                // Start/stop CAVA regardless of overlay visibility so it's warm when needed
                if (m_settings->enableAudioVisualizer()) {
                    if (m_cavaService) {
                        m_cavaService->setBarCount(m_settings->audioSpectrumBarCount());
                        m_cavaService->setFramerate(m_settings->shaderFrameRate());
                        m_cavaService->start();
                    }
                } else {
                    if (m_cavaService) {
                        m_cavaService->stop();
                        for (auto* window : std::as_const(m_overlayWindows)) {
                            if (window) {
                                writeQmlProperty(window, QStringLiteral("audioSpectrum"), QVariantList());
                            }
                        }
                        if (m_shaderPreviewWindow) {
                            writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("audioSpectrum"), QVariantList());
                        }
                    }
                }
            });

            connect(m_settings, &ISettings::audioSpectrumBarCountChanged, this, [this]() {
                if (m_cavaService) {
                    m_cavaService->setBarCount(m_settings->audioSpectrumBarCount());
                }
            });

            connect(m_settings, &ISettings::shaderFrameRateChanged, this, [this]() {
                if (m_cavaService && m_settings) {
                    m_cavaService->setFramerate(m_settings->shaderFrameRate());
                }
            });

            // Eagerly start CAVA at daemon boot so spectrum data is warm when overlay shows
            if (m_settings->enableAudioVisualizer() && m_cavaService) {
                m_cavaService->setBarCount(m_settings->audioSpectrumBarCount());
                m_cavaService->setFramerate(m_settings->shaderFrameRate());
                m_cavaService->start();
                qCInfo(lcOverlay) << "CAVA started eagerly (audio visualization enabled)";
            }
        }
    }
}

void OverlayService::setLayoutManager(ILayoutManager* layoutManager)
{
    // Disconnect from old layout manager if exists
    if (m_layoutManager) {
        auto* oldManager = dynamic_cast<LayoutManager*>(m_layoutManager);
        if (oldManager) {
            disconnect(oldManager, &LayoutManager::activeLayoutChanged, this, nullptr);
            disconnect(oldManager, &LayoutManager::layoutAssigned, this, nullptr);
        }
    }

    m_layoutManager = layoutManager;

    // Connect to layout change signals from the concrete LayoutManager
    // ILayoutManager is a pure interface without signals, so we need to cast
    if (m_layoutManager) {
        auto* manager = dynamic_cast<LayoutManager*>(m_layoutManager);
        if (manager) {
            // Update visible zone selector and overlay windows when layout changes.
            // Hidden windows are skipped — showZoneSelector()/show() refresh before showing.
            connect(manager, &LayoutManager::activeLayoutChanged, this, [this](Layout* /*layout*/) {
                refreshVisibleWindows();
            });
            connect(manager, &LayoutManager::layoutAssigned, this, [this](const QString& /*screenName*/, Layout* /*layout*/) {
                refreshVisibleWindows();
            });
        }
    }
}

void OverlayService::refreshVisibleWindows()
{
    if (m_zoneSelectorVisible) {
        for (QScreen* screen : m_zoneSelectorWindows.keys()) {
            updateZoneSelectorWindow(screen);
        }
    }
    if (m_visible) {
        for (QScreen* screen : m_overlayWindows.keys()) {
            updateOverlayWindow(screen);
        }
    }
}

Layout* OverlayService::resolveScreenLayout(QScreen* screen) const
{
    Layout* screenLayout = nullptr;
    if (m_layoutManager && screen) {
        screenLayout =
            m_layoutManager->layoutForScreen(Utils::screenIdentifier(screen), m_currentVirtualDesktop, m_currentActivity);
        if (!screenLayout) {
            screenLayout = m_layoutManager->defaultLayout();
        }
    }
    if (!screenLayout) {
        screenLayout = m_layout;
    }
    return screenLayout;
}

void OverlayService::setCurrentVirtualDesktop(int desktop)
{
    if (m_currentVirtualDesktop != desktop) {
        m_currentVirtualDesktop = desktop;
        qCInfo(lcOverlay) << "Virtual desktop changed to" << desktop;

        // Update zone selector windows with the new active layout for this desktop
        if (!m_zoneSelectorWindows.isEmpty()) {
            for (auto* screen : m_zoneSelectorWindows.keys()) {
                updateZoneSelectorWindow(screen);
            }
        }
        // Also refresh overlay windows when visible (symmetry with activity; overlay shows per-desktop layout)
        if (m_visible && !m_overlayWindows.isEmpty()) {
            for (auto* screen : m_overlayWindows.keys()) {
                updateOverlayWindow(screen);
            }
        }
    }
}

void OverlayService::setCurrentActivity(const QString& activityId)
{
    if (m_currentActivity != activityId) {
        m_currentActivity = activityId;
        qCInfo(lcOverlay) << "Activity changed activity= " << activityId;

        // Update zone selector windows with the new active layout for this activity
        if (!m_zoneSelectorWindows.isEmpty()) {
            for (auto* screen : m_zoneSelectorWindows.keys()) {
                updateZoneSelectorWindow(screen);
            }
        }
        // Also refresh overlay windows when visible (symmetry with desktop; overlay shows per-activity layout)
        if (m_visible && !m_overlayWindows.isEmpty()) {
            for (auto* screen : m_overlayWindows.keys()) {
                updateOverlayWindow(screen);
            }
        }
    }
}

void OverlayService::setupForScreen(QScreen* screen)
{
    if (!m_overlayWindows.contains(screen)) {
        createOverlayWindow(screen);
    }
}

void OverlayService::removeScreen(QScreen* screen)
{
    destroyOverlayWindow(screen);
}

void OverlayService::assertWindowOnScreen(QWindow* window, QScreen* screen)
{
    if (!window || !screen) {
        return;
    }
    if (window->screen() != screen) {
        window->setScreen(screen);
    }
    window->setGeometry(screen->geometry());
}

void OverlayService::handleScreenAdded(QScreen* screen)
{
    if (m_visible && screen && (!m_settings || !m_settings->isMonitorDisabled(Utils::screenIdentifier(screen)))) {
        createOverlayWindow(screen);
        updateOverlayWindow(screen);
        if (auto* window = m_overlayWindows.value(screen)) {
            assertWindowOnScreen(window, screen);
            window->show();
        }
    }
}

void OverlayService::handleScreenRemoved(QScreen* screen)
{
    destroyOverlayWindow(screen);
    destroyZoneSelectorWindow(screen);
    destroyLayoutOsdWindow(screen);
    destroyNavigationOsdWindow(screen);
    // Clean up failed creation tracking
    m_navigationOsdCreationFailed.remove(screen);
}

void OverlayService::showZoneSelector(QScreen* targetScreen)
{
    if (m_zoneSelectorVisible) {
        return;
    }

    // Check if zone selector is enabled in settings
    if (m_settings && !m_settings->zoneSelectorEnabled()) {
        return;
    }

    m_zoneSelectorVisible = true;

    for (auto* screen : Utils::allScreens()) {
        // Only show on the target screen (nullptr = all screens)
        if (targetScreen && screen != targetScreen) {
            continue;
        }
        // Skip monitors where PlasmaZones is disabled
        if (m_settings && m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
            continue;
        }
        if (!m_zoneSelectorWindows.contains(screen)) {
            createZoneSelectorWindow(screen);
        }
        if (auto* window = m_zoneSelectorWindows.value(screen)) {
            assertWindowOnScreen(window, screen);
            updateZoneSelectorWindow(screen);
            window->show();
        } else {
            qCWarning(lcOverlay) << "No window found for screen" << screen->name();
        }
    }

    Q_EMIT zoneSelectorVisibilityChanged(true);
}

void OverlayService::hideZoneSelector()
{
    if (!m_zoneSelectorVisible) {
        return;
    }

    m_zoneSelectorVisible = false;

    // Note: Don't clear selected zone here - we need it for snapping when drag ends
    // The selected zone will be cleared after the snap is processed

    for (auto* window : std::as_const(m_zoneSelectorWindows)) {
        if (window) {
            window->hide();
        }
    }

    Q_EMIT zoneSelectorVisibilityChanged(false);
}

void OverlayService::updateSelectorPosition(int cursorX, int cursorY)
{
    if (!m_zoneSelectorVisible) {
        return;
    }

    // Find which screen the cursor is on
    QScreen* screen = Utils::findScreenAtPosition(cursorX, cursorY);

    if (!screen) {
        return;
    }

    // Update the zone selector window with cursor position for hover effects
    if (auto* window = m_zoneSelectorWindows.value(screen)) {
        // With exclusiveZone=-1, the window is positioned deterministically
        // and mapFromGlobal gives us accurate local coordinates without compensation
        const QPoint localPos = window->mapFromGlobal(QPoint(cursorX, cursorY));
        int localX = localPos.x();
        int localY = localPos.y();

        window->setProperty("cursorX", localX);
        window->setProperty("cursorY", localY);

        // Get layouts from QML window
        QVariantList layouts = window->property("layouts").toList();
        if (layouts.isEmpty()) {
            return;
        }

        const int layoutCount = layouts.size();
        const ZoneSelectorConfig selectorConfig = m_settings
            ? m_settings->resolvedZoneSelectorConfig(Utils::screenIdentifier(screen))
            : defaultZoneSelectorConfig();
        const ZoneSelectorLayout layout = computeZoneSelectorLayout(selectorConfig, screen, layoutCount);

        // Get grid position from QML - it knows exactly where the content is rendered
        int contentGridX = 0;
        int contentGridY = 0;

        if (auto* contentRoot = window->contentItem()) {
            if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
                QRectF gridRect =
                    gridItem->mapRectToItem(contentRoot, QRectF(0, 0, gridItem->width(), gridItem->height()));
                contentGridX = qRound(gridRect.x());
                contentGridY = qRound(gridRect.y());
            }
        }

        // Check each layout indicator
        for (int i = 0; i < layouts.size(); ++i) {
            int row = (layout.columns > 0) ? (i / layout.columns) : 0;
            int col = (layout.columns > 0) ? (i % layout.columns) : 0;
            int indicatorX = contentGridX + col * (layout.indicatorWidth + layout.indicatorSpacing);
            int indicatorY =
                contentGridY + row * (layout.indicatorHeight + layout.labelSpace + layout.indicatorSpacing);

            // Check if cursor is over this indicator
            if (localX >= indicatorX && localX < indicatorX + layout.indicatorWidth && localY >= indicatorY
                && localY < indicatorY + layout.indicatorHeight) {
                QVariantMap layoutMap = layouts[i].toMap();
                QString layoutId = layoutMap[QStringLiteral("id")].toString();

                // Per-zone hit testing
                QVariantList zones = layoutMap[QStringLiteral("zones")].toList();
                int scaledPadding = window->property("scaledPadding").toInt();
                if (scaledPadding <= 0)
                    scaledPadding = 1;
                constexpr int minZoneSize = 8;

                for (int z = 0; z < zones.size(); ++z) {
                    QVariantMap zoneMap = zones[z].toMap();
                    QVariantMap relGeo = zoneMap[QStringLiteral("relativeGeometry")].toMap();
                    qreal rx = relGeo[QStringLiteral("x")].toReal();
                    qreal ry = relGeo[QStringLiteral("y")].toReal();
                    qreal rw = relGeo[QStringLiteral("width")].toReal();
                    qreal rh = relGeo[QStringLiteral("height")].toReal();

                    // Calculate zone rectangle exactly as QML does
                    int zoneX = indicatorX + static_cast<int>(rx * layout.indicatorWidth) + scaledPadding;
                    int zoneY = indicatorY + static_cast<int>(ry * layout.indicatorHeight) + scaledPadding;
                    int zoneW = std::max(minZoneSize, static_cast<int>(rw * layout.indicatorWidth) - scaledPadding * 2);
                    int zoneH =
                        std::max(minZoneSize, static_cast<int>(rh * layout.indicatorHeight) - scaledPadding * 2);

                    if (localX >= zoneX && localX < zoneX + zoneW && localY >= zoneY && localY < zoneY + zoneH) {
                        // Found the zone - update selection
                        if (m_selectedLayoutId != layoutId || m_selectedZoneIndex != z) {
                            m_selectedLayoutId = layoutId;
                            m_selectedZoneIndex = z;
                            m_selectedZoneRelGeo = QRectF(rx, ry, rw, rh);
                            window->setProperty("selectedLayoutId", layoutId);
                            window->setProperty("selectedZoneIndex", z);
                        }
                        return;
                    }
                }
                // Cursor is over layout indicator but not on a specific zone
                // Clear selection if we had one in a different layout
                if (!m_selectedLayoutId.isEmpty() && m_selectedLayoutId != layoutId) {
                    m_selectedLayoutId.clear();
                    m_selectedZoneIndex = -1;
                    m_selectedZoneRelGeo = QRectF();
                    window->setProperty("selectedLayoutId", QString());
                    window->setProperty("selectedZoneIndex", -1);
                }
                return;
            }
        }

        // Cursor is not over any layout indicator - clear selection
        if (!m_selectedLayoutId.isEmpty()) {
            m_selectedLayoutId.clear();
            m_selectedZoneIndex = -1;
            m_selectedZoneRelGeo = QRectF();
            window->setProperty("selectedLayoutId", QString());
            window->setProperty("selectedZoneIndex", -1);
        }
    }
}

void OverlayService::updateMousePosition(int cursorX, int cursorY)
{
    if (!m_visible) {
        return;
    }

    // Update mouse position on all overlay windows for shader effects
    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            // Convert global cursor position to window-local coordinates
            const QPoint localPos = window->mapFromGlobal(QPoint(cursorX, cursorY));
            window->setProperty("mousePosition", QPointF(localPos.x(), localPos.y()));
        }
    }
}

void OverlayService::createZoneSelectorWindow(QScreen* screen)
{
    if (m_zoneSelectorWindows.contains(screen)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/ZoneSelectorWindow.qml")), screen, "zone selector");
    if (!window) {
        return;
    }

    const QRect screenGeom = screen->geometry();

    // Build resolved per-screen config
    const ZoneSelectorConfig config = m_settings
        ? m_settings->resolvedZoneSelectorConfig(Utils::screenIdentifier(screen))
        : defaultZoneSelectorConfig();
    const auto pos = static_cast<ZoneSelectorPosition>(config.position);

    // Configure LayerShellQt for zone selector (LayerTop for pointer input)
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerTop);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);

        layerWindow->setAnchors(getAnchorsForPosition(pos));
        layerWindow->setExclusiveZone(-1);
        layerWindow->setScope(QStringLiteral("plasmazones-selector-%1").arg(screen->name()));
    }

    // Set screen properties for layout preview scaling
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("screenWidth"), screenGeom.width());

    // Pass zone appearance settings for scaled preview (global settings)
    if (m_settings) {
        writeQmlProperty(window, QStringLiteral("zonePadding"), m_settings->zonePadding());
        writeQmlProperty(window, QStringLiteral("zoneBorderWidth"), m_settings->borderWidth());
        writeQmlProperty(window, QStringLiteral("zoneBorderRadius"), m_settings->borderRadius());
    }
    // Pass resolved per-screen config values to QML
    writeQmlProperty(window, QStringLiteral("selectorPosition"), config.position);
    writeQmlProperty(window, QStringLiteral("selectorLayoutMode"), config.layoutMode);
    writeQmlProperty(window, QStringLiteral("selectorGridColumns"), config.gridColumns);
    writeQmlProperty(window, QStringLiteral("previewWidth"), config.previewWidth);
    writeQmlProperty(window, QStringLiteral("previewHeight"), config.previewHeight);
    writeQmlProperty(window, QStringLiteral("previewLockAspect"), config.previewLockAspect);

    const int layoutCount = LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, Utils::screenIdentifier(screen), m_currentVirtualDesktop, m_currentActivity).size();
    updateZoneSelectorWindowLayout(window, screen, config, m_settings, layoutCount);

    window->setVisible(false);
    auto conn = connect(window, SIGNAL(zoneSelected(QString, int, QVariant)), this, SLOT(onZoneSelected(QString, int, QVariant)));
    if (!conn) {
        qCWarning(lcOverlay) << "Failed to connect zoneSelected signal for screen" << screen->name()
                             << "- zone selector layout switching will not work";
    }
    m_zoneSelectorWindows.insert(screen, window);
}

void OverlayService::destroyZoneSelectorWindow(QScreen* screen)
{
    if (auto* window = m_zoneSelectorWindows.take(screen)) {
        window->close();
        window->deleteLater();
    }
}

void OverlayService::updateZoneSelectorWindow(QScreen* screen)
{
    if (!screen) {
        return;
    }

    auto* window = m_zoneSelectorWindows.value(screen);
    if (!window) {
        return;
    }

    // Update screen properties (in case screen geometry changed)
    const QRect screenGeom = screen->geometry();
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("screenWidth"), screenGeom.width());

    // Build resolved per-screen config
    const ZoneSelectorConfig config = m_settings
        ? m_settings->resolvedZoneSelectorConfig(Utils::screenIdentifier(screen))
        : defaultZoneSelectorConfig();

    // Update settings-based properties
    if (m_settings) {
        writeQmlProperty(window, QStringLiteral("highlightColor"), m_settings->highlightColor());
        writeQmlProperty(window, QStringLiteral("inactiveColor"), m_settings->inactiveColor());
        writeQmlProperty(window, QStringLiteral("borderColor"), m_settings->borderColor());
        // Zone appearance settings for scaled preview (global)
        writeQmlProperty(window, QStringLiteral("zonePadding"), m_settings->zonePadding());
        writeQmlProperty(window, QStringLiteral("zoneBorderWidth"), m_settings->borderWidth());
        writeQmlProperty(window, QStringLiteral("zoneBorderRadius"), m_settings->borderRadius());
        // Font settings for zone number labels
        writeFontProperties(window, m_settings);
    }
    // Pass resolved per-screen config values to QML
    writeQmlProperty(window, QStringLiteral("selectorPosition"), config.position);
    writeQmlProperty(window, QStringLiteral("selectorLayoutMode"), config.layoutMode);
    writeQmlProperty(window, QStringLiteral("selectorGridColumns"), config.gridColumns);
    writeQmlProperty(window, QStringLiteral("previewWidth"), config.previewWidth);
    writeQmlProperty(window, QStringLiteral("previewHeight"), config.previewHeight);
    writeQmlProperty(window, QStringLiteral("previewLockAspect"), config.previewLockAspect);

    // Build and pass layout data (filtered for this screen's context)
    QVariantList layouts = buildLayoutsList(screen->name());
    writeQmlProperty(window, QStringLiteral("layouts"), layouts);

    // Set active layout ID for this screen
    // Per-screen assignment takes priority so each monitor highlights its own layout
    QString activeLayoutId;
    Layout* screenLayout = resolveScreenLayout(screen);
    if (screenLayout) {
        activeLayoutId = screenLayout->id().toString();
    }
    writeQmlProperty(window, QStringLiteral("activeLayoutId"), activeLayoutId);

    // Compute layout for geometry updates using per-screen config
    const int layoutCount = layouts.size();
    const ZoneSelectorLayout layout = computeZoneSelectorLayout(config, screen, layoutCount);

    // Set positionIsVertical before layout properties; QML anchors depend on it for
    // containerWidth/Height, so it has to be correct before we apply the layout.
    const auto pos = static_cast<ZoneSelectorPosition>(config.position);
    writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                     (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));

    // Apply layout and geometry
    applyZoneSelectorLayout(window, layout);

    // Update computed properties that depend on layout and settings
    updateZoneSelectorComputedProperties(window, screen, config, m_settings, layout);

    // Schedule QML polish for next render frame (do NOT call processEvents here —
    // re-entrant event processing during a Wayland drag can deadlock with the
    // compositor, causing a hard system freeze; see GitHub discussion #152).
    if (auto* contentRoot = window->contentItem()) {
        contentRoot->polish();
    }
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        const int screenW = screenGeom.width();
        const int screenH = screenGeom.height();
        const int hMargin = std::max(0, (screenW - layout.barWidth) / 2);
        const int vMargin = std::max(0, (screenH - layout.barHeight) / 2);

        // exclusiveZone(-1) ignores panel geometry; the popup renders at absolute screen
        // coordinates over any panels, so hover coordinates match (no offset mismatch).

        // Initialize to Top position as safe default
        LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
        QMargins margins = QMargins(hMargin, 0, hMargin, std::max(0, screenH - layout.barHeight));

        switch (pos) {
        case ZoneSelectorPosition::TopLeft:
            anchors = LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft);
            margins = QMargins(0, 0, screenW - layout.barWidth, screenH - layout.barHeight);
            break;
        case ZoneSelectorPosition::Top:
            anchors = LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft
                                                    | LayerShellQt::Window::AnchorRight);
            margins = QMargins(hMargin, 0, hMargin, std::max(0, screenH - layout.barHeight));
            break;
        case ZoneSelectorPosition::TopRight:
            anchors =
                LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorRight);
            margins = QMargins(screenW - layout.barWidth, 0, 0, screenH - layout.barHeight);
            break;
        case ZoneSelectorPosition::Left:
            anchors = LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorTop
                                                    | LayerShellQt::Window::AnchorBottom);
            margins = QMargins(0, vMargin, 0, vMargin);
            break;
        case ZoneSelectorPosition::Right:
            anchors = LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorRight | LayerShellQt::Window::AnchorTop
                                                    | LayerShellQt::Window::AnchorBottom);
            margins = QMargins(0, vMargin, 0, vMargin);
            break;
        case ZoneSelectorPosition::BottomLeft:
            anchors =
                LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft);
            margins = QMargins(0, screenH - layout.barHeight, screenW - layout.barWidth, 0);
            break;
        case ZoneSelectorPosition::Bottom:
            anchors =
                LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft
                                              | LayerShellQt::Window::AnchorRight);
            margins = QMargins(hMargin, std::max(0, screenH - layout.barHeight), hMargin, 0);
            break;
        case ZoneSelectorPosition::BottomRight:
            anchors =
                LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorRight);
            margins = QMargins(screenW - layout.barWidth, screenH - layout.barHeight, 0, 0);
            break;
        default:
            // Already initialized to Top position
            break;
        }
        layerWindow->setAnchors(anchors);
        layerWindow->setMargins(margins);
    }
    applyZoneSelectorGeometry(window, screen, layout, pos);

    if (auto* contentRoot = window->contentItem()) {
        // Ensure the root item matches the window size after geometry changes.
        // This avoids anchors evaluating against a 0x0 root during rapid updates.
        contentRoot->setWidth(window->width());
        contentRoot->setHeight(window->height());

        // Schedule polish for next render frame (NO processEvents — see #152)
        contentRoot->polish();
    }

    // Schedule QML items for layout recalculation on the next frame
    if (auto* contentRoot = window->contentItem()) {
        if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
            gridItem->polish();
            gridItem->update();
        }
        if (auto* containerItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContainer"))) {
            containerItem->polish();
            containerItem->update();
        }
    }
}

void OverlayService::createOverlayWindow(QScreen* screen)
{
    if (m_overlayWindows.contains(screen)) {
        return;
    }

    // Choose overlay type based on shader settings for THIS screen's layout
    bool usingShader = useShaderForScreen(screen);

    // Expose overlayService to QML context for error reporting
    m_engine->rootContext()->setContextProperty(QStringLiteral("overlayService"), this);

    // Try shader overlay first, fall back to standard overlay if it fails
    QQuickWindow* window = nullptr;
    if (usingShader) {
        // Set labelsTexture before QML loads so ZoneShaderItem binding never sees undefined
        QImage placeholder(1, 1, QImage::Format_ARGB32);
        placeholder.fill(Qt::transparent);
        QVariantMap initProps;
        initProps.insert(QStringLiteral("labelsTexture"), QVariant::fromValue(placeholder));
        window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/RenderNodeOverlay.qml")), screen, "shader overlay",
                                 initProps);
        if (window) {
            qCInfo(lcOverlay) << "Overlay window created: RenderNodeOverlay (ZoneShaderItem) for screen" << screen->name();
        } else {
            qCWarning(lcOverlay) << "Falling back to standard overlay";
            usingShader = false;
        }
    }
    if (!window) {
        window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/ZoneOverlay.qml")), screen, "overlay");
        if (!window) {
            return;
        }
    }

    // Set window geometry to cover full screen
    const QRect geom = screen->geometry();
    window->setX(geom.x());
    window->setY(geom.y());
    window->setWidth(geom.width());
    window->setHeight(geom.height());

    // Mark window type for reliable type detection
    window->setProperty("isShaderOverlay", usingShader);

    // Set shader-specific properties (use QQmlProperty so QML bindings see updates)
    // Use per-screen layout (same resolution as updateOverlayWindow) so each monitor
    // gets the correct shader when per-screen assignments differ
    Layout* screenLayout = resolveScreenLayout(screen);

    if (usingShader && screenLayout) {
        auto* registry = ShaderRegistry::instance();
        if (registry) {
            const QString shaderId = screenLayout->shaderId();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            qCDebug(lcOverlay) << "Overlay shader=" << shaderId << "multipass=" << info.isMultipass
                              << "bufferPaths=" << info.bufferShaderPaths.size();
            writeQmlProperty(window, QStringLiteral("shaderSource"), info.shaderUrl);
            writeQmlProperty(window, QStringLiteral("bufferShaderPath"), info.bufferShaderPath);
            QVariantList pathList;
            for (const QString &p : info.bufferShaderPaths) {
                pathList.append(p);
            }
            writeQmlProperty(window, QStringLiteral("bufferShaderPaths"), pathList);
            writeQmlProperty(window, QStringLiteral("bufferFeedback"), info.bufferFeedback);
            writeQmlProperty(window, QStringLiteral("bufferScale"), info.bufferScale);
            writeQmlProperty(window, QStringLiteral("bufferWrap"), info.bufferWrap);
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, screenLayout->shaderParams());
            writeQmlProperty(window, QStringLiteral("shaderParams"), QVariant::fromValue(translatedParams));
        }
    }

    // Configure LayerShellQt for full-screen overlay
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                          | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layerWindow->setExclusiveZone(-1);
        layerWindow->setScope(QStringLiteral("plasmazones-overlay-%1").arg(screen->name()));
    }

    if (!Platform::isSupported()) {
        qCWarning(lcOverlay) << "Platform not supported - PlasmaZones requires Wayland";
    }

    window->setVisible(false);

    // Connect to screen geometry changes
    QPointer<QScreen> screenPtr = screen;
    connect(screen, &QScreen::geometryChanged, window, [this, screenPtr](const QRect& newGeom) {
        if (!screenPtr) {
            return;
        }
        if (auto* w = m_overlayWindows.value(screenPtr)) {
            w->setX(newGeom.x());
            w->setY(newGeom.y());
            w->setWidth(newGeom.width());
            w->setHeight(newGeom.height());
            updateOverlayWindow(screenPtr);
        }
    });

    if (usingShader) {
        writeQmlProperty(window, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
    }

    m_overlayWindows.insert(screen, window);
}

void OverlayService::destroyOverlayWindow(QScreen* screen)
{
    if (auto* window = m_overlayWindows.take(screen)) {
        // Disconnect so no signals (e.g. geometryChanged) are delivered to a window we're destroying
        disconnect(screen, nullptr, window, nullptr);
        window->close();
        window->deleteLater();
    }
}

void OverlayService::updateOverlayWindow(QScreen* screen)
{
    auto* window = m_overlayWindows.value(screen);
    if (!window) {
        return;
    }

    // Get the layout for this screen to use layout-specific settings
    // Prefer per-screen assignment, fall back to global active layout
    Layout* screenLayout = resolveScreenLayout(screen);

    // Update settings-based properties on the window itself (QML root)
    if (m_settings) {
        window->setProperty("highlightColor", m_settings->highlightColor());
        window->setProperty("inactiveColor", m_settings->inactiveColor());
        window->setProperty("borderColor", m_settings->borderColor());
        window->setProperty("activeOpacity", m_settings->activeOpacity());
        window->setProperty("inactiveOpacity", m_settings->inactiveOpacity());
        window->setProperty("borderWidth", m_settings->borderWidth());
        window->setProperty("borderRadius", m_settings->borderRadius());
        window->setProperty("enableBlur", m_settings->enableBlur());
        // Layout's showZoneNumbers takes precedence over global setting
        bool showNumbers = screenLayout ? screenLayout->showZoneNumbers() : m_settings->showZoneNumbers();
        window->setProperty("showNumbers", showNumbers);
        writeFontProperties(window, m_settings);
    }

    // Update shader-specific properties if using shader overlay
    // Only update if this window is actually a shader overlay window (check isShaderOverlay property)
    const bool windowIsShader = window->property("isShaderOverlay").toBool();
    const bool screenUsesShader = useShaderForScreen(screen);
    if (windowIsShader && screenUsesShader && screenLayout) {
        auto* registry = ShaderRegistry::instance();
        if (registry) {
            const QString shaderId = screenLayout->shaderId();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            writeQmlProperty(window, QStringLiteral("shaderSource"), info.shaderUrl);
            writeQmlProperty(window, QStringLiteral("bufferShaderPath"), info.bufferShaderPath);
            QVariantList pathList;
            for (const QString &p : info.bufferShaderPaths) {
                pathList.append(p);
            }
            writeQmlProperty(window, QStringLiteral("bufferShaderPaths"), pathList);
            writeQmlProperty(window, QStringLiteral("bufferFeedback"), info.bufferFeedback);
            writeQmlProperty(window, QStringLiteral("bufferScale"), info.bufferScale);
            writeQmlProperty(window, QStringLiteral("bufferWrap"), info.bufferWrap);
            // Translate parameter IDs to shader uniform names (mapsTo values)
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, screenLayout->shaderParams());
            writeQmlProperty(window, QStringLiteral("shaderParams"), QVariant::fromValue(translatedParams));
        }
    } else if (windowIsShader && !screenUsesShader) {
        // Clear shader properties if window is shader type but shaders are now disabled
        writeQmlProperty(window, QStringLiteral("shaderSource"), QUrl());
        writeQmlProperty(window, QStringLiteral("bufferShaderPath"), QString());
        writeQmlProperty(window, QStringLiteral("bufferShaderPaths"), QVariantList());
        writeQmlProperty(window, QStringLiteral("bufferFeedback"), false);
        writeQmlProperty(window, QStringLiteral("bufferScale"), 1.0);
        writeQmlProperty(window, QStringLiteral("bufferWrap"), QStringLiteral("clamp"));
        writeQmlProperty(window, QStringLiteral("shaderParams"), QVariantMap());
    }

    // Update zones on the window (QML root has the zones property).
    // Patch isHighlighted from overlay's highlightedZoneId/highlightedZoneIds so
    // ZoneDataProvider and zone components see the correct state.
    QVariantList zones = buildZonesList(screen);
    QVariantList patched = patchZonesWithHighlight(zones, window);
    writeQmlProperty(window, QStringLiteral("zones"), patched);

    // Shader overlay: zoneCount, highlightedCount, zoneDataVersion, labelsTexture
    if (windowIsShader && screenUsesShader) {
        int highlightedCount = 0;
        for (const QVariant& z : patched) {
            if (z.toMap().value(QLatin1String("isHighlighted")).toBool()) {
                ++highlightedCount;
            }
        }
        writeQmlProperty(window, QStringLiteral("zoneCount"), patched.size());
        writeQmlProperty(window, QStringLiteral("highlightedCount"), highlightedCount);
        ++m_zoneDataVersion;

        updateLabelsTextureForWindow(window, patched, screen, screenLayout);
        for (auto* w : std::as_const(m_overlayWindows)) {
            if (w) {
                writeQmlProperty(w, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
            }
        }
    }
}

void OverlayService::updateLabelsTextureForWindow(QQuickWindow* window,
                                                 const QVariantList& patched,
                                                 QScreen* screen,
                                                 Layout* screenLayout)
{
    Q_UNUSED(screen)
    if (!window) {
        return;
    }
    const bool showNumbers =
        screenLayout ? screenLayout->showZoneNumbers() : (m_settings ? m_settings->showZoneNumbers() : true);
    const QColor labelFontColor = m_settings ? m_settings->labelFontColor() : QColor(Qt::white);
    QColor backgroundColor = Qt::black;
    if (m_settings) {
        KColorScheme scheme(QPalette::Active, KColorScheme::View);
        backgroundColor = scheme.background(KColorScheme::NormalBackground).color();
    }
    const QString fontFamily = m_settings ? m_settings->labelFontFamily() : QString();
    const qreal fontSizeScale = m_settings ? m_settings->labelFontSizeScale() : 1.0;
    const int fontWeight = m_settings ? m_settings->labelFontWeight() : QFont::Bold;
    const bool fontItalic = m_settings ? m_settings->labelFontItalic() : false;
    const bool fontUnderline = m_settings ? m_settings->labelFontUnderline() : false;
    const bool fontStrikeout = m_settings ? m_settings->labelFontStrikeout() : false;
    const QSize size(qMax(1, static_cast<int>(window->width())), qMax(1, static_cast<int>(window->height())));
    QImage labelsImage =
        ZoneLabelTextureBuilder::build(patched, size, labelFontColor, showNumbers, backgroundColor,
                                       fontFamily, fontSizeScale, fontWeight, fontItalic,
                                       fontUnderline, fontStrikeout);
    if (labelsImage.isNull()) {
        labelsImage = QImage(1, 1, QImage::Format_ARGB32);
        labelsImage.fill(Qt::transparent);
    }
    window->setProperty("labelsTexture", QVariant::fromValue(labelsImage));
}

QVariantList OverlayService::buildZonesList(QScreen* screen) const
{
    QVariantList zonesList;

    if (!screen) {
        return zonesList;
    }

    // Get the layout for this specific screen, fall back to global active layout
    // Per-screen assignments take priority so each monitor shows its own layout
    Layout* screenLayout = resolveScreenLayout(screen);

    if (!screenLayout) {
        return zonesList;
    }

    qCDebug(lcOverlay) << "buildZonesList: screen=" << screen->name()
                       << "screenGeom=" << screen->geometry()
                       << "availGeom=" << ScreenManager::actualAvailableGeometry(screen)
                       << "layout=" << screenLayout->name()
                       << "zones=" << screenLayout->zones().size();

    for (auto* zone : screenLayout->zones()) {
        if (zone) {
            zonesList.append(zoneToVariantMap(zone, screen, screenLayout));
        }
    }

    return zonesList;
}

QVariantMap OverlayService::zoneToVariantMap(Zone* zone, QScreen* screen, Layout* layout) const
{
    QVariantMap map;

    // Null check to prevent SIGSEGV
    if (!zone) {
        qCWarning(lcOverlay) << "Zone is null";
        return map;
    }

    // Calculate zone geometry with gaps applied (matches snap geometry).
    // Uses the layout's geometry preference: available area (excluding panels/taskbars)
    // or full screen geometry depending on useFullScreenGeometry setting.
    // Layout's zonePadding/outerGap takes precedence over global settings
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
    bool useAvail = !(layout && layout->useFullScreenGeometry());
    QRectF geom = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGap, useAvail);

    // Convert to overlay window local coordinates
    // The overlay covers the full screen, but zones are positioned within available area
    QRectF overlayGeom = GeometryUtils::availableAreaToOverlayCoordinates(geom, screen);

    map[JsonKeys::Id] = zone->id().toString(); // Include zone ID for stable selection
    map[JsonKeys::X] = overlayGeom.x();
    map[JsonKeys::Y] = overlayGeom.y();
    map[JsonKeys::Width] = overlayGeom.width();
    map[JsonKeys::Height] = overlayGeom.height();
    map[JsonKeys::ZoneNumber] = zone->zoneNumber();
    map[JsonKeys::Name] = zone->name();
    map[JsonKeys::IsHighlighted] = zone->isHighlighted();

    // Always include useCustomColors flag so QML can check it
    map[JsonKeys::UseCustomColors] = zone->useCustomColors();

    // Always include zone colors as hex strings (ARGB format) so QML can use them
    // when useCustomColors is true. QML expects color strings, not QColor objects.
    // This allows QML to always have access to zone colors and decide whether to use them.
    map[JsonKeys::HighlightColor] = zone->highlightColor().name(QColor::HexArgb);
    map[JsonKeys::InactiveColor] = zone->inactiveColor().name(QColor::HexArgb);
    map[JsonKeys::BorderColor] = zone->borderColor().name(QColor::HexArgb);

    // Always include appearance properties so QML can use them when useCustomColors is true
    map[JsonKeys::ActiveOpacity] = zone->activeOpacity();
    map[JsonKeys::InactiveOpacity] = zone->inactiveOpacity();
    map[JsonKeys::BorderWidth] = zone->borderWidth();
    map[JsonKeys::BorderRadius] = zone->borderRadius();

    // ═══════════════════════════════════════════════════════════════════════════════
    // Shader-specific data (ZoneDataProvider texture)
    // ═══════════════════════════════════════════════════════════════════════════════

    // Normalized coordinates 0-1 over the overlay (full screen). relativeGeometry is 0-1
    // over the available area only; the overlay covers the full screen, so we must use
    // overlay-based normalized so shader (rect * iResolution) matches overlay pixels.
    const QRectF screenGeom = screen->geometry();
    const qreal ow = screenGeom.width() > 0 ? screenGeom.width() : 1.0;
    const qreal oh = screenGeom.height() > 0 ? screenGeom.height() : 1.0;
    map[QLatin1String("normalizedX")] = overlayGeom.x() / ow;
    map[QLatin1String("normalizedY")] = overlayGeom.y() / oh;
    map[QLatin1String("normalizedWidth")] = overlayGeom.width() / ow;
    map[QLatin1String("normalizedHeight")] = overlayGeom.height() / oh;

    // Fill color (RGBA premultiplied alpha) for shader
    QColor fillColor = zone->useCustomColors() ? zone->highlightColor()
                                               : (m_settings ? m_settings->highlightColor() : QColor(Qt::blue));
    qreal alpha = zone->useCustomColors() ? zone->activeOpacity() : (m_settings ? m_settings->activeOpacity() : 0.5);
    map[QLatin1String("fillR")] = fillColor.redF() * alpha;
    map[QLatin1String("fillG")] = fillColor.greenF() * alpha;
    map[QLatin1String("fillB")] = fillColor.blueF() * alpha;
    map[QLatin1String("fillA")] = alpha;

    // Border color (RGBA) for shader
    QColor borderClr =
        zone->useCustomColors() ? zone->borderColor() : (m_settings ? m_settings->borderColor() : QColor(Qt::white));
    map[QLatin1String("borderR")] = borderClr.redF();
    map[QLatin1String("borderG")] = borderClr.greenF();
    map[QLatin1String("borderB")] = borderClr.blueF();
    map[QLatin1String("borderA")] = borderClr.alphaF();

    // Shader params: borderRadius, borderWidth (from zone or settings)
    map[QLatin1String("shaderBorderRadius")] =
        zone->useCustomColors() ? zone->borderRadius() : (m_settings ? m_settings->borderRadius() : 8);
    map[QLatin1String("shaderBorderWidth")] =
        zone->useCustomColors() ? zone->borderWidth() : (m_settings ? m_settings->borderWidth() : 2);

    return map;
}

QVariantList OverlayService::buildLayoutsList(const QString& screenName) const
{
    const auto entries = LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, screenName, m_currentVirtualDesktop, m_currentActivity);
    return LayoutUtils::toVariantList(entries);
}

bool OverlayService::hasSelectedZone() const
{
    return !m_selectedLayoutId.isEmpty() && m_selectedZoneIndex >= 0;
}

void OverlayService::clearSelectedZone()
{
    m_selectedLayoutId.clear();
    m_selectedZoneIndex = -1;
    m_selectedZoneRelGeo = QRectF();
}

QRect OverlayService::getSelectedZoneGeometry(QScreen* screen) const
{
    if (!hasSelectedZone() || !screen) {
        return QRect();
    }

    // Use the same geometry pipeline as the overlay snap path
    // (GeometryUtils::getZoneGeometryWithGaps) so that gap handling,
    // outer-gap vs inner-gap edge detection, and usable-geometry respect
    // are identical regardless of whether the user snaps via the overlay
    // or the zone selector.
    if (m_layoutManager && !m_selectedLayoutId.isEmpty()) {
        Layout* selectedLayout = m_layoutManager->layoutById(QUuid::fromString(m_selectedLayoutId));
        if (selectedLayout && m_selectedZoneIndex >= 0
            && m_selectedZoneIndex < static_cast<int>(selectedLayout->zones().size())) {
            Zone* zone = selectedLayout->zones().at(m_selectedZoneIndex);
            if (zone) {
                int zonePadding = GeometryUtils::getEffectiveZonePadding(selectedLayout, m_settings);
                int outerGap = GeometryUtils::getEffectiveOuterGap(selectedLayout, m_settings);
                bool useAvail = !(selectedLayout && selectedLayout->useFullScreenGeometry());
                QRectF geom = GeometryUtils::getZoneGeometryWithGaps(
                    zone, screen, zonePadding, outerGap, useAvail);
                return geom.toRect();
            }
        }
    }

    // Fallback: manual calculation when layout/zone lookup fails
    QRect availableGeom = ScreenManager::actualAvailableGeometry(screen);

    int x = availableGeom.x() + static_cast<int>(m_selectedZoneRelGeo.x() * availableGeom.width());
    int y = availableGeom.y() + static_cast<int>(m_selectedZoneRelGeo.y() * availableGeom.height());
    int width = static_cast<int>(m_selectedZoneRelGeo.width() * availableGeom.width());
    int height = static_cast<int>(m_selectedZoneRelGeo.height() * availableGeom.height());

    return QRect(x, y, width, height);
}

void OverlayService::onZoneSelected(const QString& layoutId, int zoneIndex, const QVariant& relativeGeometry)
{
    m_selectedLayoutId = layoutId;
    m_selectedZoneIndex = zoneIndex;

    // Convert QVariant to QVariantMap and extract relative geometry
    QVariantMap relGeoMap = relativeGeometry.toMap();
    qreal x = relGeoMap.value(QStringLiteral("x"), 0.0).toReal();
    qreal y = relGeoMap.value(QStringLiteral("y"), 0.0).toReal();
    qreal width = relGeoMap.value(QStringLiteral("width"), 0.0).toReal();
    qreal height = relGeoMap.value(QStringLiteral("height"), 0.0).toReal();
    m_selectedZoneRelGeo = QRectF(x, y, width, height);

    // Determine which screen the zone selector is on from the sender window
    // Primary: look up in our window-to-screen map (authoritative assignment)
    // Fallback: use Qt's screen assignment on the window itself
    QString screenName;
    auto* senderWindow = qobject_cast<QQuickWindow*>(sender());
    if (senderWindow) {
        for (auto it = m_zoneSelectorWindows.constBegin(); it != m_zoneSelectorWindows.constEnd(); ++it) {
            if (it.value() == senderWindow) {
                screenName = it.key()->name();
                break;
            }
        }
        if (screenName.isEmpty() && senderWindow->screen()) {
            screenName = senderWindow->screen()->name();
        }
    }

    qCInfo(lcOverlay) << "Layout selected from zone selector:" << layoutId << "on screen:" << screenName;
    Q_EMIT manualLayoutSelected(layoutId, screenName);
}

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
    if (m_shaderPreviewWindow && m_shaderPreviewWindow->isVisible() && m_settings && m_settings->enableAudioVisualizer()) {
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

void OverlayService::updateZonesForAllWindows()
{
    m_zoneDataDirty = false;

    for (auto it = m_overlayWindows.begin(); it != m_overlayWindows.end(); ++it) {
        QScreen* screen = it.key();
        QQuickWindow* window = it.value();

        if (!window) {
            continue;
        }

        QVariantList zones = buildZonesList(screen);
        QVariantList patched = patchZonesWithHighlight(zones, window);

        int highlightedCount = 0;
        for (const QVariant& z : patched) {
            if (z.toMap().value(QLatin1String("isHighlighted")).toBool()) {
                ++highlightedCount;
            }
        }

        writeQmlProperty(window, QStringLiteral("zones"), patched);
        writeQmlProperty(window, QStringLiteral("zoneCount"), patched.size());
        writeQmlProperty(window, QStringLiteral("highlightedCount"), highlightedCount);

        if (useShaderForScreen(screen)) {
            Layout* screenLayout = resolveScreenLayout(screen);
            updateLabelsTextureForWindow(window, patched, screen, screenLayout);
        }
    }

    ++m_zoneDataVersion;
    for (auto* w : std::as_const(m_overlayWindows)) {
        if (w) {
            writeQmlProperty(w, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
        }
    }
}

void OverlayService::onPrepareForSleep(bool goingToSleep)
{
    if (goingToSleep) {
        // System going to sleep - nothing to do
        return;
    }

    // System waking up - restart shader timer to avoid large iTimeDelta
    QMutexLocker locker(&m_shaderTimerMutex);
    if (m_visible && m_shaderTimer.isValid()) {
        m_shaderTimer.restart();
        m_lastFrameTime.store(0);
        qCInfo(lcOverlay) << "Shader timer restarted after system resume";
    }
}

void OverlayService::onShaderError(const QString& errorLog)
{
    qCWarning(lcOverlay) << "Shader error during overlay:" << errorLog;
    m_pendingShaderError = errorLog;
    // Don't set m_shaderErrorPending - retry shaders on next show (fix bugs, don't mask)
}

bool OverlayService::prepareLayoutOsdWindow(QQuickWindow*& window, QRect& screenGeom, qreal& aspectRatio,
                                            const QString& screenName)
{
    // Resolve target screen: explicit name > primary
    // Note: QCursor::pos() is NOT used here — it returns stale data for background
    // daemons on Wayland. Callers should always pass screenName from KWin effect data.
    QScreen* screen = nullptr;
    if (!screenName.isEmpty()) {
        screen = Utils::findScreenByName(screenName);
    }
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        qCWarning(lcOverlay) << "No screen available for layout OSD";
        return false;
    }

    if (!m_layoutOsdWindows.contains(screen)) {
        createLayoutOsdWindow(screen);
    }

    window = m_layoutOsdWindows.value(screen);
    if (!window) {
        qCWarning(lcOverlay) << "Failed to get layout OSD window";
        return false;
    }

    assertWindowOnScreen(window, screen);

    screenGeom = screen->geometry();
    aspectRatio = (screenGeom.height() > 0)
        ? static_cast<qreal>(screenGeom.width()) / screenGeom.height()
        : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);

    return true;
}

void OverlayService::showLayoutOsd(Layout* layout, const QString& screenName)
{
    if (!layout) {
        qCDebug(lcOverlay) << "No layout provided for OSD";
        return;
    }

    if (layout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout:" << layout->name();
        return;
    }

    // Hide any existing layout OSD before showing new one (prevent stale OSD on other screen)
    hideLayoutOsd();

    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio, screenName)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("layoutId"), layout->id().toString());
    writeQmlProperty(window, QStringLiteral("layoutName"), layout->name());
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("category"), static_cast<int>(LayoutCategory::Manual));
    writeQmlProperty(window, QStringLiteral("autoAssign"), layout->autoAssign());
    writeQmlProperty(window, QStringLiteral("zones"), LayoutUtils::zonesToVariantList(layout, ZoneField::Full));
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, screenGeom, aspectRatio);
    QMetaObject::invokeMethod(window, "show");

    qCInfo(lcOverlay) << "Showing layout OSD for:" << layout->name() << "on screen:" << screenName;
}

void OverlayService::showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                                   bool autoAssign, const QString& screenName)
{
    if (zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout:" << name;
        return;
    }

    // Hide any existing layout OSD before showing new one (prevent stale OSD on other screen)
    hideLayoutOsd();

    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio, screenName)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("layoutId"), id);
    writeQmlProperty(window, QStringLiteral("layoutName"), name);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("category"), category);
    writeQmlProperty(window, QStringLiteral("autoAssign"), autoAssign);
    writeQmlProperty(window, QStringLiteral("zones"), zones);
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, screenGeom, aspectRatio);
    QMetaObject::invokeMethod(window, "show");

    qCInfo(lcOverlay) << "Showing layout OSD for:" << name << "category:" << category << "on screen:" << screenName;
}

void OverlayService::hideLayoutOsd()
{
    for (auto* window : std::as_const(m_layoutOsdWindows)) {
        if (window && window->isVisible()) {
            QMetaObject::invokeMethod(window, "hide");
        }
    }
}

void OverlayService::createLayoutOsdWindow(QScreen* screen)
{
    if (m_layoutOsdWindows.contains(screen)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/LayoutOsd.qml")), screen, "layout OSD");
    if (!window) {
        return;
    }

    // Configure LayerShellQt for Wayland overlay (prevents window from appearing in taskbar)
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // Anchors will be set dynamically in showLayoutOsd() based on window size
        layerWindow->setScope(QStringLiteral("plasmazones-layout-osd-%1").arg(screen->name()));
        layerWindow->setExclusiveZone(-1);
    }

    auto layoutOsdConn = connect(window, SIGNAL(dismissed()), this, SLOT(hideLayoutOsd()));
    if (!layoutOsdConn) {
        qCWarning(lcOverlay) << "Failed to connect dismissed signal for layout OSD on screen" << screen->name();
    }
    window->setVisible(false);
    m_layoutOsdWindows.insert(screen, window);
}

void OverlayService::destroyLayoutOsdWindow(QScreen* screen)
{
    if (auto* window = m_layoutOsdWindows.take(screen)) {
        window->close();
        window->deleteLater();
    }
}

void OverlayService::showNavigationOsd(bool success, const QString& action, const QString& reason,
                                       const QString& sourceZoneId, const QString& targetZoneId,
                                       const QString& screenName)
{
    qCDebug(lcOverlay) << "showNavigationOsd called: action=" << action << "reason=" << reason
                       << "screen=" << screenName << "success=" << success;

    // Only show OSD for successful actions - failures (no windows, no zones, etc.) don't need feedback
    if (!success) {
        qCDebug(lcOverlay) << "Skipping navigation OSD for failure:" << action << reason;
        return;
    }

    // Deduplicate: Skip if same action+reason within 200ms (prevents duplicate from Qt signal + D-Bus signal)
    QString actionKey = action + QLatin1String(":") + reason;
    if (actionKey == m_lastNavigationAction + QLatin1String(":") + m_lastNavigationReason
        && m_lastNavigationTime.isValid() && m_lastNavigationTime.elapsed() < 200) {
        qCDebug(lcOverlay) << "Skipping duplicate navigation OSD:" << action << reason;
        return;
    }
    m_lastNavigationAction = action;
    m_lastNavigationReason = reason;
    m_lastNavigationTime.restart();

    // Show on the screen where the navigation occurred, fallback to primary
    QScreen* screen = Utils::findScreenByName(screenName);
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        qCWarning(lcOverlay) << "No screen available for navigation OSD";
        return;
    }

    // Resolve per-screen layout (not the global m_layout which may belong to another screen)
    Layout* screenLayout = resolveScreenLayout(screen);
    if (!screenLayout || screenLayout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "No layout or zones for navigation OSD: screen=" << screen->name()
                           << "layout=" << (screenLayout ? screenLayout->name() : QStringLiteral("null"))
                           << "zones=" << (screenLayout ? screenLayout->zones().size() : 0);
        return;
    }

    // Create window if needed
    if (!m_navigationOsdWindows.contains(screen)) {
        // Only try to create if we haven't failed before (prevents log spam)
        if (!m_navigationOsdCreationFailed.value(screen, false)) {
            createNavigationOsdWindow(screen);
        }
    }

    auto* window = m_navigationOsdWindows.value(screen);
    if (!window) {
        // Only warn once per screen to prevent log spam
        if (!m_navigationOsdCreationFailed.value(screen, false)) {
            qCWarning(lcOverlay) << "Failed to get navigation OSD window for screen:" << screen->name();
            m_navigationOsdCreationFailed.insert(screen, true);
        }
        qCDebug(lcOverlay) << "No navigation OSD window for screen:" << screen->name();
        return;
    }

    // Process reason field - for rotation/resnap, extract window count
    // Format: "clockwise:N" or "counterclockwise:N" or "resnap:N" where N is window count
    int windowCount = 1;
    QString displayReason = reason;
    if (reason.contains(QLatin1Char(':'))) {
        QStringList parts = reason.split(QLatin1Char(':'));
        if (parts.size() >= 2) {
            bool ok = false;
            int count = parts.at(1).toInt(&ok);
            if (ok && count > 0) {
                windowCount = count;
            }
            if (action == QStringLiteral("rotate")) {
                displayReason = parts.at(0); // "clockwise" or "counterclockwise"
            }
            // resnap keeps full reason for displayReason (optional)
        }
    }

    // Set OSD data
    writeQmlProperty(window, QStringLiteral("success"), success);
    writeQmlProperty(window, QStringLiteral("action"), action);
    writeQmlProperty(window, QStringLiteral("reason"), displayReason);
    writeQmlProperty(window, QStringLiteral("windowCount"), windowCount);

    // Pass source zone ID for swap operations
    writeQmlProperty(window, QStringLiteral("sourceZoneId"), sourceZoneId);

    // Build highlighted zone IDs list (target zones)
    QStringList highlightedZoneIds;
    if (!targetZoneId.isEmpty()) {
        highlightedZoneIds.append(targetZoneId);
    }
    writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), highlightedZoneIds);

    // Use shared LayoutUtils with minimal fields for zone number lookup
    // (only need zoneId and zoneNumber, not name/appearance)
    QVariantList zonesList = LayoutUtils::zonesToVariantList(screenLayout, ZoneField::Minimal);
    writeQmlProperty(window, QStringLiteral("zones"), zonesList);

    // Hide any existing navigation OSD before showing new one (prevent overlap)
    hideNavigationOsd();

    // Ensure the window is on the correct Wayland output (must come before sizing —
    // assertWindowOnScreen calls setGeometry(screen) which would override setWidth/setHeight)
    assertWindowOnScreen(window, screen);

    // Size and center: setWidth/setHeight AFTER assertWindowOnScreen so the final
    // QWindow geometry matches the OSD size (same pattern as sizeAndCenterOsd for LayoutOsd)
    const QRect screenGeom = screen->geometry();
    const int osdWidth = 240; // Compact width for text
    const int osdHeight = 70; // Text message + margins
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    centerLayerWindowOnScreen(window, screenGeom, osdWidth, osdHeight);

    // Show with animation
    QMetaObject::invokeMethod(window, "show");

    qCInfo(lcOverlay) << "Showing navigation OSD: success=" << success << "action=" << action
                       << "reason=" << reason << "highlightedZones=" << highlightedZoneIds;
}

void OverlayService::hideNavigationOsd()
{
    for (auto* window : std::as_const(m_navigationOsdWindows)) {
        if (window && window->isVisible()) {
            QMetaObject::invokeMethod(window, "hide");
        }
    }
}

void OverlayService::createNavigationOsdWindow(QScreen* screen)
{
    if (m_navigationOsdWindows.contains(screen)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/NavigationOsd.qml")), screen, "navigation OSD");
    if (!window) {
        m_navigationOsdCreationFailed.insert(screen, true);
        return;
    }

    // Configure LayerShellQt for Wayland overlay
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setScope(QStringLiteral("plasmazones-navigation-osd-%1").arg(screen->name()));
        layerWindow->setExclusiveZone(-1);
    }

    auto navOsdConn = connect(window, SIGNAL(dismissed()), this, SLOT(hideNavigationOsd()));
    if (!navOsdConn) {
        qCWarning(lcOverlay) << "Failed to connect dismissed signal for navigation OSD on screen" << screen->name();
    }
    window->setVisible(false);
    m_navigationOsdWindows.insert(screen, window);
    m_navigationOsdCreationFailed.remove(screen);
}

void OverlayService::destroyNavigationOsdWindow(QScreen* screen)
{
    if (auto* window = m_navigationOsdWindows.take(screen)) {
        window->close();
        window->deleteLater();
    }
    // Clear failed flag when destroying window
    m_navigationOsdCreationFailed.remove(screen);
}

void OverlayService::showShaderPreview(int x, int y, int width, int height, const QString& screenName,
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
    if (!screenName.isEmpty()) {
        screen = Utils::findScreenByName(screenName);
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
    const QVariantMap shaderParams = parseShaderParamsJson(shaderParamsJson, "showShaderPreview:");

    if (!m_shaderPreviewWindow || m_shaderPreviewScreen != screen) {
        destroyShaderPreviewWindow();
        createShaderPreviewWindow(screen);
    }

    if (!m_shaderPreviewWindow) {
        return;
    }

    m_shaderPreviewScreen = screen;
    m_shaderPreviewWindow->setScreen(screen);
    m_shaderPreviewWindow->setGeometry(x, y, width, height);

    if (auto* layerWindow = LayerShellQt::Window::get(m_shaderPreviewWindow)) {
        const QRect screenGeom = screen->geometry();
        const int localX = x - screenGeom.x();
        const int localY = y - screenGeom.y();
        layerWindow->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
        layerWindow->setMargins(QMargins(localX, localY, 0, 0));
    }

    // Shader properties
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("shaderSource"), info.shaderUrl);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("bufferShaderPath"), info.bufferShaderPath);
    QVariantList pathList;
    for (const QString& p : info.bufferShaderPaths) {
        pathList.append(p);
    }
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("bufferShaderPaths"), pathList);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("bufferFeedback"), info.bufferFeedback);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("bufferScale"), info.bufferScale);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("bufferWrap"), info.bufferWrap);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("shaderParams"), QVariant::fromValue(shaderParams));

    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("zones"), zones);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("zoneCount"), zones.size());
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("highlightedCount"), 0);

    const QSize size(qMax(1, width), qMax(1, height));
    const QImage labelsImage = buildLabelsImageForPreviewZones(zones, size, m_settings);
    writeQmlProperty(m_shaderPreviewWindow, QStringLiteral("labelsTexture"), QVariant::fromValue(labelsImage));

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

void OverlayService::updateShaderPreview(int x, int y, int width, int height,
                                         const QString& shaderParamsJson, const QString& zonesJson)
{
    if (!m_shaderPreviewWindow) {
        return;
    }

    if (width > 0 && height > 0) {
        QScreen* screen = m_shaderPreviewWindow->screen();
        if (screen) {
            m_shaderPreviewWindow->setGeometry(x, y, width, height);
            if (auto* layerWindow = LayerShellQt::Window::get(m_shaderPreviewWindow)) {
                const QRect screenGeom = screen->geometry();
                const int localX = x - screenGeom.x();
                const int localY = y - screenGeom.y();
                layerWindow->setMargins(QMargins(localX, localY, 0, 0));
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

    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setScope(QStringLiteral("plasmazones-shader-preview"));
        layerWindow->setExclusiveZone(-1);
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
    // Stop shader timer only if main overlay is also not visible
    if (!m_visible && m_shaderUpdateTimer && m_shaderUpdateTimer->isActive()) {
        stopShaderAnimation();
    }
}

void OverlayService::showSnapAssist(const QString& screenName, const QString& emptyZonesJson,
                                     const QString& candidatesJson)
{
    if (emptyZonesJson.isEmpty() || candidatesJson.isEmpty()) {
        qCDebug(lcOverlay) << "showSnapAssist: no empty zones or candidates";
        Q_EMIT snapAssistDismissed(); // Notify listeners that snap assist won't show
        return;
    }

    QScreen* screen = nullptr;
    if (!screenName.isEmpty()) {
        screen = Utils::findScreenByName(screenName);
    }
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        qCWarning(lcOverlay) << "showSnapAssist: no screen available";
        Q_EMIT snapAssistDismissed();
        return;
    }

    // Always destroy and recreate to avoid stale QML state (zone sizes wrong after continuation)
    destroySnapAssistWindow();
    createSnapAssistWindow();
    if (!m_snapAssistWindow) {
        Q_EMIT snapAssistDismissed();
        return;
    }

    m_snapAssistScreen = screen;
    m_snapAssistWindow->setScreen(screen);

    // Parse JSON using shared helper (same format: array of objects)
    const QVariantList zonesList = parseZonesJson(emptyZonesJson, "showSnapAssist:");
    QVariantList candidatesList = parseZonesJson(candidatesJson, "showSnapAssist:");

    // Start async thumbnail capture via KWin ScreenShot2. Overlay shows icons immediately.
    // Requires KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 when desktop matching fails (local install).
    // Sequential capture (one at a time) to avoid overwhelming KWin; concurrent CaptureWindow
    // requests can cause thumbnails to stop working after the first few.
    if (!m_thumbnailService) {
        m_thumbnailService = std::make_unique<WindowThumbnailService>(this);
        connect(m_thumbnailService.get(), &WindowThumbnailService::captureFinished, this,
                [this](const QString& kwinHandle, const QString& dataUrl) {
                    updateSnapAssistCandidateThumbnail(kwinHandle, dataUrl);
                    processNextThumbnailCapture();
                });
    }
    // Apply cached thumbnails and queue only uncached ones (reuse across continuation)
    m_snapAssistCandidates.clear();
    m_thumbnailCaptureQueue.clear();
    if (m_thumbnailService->isAvailable()) {
        for (int i = 0; i < candidatesList.size(); ++i) {
            QVariantMap cand = candidatesList[i].toMap();
            QString kwinHandle = cand.value(QStringLiteral("kwinHandle")).toString();
            if (!kwinHandle.isEmpty()) {
                auto it = m_thumbnailCache.constFind(kwinHandle);
                if (it != m_thumbnailCache.constEnd() && !it.value().isEmpty()) {
                    cand[QStringLiteral("thumbnail")] = it.value();
                } else {
                    m_thumbnailCaptureQueue.append(kwinHandle);
                }
            }
            m_snapAssistCandidates.append(cand);
        }
        qCDebug(lcOverlay) << "showSnapAssist: " << m_thumbnailCache.size() << "cached,"
                          << m_thumbnailCaptureQueue.size() << "to capture";
        processNextThumbnailCapture();
    } else {
        m_snapAssistCandidates = candidatesList;
        qCDebug(lcOverlay) << "showSnapAssist: thumbnail service not available (auth?)";
    }

    writeQmlProperty(m_snapAssistWindow, QStringLiteral("emptyZones"), zonesList);
    writeQmlProperty(m_snapAssistWindow, QStringLiteral("candidates"), m_snapAssistCandidates);
    writeQmlProperty(m_snapAssistWindow, QStringLiteral("screenWidth"), screen->geometry().width());
    writeQmlProperty(m_snapAssistWindow, QStringLiteral("screenHeight"), screen->geometry().height());

    // Zone appearance defaults (used when zone.useCustomColors is false) - match main overlay
    if (m_settings) {
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("highlightColor"), m_settings->highlightColor());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("inactiveColor"), m_settings->inactiveColor());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderColor"), m_settings->borderColor());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("activeOpacity"), m_settings->activeOpacity());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("inactiveOpacity"), m_settings->inactiveOpacity());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderWidth"), m_settings->borderWidth());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderRadius"), m_settings->borderRadius());
    }

    // Match main overlay: full-screen anchors so zone coordinates (overlay-local) line up
    if (auto* layerWindow = LayerShellQt::Window::get(m_snapAssistWindow)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerTop);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        layerWindow->setAnchors(LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
            | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layerWindow->setExclusiveZone(-1);
        layerWindow->setScope(QStringLiteral("plasmazones-snap-assist"));
    }

    assertWindowOnScreen(m_snapAssistWindow, screen);
    m_snapAssistWindow->setGeometry(screen->geometry());
    m_snapAssistWindow->show();
    // Ensure the window receives keyboard focus for Escape handling on Wayland.
    // KeyboardInteractivityExclusive tells the compositor to send keyboard events,
    // but Qt may not set internal focus without an explicit activation request.
    m_snapAssistWindow->requestActivate();
    qCInfo(lcOverlay) << "showSnapAssist: screen=" << screenName << "zones=" << zonesList.size()
                      << "candidates=" << candidatesList.size();

    Q_EMIT snapAssistShown(screenName, emptyZonesJson, candidatesJson);
}

void OverlayService::setSnapAssistThumbnail(const QString& kwinHandle, const QString& dataUrl)
{
    updateSnapAssistCandidateThumbnail(kwinHandle, dataUrl);
}

void OverlayService::updateSnapAssistCandidateThumbnail(const QString& kwinHandle, const QString& dataUrl)
{
    if (dataUrl.isEmpty()) {
        return;
    }
    m_thumbnailCache.insert(kwinHandle, dataUrl);
    if (!m_snapAssistWindow || !m_snapAssistWindow->isVisible()) {
        return;
    }
    for (int i = 0; i < m_snapAssistCandidates.size(); ++i) {
        QVariantMap cand = m_snapAssistCandidates[i].toMap();
        if (cand.value(QStringLiteral("kwinHandle")).toString() == kwinHandle) {
            cand[QStringLiteral("thumbnail")] = dataUrl;
            m_snapAssistCandidates[i] = cand;
            writeQmlProperty(m_snapAssistWindow, QStringLiteral("candidates"), m_snapAssistCandidates);
            qCDebug(lcOverlay) << "SnapAssist: thumbnail updated for" << kwinHandle;
            break;
        }
    }
}

void OverlayService::processNextThumbnailCapture()
{
    if (!m_thumbnailService || m_thumbnailCaptureQueue.isEmpty()) {
        return;
    }
    const QString kwinHandle = m_thumbnailCaptureQueue.takeFirst();
    m_thumbnailService->captureWindowAsync(kwinHandle, 256);
}

bool OverlayService::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_snapAssistWindow && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            // Defer destruction to avoid deleting the window from within its own event handler
            QTimer::singleShot(0, this, &OverlayService::hideSnapAssist);
            return true;
        }
    }
    if (obj == m_layoutPickerWindow && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            QTimer::singleShot(0, this, &OverlayService::hideLayoutPicker);
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void OverlayService::hideSnapAssist()
{
    bool wasVisible = isSnapAssistVisible();
    m_thumbnailCache.clear();
    destroySnapAssistWindow();
    if (wasVisible) {
        Q_EMIT snapAssistDismissed();
    }
}

bool OverlayService::isSnapAssistVisible() const
{
    return m_snapAssistWindow && m_snapAssistWindow->isVisible();
}

void OverlayService::createSnapAssistWindow()
{
    if (m_snapAssistWindow) {
        return;
    }

    QScreen* screen = Utils::primaryScreen();
    if (!screen) {
        qCWarning(lcOverlay) << "createSnapAssistWindow: no screen";
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/SnapAssistOverlay.qml")), screen, "snap assist");
    if (!window) {
        qCWarning(lcOverlay) << "Failed to create snap assist overlay";
        return;
    }

    connect(window, &QObject::destroyed, this, [this]() {
        m_snapAssistWindow = nullptr;
        m_snapAssistScreen = nullptr;
    });

    // Emit snapAssistDismissed when the window is closed by QML (user selection, backdrop click, Escape)
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) {
            Q_EMIT snapAssistDismissed();
        }
    });

    // Connect windowSelected from QML: convert overlay-local geometry to screen
    // coordinates before emitting (KWin effect needs global coordinates for moveResize)
    connect(window, SIGNAL(windowSelected(QString, QString, QString)), this,
            SLOT(onSnapAssistWindowSelected(QString, QString, QString)));

    // Install event filter for reliable Escape key handling on Wayland.
    // The QML Shortcut may not fire if the layer shell keyboard focus
    // isn't fully reflected in Qt's internal focus model.
    window->installEventFilter(this);

    m_snapAssistWindow = window;
    m_snapAssistScreen = screen;
    window->setVisible(false);
}

void OverlayService::destroySnapAssistWindow()
{
    if (m_snapAssistWindow) {
        // Disconnect visibleChanged before closing to prevent spurious snapAssistDismissed
        // when the window is being destroyed and recreated (e.g. showSnapAssist recreate cycle)
        disconnect(m_snapAssistWindow, &QWindow::visibleChanged, this, nullptr);
        m_snapAssistWindow->close();
        m_snapAssistWindow->deleteLater();
        m_snapAssistWindow = nullptr;
    }
    m_snapAssistScreen = nullptr;
}

void OverlayService::onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId,
                                                const QString& geometryJson)
{
    QString screenName = m_snapAssistScreen ? m_snapAssistScreen->name() : QString();
    // geometryJson is overlay-local; daemon will fetch authoritative zone geometry from service
    Q_EMIT snapAssistWindowSelected(windowId, zoneId, geometryJson, screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Layout Picker Overlay
// ═══════════════════════════════════════════════════════════════════════════════

void OverlayService::showLayoutPicker(const QString& screenName)
{
    // Guard: if picker window already exists (visible or being set up), do nothing.
    // Prevents double-trigger when shortcut fires before KeyboardInteractivityExclusive
    // grabs the keyboard on Wayland, and avoids deleteLater() races with stale grabs.
    if (m_layoutPickerWindow) {
        return;
    }

    // Resolve target screen
    QScreen* screen = nullptr;
    if (!screenName.isEmpty()) {
        screen = Utils::findScreenByName(screenName);
    }
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        qCWarning(lcOverlay) << "showLayoutPicker: no screen available";
        return;
    }

    // Always destroy and recreate for fresh state
    destroyLayoutPickerWindow();
    createLayoutPickerWindow(screen);
    if (!m_layoutPickerWindow) {
        return;
    }

    m_layoutPickerWindow->setScreen(screen);

    // Build layouts list
    const QString screenId = Utils::screenIdentifier(screen);
    QVariantList layoutsList = buildLayoutsList(screenId);
    if (layoutsList.isEmpty()) {
        qCDebug(lcOverlay) << "showLayoutPicker: no layouts available";
        destroyLayoutPickerWindow();
        return;
    }

    // Determine active layout ID
    QString activeId;
    if (m_layoutManager) {
        Layout* activeLayout = resolveScreenLayout(screen);
        if (activeLayout) {
            activeId = activeLayout->id().toString();
        }
    }

    // Calculate screen aspect ratio
    const QRect screenGeom = screen->geometry();
    qreal aspectRatio = (screenGeom.height() > 0)
        ? static_cast<qreal>(screenGeom.width()) / screenGeom.height()
        : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);

    // Set properties
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("layouts"), layoutsList);
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("activeLayoutId"), activeId);
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeFontProperties(m_layoutPickerWindow, m_settings);

    // Theme colors
    if (m_settings) {
        writeQmlProperty(m_layoutPickerWindow, QStringLiteral("highlightColor"), m_settings->highlightColor());
    }

    // Full-screen layer shell with keyboard interactivity
    if (auto* layerWindow = LayerShellQt::Window::get(m_layoutPickerWindow)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerTop);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        layerWindow->setAnchors(LayerShellQt::Window::Anchors(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
            | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layerWindow->setExclusiveZone(-1);
        layerWindow->setScope(QStringLiteral("plasmazones-layout-picker"));
    }

    assertWindowOnScreen(m_layoutPickerWindow, screen);
    m_layoutPickerWindow->setGeometry(screenGeom);
    QMetaObject::invokeMethod(m_layoutPickerWindow, "show");
    m_layoutPickerWindow->requestActivate();

    qCInfo(lcOverlay) << "showLayoutPicker: screen=" << screen->name()
                      << "layouts=" << layoutsList.size() << "active=" << activeId;
}

void OverlayService::hideLayoutPicker()
{
    destroyLayoutPickerWindow();
}

bool OverlayService::isLayoutPickerVisible() const
{
    return m_layoutPickerWindow && m_layoutPickerWindow->isVisible();
}

void OverlayService::createLayoutPickerWindow(QScreen* screen)
{
    if (m_layoutPickerWindow) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/LayoutPickerOverlay.qml")), screen, "layout picker");
    if (!window) {
        qCWarning(lcOverlay) << "Failed to create layout picker overlay";
        return;
    }

    connect(window, &QObject::destroyed, this, [this]() {
        m_layoutPickerWindow = nullptr;
    });

    // Connect layoutSelected and dismissed signals from QML
    connect(window, SIGNAL(layoutSelected(QString)), this, SLOT(onLayoutPickerSelected(QString)));
    connect(window, SIGNAL(dismissed()), this, SLOT(hideLayoutPicker()));

    // Install event filter for reliable Escape key handling on Wayland
    window->installEventFilter(this);

    m_layoutPickerWindow = window;
    window->setVisible(false);
}

void OverlayService::destroyLayoutPickerWindow()
{
    if (m_layoutPickerWindow) {
        disconnect(m_layoutPickerWindow, &QWindow::visibleChanged, this, nullptr);
        m_layoutPickerWindow->close();
        m_layoutPickerWindow->deleteLater();
        m_layoutPickerWindow = nullptr;
    }
}

void OverlayService::onLayoutPickerSelected(const QString& layoutId)
{
    qCInfo(lcOverlay) << "Layout picker selected:" << layoutId;
    hideLayoutPicker();
    Q_EMIT layoutPickerSelected(layoutId);
}

} // namespace PlasmaZones
