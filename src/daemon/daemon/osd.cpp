// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../modetracker.h"
#include "../../core/layoutmanager.h"
#include "../../core/screenmanager.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../core/activitymanager.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/zonedetector.h"
#include "../../autotile/AutotileEngine.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "helpers.h"
#include "../../autotile/AlgorithmRegistry.h"
#include "../../autotile/TilingState.h"
#include "../config/settings.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QScreen>
#include <QTimer>
#include <KLocalizedString>

namespace PlasmaZones {

void Daemon::showOverlay()
{
    // Don't show overlay when all screens are in autotile mode
    // (the overlay is for manual zone selection during drag)
    if (m_autotileEngine && m_screenManager) {
        const auto& autotileScreens = m_autotileEngine->autotileScreens();
        if (!autotileScreens.isEmpty()) {
            bool allAutotile = true;
            for (QScreen* screen : m_screenManager->screens()) {
                if (!autotileScreens.contains(screen->name())) {
                    allAutotile = false;
                    break;
                }
            }
            if (allAutotile) {
                return;
            }
        }
    }
    // Per-screen autotile exclusion is handled by OverlayService::initializeOverlay()
    // via m_excludedScreens (set in updateAutotileScreens)
    m_overlayService->show();
}

void Daemon::hideOverlay()
{
    clearHighlight();
    m_overlayService->hide();
}

bool Daemon::isOverlayVisible() const
{
    return m_overlayService->isVisible();
}

void Daemon::clearHighlight()
{
    m_zoneDetector->clearHighlights();
}

void Daemon::showLayoutOsd(Layout* layout, const QString& screenName)
{
    if (!layout) {
        return;
    }

    const QString layoutName = layout->name();

    // Check OSD style setting
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;

    switch (style) {
    case OsdStyle::None:
        // No OSD
        qCInfo(lcDaemon) << "OSD disabled, skipping for layout=" << layoutName;
        return;

    case OsdStyle::Text:
        // Use KDE Plasma's OSD service for text-only notification
        {
            QDBusMessage msg = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));

            QString displayText = i18n("Layout: %1", layoutName);
            msg << QStringLiteral("plasmazones") << displayText;

            QDBusConnection::sessionBus().asyncCall(msg);
            qCInfo(lcDaemon) << "Showing text OSD for layout=" << layoutName;
        }
        break;

    case OsdStyle::Preview:
        // Use visual layout preview OSD
        if (m_overlayService) {
            m_overlayService->showLayoutOsd(layout, screenName);
            qCInfo(lcDaemon) << "Preview OSD: layout=" << layoutName << "screen=" << screenName;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
}

void Daemon::showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName,
                                       const QString& screenName)
{
    auto* algo = AlgorithmRegistry::instance()->algorithm(algorithmId);
    if (!algo) {
        qCWarning(lcDaemon) << "OSD: algorithm not found, algorithmId=" << algorithmId;
        return;
    }

    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;

    switch (style) {
    case OsdStyle::None:
        qCInfo(lcDaemon) << "OSD disabled, skipping for algorithm=" << displayName;
        return;

    case OsdStyle::Text: {
        QDBusMessage msg =
            QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                                           QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));

        QString displayText = i18n("Tiling: %1", displayName);
        msg << QStringLiteral("plasmazones") << displayText;

        QDBusConnection::sessionBus().asyncCall(msg);
        qCInfo(lcDaemon) << "Showing text OSD for algorithm=" << displayName;
    } break;

    case OsdStyle::Preview:
        if (m_overlayService) {
            int windowCount = 0;
            if (m_autotileEngine) {
                TilingState* state = m_autotileEngine->stateForScreen(screenName);
                if (state) {
                    windowCount = state->tiledWindowCount();
                }
            }
            QVariantList zones = AlgorithmRegistry::generatePreviewZones(algo, windowCount > 0 ? windowCount : -1);
            QString layoutId = LayoutId::makeAutotileId(algorithmId);
            m_overlayService->showLayoutOsd(layoutId, displayName, zones, static_cast<int>(LayoutCategory::Autotile),
                                            false, screenName);
            qCInfo(lcDaemon) << "Preview OSD: algorithm=" << displayName << "screen=" << screenName;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
}

void Daemon::showLayoutOsdDeferred(const QUuid& layoutId, const QString& screenName)
{
    // Defer OSD display so first-time QML compilation of LayoutOsd.qml (~100-300ms)
    // doesn't block the daemon event loop during layout switches.
    QTimer::singleShot(0, this, [this, layoutId, screenName]() {
        Layout* l = m_layoutManager ? m_layoutManager->layoutById(layoutId) : nullptr;
        if (l) {
            showLayoutOsd(l, screenName);
        }
    });
}

void Daemon::showAlgorithmOsdDeferred(const QString& algorithmId, const QString& algorithmName,
                                      const QString& screenName)
{
    QTimer::singleShot(0, this, [this, algorithmId, algorithmName, screenName]() {
        showLayoutOsdForAlgorithm(algorithmId, algorithmName, screenName);
    });
}

void Daemon::connectToKWinScript()
{
    // The KWin script will call us via D-Bus
    // We just need to be ready to receive calls

    // Monitor for KWin script connection
    // The script will call getActiveLayout() on startup
}

void Daemon::updateLayoutFilter()
{
    if (!m_settings) {
        return;
    }

    // Derive autotile state from the CURRENT desktop's actual assignments,
    // not from the global ModeTracker. This ensures the layout filter reflects
    // the per-desktop tiling mode, not the last-toggled global mode.
    // In mixed-mode multi-monitor (some screens autotile, some manual), include
    // both layout types so cycling works correctly on both screens.
    bool autotileActive = false;
    bool manualActive = false;
    if (m_settings->autotileEnabled() && m_layoutManager && m_screenManager) {
        const int desktop = currentDesktop();
        const QString activity = currentActivity();
        for (QScreen* screen : m_screenManager->screens()) {
            const QString screenId = Utils::screenIdentifier(screen);
            const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
            if (LayoutId::isAutotile(assignmentId)) {
                autotileActive = true;
            } else {
                manualActive = true;
            }
        }
    } else {
        manualActive = true;
    }
    const bool includeManual = manualActive || !autotileActive;
    const bool includeAutotile = autotileActive;

    if (m_overlayService) {
        m_overlayService->setLayoutFilter(includeManual, includeAutotile);
    }
    if (m_unifiedLayoutController) {
        m_unifiedLayoutController->setLayoutFilter(includeManual, includeAutotile);
    }

    qCDebug(lcDaemon) << "Layout filter updated: manual=" << includeManual << "autotile=" << includeAutotile;
}

void Daemon::syncModeFromAssignments()
{
    if (!m_layoutManager || !m_screenManager) {
        return;
    }

    const int desktop = currentDesktop();
    const QString activity = currentActivity();

    // Derive the tiling mode from actual per-desktop assignments.
    // If ANY screen has an autotile assignment on this desktop, mode is Autotile.
    bool anyAutotile = false;
    for (QScreen* screen : m_screenManager->screens()) {
        const QString screenId = Utils::screenIdentifier(screen);
        const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (LayoutId::isAutotile(assignmentId)) {
            anyAutotile = true;
            break;
        }
    }

    // Update ModeTracker to reflect this desktop's actual state.
    // Use QSignalBlocker to prevent the mode change from triggering
    // updateLayoutFilter() via currentModeChanged — we call it ourselves
    // at the end to ensure a single consistent update.
    if (m_modeTracker) {
        QSignalBlocker blocker(m_modeTracker.get());
        m_modeTracker->setCurrentMode(anyAutotile ? TilingMode::Autotile : TilingMode::Manual);
    }

    // Sync UnifiedLayoutController's current layout ID to match this desktop.
    // Without this, layout cycling uses the old desktop's current index.
    if (m_unifiedLayoutController) {
        QScreen* focusedScreen = m_windowTrackingAdaptor ? resolveShortcutScreen(m_windowTrackingAdaptor) : nullptr;
        if (!focusedScreen && !m_screenManager->screens().isEmpty()) {
            focusedScreen = m_screenManager->screens().first();
        }
        if (focusedScreen) {
            const QString focusedScreenId = Utils::screenIdentifier(focusedScreen);
            const QString focusedAssignmentId =
                m_layoutManager->assignmentIdForScreen(focusedScreenId, desktop, activity);
            m_unifiedLayoutController->setCurrentScreenName(focusedScreenId);
            // Pass the per-desktop assignment as override — syncFromExternalState()
            // without override only reads the global active layout, which doesn't
            // reflect per-desktop autotile assignments.
            m_unifiedLayoutController->syncFromExternalState(focusedAssignmentId);

            // Update the global active layout to match this desktop's per-screen
            // assignment. Without this, LayoutManager::activeLayout() returns the
            // previous desktop's layout, causing zone detection, overlay, and
            // onLayoutChanged to operate on the wrong zones.
            // Use QSignalBlocker to prevent activeLayoutChanged from firing
            // onLayoutChanged → resnap buffer population. Desktop switch is not
            // a layout change — the resnap buffer entries would be stale and corrupt
            // the next real manual layout switch.
            if (!anyAutotile && !LayoutId::isAutotile(focusedAssignmentId)) {
                Layout* desktopLayout = m_layoutManager->layoutForScreen(focusedScreenId, desktop, activity);
                if (desktopLayout && desktopLayout != m_layoutManager->activeLayout()) {
                    QSignalBlocker blocker(m_layoutManager.get());
                    m_layoutManager->setActiveLayout(desktopLayout);
                }
            }
        }

        // Populate m_lastManualAssignments / m_lastAutotileAssignments for ALL
        // screens on this desktop so the first toggle has a correct fallback
        // (not just the focused screen — multi-monitor needs all screens seeded).
        for (QScreen* screen : m_screenManager->screens()) {
            const QString screenId = Utils::screenIdentifier(screen);
            const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
            DesktopContextKey ctxKey{screenId, desktop, activity};
            if (LayoutId::isAutotile(assignmentId)) {
                if (!m_lastAutotileAssignments.contains(ctxKey)) {
                    m_lastAutotileAssignments[ctxKey] = assignmentId;
                }
            } else {
                if (!m_lastManualAssignments.contains(ctxKey)) {
                    m_lastManualAssignments[ctxKey] = assignmentId;
                }
            }
        }
    }

    updateLayoutFilter();
}

void Daemon::showDesktopSwitchOsd(int desktop, const QString& activity)
{
    if (!m_settings || !m_settings->showOsdOnLayoutSwitch() || !m_overlayService || !m_layoutManager
        || !m_screenManager) {
        return;
    }
    QScreen* screen = m_windowTrackingAdaptor ? resolveShortcutScreen(m_windowTrackingAdaptor) : nullptr;
    if (!screen && !m_screenManager->screens().isEmpty()) {
        screen = m_screenManager->screens().first();
    }
    if (!screen) {
        return;
    }
    const QString screenId = Utils::screenIdentifier(screen);
    const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
    if (LayoutId::isAutotile(assignmentId)) {
        const QString algoId = LayoutId::extractAlgorithmId(assignmentId);
        auto* algo = AlgorithmRegistry::instance()->algorithm(algoId);
        const QString displayName = algo ? algo->name() : algoId;
        showAlgorithmOsdDeferred(algoId, displayName, screenId);
    } else {
        Layout* layout = m_layoutManager->layoutForScreen(screenId, desktop, activity);
        if (layout) {
            showLayoutOsdDeferred(layout->id(), screenId);
        }
    }
}

int Daemon::currentDesktop() const
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

QString Daemon::currentActivity() const
{
    return (m_activityManager && ActivityManager::isAvailable()) ? m_activityManager->currentActivity() : QString();
}

} // namespace PlasmaZones
