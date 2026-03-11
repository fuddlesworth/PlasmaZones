// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowdragadaptor.h"
#include "../windowtrackingadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/zone.h"
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../autotile/AutotileEngine.h"
#include <QScreen>

namespace PlasmaZones {

void WindowDragAdaptor::dragStopped(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons,
                                    int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldApplyGeometry,
                                    QString& releaseScreenNameOut, bool& restoreSizeOnlyOut,
                                    bool& snapAssistRequestedOut, QString& emptyZonesJsonOut)
{
    // Initialize output parameters
    // shouldApplyGeometry: true = KWin should set window to (snapX, snapY, snapWidth, snapHeight)
    // restoreSizeOnly: when true with shouldApplyGeometry, effect uses current position + returned size
    // (drag-to-unsnap)
    snapX = 0;
    snapY = 0;
    snapWidth = 0;
    snapHeight = 0;
    shouldApplyGeometry = false;
    releaseScreenNameOut.clear();
    restoreSizeOnlyOut = false;
    snapAssistRequestedOut = false;
    emptyZonesJsonOut.clear();

    if (windowId != m_draggedWindowId) {
        return;
    }

    // Release screen: use cursor position passed from effect (at release time), not last dragMoved
    QScreen* releaseScreen = screenAtPoint(cursorX, cursorY);
    QString releaseScreenName = releaseScreen ? releaseScreen->name() : QString();
    QString releaseScreenId = releaseScreen ? Utils::screenIdentifier(releaseScreen) : QString();
    releaseScreenNameOut = releaseScreenName;
    qCDebug(lcDbusWindow) << "dragStopped: cursor=" << cursorX << "," << cursorY
                          << "releaseScreen=" << releaseScreenName;

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
    const int capturedLastCursorX = m_lastCursorX;
    const int capturedLastCursorY = m_lastCursorY;

    // Release on a disabled monitor: do not snap to overlay zone (avoids snapping to a zone on another screen)
    bool useOverlayZone = true;
    if (releaseScreen && m_settings && m_settings->isMonitorDisabled(releaseScreenId)) {
        useOverlayZone = false;
    }

    // Release on an autotile screen: do not snap to manual overlay zone.
    // The autotile engine manages window placement on these screens; allowing a
    // manual drag-snap would conflict with the engine's layout.
    if (useOverlayZone && releaseScreen && m_autotileEngine && m_autotileEngine->isAutotileScreen(releaseScreenName)) {
        useOverlayZone = false;
    }

    // Check if a zone was selected via the zone selector (takes priority)
    bool usedZoneSelector = false;
    if (!capturedSnapCancelled && capturedZoneSelectorShown && m_overlayService
        && m_overlayService->hasSelectedZone()) {
        QString selectedLayoutId = m_overlayService->selectedLayoutId();
        QScreen* screen = screenAtPoint(capturedLastCursorX, capturedLastCursorY);

        if (screen && (!m_settings || !m_settings->isMonitorDisabled(Utils::screenIdentifier(screen)))) {
            QRect zoneGeom = m_overlayService->getSelectedZoneGeometry(screen);
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
                    Layout* selectedLayout = nullptr;
                    if (layoutUuidOpt) {
                        QUuid layoutUuid = *layoutUuidOpt;
                        selectedLayout = m_layoutManager->layoutById(layoutUuid);
                        if (selectedLayout && selectedZoneIndex >= 0
                            && selectedZoneIndex < selectedLayout->zones().size()) {
                            Zone* zone = selectedLayout->zones().at(selectedZoneIndex);
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
                    m_windowTracking->windowSnapped(windowId, zoneUuid, releaseScreenName);
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
                        Layout* currentLayout =
                            m_layoutManager->resolveLayoutForScreen(Utils::screenIdentifier(screen));
                        if (currentLayout != selectedLayout) {
                            // Hide overlay/selector BEFORE the layout change so signal
                            // handlers (updateZoneSelectorWindow, updateOverlayWindow) find
                            // hidden windows and skip heavy QML property updates / LayerShell
                            // recalculations. All overlay queries are already done above.
                            hideOverlayAndSelector();
                            m_layoutManager->assignLayout(Utils::screenIdentifier(screen),
                                                          m_layoutManager->currentVirtualDesktop(),
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
                m_windowTracking->windowSnappedMultiZone(windowId, allZoneIds, releaseScreenName);
                // Record user-initiated snap (not auto-snap)
                m_windowTracking->recordSnapIntent(windowId, true);
            }
        } else if (capturedZoneGeometry.isValid()) {
            snapX = capturedZoneGeometry.x();
            snapY = capturedZoneGeometry.y();
            snapWidth = capturedZoneGeometry.width();
            snapHeight = capturedZoneGeometry.height();
            shouldApplyGeometry = true;
            tryStorePreSnapGeometry(windowId, capturedWasSnapped, capturedOriginalGeometry);
            if (m_windowTracking) {
                m_windowTracking->windowSnapped(windowId, capturedZoneId, releaseScreenName);
                // Record user-initiated snap (not auto-snap)
                m_windowTracking->recordSnapIntent(windowId, true);
            }
        }
    }

    // Handle unsnap - window was snapped but dropped outside any zone
    // Use same state as float shortcut: save zone for restore and mark floating, so unfloat/snap-back works.
    if (!shouldApplyGeometry && capturedWasSnapped) {
        if (m_windowTracking && !m_windowTracking->getZoneForWindow(windowId).isEmpty()) {
            m_windowTracking->windowUnsnappedForFloat(windowId);
            m_windowTracking->setWindowFloating(windowId, true);
        }

        // On drag-to-unsnap: apply only pre-snap width/height; window keeps drop position.
        // Float-toggle shortcut uses calculateUnfloatRestore and restores full x/y/w/h.
        if (m_settings && m_settings->restoreOriginalSizeOnUnsnap()) {
            int origX, origY, origW, origH;
            if (m_windowTracking
                && m_windowTracking->getValidatedPreTileGeometry(windowId, origX, origY, origW, origH)) {
                snapWidth = origW;
                snapHeight = origH;
                shouldApplyGeometry = true;
                restoreSizeOnlyOut = true;
            }
        }

        // Clear pre-tile geometry to prevent memory accumulation
        if (m_windowTracking) {
            m_windowTracking->clearPreTileGeometry(windowId);
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
        Layout* layout = m_layoutManager->resolveLayoutForScreen(releaseScreenId);
        if (layout) {
            QString emptyJson =
                GeometryUtils::buildEmptyZonesJson(layout, releaseScreen, m_settings, [this](const Zone* z) {
                    return m_windowTracking->getWindowsInZone(z->id().toString()).isEmpty();
                });
            if (!emptyJson.isEmpty() && emptyJson != QLatin1String("[]")) {
                snapAssistRequestedOut = true;
                emptyZonesJsonOut = emptyJson;
            }
        }
    }

    // Reset drag state for next operation.
    // If snap assist will be shown, keep the Escape shortcut registered so
    // KGlobalAccel can still dismiss it (the snap assist window may not have
    // Wayland keyboard focus yet when the user presses Escape).
    resetDragState(/*keepEscapeShortcut=*/snapAssistRequestedOut);
}

} // namespace PlasmaZones
