// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice.h"
#include "../config/configdefaults.h"

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
#include <QPointer>
#include <QTimer>
#include <QMutexLocker>
#include "../core/logging.h"
#include <KLocalizedContext>
#include <cmath>

#include <LayerShellQt/Window>
#include <LayerShellQt/Shell>

namespace PlasmaZones {

namespace {

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

struct ZoneSelectorLayout
{
    int indicatorWidth = 180;
    int indicatorHeight = 101;
    int indicatorSpacing = 18;
    int containerPadding = 36;
    int containerTopMargin = 10;
    int containerSideMargin = 10; // Margin from left/right screen edge
    int labelTopMargin = 8;
    int labelHeight = 20;
    int labelSpace = 28;
    int paddingSide = 18;
    int columns = 1;
    int rows = 1; // Visible rows (may be limited by maxRows)
    int totalRows = 1; // Total rows (for scroll content height)
    int contentWidth = 0;
    int contentHeight = 0;
    int scrollContentHeight = 0; // Full content height for scrolling
    int scrollContentWidth = 0;  // Full content width for horizontal scrolling
    int containerWidth = 0;
    int containerHeight = 0;
    int barHeight = 0;
    int barWidth = 0;
    bool needsScrolling = false;
    bool needsHorizontalScrolling = false;
};

ZoneSelectorLayout computeZoneSelectorLayout(const ISettings* settings, QScreen* screen, int layoutCount)
{
    ZoneSelectorLayout layout;
    const QRect screenGeom = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    const qreal screenAspectRatio =
        screenGeom.height() > 0 ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);

    // Determine size mode (Auto vs Manual) - use ConfigDefaults for null-safety
    const ZoneSelectorSizeMode sizeMode = settings ? settings->zoneSelectorSizeMode()
        : static_cast<ZoneSelectorSizeMode>(ConfigDefaults::sizeMode());
    const int maxRows = settings ? settings->zoneSelectorMaxRows() : ConfigDefaults::maxRows();

    if (sizeMode == ZoneSelectorSizeMode::Auto) {
        // Auto-sizing: Calculate preview size as ~10% of screen width, bounded 120-280px
        // This follows KDE HIG principles for adaptive sizing
        const int autoWidth = qBound(120, screenGeom.width() / 10, 280);
        layout.indicatorWidth = autoWidth;
        // Always lock aspect ratio in Auto mode for consistent appearance
        layout.indicatorHeight = qRound(static_cast<qreal>(layout.indicatorWidth) / screenAspectRatio);
    } else {
        // Manual mode: Use explicit settings
        if (settings) {
            layout.indicatorWidth = settings->zoneSelectorPreviewWidth();
            if (settings->zoneSelectorPreviewLockAspect()) {
                layout.indicatorHeight = qRound(static_cast<qreal>(layout.indicatorWidth) / screenAspectRatio);
            } else {
                layout.indicatorHeight = settings->zoneSelectorPreviewHeight();
            }
        }
    }

    const int safeLayoutCount = std::max(1, layoutCount);
    const ZoneSelectorLayoutMode layoutMode =
        settings ? settings->zoneSelectorLayoutMode() : ZoneSelectorLayoutMode::Grid;

    if (layoutMode == ZoneSelectorLayoutMode::Vertical) {
        layout.columns = 1;
        layout.rows = safeLayoutCount;
    } else if (layoutMode == ZoneSelectorLayoutMode::Grid) {
        // Always respect explicit grid columns setting (Auto mode only affects preview dimensions)
        const int gridColumns = settings ? settings->zoneSelectorGridColumns() : ConfigDefaults::gridColumns();
        layout.columns = std::max(1, gridColumns);
        layout.rows = static_cast<int>(std::ceil(static_cast<qreal>(safeLayoutCount) / layout.columns));
    } else {
        // Horizontal mode
        layout.columns = safeLayoutCount;
        layout.rows = 1;
    }

    // Store total rows before limiting for visible area
    layout.totalRows = layout.rows;

    layout.labelSpace = layout.labelTopMargin + layout.labelHeight;
    layout.paddingSide = layout.containerPadding / 2;

    // Step 1: Apply maxRows setting (Auto mode, Grid only)
    // maxRows only makes sense for Grid: in Vertical each layout is a row (maxRows would
    // arbitrarily hide layouts), in Horizontal there's always 1 row. Screen-based clamping
    // in Step 2 handles overflow for all modes.
    int visibleRows = layout.rows;
    if (sizeMode == ZoneSelectorSizeMode::Auto && layoutMode == ZoneSelectorLayoutMode::Grid && layout.rows > maxRows) {
        visibleRows = maxRows;
    }

    // Step 2: Screen-based clamping (all size modes)
    // Ensure the popup never exceeds screen bounds, enabling scrolling for overflow
    const int screenH = screenGeom.height();
    const int screenW = screenGeom.width();
    const int maxContentH = std::max(0, screenH - layout.containerPadding - 2 * layout.containerTopMargin);
    const int maxContentW = std::max(0, screenW - layout.containerPadding - 2 * layout.containerSideMargin);
    const int rowUnitH = layout.indicatorHeight + layout.labelSpace + layout.indicatorSpacing;
    if (rowUnitH > 0) {
        const int maxFittingRows = std::max(1, (maxContentH + layout.indicatorSpacing) / rowUnitH);
        if (visibleRows > maxFittingRows) {
            visibleRows = maxFittingRows;
        }
    }

    layout.rows = visibleRows;
    layout.needsScrolling = (layout.totalRows > visibleRows);

    // Full content dimensions (all items, for scroll content)
    layout.scrollContentWidth = layout.columns * layout.indicatorWidth + (layout.columns - 1) * layout.indicatorSpacing;
    layout.scrollContentHeight = layout.totalRows * (layout.indicatorHeight + layout.labelSpace)
        + (layout.totalRows - 1) * layout.indicatorSpacing;

    // Visible content dimensions (may be clamped to screen)
    layout.contentWidth = layout.scrollContentWidth;
    layout.contentHeight =
        visibleRows * (layout.indicatorHeight + layout.labelSpace) + (visibleRows - 1) * layout.indicatorSpacing;

    // Horizontal screen clamping (primarily for horizontal layout mode)
    if (layout.contentWidth > maxContentW && maxContentW > 0) {
        layout.contentWidth = maxContentW;
        layout.needsHorizontalScrolling = true;
    }

    layout.containerWidth = layout.contentWidth + layout.containerPadding;
    layout.containerHeight = layout.contentHeight + layout.containerPadding;
    layout.barHeight = layout.containerTopMargin + layout.containerHeight;
    // Include side margins so corner/side positions have room for margin
    layout.barWidth = layout.containerSideMargin + layout.containerWidth + layout.containerSideMargin;

    return layout;
}

void updateZoneSelectorComputedProperties(QQuickWindow* window, QScreen* screen, ISettings* settings,
                                          const ZoneSelectorLayout& layout)
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

    // Compute positionIsVertical
    if (settings) {
        const ZoneSelectorPosition pos = settings->zoneSelectorPosition();
        writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                         (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));

        // Compute scaled zone appearance values
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

void updateZoneSelectorWindowLayout(QQuickWindow* window, QScreen* screen, ISettings* settings, int layoutCount)
{
    if (!window || !screen) {
        return;
    }

    const ZoneSelectorLayout layout = computeZoneSelectorLayout(settings, screen, layoutCount);

    // Set positionIsVertical before layout properties; QML anchors depend on it for
    // containerWidth/Height, so it has to be correct before we apply the layout.
    if (settings) {
        const ZoneSelectorPosition pos = settings->zoneSelectorPosition();
        writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                         (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));
    }

    applyZoneSelectorLayout(window, layout);

    // Update computed properties that depend on layout and settings
    updateZoneSelectorComputedProperties(window, screen, settings, layout);

    const ZoneSelectorPosition pos = settings ? settings->zoneSelectorPosition() : ZoneSelectorPosition::Top;

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
        if (cursorScreen && m_settings && m_settings->isMonitorDisabled(cursorScreen->name())) {
            return;
        }
    }

    initializeOverlay(cursorScreen);
}

void OverlayService::showAtPosition(int cursorX, int cursorY)
{
    if (m_visible) {
        return;
    }

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
        if (cursorScreen && m_settings && m_settings->isMonitorDisabled(cursorScreen->name())) {
            return;
        }
    }

    initializeOverlay(cursorScreen);
}

void OverlayService::initializeOverlay(QScreen* cursorScreen)
{
    // Determine if we should show on all monitors (cursorScreen == nullptr means all)
    const bool showOnAllMonitors = (cursorScreen == nullptr);

    m_visible = true;

    // Initialize shader timing (shared across all monitors for synchronized effects)
    {
        QMutexLocker locker(&m_shaderTimerMutex);
        m_shaderTimer.start();
        m_lastFrameTime.store(0);
        m_frameCount.store(0);
    }
    m_zoneDataDirty = true; // Rebuild zone data on next frame

    for (auto* screen : Utils::allScreens()) {
        // Skip screens that aren't the cursor's screen when single-monitor mode is enabled
        if (!showOnAllMonitors && screen != cursorScreen) {
            continue;
        }
        // Skip monitors where PlasmaZones is disabled
        if (m_settings && m_settings->isMonitorDisabled(screen->name())) {
            continue;
        }

        if (!m_overlayWindows.contains(screen)) {
            createOverlayWindow(screen);
        }
        if (auto* window = m_overlayWindows.value(screen)) {
            updateOverlayWindow(screen);
            window->show();
        }
    }

    // Check if we need to recreate windows - this handles the case where windows
    // were created before shaders were ready (e.g., at startup after reboot)
    const bool shouldUseShader = useShaderOverlay();
    bool needsRecreate = false;

    // Check if any existing windows are the wrong type
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
        if (windowIsShader != shouldUseShader) {
            needsRecreate = true;
            qCDebug(lcOverlay) << "Overlay window type mismatch detected, will recreate"
                               << "(window is shader:" << windowIsShader << "should be:" << shouldUseShader << ")";
            break;
        }
    }

    // Recreate windows if type mismatch detected
    if (needsRecreate) {
        const auto screens = m_overlayWindows.keys();
        for (QScreen* screen : screens) {
            destroyOverlayWindow(screen);
        }
        for (QScreen* screen : screens) {
            if (!m_settings || !m_settings->isMonitorDisabled(screen->name())) {
                createOverlayWindow(screen);
                updateOverlayWindow(screen);
                if (auto* window = m_overlayWindows.value(screen)) {
                    window->show();
                }
            }
        }
    }

    if (shouldUseShader) {
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
        if (useShaderOverlay()) {
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
            if (m_settings->isMonitorDisabled(screen->name())) {
                if (auto* window = m_overlayWindows.value(screen)) {
                    window->hide();
                }
            }
        }
        for (auto* screen : m_zoneSelectorWindows.keys()) {
            if (m_settings->isMonitorDisabled(screen->name())) {
                if (auto* window = m_zoneSelectorWindows.value(screen)) {
                    window->hide();
                }
            }
        }
    }

    if (m_visible) {
        for (auto* screen : m_overlayWindows.keys()) {
            if (m_settings && m_settings->isMonitorDisabled(screen->name())) {
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
            if (m_settings && m_settings->isMonitorDisabled(screen->name())) {
                continue;
            }
            updateZoneSelectorWindow(screen);
        }
    }

    // Keep selector windows updated with the latest settings and layout data
    if (!m_zoneSelectorWindows.isEmpty()) {
        for (auto* screen : m_zoneSelectorWindows.keys()) {
            if (m_settings && m_settings->isMonitorDisabled(screen->name())) {
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
            window->setProperty("highlightedZoneId", zoneId);
            // Clear multi-zone highlighting when using single zone
            window->setProperty("highlightedZoneIds", QVariantList());
        }
    }
}

void OverlayService::highlightZones(const QStringList& zoneIds)
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    // Update the highlightedZoneIds property on all overlay windows
    // Use QQmlProperty to properly set QML property (setProperty() doesn't always work)
    QVariantList zoneIdList;
    for (const QString& zoneId : zoneIds) {
        zoneIdList.append(zoneId);
    }

    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            // Use QQmlProperty to set QML property directly (works better than setProperty for QML properties)
            QQmlProperty highlightIdsProp(window, QStringLiteral("highlightedZoneIds"));
            highlightIdsProp.write(zoneIdList);

            // Clear single zone highlighting when using multi-zone
            QQmlProperty highlightIdProp(window, QStringLiteral("highlightedZoneId"));
            highlightIdProp.write(QString());
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
            window->setProperty("highlightedZoneId", QString());
            window->setProperty("highlightedZoneIds", QVariantList());
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
                    const bool shouldUseShader = useShaderOverlay();

                    // Only recreate if the overlay type actually needs to change
                    if (wasUsingShader != shouldUseShader) {
                        qCDebug(lcOverlay) << "Shader effects setting changed, recreating overlay windows"
                                           << "(was:" << wasUsingShader << "now:" << shouldUseShader << ")";

                        // Stop shader animation if it was running
                        if (wasUsingShader) {
                            stopShaderAnimation();
                        }

                        // Store current visibility state
                        const bool wasVisible = m_visible;

                        // Recreate all overlay windows
                        const auto screens = m_overlayWindows.keys();
                        for (QScreen* screen : screens) {
                            destroyOverlayWindow(screen);
                        }

                        // Recreate windows with correct type
                        for (QScreen* screen : screens) {
                            if (!m_settings || !m_settings->isMonitorDisabled(screen->name())) {
                                createOverlayWindow(screen);
                                updateOverlayWindow(screen);
                                if (wasVisible && m_overlayWindows.value(screen)) {
                                    m_overlayWindows.value(screen)->show();
                                }
                            }
                        }

                        // Start shader animation if needed
                        if (shouldUseShader && wasVisible) {
                            updateZonesForAllWindows(); // Push initial zone data
                            startShaderAnimation();
                        }
                    }
                }
            });
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
            // Update zone selector and overlay windows when active layout changes (via shortcuts, etc.)
            connect(manager, &LayoutManager::activeLayoutChanged, this, [this](Layout* /*layout*/) {
                for (QScreen* screen : m_zoneSelectorWindows.keys()) {
                    updateZoneSelectorWindow(screen);
                }
                for (QScreen* screen : m_overlayWindows.keys()) {
                    updateOverlayWindow(screen);
                }
            });

            // Update zone selector and overlay windows when a layout is assigned to a screen
            connect(manager, &LayoutManager::layoutAssigned, this, [this](const QString& /*screenName*/, Layout* /*layout*/) {
                for (QScreen* screen : m_zoneSelectorWindows.keys()) {
                    updateZoneSelectorWindow(screen);
                }
                for (QScreen* screen : m_overlayWindows.keys()) {
                    updateOverlayWindow(screen);
                }
            });
        }
    }
}

void OverlayService::setCurrentVirtualDesktop(int desktop)
{
    if (m_currentVirtualDesktop != desktop) {
        m_currentVirtualDesktop = desktop;
        qCDebug(lcOverlay) << "Virtual desktop changed to" << desktop;

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
        qCDebug(lcOverlay) << "Activity changed activity= " << activityId;

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

void OverlayService::handleScreenAdded(QScreen* screen)
{
    if (m_visible && screen && (!m_settings || !m_settings->isMonitorDisabled(screen->name()))) {
        createOverlayWindow(screen);
        updateOverlayWindow(screen);
        if (auto* window = m_overlayWindows.value(screen)) {
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

void OverlayService::showZoneSelector()
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
        // Skip monitors where PlasmaZones is disabled
        if (m_settings && m_settings->isMonitorDisabled(screen->name())) {
            continue;
        }
        if (!m_zoneSelectorWindows.contains(screen)) {
            createZoneSelectorWindow(screen);
        }
        if (auto* window = m_zoneSelectorWindows.value(screen)) {
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
        const ZoneSelectorLayout layout = computeZoneSelectorLayout(m_settings, screen, layoutCount);

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

    // Configure LayerShellQt for zone selector (LayerTop for pointer input)
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
        layerWindow->setLayer(LayerShellQt::Window::LayerTop);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);

        ZoneSelectorPosition pos = m_settings ? m_settings->zoneSelectorPosition() : ZoneSelectorPosition::Top;
        layerWindow->setAnchors(getAnchorsForPosition(pos));
        layerWindow->setExclusiveZone(-1);
        layerWindow->setScope(QStringLiteral("plasmazones-selector-%1").arg(screen->name()));
    }

    // Set screen properties for layout preview scaling
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("screenWidth"), screenGeom.width());

    // Pass zone appearance settings for scaled preview
    if (m_settings) {
        writeQmlProperty(window, QStringLiteral("zonePadding"), m_settings->zonePadding());
        writeQmlProperty(window, QStringLiteral("zoneBorderWidth"), m_settings->borderWidth());
        writeQmlProperty(window, QStringLiteral("zoneBorderRadius"), m_settings->borderRadius());
        writeQmlProperty(window, QStringLiteral("selectorPosition"),
                         static_cast<int>(m_settings->zoneSelectorPosition()));
        writeQmlProperty(window, QStringLiteral("selectorLayoutMode"),
                         static_cast<int>(m_settings->zoneSelectorLayoutMode()));
        writeQmlProperty(window, QStringLiteral("selectorGridColumns"), m_settings->zoneSelectorGridColumns());
        writeQmlProperty(window, QStringLiteral("previewWidth"), m_settings->zoneSelectorPreviewWidth());
        writeQmlProperty(window, QStringLiteral("previewHeight"), m_settings->zoneSelectorPreviewHeight());
        writeQmlProperty(window, QStringLiteral("previewLockAspect"), m_settings->zoneSelectorPreviewLockAspect());
    }

    const int layoutCount = LayoutUtils::buildUnifiedLayoutList(m_layoutManager).size();
    updateZoneSelectorWindowLayout(window, screen, m_settings, layoutCount);

    window->setVisible(false);
    connect(window, SIGNAL(zoneSelected(QString, int, QVariant)), this, SLOT(onZoneSelected(QString, int, QVariant)));
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

    // Update settings-based properties
    if (m_settings) {
        writeQmlProperty(window, QStringLiteral("highlightColor"), m_settings->highlightColor());
        writeQmlProperty(window, QStringLiteral("inactiveColor"), m_settings->inactiveColor());
        writeQmlProperty(window, QStringLiteral("borderColor"), m_settings->borderColor());
        // Zone appearance settings for scaled preview
        writeQmlProperty(window, QStringLiteral("zonePadding"), m_settings->zonePadding());
        writeQmlProperty(window, QStringLiteral("zoneBorderWidth"), m_settings->borderWidth());
        writeQmlProperty(window, QStringLiteral("zoneBorderRadius"), m_settings->borderRadius());
        writeQmlProperty(window, QStringLiteral("selectorPosition"),
                         static_cast<int>(m_settings->zoneSelectorPosition()));
        writeQmlProperty(window, QStringLiteral("selectorLayoutMode"),
                         static_cast<int>(m_settings->zoneSelectorLayoutMode()));
        writeQmlProperty(window, QStringLiteral("selectorGridColumns"), m_settings->zoneSelectorGridColumns());
        writeQmlProperty(window, QStringLiteral("previewWidth"), m_settings->zoneSelectorPreviewWidth());
        writeQmlProperty(window, QStringLiteral("previewHeight"), m_settings->zoneSelectorPreviewHeight());
        writeQmlProperty(window, QStringLiteral("previewLockAspect"), m_settings->zoneSelectorPreviewLockAspect());
    }

    // Build and pass layout data (all available layouts with their zones)
    QVariantList layouts = buildLayoutsList();
    writeQmlProperty(window, QStringLiteral("layouts"), layouts);

    // Set active layout ID
    QString activeLayoutId;
    if (m_layoutManager) {
        // Prefer the currently active layout (set via shortcut or zone selector selection)
        // Fall back to screen assignment only if no active layout is set
        Layout* activeLayout = m_layoutManager->activeLayout();
        if (activeLayout) {
            activeLayoutId = activeLayout->id().toString();
        } else {
            // No active layout - try screen-specific assignment
            Layout* screenLayout =
                m_layoutManager->layoutForScreen(screen->name(), m_currentVirtualDesktop, m_currentActivity);
            if (screenLayout) {
                activeLayoutId = screenLayout->id().toString();
            } else if (m_layout) {
                activeLayoutId = m_layout->id().toString();
            }
        }
    } else if (m_layout) {
        activeLayoutId = m_layout->id().toString();
    }
    writeQmlProperty(window, QStringLiteral("activeLayoutId"), activeLayoutId);

    // Compute layout for geometry updates
    const int layoutCount = layouts.size();
    const ZoneSelectorLayout layout = computeZoneSelectorLayout(m_settings, screen, layoutCount);

    // Set positionIsVertical before layout properties; QML anchors depend on it for
    // containerWidth/Height, so it has to be correct before we apply the layout.
    if (m_settings) {
        const ZoneSelectorPosition pos = m_settings->zoneSelectorPosition();
        writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                         (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));
    }

    // Apply layout and geometry
    applyZoneSelectorLayout(window, layout);

    // Update computed properties that depend on layout and settings
    updateZoneSelectorComputedProperties(window, screen, m_settings, layout);

    // Force QML to process property updates immediately
    if (auto* contentRoot = window->contentItem()) {
        contentRoot->polish();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    const ZoneSelectorPosition pos = m_settings ? m_settings->zoneSelectorPosition() : ZoneSelectorPosition::Top;
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

        // Force QML to process property updates and layout changes
        contentRoot->polish();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // Force QML items to recalculate layout
    if (auto* contentRoot = window->contentItem()) {
        if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
            gridItem->polish();
            gridItem->update();
        }
        if (auto* containerItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContainer"))) {
            containerItem->polish();
            containerItem->update();
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

void OverlayService::createOverlayWindow(QScreen* screen)
{
    if (m_overlayWindows.contains(screen)) {
        return;
    }

    // Choose overlay type based on shader settings
    bool usingShader = useShaderOverlay();

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
    if (usingShader && m_layout) {
        auto* registry = ShaderRegistry::instance();
        if (registry) {
            const QString shaderId = m_layout->shaderId();
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
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, m_layout->shaderParams());
            writeQmlProperty(window, QStringLiteral("shaderParams"), QVariant::fromValue(translatedParams));
        }
    }

    // Configure LayerShellQt for full-screen overlay
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
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
        window->setProperty("zoneDataVersion", m_zoneDataVersion);
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
    // Prefer the currently active layout, fall back to screen assignment
    Layout* screenLayout = nullptr;
    if (m_layoutManager) {
        screenLayout = m_layoutManager->activeLayout();
        if (!screenLayout) {
            screenLayout =
                m_layoutManager->layoutForScreen(screen->name(), m_currentVirtualDesktop, m_currentActivity);
        }
    }
    if (!screenLayout) {
        screenLayout = m_layout;
    }

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
    }

    // Update shader-specific properties if using shader overlay
    // Only update if this window is actually a shader overlay window (check isShaderOverlay property)
    const bool windowIsShader = window->property("isShaderOverlay").toBool();
    if (windowIsShader && useShaderOverlay() && screenLayout) {
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
    } else if (windowIsShader && !useShaderOverlay()) {
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
    window->setProperty("zones", patched);

    // Shader overlay: zoneCount, highlightedCount, zoneDataVersion, labelsTexture
    if (useShaderOverlay()) {
        int highlightedCount = 0;
        for (const QVariant& z : patched) {
            if (z.toMap().value(QLatin1String("isHighlighted")).toBool()) {
                ++highlightedCount;
            }
        }
        window->setProperty("zoneCount", patched.size());
        window->setProperty("highlightedCount", highlightedCount);
        ++m_zoneDataVersion;

        updateLabelsTextureForWindow(window, patched, screen, screenLayout);
        for (auto* w : std::as_const(m_overlayWindows)) {
            if (w) {
                w->setProperty("zoneDataVersion", m_zoneDataVersion);
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
    const QColor numberColor = m_settings ? m_settings->numberColor() : QColor(Qt::white);
    QColor backgroundColor = Qt::black;
    if (m_settings) {
        KColorScheme scheme(QPalette::Active, KColorScheme::View);
        backgroundColor = scheme.background(KColorScheme::NormalBackground).color();
    }
    const QSize size(qMax(1, static_cast<int>(window->width())), qMax(1, static_cast<int>(window->height())));
    QImage labelsImage =
        ZoneLabelTextureBuilder::build(patched, size, numberColor, showNumbers, backgroundColor);
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

    // Get the currently active layout first, then fall back to screen-specific assignment
    // This ensures the overlay shows the layout selected via shortcuts or zone selector
    Layout* screenLayout = nullptr;
    if (m_layoutManager) {
        screenLayout = m_layoutManager->activeLayout();
        if (!screenLayout) {
            // Fall back to screen-specific assignment if no active layout is set
            screenLayout =
                m_layoutManager->layoutForScreen(screen->name(), m_currentVirtualDesktop, m_currentActivity);
        }
    }

    // Fall back to the global layout if nothing else is set
    if (!screenLayout) {
        screenLayout = m_layout;
    }

    if (!screenLayout) {
        return zonesList;
    }

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
    // useAvailableGeometry=true means zones are calculated within the usable screen area
    // (excluding panels/taskbars), so windows won't overlap with system UI.
    // Layout's zonePadding/outerGap takes precedence over global settings
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
    QRectF geom = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGap, true);

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

QVariantList OverlayService::buildLayoutsList() const
{
    const auto entries = LayoutUtils::buildUnifiedLayoutList(m_layoutManager);
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

    // Use actualAvailableGeometry which excludes panels/taskbars (queries PlasmaShell on Wayland)
    QRect availableGeom = ScreenManager::actualAvailableGeometry(screen);

    int x = availableGeom.x() + static_cast<int>(m_selectedZoneRelGeo.x() * availableGeom.width());
    int y = availableGeom.y() + static_cast<int>(m_selectedZoneRelGeo.y() * availableGeom.height());
    int width = static_cast<int>(m_selectedZoneRelGeo.width() * availableGeom.width());
    int height = static_cast<int>(m_selectedZoneRelGeo.height() * availableGeom.height());

    // Apply zone padding - layout's zonePadding takes precedence over global settings
    int padding = 0;
    if (m_layoutManager && !m_selectedLayoutId.isEmpty()) {
        Layout* selectedLayout = m_layoutManager->layoutById(QUuid::fromString(m_selectedLayoutId));
        if (selectedLayout) {
            padding = selectedLayout->zonePadding();
        } else if (m_settings) {
            padding = m_settings->zonePadding();
        }
    } else if (m_settings) {
        padding = m_settings->zonePadding();
    }

    if (padding > 0) {
        x += padding;
        y += padding;
        width -= padding * 2;
        height -= padding * 2;
        // Ensure minimum size
        width = std::max(width, 50);
        height = std::max(height, 50);
    }

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

    qCInfo(lcOverlay) << "Layout selected from zone selector:" << layoutId;
    Q_EMIT manualLayoutSelected(layoutId);
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

bool OverlayService::useShaderOverlay() const
{
    if (!canUseShaders()) {
        return false;
    }
    if (!m_layout || ShaderRegistry::isNoneShader(m_layout->shaderId())) {
        return false;
    }
    // Don't permanently give up after one error - retry each show (fallbacks mask bugs)
    if (m_settings && !m_settings->enableShaderEffects()) {
        return false; // User disabled shaders globally
    }

    auto* registry = ShaderRegistry::instance();
    return registry && registry->shader(m_layout->shaderId()).isValid();
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

    qCDebug(lcOverlay) << "Shader animation started at" << (1000 / interval) << "fps";
}

void OverlayService::stopShaderAnimation()
{
    if (m_shaderUpdateTimer) {
        m_shaderUpdateTimer->stop();
        qCDebug(lcOverlay) << "Shader animation stopped";
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
            // Set time uniforms on the window (QML root)
            window->setProperty("iTime", static_cast<qreal>(iTime));
            window->setProperty("iTimeDelta", static_cast<qreal>(iTimeDelta));
            window->setProperty("iFrame", frame);
        }
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

        window->setProperty("zones", patched);
        window->setProperty("zoneCount", patched.size());
        window->setProperty("highlightedCount", highlightedCount);

        if (useShaderOverlay()) {
            Layout* screenLayout = nullptr;
            if (m_layoutManager) {
                screenLayout = m_layoutManager->activeLayout();
                if (!screenLayout) {
                    screenLayout =
                        m_layoutManager->layoutForScreen(screen->name(), m_currentVirtualDesktop, m_currentActivity);
                }
            }
            if (!screenLayout) {
                screenLayout = m_layout;
            }
            updateLabelsTextureForWindow(window, patched, screen, screenLayout);
        }
    }

    ++m_zoneDataVersion;
    for (auto* w : std::as_const(m_overlayWindows)) {
        if (w) {
            w->setProperty("zoneDataVersion", m_zoneDataVersion);
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
        qCDebug(lcOverlay) << "Shader timer restarted after system resume";
    }
}

void OverlayService::onShaderError(const QString& errorLog)
{
    qCWarning(lcOverlay) << "Shader error during overlay:" << errorLog;
    m_pendingShaderError = errorLog;
    // Don't set m_shaderErrorPending - retry shaders on next show (fix bugs, don't mask)
}

bool OverlayService::prepareLayoutOsdWindow(QQuickWindow*& window, QRect& screenGeom, qreal& aspectRatio)
{
    QScreen* screen = Utils::primaryScreen();
    if (!screen) {
        qCWarning(lcOverlay) << "No primary screen for layout OSD";
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

    screenGeom = screen->geometry();
    aspectRatio = (screenGeom.height() > 0)
        ? static_cast<qreal>(screenGeom.width()) / screenGeom.height()
        : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);

    return true;
}

void OverlayService::showLayoutOsd(Layout* layout)
{
    if (!layout) {
        qCDebug(lcOverlay) << "No layout provided for OSD";
        return;
    }

    if (layout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout:" << layout->name();
        return;
    }

    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("layoutId"), layout->id().toString());
    writeQmlProperty(window, QStringLiteral("layoutName"), layout->name());
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("category"), 0);
    writeQmlProperty(window, QStringLiteral("zones"), LayoutUtils::zonesToVariantList(layout, ZoneField::Full));

    sizeAndCenterOsd(window, screenGeom, aspectRatio);
    QMetaObject::invokeMethod(window, "show");

    qCDebug(lcOverlay) << "Showing layout OSD for:" << layout->name();
}

void OverlayService::showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category)
{
    if (zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout:" << name;
        return;
    }

    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("layoutId"), id);
    writeQmlProperty(window, QStringLiteral("layoutName"), name);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("category"), category);
    writeQmlProperty(window, QStringLiteral("zones"), zones);

    sizeAndCenterOsd(window, screenGeom, aspectRatio);
    QMetaObject::invokeMethod(window, "show");

    qCDebug(lcOverlay) << "Showing layout OSD for:" << name << "category:" << category;
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
        layerWindow->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // Anchors will be set dynamically in showLayoutOsd() based on window size
        layerWindow->setScope(QStringLiteral("plasmazones-layout-osd-%1").arg(screen->name()));
        layerWindow->setExclusiveZone(-1);
    }

    connect(window, SIGNAL(dismissed()), this, SLOT(hideLayoutOsd()));
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
    // Only show OSD for successful actions - failures (no windows, no zones, etc.) don't need feedback
    if (!success) {
        qCDebug(lcOverlay) << "Skipping navigation OSD for failure:" << action << reason;
        return;
    }

    if (!m_layout || m_layout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "No layout or zones available for navigation OSD";
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
    QVariantList zonesList = LayoutUtils::zonesToVariantList(m_layout, ZoneField::Minimal);
    writeQmlProperty(window, QStringLiteral("zones"), zonesList);

    // Get screen geometry for window positioning
    const QRect screenGeom = screen->geometry();

    // Set explicit window size before positioning - text-only OSD
    const int osdWidth = 240; // Compact width for text
    const int osdHeight = 70; // Text message + margins
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    centerLayerWindowOnScreen(window, screenGeom, osdWidth, osdHeight);

    // Hide any existing navigation OSD before showing new one (prevent overlap)
    hideNavigationOsd();

    // Show with animation
    QMetaObject::invokeMethod(window, "show");

    qCDebug(lcOverlay) << "Showing navigation OSD: success=" << success << "action=" << action
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
        layerWindow->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setScope(QStringLiteral("plasmazones-navigation-osd-%1").arg(screen->name()));
        layerWindow->setExclusiveZone(-1);
    }

    connect(window, SIGNAL(dismissed()), this, SLOT(hideNavigationOsd()));
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

} // namespace PlasmaZones
