// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../modetracker.h"
#include "../../core/layoutmanager.h"
#include "../../core/screenmanagerservice.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../core/activitymanager.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorZones/ZoneDetector.h>
#include "../../autotile/AutotileEngine.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "helpers.h"
#include <PhosphorLayoutApi/LayoutPreview.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotilePreviewRender.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "../config/settings.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QScreen>
#include <QTimer>
#include "pz_i18n.h"

namespace PlasmaZones {

namespace {

void showKdeTextOsd(const QString& icon, const QString& text)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                                       QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));
    msg << icon << text;
    QDBusConnection::sessionBus().asyncCall(msg);
}

} // namespace

void Daemon::showOverlay()
{
    // Don't show overlay when all screens are in autotile mode
    // (the overlay is for manual zone selection during drag)
    if (m_autotileEngine && m_screenManager) {
        const auto& autotileScreens = m_autotileEngine->autotileScreens();
        if (!autotileScreens.isEmpty()) {
            bool allAutotile = true;
            const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
            for (const QString& screenId : effectiveIds) {
                if (!autotileScreens.contains(screenId)) {
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

void Daemon::showLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId)
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

    case OsdStyle::Text: {
        QString displayText = PzI18n::tr("Layout: %1").arg(layoutName);
        showKdeTextOsd(QStringLiteral("plasmazones"), displayText);
        qCInfo(lcDaemon) << "Showing text OSD for layout=" << layoutName;
    } break;

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

    showKdeTextOsd(QStringLiteral("object-locked"), PzI18n::tr("Layout Locked"));
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
        PhosphorZones::Layout* layout =
            m_layoutManager->resolveLayoutForScreen(resolvedId.isEmpty() ? screenId : resolvedId);
        if (layout) {
            m_overlayService->showLockedLayoutOsd(layout, resolvedId.isEmpty() ? screenId : resolvedId);
            return;
        }
    }

    // Fall back to text OSD
    showLockedOsd(screenId);
}

void Daemon::showContextDisabledOsd(const QString& screenId, int desktop, const QString& activity,
                                    DisabledReason reason)
{
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }

    if (reason == DisabledReason::NotDisabled) {
        qCWarning(lcDaemon) << "showContextDisabledOsd called but context is not disabled"
                            << "screen=" << screenId << "desktop=" << desktop;
        return;
    }

    QString reasonText;
    switch (reason) {
    case DisabledReason::MonitorDisabled:
        reasonText = PzI18n::tr("Disabled on this monitor");
        break;
    case DisabledReason::DesktopDisabled: {
        QString desktopLabel;
        if (m_virtualDesktopManager) {
            const QStringList names = m_virtualDesktopManager->desktopNames();
            if (desktop > 0 && desktop <= names.size()) {
                desktopLabel = names[desktop - 1];
            }
        }
        if (desktopLabel.isEmpty()) {
            desktopLabel = PzI18n::tr("Desktop %1").arg(desktop);
        }
        reasonText = PzI18n::tr("Disabled on %1").arg(desktopLabel);
        break;
    }
    case DisabledReason::ActivityDisabled: {
        QString activityLabel;
        if (m_activityManager && !activity.isEmpty()) {
            activityLabel = m_activityManager->activityName(activity);
        }
        if (activityLabel.isEmpty()) {
            reasonText = PzI18n::tr("Disabled on this activity");
        } else {
            reasonText = PzI18n::tr("Disabled on %1").arg(activityLabel);
        }
        break;
    }
    case DisabledReason::NotDisabled:
        Q_UNREACHABLE();
    }

    if (style == OsdStyle::Preview && m_overlayService) {
        m_overlayService->showDisabledOsd(reasonText, screenId);
        qCInfo(lcDaemon) << "Showing disabled preview OSD:" << reasonText << "screen=" << screenId;
        return;
    }

    // Fall back to text OSD
    showKdeTextOsd(QStringLiteral("dialog-cancel"), reasonText);
    qCInfo(lcDaemon) << "Showing disabled text OSD:" << reasonText << "screen=" << screenId;
}

void Daemon::showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenId)
{
    auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(algorithmId);
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
        QString displayText = PzI18n::tr("Tiling: %1").arg(displayName);
        showKdeTextOsd(QStringLiteral("plasmazones"), displayText);
        qCInfo(lcDaemon) << "Showing text OSD for algorithm=" << displayName;
    } break;

    case OsdStyle::Preview:
        if (m_overlayService) {
            int windowCount = 0;
            int masterCount = 1;
            if (m_autotileEngine) {
                PhosphorTiles::TilingState* state = m_autotileEngine->stateForScreen(screenId);
                if (state) {
                    windowCount = state->tiledWindowCount();
                    masterCount = state->masterCount();
                }
            }
            // Build the OSD QVariantList from the canonical preview. The QML
            // overlay expects `{zoneNumber, relativeGeometry:{x,y,w,h},
            // id, name, useCustomColors}` — we project the preview's zones
            // directly without going through a second preview-generation path.
            const PhosphorLayout::LayoutPreview preview =
                PhosphorTiles::previewFromAlgorithm(algorithmId, algo, windowCount > 0 ? windowCount : -1);
            QVariantList zones;
            zones.reserve(preview.zones.size());
            for (int i = 0; i < preview.zones.size(); ++i) {
                const QRectF& rel = preview.zones.at(i);
                QVariantMap relGeo;
                relGeo[QLatin1String("x")] = rel.x();
                relGeo[QLatin1String("y")] = rel.y();
                relGeo[QLatin1String("width")] = rel.width();
                relGeo[QLatin1String("height")] = rel.height();

                QVariantMap zoneMap;
                zoneMap[QLatin1String("zoneNumber")] =
                    (i < preview.zoneNumbers.size()) ? preview.zoneNumbers.at(i) : (i + 1);
                zoneMap[QLatin1String("relativeGeometry")] = relGeo;
                zoneMap[QLatin1String("id")] = QString::number(i);
                zoneMap[QLatin1String("name")] = QString();
                zoneMap[QLatin1String("useCustomColors")] = false;
                zones.append(zoneMap);
            }
            QString layoutId = PhosphorLayout::LayoutId::makeAutotileId(algorithmId);
            m_overlayService->showLayoutOsd(layoutId, displayName, zones,
                                            static_cast<int>(PhosphorZones::LayoutCategory::Autotile), false, screenId,
                                            algo->supportsMasterCount(), algo->producesOverlappingZones(),
                                            algo->zoneNumberDisplay(), masterCount);
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
        PhosphorZones::Layout* l = m_layoutManager ? m_layoutManager->layoutById(layoutId) : nullptr;
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
            if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
                autotileActive = true;
            } else {
                manualActive = true;
            }
        } else {
            // Global filter: union of all effective screens (includes virtual screens)
            const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
            for (const QString& screenId : effectiveIds) {
                const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
                if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
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
        QString focusedScreenId =
            m_windowTrackingAdaptor ? resolveShortcutScreenId(m_windowTrackingAdaptor) : QString();
        if (focusedScreenId.isEmpty()) {
            const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
            if (!effectiveIds.isEmpty()) {
                focusedScreenId = effectiveIds.first();
            } else if (!m_screenManager->screens().isEmpty()) {
                focusedScreenId = Utils::screenIdentifier(m_screenManager->screens().first());
            }
        }
        if (!focusedScreenId.isEmpty()) {
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
            if (!PhosphorLayout::LayoutId::isAutotile(focusedAssignmentId)) {
                PhosphorZones::Layout* desktopLayout =
                    m_layoutManager->layoutForScreen(focusedScreenId, desktop, activity);
                if (desktopLayout && desktopLayout != m_layoutManager->activeLayout()) {
                    QSignalBlocker blocker(m_layoutManager.get());
                    m_layoutManager->setActiveLayout(desktopLayout);
                }
            }
        }

        // Update ModeTracker context to reflect this desktop
        if (m_modeTracker && !focusedScreenId.isEmpty()) {
            m_modeTracker->setContext(focusedScreenId, desktop, activity);
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
    showOsdForAllScreens(desktop, activity);
}

void Daemon::showOsdForAllScreens(int desktop, const QString& activity)
{
    if (!m_layoutManager || !m_screenManager) {
        return;
    }
    // Show OSD on ALL effective screens — each screen may have a different
    // per-desktop assignment (autotile vs snapping, different layouts/algorithms).
    // Uses effectiveScreenIds() so virtual screens each get their own OSD
    // showing the correct per-VS layout/algorithm assignment.
    // Screens where PlasmaZones is disabled show a "disabled" OSD instead.
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        const DisabledReason why = contextDisabledReason(m_settings.get(), screenId, desktop, activity);
        if (why != DisabledReason::NotDisabled) {
            showContextDisabledOsd(screenId, desktop, activity, why);
            continue;
        }
        const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
            const QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId);
            auto* algo = PhosphorTiles::AlgorithmRegistry::instance()->algorithm(algoId);
            const QString displayName = algo ? algo->name() : algoId;
            showAlgorithmOsdDeferred(algoId, displayName, screenId);
        } else {
            PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(screenId, desktop, activity);
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
    if (!m_settings)
        return false;
    return m_settings->isContextLocked(Utils::contextLockKey(0, screenId), currentDesktop(), currentActivity())
        || m_settings->isContextLocked(Utils::contextLockKey(1, screenId), currentDesktop(), currentActivity());
}

bool Daemon::isCurrentContextLockedForMode(const QString& screenId, int mode) const
{
    if (!m_settings)
        return false;
    return m_settings->isContextLocked(Utils::contextLockKey(mode, screenId), currentDesktop(), currentActivity());
}

} // namespace PlasmaZones
