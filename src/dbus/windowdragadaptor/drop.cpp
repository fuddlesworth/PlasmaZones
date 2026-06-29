// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowdragadaptor.h"
#include "../windowtrackingadaptor.h"
#include "../snapadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include "../../core/interfaces.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

void WindowDragAdaptor::dragStopped(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons,
                                    int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldApplyGeometry,
                                    QString& releaseScreenIdOut, bool& restoreSizeOnlyOut, bool& snapAssistRequestedOut,
                                    PhosphorProtocol::EmptyZoneList& emptyZonesOut, QString& resolvedZoneIdOut)
{
    // Initialize output parameters
    // shouldApplyGeometry: true = KWin should set window to (snapX, snapY, snapWidth, snapHeight)
    // restoreSizeOnly: when true with shouldApplyGeometry, effect uses current position + returned size
    // (drag-to-unsnap)
    // resolvedZoneIdOut: the primary zone ID the window snapped to, across every snap
    //   path (hover-detect, zone selector, modifier multi-zone). Empty when no snap
    //   happened. The caller uses this for PhosphorProtocol::DragOutcome.zoneId, which the post-Phase-1B
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

    // Cross-screen stale-geometry guard. capturedZoneGeometry / capturedMultiZoneGeometry
    // come from the last dragMoved tick, which may have resolved a zone on a different
    // output than this release. Applying that rect would place the window on the wrong
    // screen while commitSnap below records it on releaseScreenId. Resolve the captured
    // rect's own physical screen and require it to match the release screen; on a
    // mismatch the snap is declined and the window float-drops at its release position.
    // See discussion #511.
    bool capturedGeometryMatchesReleaseScreen = true;
    {
        const QRect activeCapturedGeometry = (capturedIsMultiZoneMode && capturedMultiZoneGeometry.isValid())
            ? capturedMultiZoneGeometry
            : capturedZoneGeometry;
        if (activeCapturedGeometry.isValid()) {
            const QString geometryPhysicalId = resolveScreenAt(QPointF(activeCapturedGeometry.center())).physicalId;
            if (!geometryPhysicalId.isEmpty() && !releaseResolved.physicalId.isEmpty()
                && geometryPhysicalId != releaseResolved.physicalId) {
                capturedGeometryMatchesReleaseScreen = false;
                qCWarning(lcDbusWindow) << "dragStopped: captured zone geometry" << activeCapturedGeometry
                                        << "is on screen" << geometryPhysicalId << "but window released on"
                                        << releaseResolved.physicalId << "- declining cross-screen stale snap for"
                                        << windowId;
            }
        }
    }

    // Release on a disabled context: do not snap to overlay zone.
    // Routes through PhosphorContext::IContextResolver's `handleFor`
    // — live mode — not `handleForMode(Snapping)`. The release screen
    // can change mode between drag-start and drop (the daemon may have
    // re-routed it to autotile, or the user may have toggled snap off
    // and the autotile engine is now in charge). Gating on the
    // hard-coded Snapping disable list would silently consult the
    // wrong list whenever the live mode is anything else. This
    // mirrors the same fix applied to drag_protocol.cpp::computeDragPolicy.
    bool useOverlayZone = true;
    if (releaseScreen && m_contextResolver
        && m_contextResolver->isDisabled(m_contextResolver->handleFor(releaseScreenId))) {
        useOverlayZone = false;
    }

    // Release on an autotile screen: do not snap to manual overlay zone.
    // The autotile engine manages window placement on these screens; allowing a
    // manual drag-snap would conflict with the engine's layout.
    if (useOverlayZone && releaseScreen && m_autotileEngine && m_autotileEngine->isActiveOnScreen(releaseScreenId)) {
        useOverlayZone = false;
    }

    // Cross-screen drag: when the window's owning engine differs from the
    // engine that owns the release screen, run the IPlacementEngine handoff
    // contract NOW — before any destination-side snap/tile logic runs.
    // During drag, outputChanged's windowScreenChanged is skipped (drag owns
    // state), so this is the single point where cross-screen ownership
    // transfer happens.
    //
    // The release uses the contract so both source modes (snap zone, autotile
    // tile) drop tracking via the same call; the receive only fires when the
    // destination engine type differs from the source — same-mode cross-screen
    // (snap→snap, autotile→autotile) is already handled by the receiving
    // engine's normal commit / drag-insert paths below.
    //
    // Pre-tile / pre-float geometry captures are preserved by handoffRelease
    // for the receiving side to consult on a future return handoff.
    if (releaseScreen && m_windowTracking) {
        auto* snapEngine = m_windowTracking->snapEngine();
        const QString snapScreen = snapEngine ? snapEngine->screenForTrackedWindow(windowId) : QString();
        const QString autotileScreen =
            m_autotileEngine ? m_autotileEngine->screenForTrackedWindow(windowId) : QString();
        PhosphorEngine::IPlacementEngine* sourceEngine = nullptr;
        QString sourceScreen;
        if (!snapScreen.isEmpty() && snapScreen != releaseScreenId) {
            sourceEngine = snapEngine;
            sourceScreen = snapScreen;
        } else if (!autotileScreen.isEmpty() && autotileScreen != releaseScreenId) {
            sourceEngine = m_autotileEngine;
            sourceScreen = autotileScreen;
        }
        if (sourceEngine) {
            const bool destIsAutotile = m_autotileEngine && m_autotileEngine->isActiveOnScreen(releaseScreenId);
            PhosphorEngine::IPlacementEngine* destEngine = destIsAutotile ? m_autotileEngine : snapEngine;
            const bool engineTypeChanged = destEngine && destEngine != sourceEngine;

            sourceEngine->handoffRelease(windowId);
            qCInfo(lcDbusWindow) << "Cross-screen drag: released" << sourceEngine->engineId() << "state for" << windowId
                                 << "from" << sourceScreen << "to" << releaseScreenId;

            // Engine-type change: hand off to the destination so it can
            // adopt the window. The committing snap/tile path below may
            // still finalize the placement — handoffReceive only sets up
            // tracking with the right floating disposition.
            if (engineTypeChanged) {
                PhosphorEngine::IPlacementEngine::HandoffContext ctx;
                ctx.windowId = windowId;
                ctx.toScreenId = releaseScreenId;
                ctx.fromEngineId = sourceEngine->engineId();
                ctx.dropPos = QPoint(cursorX, cursorY);
                ctx.sourceGeometry = capturedOriginalGeometry;
                ctx.wasFloating = m_windowTracking->service()->isWindowFloating(windowId);
                if (capturedWasSnapped && !ctx.wasFloating && !capturedZoneId.isEmpty()) {
                    // sourceZoneIds is informational for receiving engines —
                    // populated from the pre-drop captured zone since
                    // handoffRelease has already cleared live tracking.
                    ctx.sourceZoneIds = QStringList{capturedZoneId};
                }
                destEngine->handoffReceive(ctx);
            }
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

        // Block entire zone selector snap path when screen is locked for its
        // current mode. Take a single resolver snapshot of (desktop, activity)
        // via `handleFor`, then ask the layout manager to resolve the mode
        // against THOSE frozen axes — using `currentVirtualDesktop()` /
        // `currentActivity()` on the resolver re-reads workspace state on
        // every call (per IContextResolver.h's "NOT cached snapshots of an
        // earlier handleFor() result" note on the raw workspace
        // accessors), so a virtual-desktop switch
        // between the mode lookup and the gate read would silently decouple
        // them. The handle's existing fields are authoritative — overwriting
        // `mode` in place keeps the gate consistent.
        bool selectorScreenLocked = false;
        PhosphorContext::ContextHandle selectorCtx;
        if (screen && m_contextResolver && m_layoutManager) {
            selectorCtx = m_contextResolver->handleFor(selectorScreenId);
            selectorCtx.mode =
                m_layoutManager->modeForScreen(selectorScreenId, selectorCtx.virtualDesktop, selectorCtx.activity);
            selectorScreenLocked = m_contextResolver->isLocked(selectorCtx);
        }
        // Reuse the `selectorCtx` handle built above for the lock check —
        // that handle already carries the resolved live mode for the
        // selector screen. Hard-coding `Snapping` here (the previous
        // shape) would have consulted the wrong disable list whenever
        // the user had reconfigured the screen between drag-start and
        // drop, mirroring the same fix applied to useOverlayZone above.
        // Include `m_layoutManager` in the gate — `selectorCtx` is only
        // populated under the matching gate above. A degenerate case
        // where `m_contextResolver` is set but `m_layoutManager` is null
        // would otherwise call `isDisabled` against a default-constructed
        // handle (empty screenId / desktop=0 / activity=""), which
        // short-circuits to "not disabled" and lets the snap path
        // proceed against stale defaults.
        if (screen && !selectorScreenLocked && m_contextResolver && m_layoutManager
            && !m_contextResolver->isDisabled(selectorCtx)) {
            QRect zoneGeom = m_overlayService->getSelectedZoneGeometry(selectorScreenId);
            if (zoneGeom.isValid()) {
                snapX = zoneGeom.x();
                snapY = zoneGeom.y();
                snapWidth = zoneGeom.width();
                snapHeight = zoneGeom.height();
                shouldApplyGeometry = true;
                usedZoneSelector = true;

                tryStorePreSnapGeometry(windowId, capturedOriginalGeometry);

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
                        // Refuse to commit a synthetic non-UUID id: it would
                        // propagate into zoneAssignments + persistence verbatim
                        // and break every downstream consumer that parses zone
                        // ids as QUuid (navigation, restore-by-id, serialisation
                        // round-trips). Skip the commit entirely and let the
                        // window float-drop at the release cursor.
                        qCWarning(lcDbusWindow) << "Could not resolve zone UUID from selector — skipping snap commit"
                                                << "layout:" << selectedLayoutId << "index:" << selectedZoneIndex;
                        shouldApplyGeometry = false;
                    } else {
                        // Publish the resolved id so drag_protocol.cpp can
                        // populate PhosphorProtocol::DragOutcome.zoneId.
                        // Without this, the zone-selector path would still
                        // surface m_currentZoneId (which is never written by
                        // this branch) and the post-Phase-1B validator would
                        // drop the ApplySnap outcome for an empty zoneId.
                        resolvedZoneIdOut = zoneUuid;
                        auto* snapEng = m_windowTracking->snapEngine();
                        if (snapEng)
                            snapEng->commitSnap(windowId, zoneUuid, releaseScreenId);
                        // Record user-initiated snap (not auto-snap)
                        // This prevents auto-snapping windows that were never manually snapped by user
                        m_windowTracking->service()->recordSnapIntent(windowId, true);
                    }

                    // The QML hover commit path is gone — ZoneSelectorContent is
                    // `interactive: false`, so the slot's MouseAreas never fire and
                    // selector.cpp::onZoneSelected was removed along with the
                    // manualLayoutSelected signal it used to emit. Selection state
                    // (m_selectedLayoutId / m_selectedZoneIndex) is now written
                    // exclusively by the C++ updateSelectorPosition hit-test during
                    // drag (selector.cpp). This branch is therefore the sole commit
                    // point for a cross-layout zone-selector drop, so activate the
                    // selected layout directly here. Doing the activation earlier on
                    // hover would resnap every other window mid-drag — the "layouts
                    // changing when holding alt to move window" bug.
                    if (selectedLayout) {
                        // Reuse the `selectorCtx` snapshot built at the top of
                        // this block — its (desktop, activity) is the same
                        // frozen tuple the gate above already consulted, and
                        // its mode was overwritten in place using the layout
                        // manager's per-(desktop, activity) lookup. Re-reading
                        // workspace state via the resolver's `currentVirtualDesktop()`
                        // / `currentActivity()` would re-cross the workspace
                        // and risk a virtual-desktop switch mid-handler
                        // decoupling the lock check from the assignLayout
                        // write below.
                        const bool screenLocked = m_contextResolver && m_contextResolver->isLocked(selectorCtx);
                        PhosphorZones::Layout* currentLayout =
                            m_layoutManager->resolveLayoutForScreen(selectorScreenId);
                        if (currentLayout != selectedLayout && !screenLocked) {
                            // Hide overlay/selector BEFORE the layout change so signal
                            // handlers (updateZoneSelectorWindow, updateOverlayWindow) find
                            // hidden windows and skip heavy QML property updates / LayerShell
                            // recalculations. All overlay queries are already done above.
                            hideOverlayAndSelector();
                            // Pass the snapshot's (desktop, activity) so the
                            // write lands on the same axes the gate consulted.
                            // Routing through `m_layoutManager->currentVirtualDesktop()`
                            // (a third, independent read) would reintroduce the
                            // split-snapshot race the resolver was added to remove.
                            m_layoutManager->assignLayout(selectorScreenId, selectorCtx.virtualDesktop,
                                                          selectorCtx.activity, selectedLayout);
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
        if (capturedIsMultiZoneMode && capturedMultiZoneGeometry.isValid() && capturedGeometryMatchesReleaseScreen) {
            snapX = capturedMultiZoneGeometry.x();
            snapY = capturedMultiZoneGeometry.y();
            snapWidth = capturedMultiZoneGeometry.width();
            snapHeight = capturedMultiZoneGeometry.height();
            shouldApplyGeometry = true;
            // Publish primary zone id — identical reason to the zone-selector
            // branch above. capturedZoneId is the primary zone of the
            // multi-zone snap as resolved by dragMoved.
            resolvedZoneIdOut = capturedZoneId;
            tryStorePreSnapGeometry(windowId, capturedOriginalGeometry);
            if (m_windowTracking) {
                // Pass ALL zone IDs for multi-zone snap (not just primary)
                QStringList allZoneIds;
                for (const QUuid& id : capturedAdjacentZoneIds) {
                    allZoneIds.append(id.toString());
                }
                if (allZoneIds.isEmpty()) {
                    allZoneIds.append(capturedZoneId);
                }
                auto* snapMulti = m_windowTracking->snapEngine();
                if (snapMulti)
                    snapMulti->commitMultiZoneSnap(windowId, allZoneIds, releaseScreenId);
                // Record user-initiated snap (not auto-snap)
                m_windowTracking->service()->recordSnapIntent(windowId, true);
            }
        } else if (capturedZoneGeometry.isValid() && capturedGeometryMatchesReleaseScreen) {
            snapX = capturedZoneGeometry.x();
            snapY = capturedZoneGeometry.y();
            snapWidth = capturedZoneGeometry.width();
            snapHeight = capturedZoneGeometry.height();
            shouldApplyGeometry = true;
            resolvedZoneIdOut = capturedZoneId;
            tryStorePreSnapGeometry(windowId, capturedOriginalGeometry);
            if (m_windowTracking) {
                auto* snapSingle = m_windowTracking->snapEngine();
                if (snapSingle)
                    snapSingle->commitSnap(windowId, capturedZoneId, releaseScreenId);
                // Record user-initiated snap (not auto-snap)
                m_windowTracking->service()->recordSnapIntent(windowId, true);
            }
        }
    }

    // Handle unsnap - window was snapped but dropped outside any zone
    // Use same state as float shortcut: save zone for restore and mark floating, so unfloat/snap-back works.
    // Call unconditionally when capturedWasSnapped: unsnapForFloat handles the
    // no-zone case internally, and setWindowFloating ensures windowClosed won't persist
    // the zone (floating windows are excluded from persistence).
    if (!shouldApplyGeometry && capturedWasSnapped) {
        qCInfo(lcDbusWindow) << "Drag-out unsnap for" << windowId << "releaseScreen:" << releaseScreenId;
        if (m_windowTracking) {
            // unsnapForFloat on WTS: saves zone for restore, clears assignment.
            // No-op if the window wasn't snapped.
            m_windowTracking->service()->unsnapForFloat(windowId);
            m_windowTracking->setWindowFloating(windowId, true);
        }

        // On drag-to-unsnap: restore pre-snap width/height; window keeps drop position.
        // Float-toggle shortcut uses calculateUnfloatRestore and restores full x/y/w/h.
        // Pass the release screen for proper cross-screen geometry validation (the float
        // toggle path passes screenId to validatedUnmanagedGeometry; without it,
        // coordinates captured on another screen may fail the service's on-screen
        // visibility check and not restore).
        if (m_settings && m_settings->restoreOriginalSizeOnUnsnap() && m_windowTracking) {
            auto* wts = m_windowTracking->service();
            auto geo = wts->validatedUnmanagedGeometry(windowId, releaseScreenId);
            // Require strictly-positive dimensions: a degenerate stored
            // rect would produce a RestoreSize outcome that validates to
            // "requires non-zero size" and gets dropped effect-side, so
            // the window would never actually restore.
            if (geo && geo->width() > 0 && geo->height() > 0) {
                snapWidth = geo->width();
                snapHeight = geo->height();
                shouldApplyGeometry = true;
                restoreSizeOnlyOut = true;
                // Single float-back store: clear the record's shared free geometry
                // after we've consumed it for this drag-out restore.
                wts->clearFreeGeometry(windowId);
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
        // Snapshot the desktop the drop landed on. computeAndEmitSnapAssist
        // runs after the endDrag reply returns; if the user changes virtual
        // desktops between here and the timer firing, a live currentDesktop()
        // read would describe the new desktop's occupancy instead of the
        // desktop the user actually dropped on.
        m_snapAssistPendingDesktop = m_layoutManager->currentVirtualDesktopForScreen(releaseScreenId);
        // Bind the KGlobalAccel Escape grab for the snap-assist phase. The
        // drag itself relied on the kwin-effect's keyboard grab, which the
        // effect releases on dragStopped — so the post-drop snap-assist UI
        // (which may not yet hold Wayland keyboard focus) has no Escape
        // handler unless we bind one now. This is the only spot that pays a
        // kglobalshortcutsrc write, and only when snap assist actually
        // appears, not on every drag (discussion #167). onSnapAssistDismissed
        // releases it again when the snap-assist UI goes away.
        registerCancelOverlayShortcut();
    }

    // Reset drag state for next operation. keepEscapeShortcut is true exactly
    // when snap assist was requested above (and the grab was just bound), so
    // resetDragState skips the unregister and the snap-assist phase keeps its
    // Escape handler. When snap assist is not requested nothing was bound, so
    // the unregister on the false path is a no-op (Registry::unbind ignores
    // unknown ids).
    resetDragState(/*keepEscapeShortcut=*/snapAssistRequestedOut);
}

void WindowDragAdaptor::computeAndEmitSnapAssist()
{
    // Consume pending state — cleared regardless of whether we emit, so a
    // follow-up drag never sees stale IDs.
    const QString windowId = m_snapAssistPendingWindowId;
    const QString screenId = m_snapAssistPendingScreenId;
    const int desktopAtDrop = m_snapAssistPendingDesktop;
    m_snapAssistPendingWindowId.clear();
    m_snapAssistPendingScreenId.clear();
    m_snapAssistPendingDesktop = 0; // mirror snapshot clear; see header

    if (windowId.isEmpty() || screenId.isEmpty() || !m_layoutManager || !m_windowTracking) {
        return;
    }

    // Resolve the physical QScreen from the stored screen id. The
    // buildEmptyZoneList(layout, screenId, physScreen, ...) overload falls
    // back to the physScreen path if no valid virtual-screen geometry is
    // found, so we pass nullptr when we can't match — the VS path will
    // handle it via screenId lookup inside buildEmptyZoneList itself.
    QScreen* releaseScreen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);

    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        return;
    }

    // Screen-filtered + desktop-filtered occupancy. Without the desktop filter,
    // windows parked on other virtual desktops keep their zone occupied on the
    // current desktop, which blocks snap assist from offering the zone (discussion
    // #323) — even though SnapAssistHandler::buildCandidates() already excludes
    // other-desktop windows from the candidate list. Matches the filtering done
    // by PhosphorPlacement::WindowTrackingService::getEmptyZones()/calculateSnapAllWindows().
    //
    // Use the drop-time snapshot, not a live read: a user who changed
    // virtual desktops between endDrag and this deferred fire would
    // otherwise see snap-assist describing the NEW desktop's occupancy.
    // Falls back to a live read only when the snapshot is missing (0,
    // pre-snapshot codepath or invalid pending state).
    // m_layoutManager non-null is guaranteed by the early-return above; the
    // earlier ternary was dead-defensive.
    const int desktopFilter =
        desktopAtDrop > 0 ? desktopAtDrop : m_layoutManager->currentVirtualDesktopForScreen(screenId);
    QSet<QUuid> occupied = m_windowTracking->service()->buildOccupiedZoneSet(screenId, desktopFilter);
    PhosphorProtocol::EmptyZoneList emptyZones = GeometryUtils::buildEmptyZoneList(
        m_screenManager, layout, screenId, releaseScreen, m_settings,
        [&occupied](const PhosphorZones::Zone* z) {
            return !occupied.contains(z->id());
        },
        m_layoutManager);

    if (emptyZones.isEmpty()) {
        return;
    }

    qCDebug(lcDbusWindow) << "snapAssistReady: emitting" << emptyZones.size() << "empty zones for window" << windowId
                          << "on screen" << screenId;
    Q_EMIT snapAssistReady(windowId, screenId, emptyZones);
}

} // namespace PlasmaZones
