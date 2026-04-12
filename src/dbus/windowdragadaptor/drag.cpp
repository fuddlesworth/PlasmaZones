// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowdragadaptor.h"
#include <QGuiApplication>
#include <QScreen>
#include <cmath>
#include "../../config/configdefaults.h"
#include "../windowtrackingadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/zone.h"
#include "../../core/geometryutils.h"
#include "../../core/screenmanager.h"
#include "../../core/zoneselectorlayout.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/virtualscreen.h"
#include "../../core/constants.h"
#include "../../autotile/AutotileEngine.h"

namespace PlasmaZones {

void WindowDragAdaptor::dragStarted(const QString& windowId, double x, double y, double width, double height,
                                    int mouseButtons)
{
    Q_UNUSED(mouseButtons); // Only used in dragMoved for dynamic activation

    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "dragStarted: empty windowId";
        return;
    }

    if (!m_settings) {
        return;
    }

    // Pre-parse triggers to avoid QVariantMap unboxing on every dragMoved tick
    m_cachedActivationTriggers = parseTriggers(m_settings->dragActivationTriggers());
    m_cachedZoneSpanTriggers = parseTriggers(m_settings->zoneSpanTriggers());
    m_cachedAutotileDragInsertTriggers = parseTriggers(m_settings->autotileDragInsertTriggers());
    // Ensure no stale preview carries over from a prior drag.
    cancelDragInsertIfActive();

    // Check if snapping is enabled
    if (!m_settings->snappingEnabled()) {
        qCInfo(lcDbusWindow) << "Snapping disabled in settings, ignoring drag";
        m_snapCancelled = true;
        m_draggedWindowId.clear();
        return;
    }

    // Dismiss any visible snap assist overlay from a previous snap.
    // The user is starting a new drag, so the previous snap assist is stale.
    if (m_overlayService && m_overlayService->isSnapAssistVisible()) {
        m_overlayService->hideSnapAssist();
    }

    registerCancelOverlayShortcut();
    m_draggedWindowId = windowId;
    m_originalGeometry = QRect(qRound(x), qRound(y), qRound(width), qRound(height));
    m_currentZoneId.clear();
    m_currentZoneGeometry = QRect();
    m_currentAdjacentZoneIds.clear();
    m_isMultiZoneMode = false;
    m_currentMultiZoneGeometry = QRect();
    m_paintedZoneIds.clear();
    m_modifierConflictWarned = false;
    m_lastEmittedZoneGeometry = QRect();
    m_restoreSizeEmittedDuringDrag = false;
    m_lastLoggedActivationActive = false;
    m_snapCancelled = false;
    m_triggerReleasedAfterCancel = false;
    m_activationToggled = false;
    m_prevTriggerHeld = false;
    m_overlayShown = false;
    m_zoneSelectorShown = false;
    m_lastCursorX = 0;
    m_lastCursorY = 0;

    // Note: KWin Quick Tile override is now handled permanently by Daemon
    // (using kwriteconfig6 + KWin.reconfigure()) instead of per-drag toggling

    // Check if window started inside a zone (for restoreOriginalSizeOnUnsnap feature)
    // Primary method: Check if window is tracked as snapped in WindowTrackingAdaptor
    // This is more reliable than geometry matching because KWin may report window
    // positions differently (decorations, shadows, etc.) than how we calculated zones
    m_wasSnapped = false;
    if (m_windowTracking) {
        QString trackedZone = m_windowTracking->getZoneForWindow(windowId);
        if (!trackedZone.isEmpty()) {
            m_wasSnapped = true;
        }
    }

    // Fallback: If not tracked, try geometry matching (handles windows snapped before daemon restart)
    if (!m_wasSnapped) {
        QScreen* screen = screenAtPoint(m_originalGeometry.center().x(), m_originalGeometry.center().y());

        if (screen) {
            QString screenId = effectiveScreenIdAt(m_originalGeometry.center().x(), m_originalGeometry.center().y());
            if (screenId.isEmpty())
                screenId = Utils::screenIdentifier(screen);
            auto* layout = m_layoutManager->resolveLayoutForScreen(screenId);
            if (layout) {
                layout->recalculateZoneGeometries(GeometryUtils::effectiveScreenGeometry(layout, screenId));

                for (auto* zone : layout->zones()) {
                    QRect zoneRect =
                        GeometryUtils::getZoneGeometryForScreen(zone, screen, screenId, layout, m_settings);

                    // Use class constants for tolerances
                    int xDiff = std::abs(m_originalGeometry.x() - zoneRect.x());
                    int yDiff = std::abs(m_originalGeometry.y() - zoneRect.y());
                    int wDiff = std::abs(m_originalGeometry.width() - zoneRect.width());
                    int hDiff = std::abs(m_originalGeometry.height() - zoneRect.height());

                    // Size must match closely, position can be off due to decorations
                    if (wDiff <= SizeTolerance && hDiff <= SizeTolerance && xDiff <= PositionTolerance
                        && yDiff <= PositionTolerance) {
                        m_wasSnapped = true;
                        break;
                    }
                }
            }
        }
    }
}

Layout* WindowDragAdaptor::prepareHandlerContext(int x, int y, QScreen*& outScreen, QString& outScreenId)
{
    // Resolve effective (virtual-aware) screen ID
    auto resolved = resolveScreenAt(QPointF(x, y));
    outScreen = resolved.qscreen;
    if (!outScreen) {
        return nullptr;
    }
    outScreenId = resolved.screenId;
    if (isContextDisabled(m_settings, outScreenId, m_layoutManager->currentVirtualDesktop(),
                          m_layoutManager->currentActivity())) {
        if (m_overlayShown && m_overlayService) {
            m_overlayService->hide();
            m_overlayShown = false;
        }
        return nullptr;
    }

    // Skip overlay and zone detection on autotile-managed screens
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(outScreenId)) {
        if (m_overlayShown && m_overlayService) {
            m_overlayService->hide();
            m_overlayShown = false;
        }
        return nullptr;
    }

    // Call showAtPosition unconditionally on every tick. OverlayService
    // short-circuits on same-physical-monitor re-shows (compares extracted
    // physical ids of m_currentOverlayScreenId vs the cursor's effective id),
    // so rapid drag ticks that stay on one monitor cost only two findScreen
    // calls and a string extract — no Vulkan churn. The adaptor-side
    // m_overlayScreenId comparator that used to gate this was fed by the
    // same jittery effective-id source, and its string mismatches triggered
    // a second path into initializeOverlay even when the cursor hadn't
    // left the physical monitor. Dropping the comparator makes
    // OverlayService the single source of truth for "is a re-init needed".
    if (!m_overlayShown) {
        qCInfo(lcDbusWindow) << "prepareHandlerContext: show overlay at" << x << y << "screen=" << outScreenId;
    }
    m_overlayService->showAtPosition(x, y);
    m_overlayShown = true;

    auto* layout = m_layoutManager->resolveLayoutForScreen(outScreenId);
    if (!layout) {
        return nullptr;
    }

    // Use virtual screen geometry for zone calculation when available
    auto* mgr = ScreenManager::instance();
    QRectF effectiveGeom;
    if (mgr) {
        QRect vsGeom = mgr->screenGeometry(outScreenId);
        if (vsGeom.isValid()) {
            if (layout->useFullScreenGeometry()) {
                effectiveGeom = QRectF(vsGeom);
            } else {
                QRect vsAvailGeom = mgr->screenAvailableGeometry(outScreenId);
                effectiveGeom = vsAvailGeom.isValid() ? QRectF(vsAvailGeom) : QRectF(vsGeom);
            }
        }
    }
    if (!effectiveGeom.isValid()) {
        effectiveGeom = GeometryUtils::effectiveScreenGeometry(layout, outScreen);
    }
    layout->recalculateZoneGeometries(effectiveGeom);
    return layout;
}

void WindowDragAdaptor::hideOverlayAndClearZoneState()
{
    // Fast path: if overlay isn't shown and zone state is already clear, skip all work.
    // dragMoved calls this on every poll tick when no activation trigger is held, so
    // avoiding redundant clearHighlights()/clearHighlight() calls (which may touch QML
    // objects) prevents daemon event-loop congestion and D-Bus back-pressure on the
    // compositor thread (see discussion #167).
    if (!m_overlayShown && m_currentZoneId.isEmpty() && !m_isMultiZoneMode && m_paintedZoneIds.isEmpty()) {
        return;
    }

    if (m_overlayShown && m_overlayService) {
        qCInfo(lcDbusWindow) << "hideOverlayAndClearZoneState: hide overlay triggerHeld=false";
        m_overlayService->hide();
        m_overlayShown = false;
    }
    if (m_zoneDetector) {
        m_zoneDetector->clearHighlights();
    }
    if (m_overlayService) {
        m_overlayService->clearHighlight();
    }
    m_currentZoneId.clear();
    m_currentAdjacentZoneIds.clear();
    m_isMultiZoneMode = false;
    m_currentZoneGeometry = QRect();
    m_currentMultiZoneGeometry = QRect();
    m_paintedZoneIds.clear();
}

void WindowDragAdaptor::handleZoneSpanModifier(int x, int y)
{
    QScreen* screen = nullptr;
    QString screenId;
    auto* layout = prepareHandlerContext(x, y, screen, screenId);
    if (!layout) {
        return;
    }

    // Clear stale multi-zone state from proximity mode when transitioning to paint-to-span
    if (m_isMultiZoneMode && m_paintedZoneIds.isEmpty()) {
        m_currentAdjacentZoneIds.clear();
        m_isMultiZoneMode = false;
        m_currentMultiZoneGeometry = QRect();
    }

    // Find zone at cursor position using layout's smallest-area heuristic
    // (zone geometry already recalculated to absolute coords by prepareHandlerContext)
    Zone* foundZone = layout->zoneAtPoint(QPointF(x, y));

    // Accumulate painted zones (never remove during a paint drag)
    if (foundZone) {
        m_paintedZoneIds.insert(foundZone->id());
    }

    // Build zone list from painted zones, then expand using same raycast algorithm as editor
    if (!m_paintedZoneIds.isEmpty()) {
        QVector<Zone*> paintedZones;
        for (auto* zone : layout->zones()) {
            if (m_paintedZoneIds.contains(zone->id())) {
                paintedZones.append(zone);
            }
        }

        if (!paintedZones.isEmpty()) {
            // Use same raycasting/intersection algorithm as detectMultiZone and editor:
            // expand to include all zones that intersect the bounding rect of painted zones
            m_zoneDetector->setLayout(layout);
            QVector<Zone*> zonesToSnap = m_zoneDetector->expandPaintedZonesToRect(paintedZones);

            if (zonesToSnap.isEmpty()) {
                return;
            }

            QRectF combinedGeom = computeCombinedZoneGeometry(zonesToSnap, screen, layout, screenId);

            // Update multi-zone state from expanded zones (what we actually snap to)
            QVector<QUuid> zoneIds;
            zoneIds.reserve(zonesToSnap.size());
            for (auto* zone : zonesToSnap) {
                zoneIds.append(zone->id());
            }

            m_currentZoneId = zonesToSnap.first()->id().toString();
            m_currentAdjacentZoneIds = zoneIds;
            m_isMultiZoneMode = (zonesToSnap.size() > 1);
            m_currentMultiZoneGeometry = GeometryUtils::snapToRect(combinedGeom);
            if (zonesToSnap.size() == 1) {
                m_currentZoneGeometry = GeometryUtils::snapToRect(combinedGeom);
            }

            // Highlight expanded zones (raycasted) so user sees what they are actually snapping to
            m_zoneDetector->highlightZones(zonesToSnap);
            m_overlayService->highlightZones(zoneIdsToStringList(zoneIds));
        }
    }
}

void WindowDragAdaptor::handleMultiZoneModifier(int x, int y)
{
    QScreen* screen = nullptr;
    QString screenId;
    auto* layout = prepareHandlerContext(x, y, screen, screenId);
    if (!layout) {
        return;
    }

    m_zoneDetector->setLayout(layout);

    // Convert cursor position to screen-relative coordinates for detection
    QPointF cursorPos(static_cast<qreal>(x), static_cast<qreal>(y));

    // Call detectMultiZone instead of detectZone
    ZoneDetectionResult result = m_zoneDetector->detectMultiZone(cursorPos);

    if (result.isMultiZone && result.primaryZone) {
        // Multi-zone detected
        QString primaryZoneId = result.primaryZone->id().toString();
        QVector<QUuid> newAdjacentZoneIds;

        // Collect zone IDs from adjacent zones
        newAdjacentZoneIds.append(result.primaryZone->id());
        for (Zone* zone : result.adjacentZones) {
            if (zone && zone->id() != result.primaryZone->id()) {
                newAdjacentZoneIds.append(zone->id());
            }
        }

        // Only update if zone selection changed
        if (primaryZoneId != m_currentZoneId || newAdjacentZoneIds != m_currentAdjacentZoneIds) {
            m_currentZoneId = primaryZoneId;
            m_currentAdjacentZoneIds = newAdjacentZoneIds;
            m_isMultiZoneMode = true;

            // Build de-duplicated zone list for geometry and highlighting
            QVector<Zone*> zonesToHighlight;
            zonesToHighlight.append(result.primaryZone);
            for (Zone* zone : result.adjacentZones) {
                if (zone && zone != result.primaryZone) {
                    zonesToHighlight.append(zone);
                }
            }

            m_currentMultiZoneGeometry =
                GeometryUtils::snapToRect(computeCombinedZoneGeometry(zonesToHighlight, screen, layout, screenId));
            m_zoneDetector->highlightZones(zonesToHighlight);
            m_overlayService->highlightZones(zoneIdsToStringList(m_currentAdjacentZoneIds));
        }
    } else if (result.primaryZone) {
        // Single zone detected (fallback from multi-zone detection)
        QString zoneId = result.primaryZone->id().toString();
        if (zoneId != m_currentZoneId || m_isMultiZoneMode) {
            m_currentZoneId = zoneId;
            m_currentAdjacentZoneIds.clear();
            m_isMultiZoneMode = false;
            m_zoneDetector->highlightZone(result.primaryZone);
            m_overlayService->highlightZone(zoneId);

            m_currentZoneGeometry =
                GeometryUtils::getZoneGeometryForScreen(result.primaryZone, screen, screenId, layout, m_settings);
            m_currentMultiZoneGeometry = QRect();
        }
    } else {
        // No zone detected
        if (!m_currentZoneId.isEmpty() || m_isMultiZoneMode) {
            m_currentZoneId.clear();
            m_currentAdjacentZoneIds.clear();
            m_isMultiZoneMode = false;
            m_currentZoneGeometry = QRect();
            m_currentMultiZoneGeometry = QRect();
            m_zoneDetector->clearHighlights();
            m_overlayService->clearHighlight();
        }
    }
}

void WindowDragAdaptor::dragMoved(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons)
{
    if (windowId != m_draggedWindowId) {
        return;
    }

    // Parse modifiers early — needed for both retrigger check and normal processing.
    // KWin Effect provides modifiers via the mouseChanged signal.
    Qt::KeyboardModifiers mods;
    if (modifiers != 0) {
        mods = static_cast<Qt::KeyboardModifiers>(modifiers);
    } else {
        // Fallback: try Qt query (may not work on Wayland without focus)
        mods = QGuiApplication::queryKeyboardModifiers();
    }

    // Use pre-parsed triggers (cached on dragStarted) to avoid QVariantMap unboxing per tick.
    const bool triggerHeld = anyTriggerHeld(m_cachedActivationTriggers, mods, mouseButtons);

    // ── Autotile drag-insert preview (runs even when m_snapCancelled) ───────
    // This block is intentionally ABOVE the snap-cancelled early return because
    // the KWin effect calls callCancelSnap() when the cursor crosses from a snap
    // screen to an autotile screen mid-drag. That sets m_snapCancelled=true, and
    // would otherwise starve this block for the entire remainder of the drag.
    // Autotile drag-insert lives on an independent trigger list, so it should
    // activate regardless of snap-overlay cancel state.
    if (m_autotileEngine) {
        const bool autotileInsertHeld = anyTriggerHeld(m_cachedAutotileDragInsertTriggers, mods, mouseButtons);
        const QString autotileScreenId = effectiveScreenIdAt(cursorX, cursorY);
        const bool onAutotileScreen =
            !autotileScreenId.isEmpty() && m_autotileEngine->isAutotileScreen(autotileScreenId);

        const bool previewActive = m_autotileEngine->hasDragInsertPreview();
        const QString previewScreenId = m_autotileEngine->dragInsertPreviewScreenId();

        if (autotileInsertHeld && onAutotileScreen) {
            // Cursor crossed between two autotile screens while the trigger is
            // held: cancel the old preview before starting a fresh one on the
            // new screen. Without this the old preview stays stuck on the
            // departed screen until the trigger is released.
            if (previewActive && previewScreenId != autotileScreenId) {
                m_autotileEngine->cancelDragInsertPreview();
            }
            if (!m_autotileEngine->hasDragInsertPreview()) {
                const bool began = m_autotileEngine->beginDragInsertPreview(windowId, autotileScreenId);
                qCDebug(lcDbusWindow) << "autotile drag-insert preview begin:" << windowId << "on" << autotileScreenId
                                      << "=>" << began;
            }
            if (m_autotileEngine->hasDragInsertPreview()
                && m_autotileEngine->dragInsertPreviewScreenId() == autotileScreenId) {
                const int targetIdx =
                    m_autotileEngine->computeDragInsertIndexAtPoint(autotileScreenId, QPoint(cursorX, cursorY));
                if (targetIdx >= 0) {
                    m_autotileEngine->updateDragInsertPreview(targetIdx);
                }
                m_lastCursorX = cursorX;
                m_lastCursorY = cursorY;
                return;
            }
        } else if (previewActive) {
            // Trigger released or cursor left the autotile screen mid-drag —
            // cancel the preview so neighbours snap back to their original order.
            m_autotileEngine->cancelDragInsertPreview();
        }
    }

    if (m_snapCancelled) {
        // Allow retriggering the overlay after Escape: the user must release the
        // activation trigger and then press it again (a full release→press cycle).
        if (!triggerHeld) {
            m_triggerReleasedAfterCancel = true;
        } else if (m_triggerReleasedAfterCancel) {
            // Trigger released and re-pressed: clear cancel, resume zone snapping
            m_snapCancelled = false;
            m_triggerReleasedAfterCancel = false;
            registerCancelOverlayShortcut();
            // Fall through to normal processing
        } else {
            return; // Trigger still held from before Escape — stay cancelled
        }

        if (m_snapCancelled) {
            return; // Trigger released but not yet re-pressed
        }
    }

    m_lastCursorX = cursorX;
    m_lastCursorY = cursorY;

    // Update mouse position for shader effects
    if (m_overlayService && m_overlayShown) {
        m_overlayService->updateMousePosition(cursorX, cursorY);
    }

    // Activation state: use the trigger check from above (already computed)
    const bool zoneActivationHeld = triggerHeld;

    // Toggle mode: detect rising edge (release→press) to flip overlay state
    bool activationActive;
    if (m_settings->toggleActivation()) {
        if (zoneActivationHeld && !m_prevTriggerHeld) {
            m_activationToggled = !m_activationToggled;
        }
        m_prevTriggerHeld = zoneActivationHeld;
        activationActive = m_activationToggled;
    } else {
        activationActive = zoneActivationHeld;
    }

    // Log activation-state transitions so overlay show/hide churn can be traced.
    if (activationActive != m_lastLoggedActivationActive) {
        qCInfo(lcDbusWindow) << "dragMoved activationActive" << m_lastLoggedActivationActive << "->" << activationActive
                             << "mods=" << static_cast<int>(mods) << "buttons=" << mouseButtons
                             << "triggerHeld=" << triggerHeld << "toggleActivation=" << m_settings->toggleActivation();
        m_lastLoggedActivationActive = activationActive;
    }

    // Check all configured zone span triggers (multi-bind support)
    const bool zoneSpanModifierHeld = anyTriggerHeld(m_cachedZoneSpanTriggers, mods, mouseButtons);

    // Conflict detection: warn once per drag when activation and zone span share a trigger
    if (!m_modifierConflictWarned) {
        m_modifierConflictWarned = true;
        for (const auto& at : m_cachedActivationTriggers) {
            if (at.modifier == 0 && at.mouseButton == 0)
                continue;
            for (const auto& st : m_cachedZoneSpanTriggers) {
                if ((at.modifier != 0 && st.modifier == at.modifier)
                    || (at.mouseButton != 0 && st.mouseButton == at.mouseButton)) {
                    qCWarning(lcDbusWindow) << "Trigger overlap: activation and zone span share trigger"
                                            << "(mod:" << at.modifier << "btn:" << at.mouseButton << ");"
                                            << "zone span takes priority when both match";
                }
            }
        }
    }

    // Mutual exclusion: overlay (modifier-triggered) and zone selector (edge-triggered)
    // cannot be active simultaneously. Modifier takes priority as an explicit user action.
    // Priority: zone span > proximity snap (always active) > none
    if (activationActive) {
        // Modifier held: overlay takes priority — dismiss zone selector if open
        if (m_zoneSelectorShown) {
            m_zoneSelectorShown = false;
            m_overlayService->hideZoneSelector();
            m_overlayService->clearSelectedZone();
        }

        if (zoneSpanModifierHeld && m_settings->zoneSpanEnabled()) {
            handleZoneSpanModifier(cursorX, cursorY);
        } else {
            // Transitioning away from zone span: clear painted zones
            if (!m_paintedZoneIds.isEmpty()) {
                m_paintedZoneIds.clear();
            }
            handleMultiZoneModifier(cursorX, cursorY);
        }
    } else {
        // No modifier: hide overlay, clear painted zones, allow zone selector
        if (!m_paintedZoneIds.isEmpty()) {
            m_paintedZoneIds.clear();
        }
        hideOverlayAndClearZoneState();
        checkZoneSelectorTrigger(cursorX, cursorY);
    }

    // Emit zone geometry during drag (effect applies only on release; overlay uses for highlight)
    // Only emit when geometry actually changes
    QRect geom = m_isMultiZoneMode ? m_currentMultiZoneGeometry : m_currentZoneGeometry;
    if (geom.isValid()) {
        if (geom != m_lastEmittedZoneGeometry) {
            m_lastEmittedZoneGeometry = geom;
            m_restoreSizeEmittedDuringDrag = false;
            Q_EMIT zoneGeometryDuringDragChanged(windowId, geom.x(), geom.y(), geom.width(), geom.height());
        }
    } else {
        // Cursor left all zones: restore pre-snap size immediately if window was snapped
        if (m_wasSnapped && !m_restoreSizeEmittedDuringDrag && m_settings && m_settings->restoreOriginalSizeOnUnsnap()
            && m_windowTracking) {
            int origX, origY, origW, origH;
            if (m_windowTracking->getValidatedPreTileGeometry(windowId, origX, origY, origW, origH)) {
                m_restoreSizeEmittedDuringDrag = true;
                m_lastEmittedZoneGeometry = QRect(); // Reset so re-entering zone will emit
                Q_EMIT restoreSizeDuringDragChanged(windowId, origW, origH);
            }
        }
    }
}

void WindowDragAdaptor::selectorScrollWheel(int angleDeltaY)
{
    if (m_overlayService) {
        m_overlayService->scrollZoneSelector(angleDeltaY);
    }
}

} // namespace PlasmaZones
