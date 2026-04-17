// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowdragadaptor.h"
#include "../windowtrackingadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/assignmententry.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/virtualscreen.h"
#include "../../autotile/AutotileEngine.h"
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace PlasmaZones {

void WindowDragAdaptor::dragStopped(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons,
                                    int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldApplyGeometry,
                                    QString& releaseScreenIdOut, bool& restoreSizeOnlyOut, bool& snapAssistRequestedOut,
                                    EmptyZoneList& emptyZonesOut, QString& resolvedZoneIdOut)
{
    // Initialize output parameters
    // shouldApplyGeometry: true = KWin should set window to (snapX, snapY, snapWidth, snapHeight)
    // restoreSizeOnly: when true with shouldApplyGeometry, effect uses current position + returned size
    // (drag-to-unsnap)
    // resolvedZoneIdOut: the primary zone ID the window snapped to, across every snap
    //   path (hover-detect, zone selector, modifier multi-zone). Empty when no snap
    //   happened. The caller uses this for DragOutcome.zoneId, which the post-Phase-1B
    //   validator requires on ApplySnap — see drag_protocol.cpp.
    snapX = 0;
    snapY = 0;
    snapWidth = 0;
    snapHeight = 0;
    shouldApplyGeometry = false;
    releaseScreenIdOut.clear();
    restoreSizeOnlyOut = false;
    snapAssistRequestedOut = false;
    emptyZonesOut.clear();
    resolvedZoneIdOut.clear();

    if (windowId != m_draggedWindowId) {
        return;
    }

    // ── Autotile drag-insert commit ─────────────────────────────────────────
    // If a drag-insert preview is live, finalize it: commit the reorder so the
    // dragged window's final geometry is applied on the next retile. Snapping
    // logic is skipped entirely — the window's place in the stack IS the drop.
    if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
        m_autotileEngine->commitDragInsertPreview(); // commit, not cancel — drop finalizes the reorder
        hideOverlayAndSelector();
        resetDragState();
        return;
    }

    // Release screen: use cursor position passed from effect (at release time), not last dragMoved.
    // Resolve the effective (virtual-aware) screen ID so zones are calculated against
    // virtual screen bounds, not physical screen bounds.
    auto releaseResolved = resolveScreenAt(QPointF(cursorX, cursorY));
    QString releaseScreenId = releaseResolved.screenId;
    QScreen* releaseScreen = releaseResolved.qscreen;
    QString releaseScreenName = releaseScreen ? releaseScreen->name() : QString();
    releaseScreenIdOut = releaseScreenId;
    qCDebug(lcDbusWindow) << "dragStopped: cursor=" << cursorX << "," << cursorY
                          << "releaseScreen=" << releaseScreenName << "releaseScreenId=" << releaseScreenId;

    // Capture zone state into locals right away. If another window starts dragging before
    // the async D-Bus reply for this dragStopped() is processed, dragMoved() would overwrite
    // m_currentZoneId; capturing here ensures this window snaps to the correct zone.
    const QString capturedZoneId = m_currentZoneId;
    const QRect capturedZoneGeometry = m_currentZoneGeometry;
    const bool capturedIsMultiZoneMode = m_isMultiZoneMode;
    const QRect capturedMultiZoneGeometry = m_currentMultiZoneGeometry;
    const QVector<QUuid> capturedAdjacentZoneIds = m_currentAdjacentZoneIds;
    const bool capturedWasSnapped = m_wasSnapped;
    const QRect capturedOriginalGeometry = m_originalGeometry;
    const bool capturedSnapCancelled = m_snapCancelled;
    const bool capturedZoneSelectorShown = m_zoneSelectorShown;
    // Release on a disabled context: do not snap to overlay zone
    bool useOverlayZone = true;
    int curDesktopDrop = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
    QString curActivityDrop = m_layoutManager ? m_layoutManager->currentActivity() : QString();
    if (releaseScreen && isContextDisabled(m_settings, releaseScreenId, curDesktopDrop, curActivityDrop)) {
        useOverlayZone = false;
    }

    // Release on an autotile screen: do not snap to manual overlay zone.
    // The autotile engine manages window placement on these screens; allowing a
    // manual drag-snap would conflict with the engine's layout.
    if (useOverlayZone && releaseScreen && m_autotileEngine && m_autotileEngine->isAutotileScreen(releaseScreenId)) {
        useOverlayZone = false;
    }

    // Cross-screen drag: if the window was snapped on a different screen, clear
    // its snap/float state NOW — before any new zone detection or snap logic runs.
    // During drag, outputChanged's windowScreenChanged is skipped (drag owns state).
    // This is the single point where cross-screen state cleanup happens.
    if (capturedWasSnapped && m_windowTracking && releaseScreen) {
        QString storedScreen = m_windowTracking->service()->screenAssignments().value(windowId);
        if (!storedScreen.isEmpty() && storedScreen != releaseScreenId) {
            m_windowTracking->windowUnsnapped(windowId);
            // Preserve pre-tile geometry: it holds the correct free-floating dimensions
            // from the original snap. Clearing it would cause tryStorePreSnapGeometry to
            // store zone geometry (capturedOriginalGeometry is zone-sized for snapped windows).
            // The existing entry's screen context may be stale, but validatedPreTileGeometry
            // handles cross-screen adjustment (clamp + center) when restoring.
            qCInfo(lcDbusWindow) << "Cross-screen drag: cleared snap state for" << windowId << "from" << storedScreen
                                 << "to" << releaseScreenId;
        }
    }

    // Check if a zone was selected via the zone selector (takes priority)
    bool usedZoneSelector = false;
    if (!capturedSnapCancelled && capturedZoneSelectorShown && m_overlayService
        && m_overlayService->hasSelectedZone()) {
        QString selectedLayoutId = m_overlayService->selectedLayoutId();
        // Resolve virtual-aware screen ID for the zone selector position
        auto selectorResolved = resolveScreenAt(QPointF(cursorX, cursorY));
        QString selectorScreenId = selectorResolved.screenId;
        QScreen* screen = selectorResolved.qscreen;

        // Block entire zone selector snap path when screen is locked for its current mode
        bool selectorScreenLocked = false;
        if (screen && m_settings && m_layoutManager) {
            int curDesktop = m_layoutManager->currentVirtualDesktop();
            QString curActivity = m_layoutManager->currentActivity();
            int curMode = static_cast<int>(m_layoutManager->modeForScreen(selectorScreenId, curDesktop, curActivity));
            selectorScreenLocked = m_settings->isContextLocked(
                QString::number(curMode) + QStringLiteral(":") + selectorScreenId, curDesktop, curActivity);
        }
        if (screen && !selectorScreenLocked
            && !isContextDisabled(m_settings, selectorScreenId, curDesktopDrop, curActivityDrop)) {
            QRect zoneGeom = m_overlayService->getSelectedZoneGeometry(selectorScreenId);
            if (zoneGeom.isValid()) {
                snapX = zoneGeom.x();
                snapY = zoneGeom.y();
                snapWidth = zoneGeom.width();
                snapHeight = zoneGeom.height();
                shouldApplyGeometry = true;
                usedZoneSelector = true;

                tryStorePreSnapGeometry(windowId, capturedWasSnapped, capturedOriginalGeometry);

                int selectedZoneIndex = m_overlayService->selectedZoneIndex();
                if (m_windowTracking && m_layoutManager) {
                    // Get the actual zone UUID from layout and zone index so navigation works
                    auto layoutUuidOpt = Utils::parseUuid(selectedLayoutId);
                    QString zoneUuid;
                    PhosphorZones::Layout* selectedLayout = nullptr;
                    if (layoutUuidOpt) {
                        QUuid layoutUuid = *layoutUuidOpt;
                        selectedLayout = m_layoutManager->layoutById(layoutUuid);
                        if (selectedLayout && selectedZoneIndex >= 0
                            && selectedZoneIndex < selectedLayout->zones().size()) {
                            PhosphorZones::Zone* zone = selectedLayout->zones().at(selectedZoneIndex);
                            if (zone) {
                                zoneUuid = zone->id().toString();
                            }
                        }
                    }
                    if (zoneUuid.isEmpty()) {
                        qCWarning(lcDbusWindow)
                            << "Could not resolve zone UUID from selector - layout:" << selectedLayoutId
                            << "index:" << selectedZoneIndex;
                        // Fallback to synthetic format (navigation won't work, but tracking still happens)
                        zoneUuid = QStringLiteral("zoneselector-%1-%2").arg(selectedLayoutId).arg(selectedZoneIndex);
                    }
                    // Publish the resolved id so drag_protocol.cpp can populate
                    // DragOutcome.zoneId. Without this, the zone-selector path
                    // would still surface m_currentZoneId (which is never written
                    // by this branch) and the post-Phase-1B validator would drop
                    // the ApplySnap outcome for an empty zoneId.
                    resolvedZoneIdOut = zoneUuid;
                    m_windowTracking->windowSnapped(windowId, zoneUuid, releaseScreenId);
                    // Record user-initiated snap (not auto-snap)
                    // This prevents auto-snapping windows that were never manually snapped by user
                    m_windowTracking->recordSnapIntent(windowId, true);

                    // During drag, the C++ updateSelectorPosition path updates selection
                    // state but does NOT emit manualLayoutSelected (only the QML hover
                    // path does, which doesn't fire during drag). Activate the selected
                    // layout directly so snap assist uses the correct layout's empty zones.
                    // We intentionally skip manualLayoutSelected to avoid a layout OSD
                    // flashing briefly before snap assist appears.
                    if (selectedLayout) {
                        // Check lock before applying layout change from drag-drop
                        int layoutChangeDesktop = m_layoutManager->currentVirtualDesktop();
                        QString layoutChangeActivity = m_layoutManager->currentActivity();
                        int lcMode = static_cast<int>(m_layoutManager->modeForScreen(
                            selectorScreenId, layoutChangeDesktop, layoutChangeActivity));
                        bool screenLocked = m_settings
                            && m_settings->isContextLocked(QString::number(lcMode) + QStringLiteral(":")
                                                               + selectorScreenId,
                                                           layoutChangeDesktop, layoutChangeActivity);
                        PhosphorZones::Layout* currentLayout =
                            m_layoutManager->resolveLayoutForScreen(selectorScreenId);
                        if (currentLayout != selectedLayout && !screenLocked) {
                            // Hide overlay/selector BEFORE the layout change so signal
                            // handlers (updateZoneSelectorWindow, updateOverlayWindow) find
                            // hidden windows and skip heavy QML property updates / LayerShell
                            // recalculations. All overlay queries are already done above.
                            hideOverlayAndSelector();
                            m_layoutManager->assignLayout(selectorScreenId, m_layoutManager->currentVirtualDesktop(),
                                                          m_layoutManager->currentActivity(), selectedLayout);
                            m_layoutManager->setActiveLayout(selectedLayout);
                        }
                    }
                }
            }
        }
    }

    // Hide overlay and zone selector UI (idempotent — may already be hidden above)
    hideOverlayAndSelector();

    // Fall back to regular zone detection if zone selector wasn't used
    // Use captured values to avoid race condition with concurrent drags
    // Do not snap to overlay zone when releasing on a disabled monitor
    if (!usedZoneSelector && !capturedSnapCancelled && !capturedZoneId.isEmpty() && useOverlayZone) {
        if (capturedIsMultiZoneMode && capturedMultiZoneGeometry.isValid()) {
            snapX = capturedMultiZoneGeometry.x();
            snapY = capturedMultiZoneGeometry.y();
            snapWidth = capturedMultiZoneGeometry.width();
            snapHeight = capturedMultiZoneGeometry.height();
            shouldApplyGeometry = true;
            // Publish primary zone id — identical reason to the zone-selector
            // branch above. capturedZoneId is the primary zone of the
            // multi-zone snap as resolved by dragMoved.
            resolvedZoneIdOut = capturedZoneId;
            tryStorePreSnapGeometry(windowId, capturedWasSnapped, capturedOriginalGeometry);
            if (m_windowTracking) {
                // Pass ALL zone IDs for multi-zone snap (not just primary)
                QStringList allZoneIds;
                for (const QUuid& id : capturedAdjacentZoneIds) {
                    allZoneIds.append(id.toString());
                }
                if (allZoneIds.isEmpty()) {
                    allZoneIds.append(capturedZoneId);
                }
                m_windowTracking->windowSnappedMultiZone(windowId, allZoneIds, releaseScreenId);
                // Record user-initiated snap (not auto-snap)
                m_windowTracking->recordSnapIntent(windowId, true);
            }
        } else if (capturedZoneGeometry.isValid()) {
            snapX = capturedZoneGeometry.x();
            snapY = capturedZoneGeometry.y();
            snapWidth = capturedZoneGeometry.width();
            snapHeight = capturedZoneGeometry.height();
            shouldApplyGeometry = true;
            resolvedZoneIdOut = capturedZoneId;
            tryStorePreSnapGeometry(windowId, capturedWasSnapped, capturedOriginalGeometry);
            if (m_windowTracking) {
                m_windowTracking->windowSnapped(windowId, capturedZoneId, releaseScreenId);
                // Record user-initiated snap (not auto-snap)
                m_windowTracking->recordSnapIntent(windowId, true);
            }
        }
    }

    // Handle unsnap - window was snapped but dropped outside any zone
    // Use same state as float shortcut: save zone for restore and mark floating, so unfloat/snap-back works.
    // Call unconditionally when capturedWasSnapped: windowUnsnappedForFloat handles the
    // no-zone case internally, and setWindowFloating ensures windowClosed won't persist
    // the zone (floating windows are excluded from persistence).
    if (!shouldApplyGeometry && capturedWasSnapped) {
        qCInfo(lcDbusWindow) << "Drag-out unsnap for" << windowId << "releaseScreen:" << releaseScreenId;
        if (m_windowTracking) {
            m_windowTracking->windowUnsnappedForFloat(windowId);
            m_windowTracking->setWindowFloating(windowId, true);
        }

        // On drag-to-unsnap: restore pre-snap width/height; window keeps drop position.
        // Float-toggle shortcut uses calculateUnfloatRestore and restores full x/y/w/h.
        // Pass the release screen for proper cross-screen geometry validation (the float
        // toggle path passes screenId to validatedPreTileGeometry; without it, coordinates
        // captured on another screen may fail isGeometryOnScreen and not restore).
        if (m_settings && m_settings->restoreOriginalSizeOnUnsnap() && m_windowTracking) {
            auto geo = m_windowTracking->service()->validatedPreTileGeometry(windowId, releaseScreenId);
            // Require strictly-positive dimensions: a degenerate stored
            // rect would produce a RestoreSize outcome that validates to
            // "requires non-zero size" and gets dropped effect-side, so
            // the window would never actually restore.
            if (geo && geo->width() > 0 && geo->height() > 0) {
                snapWidth = geo->width();
                snapHeight = geo->height();
                shouldApplyGeometry = true;
                restoreSizeOnlyOut = true;
                // Only clear pre-tile geometry after successful restore.
                // If not cleared, it remains available for a subsequent float
                // toggle (applyGeometryForFloat) if the user re-floats later.
                m_windowTracking->clearPreTileGeometry(windowId);
                qCInfo(lcDbusWindow) << "Drag-out unsnap: restoring size" << geo->width() << "x" << geo->height();
            } else {
                qCInfo(lcDbusWindow) << "Drag-out unsnap: no valid pre-tile geometry for" << windowId;
            }
        }
    }

    // Snap Assist: only when we actually SNAPPED to a zone (not when restoring size on unsnap).
    // Empty zones are those with no windows AFTER windowSnapped (called above). The zone(s)
    // we just snapped to are now occupied, so they are excluded. Remaining empty zones
    // are offered for the user to fill via the window picker.
    // Request snap assist when: always enabled OR any SnapAssistTrigger held at drop.
    const bool actuallySnapped = shouldApplyGeometry && !restoreSizeOnlyOut;
    const bool snapAssistFeatureOn = m_settings && m_settings->snapAssistFeatureEnabled();
    const bool snapAssistBySetting = snapAssistFeatureOn && m_settings->snapAssistEnabled();
    const QVariantList snapAssistTriggers = snapAssistFeatureOn ? m_settings->snapAssistTriggers() : QVariantList();
    const bool snapAssistByTrigger = !snapAssistTriggers.isEmpty()
        && anyTriggerHeld(snapAssistTriggers, static_cast<Qt::KeyboardModifiers>(modifiers), mouseButtons);
    const bool requestSnapAssist = actuallySnapped && snapAssistFeatureOn
        && (snapAssistBySetting || snapAssistByTrigger) && releaseScreen && m_layoutManager && m_windowTracking;
    if (requestSnapAssist) {
        // Snap assist compute is deferred to after the endDrag reply returns —
        // the caller schedules computeAndEmitSnapAssist() via QTimer::singleShot(0)
        // so the compositor is unblocked before buildEmptyZoneList walks the
        // zone list. Here we only flag the request; emptyZonesOut stays empty
        // and the actual list arrives via the snapAssistReady signal.
        snapAssistRequestedOut = true;
        m_snapAssistPendingWindowId = windowId;
        m_snapAssistPendingScreenId = releaseScreenId;
    }

    // Reset drag state for next operation.
    // If snap assist will be shown, keep the Escape shortcut registered so
    // KGlobalAccel can still dismiss it (the snap assist window may not have
    // Wayland keyboard focus yet when the user presses Escape).
    resetDragState(/*keepEscapeShortcut=*/snapAssistRequestedOut);
}

void WindowDragAdaptor::computeAndEmitSnapAssist()
{
    // Consume pending state — cleared regardless of whether we emit, so a
    // follow-up drag never sees stale IDs.
    const QString windowId = m_snapAssistPendingWindowId;
    const QString screenId = m_snapAssistPendingScreenId;
    m_snapAssistPendingWindowId.clear();
    m_snapAssistPendingScreenId.clear();

    if (windowId.isEmpty() || screenId.isEmpty() || !m_layoutManager || !m_windowTracking) {
        return;
    }

    // Resolve the physical QScreen from the stored screen id. The
    // buildEmptyZoneList(layout, screenId, physScreen, ...) overload falls
    // back to the physScreen path if no valid virtual-screen geometry is
    // found, so we pass nullptr when we can't match — the VS path will
    // handle it via screenId lookup inside buildEmptyZoneList itself.
    QScreen* releaseScreen = nullptr;
    for (QScreen* s : QGuiApplication::screens()) {
        if (Utils::screenIdentifier(s) == screenId || s->name() == screenId) {
            releaseScreen = s;
            break;
        }
    }

    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        return;
    }

    // Screen-filtered + desktop-filtered occupancy. Without the desktop filter,
    // windows parked on other virtual desktops keep their zone occupied on the
    // current desktop, which blocks snap assist from offering the zone (discussion
    // #323) — even though SnapAssistHandler::buildCandidates() already excludes
    // other-desktop windows from the candidate list. Matches the filtering done
    // by WindowTrackingService::getEmptyZones()/calculateSnapAllWindows().
    const int desktopFilter = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
    QSet<QUuid> occupied = m_windowTracking->service()->buildOccupiedZoneSet(screenId, desktopFilter);
    EmptyZoneList emptyZones = GeometryUtils::buildEmptyZoneList(layout, screenId, releaseScreen, m_settings,
                                                                 [&occupied](const PhosphorZones::Zone* z) {
                                                                     return !occupied.contains(z->id());
                                                                 });

    if (emptyZones.isEmpty()) {
        return;
    }

    qCDebug(lcDbusWindow) << "snapAssistReady: emitting" << emptyZones.size() << "empty zones for window" << windowId
                          << "on screen" << screenId;
    Q_EMIT snapAssistReady(windowId, screenId, emptyZones);
}

} // namespace PlasmaZones
