// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice/internal.h"
#include "overlayservice.h"
#include "cavaservice.h"
#include "windowthumbnailservice.h"

#include "../core/layout.h"
#include "../core/layoutmanager.h"
#include "../core/zone.h"
#include "../core/layoututils.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/utils.h"
#include "../core/constants.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QMutexLocker>
#include "../core/logging.h"
#include "pz_qml_i18n.h"

#include "../core/layersurface.h"

namespace PlasmaZones {

namespace {

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

} // namespace

OverlayService::OverlayService(QObject* parent)
    : IOverlayService(parent)
    , m_engine(std::make_unique<QQmlEngine>()) // No parent - unique_ptr manages lifetime
{
    // Set up i18n for QML (makes i18n() available in QML)
    auto* localizedContext = new PzLocalizedContext(m_engine.get());
    m_engine->rootContext()->setContextObject(localizedContext);

    // Connect to screen changes (with safety check for early initialization)
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, &OverlayService::handleScreenAdded);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OverlayService::handleScreenRemoved);
    } else {
        qCWarning(lcOverlay) << "Overlay: created before QGuiApplication, screen signals not connected";
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
    connect(m_cavaService.get(), &CavaService::spectrumUpdated, this, &OverlayService::onAudioSpectrumUpdated);
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

QQuickWindow* OverlayService::createQmlWindow(const QUrl& qmlUrl, QScreen* screen, const char* windowType,
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
        qCWarning(lcOverlay) << windowType << "QML component not ready, status=" << component.status();
        return nullptr;
    }

    QObject* obj =
        initialProperties.isEmpty() ? component.create() : component.createWithInitialProperties(initialProperties);
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
        if (cursorScreen
            && isContextDisabled(m_settings, Utils::screenIdentifier(cursorScreen), m_currentVirtualDesktop,
                                 m_currentActivity)) {
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
        if (cursorScreen
            && isContextDisabled(m_settings, Utils::screenIdentifier(cursorScreen), m_currentVirtualDesktop,
                                 m_currentActivity)) {
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

void OverlayService::hide()
{
    if (!m_visible) {
        return;
    }

    m_visible = false;
    m_currentOverlayScreen = nullptr;

    // Stop shader animation
    stopShaderAnimation();

    // Do NOT invalidate m_shaderTimer - keeps iTime continuous across show/hide
    // so animations feel less predictable and don't restart

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

void OverlayService::updateSettings(ISettings* settings)
{
    setSettings(settings);

    // Sync CAVA state with current settings.  The signal-based handlers
    // (enableAudioVisualizerChanged, etc.) connected in setSettings() only
    // fire when load() detects a value change.  When the KCM uses batch
    // setSettings + reloadSettings, the in-memory values are already updated
    // by the batch setters before load() runs, so load() sees no change and
    // the signals never fire.  Syncing here ensures CAVA always reflects
    // the current configuration.
    syncCavaState();

    // Hide overlay and zone selector on screens/desktops/activities that are now disabled
    for (auto* screen : m_overlayWindows.keys()) {
        if (isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                              m_currentActivity)) {
            if (auto* window = m_overlayWindows.value(screen)) {
                window->hide();
            }
        }
    }
    for (auto* screen : m_zoneSelectorWindows.keys()) {
        if (isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                              m_currentActivity)) {
            if (auto* window = m_zoneSelectorWindows.value(screen)) {
                window->hide();
            }
        }
    }

    if (m_visible) {
        for (auto* screen : m_overlayWindows.keys()) {
            if (isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                                  m_currentActivity)) {
                continue;
            }
            updateOverlayWindow(screen);
        }
    }

    // Keep zone selector windows in sync with settings changes (position, layout, sizing).
    // Without this, changing settings while the selector is visible can leave stale geometry
    // and anchors, causing corrupted rendering or incorrect window sizing.
    // Skip disabled screens/desktops/activities.
    if (!m_zoneSelectorWindows.isEmpty()) {
        for (auto* screen : m_zoneSelectorWindows.keys()) {
            if (isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                                  m_currentActivity)) {
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

void OverlayService::setLayout(Layout* layout)
{
    if (m_layout != layout) {
        m_layout = layout;
        // Mark zone data as dirty when layout changes to ensure shader overlay updates
        m_zoneDataDirty = true;
    }
}

Layout* OverlayService::resolveScreenLayout(QScreen* screen) const
{
    Layout* screenLayout = nullptr;
    if (m_layoutManager && screen) {
        screenLayout = m_layoutManager->layoutForScreen(Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                                                        m_currentActivity);
        if (!screenLayout) {
            screenLayout = m_layoutManager->defaultLayout();
        }
    }
    if (!screenLayout) {
        screenLayout = m_layout;
    }
    return screenLayout;
}

void OverlayService::hideDisabledAndRefresh()
{
    // Hide overlay/selector on screens where the current context is disabled
    if (m_settings) {
        for (auto* screen : m_zoneSelectorWindows.keys()) {
            if (isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                                  m_currentActivity)) {
                if (auto* window = m_zoneSelectorWindows.value(screen)) {
                    window->hide();
                }
            }
        }
        if (m_visible) {
            for (auto* screen : m_overlayWindows.keys()) {
                if (isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                                      m_currentActivity)) {
                    if (auto* window = m_overlayWindows.value(screen)) {
                        window->hide();
                    }
                }
            }
        }
    }

    // Update remaining (non-disabled) zone selector windows
    if (!m_zoneSelectorWindows.isEmpty()) {
        for (auto* screen : m_zoneSelectorWindows.keys()) {
            if (!isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                                   m_currentActivity)) {
                updateZoneSelectorWindow(screen);
            }
        }
    }
    // Update remaining overlay windows when visible
    if (m_visible && !m_overlayWindows.isEmpty()) {
        for (auto* screen : m_overlayWindows.keys()) {
            if (!isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                                   m_currentActivity)) {
                updateOverlayWindow(screen);
            }
        }
    }
}

void OverlayService::setCurrentVirtualDesktop(int desktop)
{
    if (m_currentVirtualDesktop != desktop) {
        m_currentVirtualDesktop = desktop;
        qCInfo(lcOverlay) << "Virtual desktop changed to" << desktop;
        hideDisabledAndRefresh();
    }
}

void OverlayService::setCurrentActivity(const QString& activityId)
{
    if (m_currentActivity != activityId) {
        m_currentActivity = activityId;
        qCInfo(lcOverlay) << "Activity changed activity=" << activityId;
        hideDisabledAndRefresh();
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
    if (m_visible && screen
        && !isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                              m_currentActivity)) {
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

QVariantList OverlayService::buildLayoutsList(const QString& screenId) const
{
    // Determine filter per-screen: check this screen's assignment to decide
    // whether to show manual layouts, autotile algorithms, or both.
    bool includeManual = m_includeManualLayouts;
    bool includeAutotile = m_includeAutotileLayouts;
    auto* layoutManager = dynamic_cast<LayoutManager*>(m_layoutManager);
    if (layoutManager) {
        const QString resolvedId = Utils::isConnectorName(screenId) ? Utils::screenIdForName(screenId) : screenId;
        if (!resolvedId.isEmpty()) {
            const QString assignmentId =
                layoutManager->assignmentIdForScreen(resolvedId, m_currentVirtualDesktop, m_currentActivity);
            if (LayoutId::isAutotile(assignmentId)) {
                includeManual = false;
                includeAutotile = true;
            } else {
                includeManual = true;
                includeAutotile = false;
            }
        }
    }
    const auto entries = LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, screenId, m_currentVirtualDesktop, m_currentActivity, includeManual, includeAutotile,
        Utils::screenAspectRatio(screenId), m_settings && m_settings->filterLayoutsByAspectRatio());
    return LayoutUtils::toVariantList(entries);
}

void OverlayService::setLayoutFilter(bool includeManual, bool includeAutotile)
{
    if (m_includeManualLayouts == includeManual && m_includeAutotileLayouts == includeAutotile) {
        return;
    }
    m_includeManualLayouts = includeManual;
    m_includeAutotileLayouts = includeAutotile;
    // Refresh visible zone selector windows with updated layout list
    refreshVisibleWindows();
}

void OverlayService::setExcludedScreens(const QSet<QString>& screenIds)
{
    m_excludedScreens = screenIds;
}

int OverlayService::visibleLayoutCount(const QString& screenId) const
{
    const auto entries = LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, screenId, m_currentVirtualDesktop, m_currentActivity, m_includeManualLayouts,
        m_includeAutotileLayouts, Utils::screenAspectRatio(screenId),
        m_settings && m_settings->filterLayoutsByAspectRatio());
    return entries.size();
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

} // namespace PlasmaZones
