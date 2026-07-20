// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../../core/screenmoderouter.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/IPlacementState.h>
#include "../../dbus/windowtrackingadaptor.h"
#include "helpers.h"
#include <PhosphorLayoutApi/LayoutPreview.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotilePreviewRender.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include "../../config/settings.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QKeySequence>
#include <QScreen>
#include <QTimer>
#include "phosphor_i18n.h"
#include <PhosphorScreens/ScreenIdentity.h>

#include <utility>

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
    // The overlay shows manual snap-zone selection during a drag. Don't
    // show it when no screen is in snap mode — that covers both
    // "every screen is autotile" and "every screen is in scrolling
    // (passthrough) mode" (a regression on the prior shape, which only
    // guarded the autotile-only case and let an all-scrolling setup
    // surface an empty overlay).
    if (m_screenModeRouter && m_screenManager) {
        const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
        const auto parts = m_screenModeRouter->partitionByMode(effectiveIds);
        if (parts.snap.isEmpty()) {
            return;
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

void Daemon::armResnapOsdSuppression(int count)
{
    if (count <= 0) {
        return;
    }
    // ADD, never clobber: overlapping async resnap streams each pre-arm before
    // emitting, and their feedbacks drain this counter one-by-one. Overwriting
    // would drop a concurrent stream's outstanding count (one OSD wrongly shown,
    // a later one wrongly suppressed). The watchdog floors a stuck count.
    m_suppressResnapOsd += count;
    m_suppressResnapOsdWatchdog.start();
}

bool Daemon::shouldSuppressOsd() const
{
    if (m_shuttingDown) {
        return true;
    }
    // See queryPlasmaWorkspaceState() for why this catches phantom sessions.
    if (!m_plasmaWorkspaceActive) {
        return true;
    }
    // Screen-removal cooldown — see m_screensSettlingUntil in daemon.h.
    if (std::chrono::steady_clock::now() < m_screensSettlingUntil) {
        return true;
    }
    return false;
}

void Daemon::showLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId)
{
    if (!layout) {
        return;
    }
    if (shouldSuppressOsd()) {
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
        QString displayText = PhosphorI18n::tr("Layout: %1").arg(layoutName);
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
    if (shouldSuppressOsd()) {
        return;
    }
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }

    showKdeTextOsd(QStringLiteral("object-locked"), PhosphorI18n::tr("Layout Locked"));
    qCInfo(lcDaemon) << "Showing locked text OSD for screen=" << screenId;
}

void Daemon::showLockedPreviewOsd(const QString& screenId)
{
    if (shouldSuppressOsd()) {
        return;
    }
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }

    // Show the visual preview OSD with lock overlay showing the current layout
    if (style == OsdStyle::Preview && m_overlayService && m_layoutManager) {
        const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
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
    if (shouldSuppressOsd()) {
        return;
    }
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
        reasonText = PhosphorI18n::tr("Disabled on this monitor");
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
            desktopLabel = PhosphorI18n::tr("Desktop %1").arg(desktop);
        }
        reasonText = PhosphorI18n::tr("Disabled on %1").arg(desktopLabel);
        break;
    }
    case DisabledReason::ActivityDisabled: {
        QString activityLabel;
        if (m_activityManager && !activity.isEmpty()) {
            activityLabel = m_activityManager->activityName(activity);
        }
        if (activityLabel.isEmpty()) {
            reasonText = PhosphorI18n::tr("Disabled on this activity");
        } else {
            reasonText = PhosphorI18n::tr("Disabled on %1").arg(activityLabel);
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

void Daemon::showNotAssignedOsd(const QString& screenId)
{
    if (shouldSuppressOsd()) {
        return;
    }
    const OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }
    const QString text = PhosphorI18n::tr("No layout assigned");
    if (style == OsdStyle::Preview && m_overlayService) {
        m_overlayService->showDisabledOsd(text, screenId);
        qCInfo(lcDaemon) << "Showing not-assigned preview OSD: screen=" << screenId;
        return;
    }
    showKdeTextOsd(QStringLiteral("dialog-information"), text);
    qCInfo(lcDaemon) << "Showing not-assigned text OSD: screen=" << screenId;
}

void Daemon::showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenId)
{
    if (shouldSuppressOsd()) {
        return;
    }
    auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(algorithmId) : nullptr;
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
        QString displayText = PhosphorI18n::tr("Tiling: %1").arg(displayName);
        showKdeTextOsd(QStringLiteral("plasmazones"), displayText);
        qCInfo(lcDaemon) << "Showing text OSD for algorithm=" << displayName;
    } break;

    case OsdStyle::Preview:
        if (m_overlayService) {
            int windowCount = 0;
            int masterCount = 1;
            if (m_autotileEngine) {
                // const overload: a non-creating lookup. The non-const overload
                // would lazily allocate an empty TilingState for a known screen
                // during this read-only OSD preview build.
                const auto* state = std::as_const(*m_autotileEngine).stateForScreen(screenId);
                if (state) {
                    windowCount = state->tiledWindowCount();
                    masterCount = state->masterCount();
                }
            }
            // Build the OSD QVariantList from the canonical preview. The QML
            // overlay expects `{zoneNumber, relativeGeometry:{x,y,w,h},
            // id, name, useCustomColors}` — we project the preview's zones
            // directly without going through a second preview-generation path.
            //
            // canvasSize: pass the target screen's available geometry so
            // aspect-sensitive algorithms (BSP / fibonacci / spiral) split
            // along the same axis the live tiler uses. Without this, BSP on
            // a portrait VS shows a left/right split in the OSD while the
            // tiler actually places windows top/bottom — preview lies. Use
            // available geometry (panel-excluded) so it matches the rect
            // the tiler computes against.
            QSize previewCanvas;
            if (m_screenManager) {
                const QRect avail = m_screenManager->screenAvailableGeometry(screenId);
                if (avail.isValid() && avail.width() > 0 && avail.height() > 0) {
                    previewCanvas = avail.size();
                }
            }
            const PhosphorLayout::LayoutPreview preview = PhosphorTiles::previewFromAlgorithm(
                algorithmId, algo, windowCount > 0 ? windowCount : -1, m_algorithmRegistry.get(), previewCanvas);
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
    if (shouldSuppressOsd()) {
        return;
    }
    // Defer so first-time QML compilation doesn't block the event loop.
    // Inner showLayoutOsd re-checks shouldSuppressOsd() on dispatch.
    QTimer::singleShot(0, this, [this, layoutId, screenId]() {
        PhosphorZones::Layout* l = m_layoutManager ? m_layoutManager->layoutById(layoutId) : nullptr;
        if (l) {
            showLayoutOsd(l, screenId);
        }
    });
}

void Daemon::showAlgorithmOsdDeferred(const QString& algorithmId, const QString& algorithmName, const QString& screenId)
{
    if (shouldSuppressOsd()) {
        return;
    }
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
        const int desktop = currentDesktopForScreen(focusedScreenId);
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

    const QString activity = currentActivity();

    // Sync UnifiedLayoutController's current layout ID to match this desktop.
    // Without this, layout cycling uses the old desktop's current index.
    if (m_unifiedLayoutController) {
        QString focusedScreenId = m_windowTrackingAdaptor
            ? resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor)
            : QString();
        if (focusedScreenId.isEmpty()) {
            const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
            if (!effectiveIds.isEmpty()) {
                focusedScreenId = effectiveIds.first();
            } else {
                const auto trackedScreens = m_screenManager->screens();
                if (!trackedScreens.isEmpty()) {
                    focusedScreenId = trackedScreens.first().identifier;
                }
            }
        }
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(focusedScreenId);
        if (!focusedScreenId.isEmpty()) {
            const QString focusedAssignmentId =
                m_layoutManager->assignmentIdForScreen(focusedScreenId, desktop, activity);
            m_unifiedLayoutController->setCurrentScreenName(focusedScreenId);
            // Pass the per-desktop assignment as override — syncFromExternalState()
            // without override only reads the global active layout, which doesn't
            // reflect per-desktop autotile assignments.
            m_unifiedLayoutController->syncFromExternalState(focusedAssignmentId);

            // Update the global active layout to match this desktop's per-screen
            // assignment. Without this, PhosphorZones::LayoutRegistry::activeLayout() returns the
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
    }

    updateLayoutFilter();
}

void Daemon::showDesktopSwitchOsd(const QString& activity)
{
    // Skip during startup — the initial activity/desktop detection fires
    // before start() completes and should not produce an OSD flash.
    if (!m_running) {
        return;
    }
    if (shouldSuppressOsd()) {
        return;
    }
    if (!m_settings || !m_settings->showOsdOnDesktopSwitch() || !m_overlayService || !m_layoutManager
        || !m_screenManager) {
        return;
    }
    showOsdForAllScreens(activity);
}

void Daemon::showDesktopSwitchOsdForScreen(const QString& screenId, const QString& activity)
{
    // Per-output virtual desktops (#648): only the screen that actually switched
    // shows the OSD, not every monitor. Same gating as the all-screens variant.
    if (!m_running) {
        return;
    }
    if (shouldSuppressOsd()) {
        return;
    }
    if (!m_settings || !m_settings->showOsdOnDesktopSwitch() || !m_overlayService || !m_layoutManager
        || !m_screenManager) {
        return;
    }
    showOsdForScreens({screenId}, activity);
}

void Daemon::showOsdForAllScreens(const QString& activity)
{
    if (!m_screenManager) {
        return;
    }
    showOsdForScreens(m_screenManager->effectiveScreenIds(), activity);
}

void Daemon::showOsdForScreens(const QStringList& screenIds, const QString& activity)
{
    if (!m_layoutManager || !m_screenManager) {
        return;
    }
    if (shouldSuppressOsd()) {
        return;
    }
    // Batch all per-screen OSD shows into one deferred call so every
    // screen's surface->show() fires in the same event loop pass and the
    // compositor renders them simultaneously.
    QTimer::singleShot(0, this, [this, screenIds, activity]() {
        if (!m_layoutManager || !m_screenManager) {
            return;
        }
        if (shouldSuppressOsd()) {
            return;
        }
        for (const QString& screenId : screenIds) {
            // Each screen reports against its OWN current virtual desktop
            // (Plasma 6.7 per-output virtual desktops, #648).
            const int desktop =
                m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : currentDesktop();
            // Route the disabled-context probe through the resolver so this
            // OSD pass uses the same single snapshot façade as every other
            // call site — the prior hand-stitched (modeFor → settings →
            // contextDisabledReason) cascade was the exact 3-step rebuild
            // PhosphorContext::IContextResolver was introduced to collapse.
            // `handleForPersisted` is the right axis here because the
            // caller already pinned the (desktop, activity) tuple the OSD
            // reports against, while the screen's mode stays live.
            DisabledReason why = DisabledReason::NotDisabled;
            if (m_contextResolver) {
                // Map PhosphorContext::DisabledReason → PlasmaZones::DisabledReason.
                // The two enums are value-identical by intent (the LGPL lib's
                // DisabledReason.h documents it mirrors the GPL daemon's),
                // but the conversion is written as a switch so a future enum
                // value added on one side surfaces here as a compile-time
                // -Wswitch warning rather than a silent value coercion.
                const auto reason = m_contextResolver->disabledReason(
                    m_contextResolver->handleForPersisted(screenId, desktop, activity));
                switch (reason) {
                case PhosphorContext::DisabledReason::NotDisabled:
                    why = DisabledReason::NotDisabled;
                    break;
                case PhosphorContext::DisabledReason::MonitorDisabled:
                    why = DisabledReason::MonitorDisabled;
                    break;
                case PhosphorContext::DisabledReason::DesktopDisabled:
                    why = DisabledReason::DesktopDisabled;
                    break;
                case PhosphorContext::DisabledReason::ActivityDisabled:
                    why = DisabledReason::ActivityDisabled;
                    break;
                }
            }
            if (why != DisabledReason::NotDisabled) {
                showContextDisabledOsd(screenId, desktop, activity, why);
                continue;
            }
            // No active layout for this context because the default assignment is
            // suppressed (global setting or per-context rule) — show a "not
            // assigned" OSD instead of the global default layout / algorithm the
            // fallback would otherwise surface for an unassigned screen.
            if (m_layoutManager->isContextActiveLayoutSuppressed(screenId, desktop, activity)) {
                showNotAssignedOsd(screenId);
                continue;
            }
            const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
            if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
                const QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId);
                // Bare autotile (mode set, no concrete algorithm) draws its
                // algorithm from the suppressed global default, so it won't tile
                // (see updateAutotileScreens) — show "not assigned" rather than
                // announcing the default algorithm. A concrete assigned algorithm
                // always shows.
                if (algoId.isEmpty()
                    && m_layoutManager->isDefaultAssignmentSuppressedForContext(screenId, desktop, activity)) {
                    showNotAssignedOsd(screenId);
                    continue;
                }
                auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(algoId) : nullptr;
                const QString displayName = algo ? algo->name() : algoId;
                showLayoutOsdForAlgorithm(algoId, displayName, screenId);
            } else {
                PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(screenId, desktop, activity);
                if (layout) {
                    showLayoutOsd(layout, screenId);
                }
            }
        }
    });
}

int Daemon::currentDesktop() const
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

int Daemon::currentDesktopForScreen(const QString& screenId) const
{
    // Per-output virtual desktops (#648): resolve THIS screen's current desktop,
    // falling back to the global current when no per-output value is on record.
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
}

QString Daemon::currentActivity() const
{
    return PhosphorWorkspaces::ActivityManager::currentActivityOrEmpty(m_activityManager.get());
}

bool Daemon::isCurrentContextLockedForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const
{
    if (!m_contextResolver) {
        return false;
    }
    return m_contextResolver->isLocked(m_contextResolver->handleForMode(screenId, mode));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shortcut cheatsheet overlay
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Dedicated Escape ad-hoc grab id — deliberately NOT the shared
// kCancelOverlayId: reusing that would drag the cheatsheet into the
// cancelSnap precedence chain and its cross-consumer release guard.
// KGlobalAccel routes one action per key, so the daemon keeps at most one
// Escape consumer active by dismissing sibling modals around show.
const QLatin1String kCheatsheetDismissId("cheatsheet_dismiss");

// String form of the per-screen tiling mode as CheatsheetContent consumes
// it. Scrolling has no engine yet but is a real router state; the sheet
// shows the mode-independent groups there.
QString cheatsheetModeString(PhosphorZones::AssignmentEntry::Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return QStringLiteral("autotile");
    case PhosphorZones::AssignmentEntry::Scrolling:
        return QStringLiteral("scrolling");
    case PhosphorZones::AssignmentEntry::Snapping:
        break;
    }
    return QStringLiteral("snapping");
}

} // namespace

void Daemon::toggleCheatsheet()
{
    if (!m_overlayService || !m_shortcutManager) {
        return;
    }
    if (m_overlayService->isCheatsheetVisible()) {
        m_overlayService->hideCheatsheet();
        return;
    }
    showCheatsheetOnCursorScreen();
}

void Daemon::showCheatsheetOnCursorScreen()
{
    if (!m_overlayService || !m_shortcutManager) {
        return;
    }
    if (m_settings && !m_settings->cheatsheetEnabled()) {
        qCDebug(lcDaemon) << "Cheatsheet: disabled in settings";
        return;
    }
    // No cheatsheet during an interactive drag: the kwin-effect holds a
    // keyboard grab for the drag's lifetime and routes Escape to cancelSnap
    // itself (see windowdragadaptor/drag.cpp), so the dismiss grab bound
    // below would never fire, and the sheet would also overlap the live
    // drag surfaces. The user can re-press after dropping the window.
    if (m_windowDragAdaptor && m_windowDragAdaptor->isDragInFlight()) {
        qCDebug(lcDaemon) << "Cheatsheet: suppressed during interactive drag";
        return;
    }

    // Screen-targeted like the picker and the mode toggle: the user's
    // intent is "the screen I am looking at", so resolve cursor-first.
    const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "Cheatsheet: no screen info";
        return;
    }

    // At most one Escape-consuming modal at a time (see kCheatsheetDismissId
    // note): dismiss the picker / snap assist first. Their dismissed signals
    // release the shared cancel-overlay Escape grab synchronously, so the
    // cheatsheet's own Escape registration below cannot be silently no-op'd
    // by a key-level conflict.
    if (m_overlayService->isLayoutPickerVisible()) {
        m_overlayService->hideLayoutPicker();
    }
    if (m_overlayService->isSnapAssistVisible()) {
        m_overlayService->hideSnapAssist();
    }

    const auto mode = currentModeFor(screenId);
    const bool autotileAvailable = m_settings && m_settings->autotileEnabled();
    m_overlayService->showCheatsheet(screenId, m_shortcutManager->cheatsheetModel(), cheatsheetModeString(mode),
                                     autotileAvailable);

    // Bind Escape only if the sheet actually became visible — showCheatsheet
    // bails on missing screen/shell/catalog, and the only releaser is
    // cheatsheetDismissed, which never fires for an invisible sheet. Binding
    // on a failed show would leak the Escape grab system-wide (same hazard
    // the picker guards against).
    if (m_overlayService->isCheatsheetVisible()) {
        m_shortcutManager->registerAdhocShortcut(kCheatsheetDismissId, QKeySequence(Qt::Key_Escape),
                                                 PhosphorI18n::tr("Dismiss Shortcut Cheatsheet"), [this] {
                                                     if (m_overlayService) {
                                                         m_overlayService->hideCheatsheet();
                                                     }
                                                 });
    }
}

void Daemon::refreshCheatsheetIfVisible()
{
    if (!m_overlayService || !m_shortcutManager || !m_overlayService->isCheatsheetVisible()) {
        return;
    }
    // Re-resolve for the screen the sheet is BOUND to — never retarget to
    // the cursor's current screen; only mode changes on the bound screen
    // refilter (the sheet stays put like the picker does).
    const QString screenId = m_overlayService->cheatsheetScreenId();
    if (screenId.isEmpty()) {
        return;
    }
    const auto mode = currentModeFor(screenId);
    const bool autotileAvailable = m_settings && m_settings->autotileEnabled();
    m_overlayService->refreshCheatsheet(m_shortcutManager->cheatsheetModel(), cheatsheetModeString(mode),
                                        autotileAvailable);
}

void Daemon::onCheatsheetDismissed()
{
    // Fires on EVERY dismissal path (toggle re-press, Escape, backdrop,
    // teardown) — the right place to release the Escape grab.
    if (m_shortcutManager) {
        m_shortcutManager->unregisterAdhocShortcut(kCheatsheetDismissId);
    }
}

} // namespace PlasmaZones
