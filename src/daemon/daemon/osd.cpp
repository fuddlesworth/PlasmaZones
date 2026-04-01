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
#include "pz_i18n.h"

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
                if (!autotileScreens.contains(Utils::screenIdentifier(screen))) {
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

void Daemon::showLayoutOsd(Layout* layout, const QString& screenId)
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

            QString displayText = PzI18n::tr("Layout: %1").arg(layoutName);
            msg << QStringLiteral("plasmazones") << displayText;

            QDBusConnection::sessionBus().asyncCall(msg);
            qCInfo(lcDaemon) << "Showing text OSD for layout=" << layoutName;
        }
        break;

    case OsdStyle::Preview:
        // Use visual layout preview OSD
        if (m_overlayService) {
            m_overlayService->showLayoutOsd(layout, screenId);
            qCInfo(lcDaemon) << "Preview OSD: layout=" << layoutName << "screen=" << screenId;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
}

void Daemon::showLockedOsd(const QString& screenId)
{
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }

    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                                       QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));

    msg << QStringLiteral("object-locked") << PzI18n::tr("Layout Locked");
    QDBusConnection::sessionBus().asyncCall(msg);
    qCInfo(lcDaemon) << "Showing locked text OSD for screen=" << screenId;
}

void Daemon::showLockedPreviewOsd(const QString& screenId)
{
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }

    // Show the visual preview OSD with lock overlay showing the current layout
    if (style == OsdStyle::Preview && m_overlayService && m_layoutManager) {
        const QString resolvedId = Utils::screenIdForName(screenId);
        Layout* layout = m_layoutManager->resolveLayoutForScreen(resolvedId.isEmpty() ? screenId : resolvedId);
        if (layout) {
            m_overlayService->showLockedLayoutOsd(layout, screenId);
            return;
        }
    }

    // Fall back to text OSD
    showLockedOsd(screenId);
}

void Daemon::showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenId)
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

        QString displayText = PzI18n::tr("Tiling: %1").arg(displayName);
        msg << QStringLiteral("plasmazones") << displayText;

        QDBusConnection::sessionBus().asyncCall(msg);
        qCInfo(lcDaemon) << "Showing text OSD for algorithm=" << displayName;
    } break;

    case OsdStyle::Preview:
        if (m_overlayService) {
            int windowCount = 0;
            if (m_autotileEngine) {
                TilingState* state = m_autotileEngine->stateForScreen(screenId);
                if (state) {
                    windowCount = state->tiledWindowCount();
                }
            }
            QVariantList zones = AlgorithmRegistry::generatePreviewZones(algo, windowCount > 0 ? windowCount : -1);
            QString layoutId = LayoutId::makeAutotileId(algorithmId);
            m_overlayService->showLayoutOsd(layoutId, displayName, zones, static_cast<int>(LayoutCategory::Autotile),
                                            false, screenId, algo->supportsMasterCount(),
                                            algo->producesOverlappingZones(), algo->zoneNumberDisplay());
            qCInfo(lcDaemon) << "Preview OSD: algorithm=" << displayName << "screen=" << screenId;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
}

void Daemon::showLayoutOsdDeferred(const QUuid& layoutId, const QString& screenId)
{
    // Defer OSD display so first-time QML compilation of LayoutOsd.qml (~100-300ms)
    // doesn't block the daemon event loop during layout switches.
    QTimer::singleShot(0, this, [this, layoutId, screenId]() {
        Layout* l = m_layoutManager ? m_layoutManager->layoutById(layoutId) : nullptr;
        if (l) {
            showLayoutOsd(l, screenId);
        }
    });
}

void Daemon::showAlgorithmOsdDeferred(const QString& algorithmId, const QString& algorithmName, const QString& screenId)
{
    QTimer::singleShot(0, this, [this, algorithmId, algorithmName, screenId]() {
        showLayoutOsdForAlgorithm(algorithmId, algorithmName, screenId);
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
    updateLayoutFilterForScreen(QString());
}

void Daemon::updateLayoutFilterForScreen(const QString& focusedScreenId)
{
    if (!m_settings) {
        return;
    }

    bool autotileActive = false;
    bool manualActive = false;

    if (m_settings->autotileEnabled() && m_layoutManager && m_screenManager) {
        const int desktop = currentDesktop();
        const QString activity = currentActivity();

        if (!focusedScreenId.isEmpty()) {
            // Per-screen filter: only check the focused screen's mode
            const QString assignmentId = m_layoutManager->assignmentIdForScreen(focusedScreenId, desktop, activity);
            if (LayoutId::isAutotile(assignmentId)) {
                autotileActive = true;
            } else {
                manualActive = true;
            }
        } else {
            // Global filter: union of all screens
            for (QScreen* screen : m_screenManager->screens()) {
                const QString screenId = Utils::screenIdentifier(screen);
                const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
                if (LayoutId::isAutotile(assignmentId)) {
                    autotileActive = true;
                } else {
                    manualActive = true;
                }
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

    qCDebug(lcDaemon) << "Layout filter updated: manual=" << includeManual << "autotile=" << includeAutotile
                      << "screen=" << (focusedScreenId.isEmpty() ? QStringLiteral("all") : focusedScreenId);
}

void Daemon::syncModeFromAssignments()
{
    if (!m_layoutManager || !m_screenManager) {
        return;
    }

    const int desktop = currentDesktop();
    const QString activity = currentActivity();

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
            // Block activeLayoutChanged to prevent resnap buffer corruption.
            // Desktop switches and KCM saves both route through here — neither
            // should trigger resnap via the global active layout signal. KCM saves
            // use populateResnapBufferForAllScreens() + resnapToNewLayout()
            // (per-screen, independent of global active layout) instead.
            if (!LayoutId::isAutotile(focusedAssignmentId)) {
                Layout* desktopLayout = m_layoutManager->layoutForScreen(focusedScreenId, desktop, activity);
                if (desktopLayout && desktopLayout != m_layoutManager->activeLayout()) {
                    QSignalBlocker blocker(m_layoutManager.get());
                    m_layoutManager->setActiveLayout(desktopLayout);
                }
            }
        }

        // Update ModeTracker context to reflect this desktop
        if (m_modeTracker && focusedScreen) {
            m_modeTracker->setContext(Utils::screenIdentifier(focusedScreen), desktop, activity);
        }
    }

    updateLayoutFilter();
}

void Daemon::showDesktopSwitchOsd(int desktop, const QString& activity)
{
    // Skip during startup — the initial activity/desktop detection fires
    // before start() completes and should not produce an OSD flash.
    if (!m_running) {
        return;
    }
    if (!m_settings || !m_settings->showOsdOnLayoutSwitch() || !m_overlayService || !m_layoutManager
        || !m_screenManager) {
        return;
    }
    // Show OSD on ALL screens — each screen may have a different per-desktop
    // assignment (autotile vs snapping, different layouts/algorithms).
    for (QScreen* screen : m_screenManager->screens()) {
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
}

int Daemon::currentDesktop() const
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

QString Daemon::currentActivity() const
{
    return (m_activityManager && ActivityManager::isAvailable()) ? m_activityManager->currentActivity() : QString();
}

bool Daemon::isCurrentContextLocked(const QString& screenId) const
{
    // Check both snapping and tiling locks (mode-agnostic check)
    return m_settings
        && (m_settings->isContextLocked(QStringLiteral("0:") + screenId, currentDesktop(), currentActivity())
            || m_settings->isContextLocked(QStringLiteral("1:") + screenId, currentDesktop(), currentActivity()));
}

bool Daemon::isCurrentContextLockedForMode(const QString& screenId, int mode) const
{
    if (!m_settings)
        return false;
    QString prefix = QString::number(mode) + QStringLiteral(":");
    return m_settings->isContextLocked(prefix + screenId, currentDesktop(), currentActivity());
}

} // namespace PlasmaZones
