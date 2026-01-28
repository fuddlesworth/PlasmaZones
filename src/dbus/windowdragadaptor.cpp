// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowdragadaptor.h"
#include "windowtrackingadaptor.h"
#include "../core/interfaces.h"
#include "../core/layoutmanager.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <QGuiApplication>
#include <QScreen>
#include <cmath>

namespace PlasmaZones {

WindowDragAdaptor::WindowDragAdaptor(IOverlayService* overlay, IZoneDetector* detector, LayoutManager* layoutManager,
                                     ISettings* settings, WindowTrackingAdaptor* windowTracking, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_overlayService(overlay)
    , m_zoneDetector(detector)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
    , m_windowTracking(windowTracking)
{
    // Debug-only assertions for development
    Q_ASSERT(overlay);
    Q_ASSERT(detector);
    Q_ASSERT(layoutManager);
    Q_ASSERT(settings);
    Q_ASSERT(windowTracking);

    // Runtime null checks for release builds - log warning but don't crash
    if (!overlay || !detector || !layoutManager || !settings || !windowTracking) {
        qCCritical(lcDbusWindow) << "One or more required dependencies are null!"
                                 << "overlay:" << (overlay != nullptr) << "detector:" << (detector != nullptr)
                                 << "layoutManager:" << (layoutManager != nullptr)
                                 << "settings:" << (settings != nullptr)
                                 << "windowTracking:" << (windowTracking != nullptr);
    }

    // Connect to layout change signals to invalidate cached zone geometry mid-drag
    // Uses LayoutManager (concrete) because ILayoutManager is a pure interface without signals
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &WindowDragAdaptor::onLayoutChanged);
    connect(m_layoutManager, &LayoutManager::layoutAssigned, this, &WindowDragAdaptor::onLayoutChanged);
}

QScreen* WindowDragAdaptor::screenAtPoint(int x, int y) const
{
    return Utils::findScreenAtPosition(x, y);
}

bool WindowDragAdaptor::checkModifier(int modifierSetting, Qt::KeyboardModifiers mods) const
{
    bool shiftHeld = mods.testFlag(Qt::ShiftModifier);
    bool ctrlHeld = mods.testFlag(Qt::ControlModifier);
    bool altHeld = mods.testFlag(Qt::AltModifier);
    bool metaHeld = mods.testFlag(Qt::MetaModifier);

    switch (modifierSetting) {
    case 0:
        return false; // Disabled - overlay never shows
    case 1:
        return shiftHeld;
    case 2:
        return ctrlHeld;
    case 3:
        return altHeld;
    case 4:
        return metaHeld;
    case 5:
        return ctrlHeld && altHeld; // Ctrl+Alt
    case 6:
        return ctrlHeld && shiftHeld; // Ctrl+Shift
    case 7:
        return altHeld && shiftHeld; // Alt+Shift
    case 8:
        return true; // AlwaysActive - overlay always shows on drag
                     // Use this on Wayland if modifier detection doesn't work
    case 9:
        return altHeld && metaHeld; // Alt+Meta
    case 10:
        return ctrlHeld && altHeld && metaHeld; // Ctrl+Alt+Meta
    default:
        return false;
    }
}

void WindowDragAdaptor::dragStarted(const QString& windowId, double x, double y, double width, double height,
                                    const QString& appName, const QString& windowClass)
{
    // Check exclusion list - if window is excluded, don't allow snapping
    if (m_settings->isWindowExcluded(appName, windowClass)) {
        qCDebug(lcDbusWindow) << "Window excluded from snapping - appName:" << appName << "windowClass:" << windowClass;
        m_snapCancelled = true;
        m_draggedWindowId.clear();
        return;
    }

    m_draggedWindowId = windowId;
    m_originalGeometry = QRect(qRound(x), qRound(y), qRound(width), qRound(height));
    m_currentZoneId.clear();
    m_currentZoneGeometry = QRect();
    m_currentAdjacentZoneIds.clear();
    m_isMultiZoneMode = false;
    m_currentMultiZoneGeometry = QRect();
    m_snapCancelled = false;
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
            auto* layout = m_layoutManager->layoutForScreen(screen->name());
            if (!layout) {
                layout = m_layoutManager->activeLayout();
            }
            if (layout) {
                layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(screen));
                int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
                int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);

                for (auto* zone : layout->zones()) {
                    QRectF zoneGeom = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGap, true);
                    QRect zoneRect = zoneGeom.toRect();

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

void WindowDragAdaptor::hideOverlayAndClearZoneState()
{
    if (m_overlayShown && m_overlayService) {
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
}

void WindowDragAdaptor::handleMultiZoneModifier(int x, int y, Qt::KeyboardModifiers mods)
{
    Q_UNUSED(mods);

    QScreen* screen = screenAtPoint(x, y);
    if (!screen || (m_settings && m_settings->isMonitorDisabled(screen->name()))) {
        return;
    }

    // Show overlay if not visible
    if (!m_overlayShown) {
        m_overlayService->showAtPosition(x, y);
        m_overlayShown = true;
    }

    // Get layout assigned to this specific screen (supports different layouts per monitor)
    auto* layout = m_layoutManager->layoutForScreen(screen->name());
    if (!layout) {
        layout = m_layoutManager->activeLayout();
    }
    if (!layout) {
        return;
    }

    // Recalculate zone geometries for the current screen before detection.
    layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(screen));

    m_zoneDetector->setLayout(layout);

    // Convert cursor position to screen-relative coordinates for detection
    QPointF cursorPos(static_cast<qreal>(x), static_cast<qreal>(y));

    // Ensure multi-zone detection is enabled
    if (!m_zoneDetector->multiZoneEnabled()) {
        m_zoneDetector->setMultiZoneEnabled(true);
    }

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

            // Calculate combined geometry with gaps
            int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
            int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
            QRectF combinedGeom =
                GeometryUtils::getZoneGeometryWithGaps(result.primaryZone, screen, zonePadding, outerGap, true);
            for (Zone* zone : result.adjacentZones) {
                if (zone && zone != result.primaryZone) {
                    QRectF zoneGeom = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGap, true);
                    combinedGeom = combinedGeom.united(zoneGeom);
                }
            }
            m_currentMultiZoneGeometry = combinedGeom.toRect();

            // Highlight all zones in multi-zone selection
            QVector<Zone*> zonesToHighlight;
            zonesToHighlight.append(result.primaryZone);
            for (Zone* zone : result.adjacentZones) {
                if (zone && zone != result.primaryZone) {
                    zonesToHighlight.append(zone);
                }
            }
            m_zoneDetector->highlightZones(zonesToHighlight);

            // Update overlay to show multi-zone selection
            QStringList zoneIds;
            for (const QUuid& id : m_currentAdjacentZoneIds) {
                zoneIds.append(id.toString());
            }
            m_overlayService->highlightZones(zoneIds);
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

            int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
            int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
            QRectF geom =
                GeometryUtils::getZoneGeometryWithGaps(result.primaryZone, screen, zonePadding, outerGap, true);
            m_currentZoneGeometry = geom.toRect();
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

void WindowDragAdaptor::handleSingleZoneModifier(int x, int y)
{
    QScreen* screen = screenAtPoint(x, y);
    if (!screen || (m_settings && m_settings->isMonitorDisabled(screen->name()))) {
        return;
    }

    // Show overlay if not visible
    if (!m_overlayShown) {
        m_overlayService->showAtPosition(x, y);
        m_overlayShown = true;
    }

    // Get layout assigned to this specific screen
    auto* layout = m_layoutManager->layoutForScreen(screen->name());
    if (!layout) {
        layout = m_layoutManager->activeLayout();
    }
    if (!layout) {
        return;
    }

    // Recalculate zone geometries for the current screen before detection.
    layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(screen));

    m_zoneDetector->setLayout(layout);

    // Convert cursor position to relative coordinates within available area
    QRectF availableGeom = ScreenManager::actualAvailableGeometry(screen);

    // Guard against zero-size geometry (disconnected or degenerate screen)
    if (availableGeom.width() <= 0 || availableGeom.height() <= 0) {
        return;
    }

    qreal relX = static_cast<qreal>(x - availableGeom.x()) / availableGeom.width();
    qreal relY = static_cast<qreal>(y - availableGeom.y()) / availableGeom.height();

    // Find zone at cursor position
    Zone* foundZone = nullptr;
    for (auto* zone : layout->zones()) {
        QRectF relGeom = zone->relativeGeometry();
        if (relGeom.contains(QPointF(relX, relY))) {
            foundZone = zone;
            break;
        }
    }

    if (foundZone) {
        QString zoneId = foundZone->id().toString();
        if (zoneId != m_currentZoneId) {
            m_currentZoneId = zoneId;
            m_zoneDetector->highlightZone(foundZone);
            m_overlayService->highlightZone(zoneId);

            int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
            int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
            QRectF geom = GeometryUtils::getZoneGeometryWithGaps(foundZone, screen, zonePadding, outerGap, true);
            m_currentZoneGeometry = geom.toRect();
        }
    } else {
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

void WindowDragAdaptor::dragMoved(const QString& windowId, int cursorX, int cursorY, int modifiers)
{
    if (windowId != m_draggedWindowId || m_snapCancelled) {
        return;
    }

    m_lastCursorX = cursorX;
    m_lastCursorY = cursorY;

    // Update mouse position for shader effects
    if (m_overlayService && m_overlayShown) {
        m_overlayService->updateMousePosition(cursorX, cursorY);
    }

    // Check if we should show/hide the zone selector based on cursor proximity to edge
    checkZoneSelectorTrigger(cursorX, cursorY);

    // KWin Effect provides modifiers via the mouseChanged signal.
    Qt::KeyboardModifiers mods;
    if (modifiers != 0) {
        mods = static_cast<Qt::KeyboardModifiers>(modifiers);
    } else {
        // Fallback: try Qt query (may not work on Wayland without focus)
        mods = QGuiApplication::queryKeyboardModifiers();
    }

    // Get modifier settings
    int dragActivationMod = static_cast<int>(m_settings->dragActivationModifier());
    int multiZoneMod = static_cast<int>(m_settings->multiZoneModifier());
    bool zoneActivationHeld = checkModifier(dragActivationMod, mods);
    bool multiZoneModifierHeld = checkModifier(multiZoneMod, mods);

    // Multi-zone modifier takes precedence (Ctrl+Alt by default)
    if (multiZoneModifierHeld) {
        handleMultiZoneModifier(cursorX, cursorY, mods);
    } else if (zoneActivationHeld) {
        // Single-zone activation (Alt by default)
        handleSingleZoneModifier(cursorX, cursorY);
    } else {
        // No modifier held - hide overlay
        hideOverlayAndClearZoneState();
    }
}

void WindowDragAdaptor::dragStopped(const QString& windowId, int& snapX, int& snapY, int& snapWidth, int& snapHeight,
                                    bool& shouldApplyGeometry)
{
    // Initialize output parameters
    // shouldApplyGeometry: true = KWin should set window to (snapX, snapY, snapWidth, snapHeight)
    // Used for both zone snapping and geometry restoration on unsnap
    snapX = 0;
    snapY = 0;
    snapWidth = 0;
    snapHeight = 0;
    shouldApplyGeometry = false;

    if (windowId != m_draggedWindowId) {
        return;
    }

    // Capture zone state into locals right away. If another window starts dragging before
    // the async D-Bus reply for this dragStopped() is processed, dragMoved() would overwrite
    // m_currentZoneId; capturing here ensures this window snaps to the correct zone.
    const QString capturedZoneId = m_currentZoneId;
    const QRect capturedZoneGeometry = m_currentZoneGeometry;
    const bool capturedIsMultiZoneMode = m_isMultiZoneMode;
    const QRect capturedMultiZoneGeometry = m_currentMultiZoneGeometry;
    const bool capturedWasSnapped = m_wasSnapped;
    const QRect capturedOriginalGeometry = m_originalGeometry;
    const bool capturedSnapCancelled = m_snapCancelled;
    const bool capturedZoneSelectorShown = m_zoneSelectorShown;
    const int capturedLastCursorX = m_lastCursorX;
    const int capturedLastCursorY = m_lastCursorY;

    // Release on a disabled monitor: do not snap to overlay zone (avoids snapping to a zone on another screen)
    bool useOverlayZone = true;
    {
        QScreen* releaseScreen = screenAtPoint(capturedLastCursorX, capturedLastCursorY);
        if (releaseScreen && m_settings && m_settings->isMonitorDisabled(releaseScreen->name())) {
            useOverlayZone = false;
        }
    }

    // Check if a zone was selected via the zone selector (takes priority)
    bool usedZoneSelector = false;
    if (!capturedSnapCancelled && capturedZoneSelectorShown && m_overlayService
        && m_overlayService->hasSelectedZone()) {
        QScreen* screen = screenAtPoint(capturedLastCursorX, capturedLastCursorY);
        if (screen && (!m_settings || !m_settings->isMonitorDisabled(screen->name()))) {
            QRect zoneGeom = m_overlayService->getSelectedZoneGeometry(screen);
            if (zoneGeom.isValid()) {
                snapX = zoneGeom.x();
                snapY = zoneGeom.y();
                snapWidth = zoneGeom.width();
                snapHeight = zoneGeom.height();
                shouldApplyGeometry = true;
                usedZoneSelector = true;

                tryStorePreSnapGeometry(windowId, capturedWasSnapped, capturedOriginalGeometry);

                QString selectedLayoutId = m_overlayService->selectedLayoutId();
                int selectedZoneIndex = m_overlayService->selectedZoneIndex();
                if (m_windowTracking && m_layoutManager) {
                    // Get the actual zone UUID from layout and zone index so navigation works
                    auto layoutUuidOpt = Utils::parseUuid(selectedLayoutId);
                    QString zoneUuid;
                    if (layoutUuidOpt) {
                        QUuid layoutUuid = *layoutUuidOpt;
                        Layout* layout = m_layoutManager->layoutById(layoutUuid);
                        if (layout && selectedZoneIndex >= 0 && selectedZoneIndex < layout->zones().size()) {
                            Zone* zone = layout->zones().at(selectedZoneIndex);
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
                    m_windowTracking->windowSnapped(windowId, zoneUuid);
                    // BUG FIX: Record that this window class was USER-snapped (not auto-snapped)
                    // This prevents auto-snapping windows that were never manually snapped by user
                    m_windowTracking->recordSnapIntent(windowId, true);
                }
            }
        }
    }

    // Hide overlay and zone selector UI
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
                m_windowTracking->windowSnapped(windowId, capturedZoneId);
                // BUG FIX: Record that this window class was USER-snapped (not auto-snapped)
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
                m_windowTracking->windowSnapped(windowId, capturedZoneId);
                // BUG FIX: Record that this window class was USER-snapped (not auto-snapped)
                m_windowTracking->recordSnapIntent(windowId, true);
            }
        }
    }

    // Handle unsnap - window was snapped but dropped outside any zone
    // Use captured wasSnapped value for consistency
    if (!shouldApplyGeometry && capturedWasSnapped) {
        // Only call windowUnsnapped if we actually track this window. capturedWasSnapped
        // can be true from geometry fallback (e.g. daemon restarted) when we don't have
        // it in m_windowZoneAssignments; calling windowUnsnapped would log a false
        // "Window not found for unsnap" warning.
        if (m_windowTracking && !m_windowTracking->getZoneForWindow(windowId).isEmpty()) {
            m_windowTracking->windowUnsnapped(windowId);
        }

        // Restore original geometry if enabled
        if (m_settings && m_settings->restoreOriginalSizeOnUnsnap()) {
            int origX, origY, origW, origH;
            if (m_windowTracking
                && m_windowTracking->getValidatedPreSnapGeometry(windowId, origX, origY, origW, origH)) {
                snapX = origX;
                snapY = origY;
                snapWidth = origW;
                snapHeight = origH;
                shouldApplyGeometry = true;
            }
        }

        // Clear pre-snap geometry to prevent memory accumulation
        if (m_windowTracking) {
            m_windowTracking->clearPreSnapGeometry(windowId);
        }
    }

    // Reset drag state for next operation
    resetDragState();
}

void WindowDragAdaptor::cancelSnap()
{
    m_snapCancelled = true;
    m_currentZoneId.clear();
    m_currentZoneGeometry = QRect();
    m_currentAdjacentZoneIds.clear();
    m_isMultiZoneMode = false;
    m_currentMultiZoneGeometry = QRect();

    // Hide overlay and zone selector UI
    hideOverlayAndSelector();
}

void WindowDragAdaptor::handleWindowClosed(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    // If this window was being dragged, clean up drag state
    if (windowId == m_draggedWindowId) {
        hideOverlayAndClearZoneState();

        // Hide zone selector if shown
        if (m_zoneSelectorShown && m_overlayService) {
            m_zoneSelectorShown = false;
            m_overlayService->hideZoneSelector();
            m_overlayService->clearSelectedZone();
        }

        // Reset all drag state
        m_draggedWindowId.clear();
        m_originalGeometry = QRect();
        m_snapCancelled = false;
        m_wasSnapped = false;
    }

    // Delegate tracking cleanup to WindowTrackingAdaptor
    if (m_windowTracking) {
        m_windowTracking->windowClosed(windowId);
    }
}

void WindowDragAdaptor::checkZoneSelectorTrigger(int cursorX, int cursorY)
{
    // Check if zone selector feature is enabled
    if (!m_settings || !m_settings->zoneSelectorEnabled()) {
        return;
    }

    QScreen* screen = screenAtPoint(cursorX, cursorY);
    if (screen && m_settings->isMonitorDisabled(screen->name())) {
        if (m_zoneSelectorShown) {
            m_zoneSelectorShown = false;
            m_overlayService->hideZoneSelector();
        }
        return;
    }

    bool nearEdge = isNearTriggerEdge(cursorX, cursorY);

    if (nearEdge && !m_zoneSelectorShown) {
        // Show zone selector when cursor approaches trigger edge
        m_zoneSelectorShown = true;
        // Call directly - QDBusAbstractAdaptor signals don't work for internal Qt connections
        m_overlayService->showZoneSelector();
    } else if (!nearEdge && m_zoneSelectorShown) {
        // Hide zone selector when cursor moves away from edge
        m_zoneSelectorShown = false;
        m_overlayService->hideZoneSelector();
    }

    // Update selector position for hover effects
    if (m_zoneSelectorShown) {
        m_overlayService->updateSelectorPosition(cursorX, cursorY);
    }
}

bool WindowDragAdaptor::isNearTriggerEdge(int cursorX, int cursorY) const
{
    if (!m_settings) {
        return false;
    }

    int triggerDistance = m_settings->zoneSelectorTriggerDistance();
    ZoneSelectorPosition position = m_settings->zoneSelectorPosition();

    QScreen* screen = screenAtPoint(cursorX, cursorY);
    if (!screen) {
        return false;
    }

    QRect screenGeom = screen->geometry();

    // Calculate selector thickness based on settings/layouts (matches overlay sizing)
    const int layoutCount = m_layoutManager ? m_layoutManager->layouts().size() : 0;
    const int indicatorWidth = m_settings->zoneSelectorPreviewWidth();
    int indicatorHeight = m_settings->zoneSelectorPreviewHeight();
    if (m_settings->zoneSelectorPreviewLockAspect()) {
        qreal aspectRatio =
            screenGeom.height() > 0 ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
        indicatorHeight = qRound(static_cast<qreal>(indicatorWidth) / aspectRatio);
    }

    constexpr int indicatorSpacing = 18;
    constexpr int containerPadding = 18; // per-side padding
    constexpr int containerTopMargin = 10;
    constexpr int labelSpace = 28;

    const int safeLayoutCount = std::max(1, layoutCount);
    const ZoneSelectorLayoutMode mode = m_settings->zoneSelectorLayoutMode();
    int columns = 1;
    int rows = 1;
    if (mode == ZoneSelectorLayoutMode::Vertical) {
        columns = 1;
        rows = safeLayoutCount;
    } else if (mode == ZoneSelectorLayoutMode::Grid) {
        columns = std::max(1, m_settings->zoneSelectorGridColumns());
        rows = static_cast<int>(std::ceil(static_cast<qreal>(safeLayoutCount) / columns));
    } else {
        columns = safeLayoutCount;
        rows = 1;
    }

    int contentWidth = columns * indicatorWidth + (columns - 1) * indicatorSpacing;
    int contentHeight = rows * (indicatorHeight + labelSpace) + (rows - 1) * indicatorSpacing;
    int containerWidth = contentWidth + 2 * containerPadding;
    int containerHeight = contentHeight + 2 * containerPadding;
    int barHeight = containerTopMargin + containerHeight;
    int barWidth = containerWidth;

    int distanceFromTop = cursorY - screenGeom.top();
    int distanceFromBottom = screenGeom.bottom() - cursorY;
    int distanceFromLeft = cursorX - screenGeom.left();
    int distanceFromRight = screenGeom.right() - cursorX;

    int hKeepVisible = m_zoneSelectorShown ? barWidth : triggerDistance;
    int vKeepVisible = m_zoneSelectorShown ? barHeight : triggerDistance;

    bool nearTop = distanceFromTop >= 0 && distanceFromTop <= vKeepVisible;
    bool nearBottom = distanceFromBottom >= 0 && distanceFromBottom <= vKeepVisible;
    bool nearLeft = distanceFromLeft >= 0 && distanceFromLeft <= hKeepVisible;
    bool nearRight = distanceFromRight >= 0 && distanceFromRight <= hKeepVisible;

    switch (position) {
    case ZoneSelectorPosition::TopLeft:
        return nearTop && nearLeft;
    case ZoneSelectorPosition::Top:
        return nearTop;
    case ZoneSelectorPosition::TopRight:
        return nearTop && nearRight;
    case ZoneSelectorPosition::Left:
        return nearLeft;
    case ZoneSelectorPosition::Right:
        return nearRight;
    case ZoneSelectorPosition::BottomLeft:
        return nearBottom && nearLeft;
    case ZoneSelectorPosition::Bottom:
        return nearBottom;
    case ZoneSelectorPosition::BottomRight:
        return nearBottom && nearRight;
    }
    return false;
}

void WindowDragAdaptor::hideOverlayAndSelector()
{
    // Hide overlay
    if (m_overlayShown && m_overlayService) {
        m_overlayService->hide();
        m_overlayShown = false;
    }

    // Hide zone selector and clear selection
    if (m_zoneSelectorShown && m_overlayService) {
        m_zoneSelectorShown = false;
        m_overlayService->hideZoneSelector();
    }
    if (m_overlayService) {
        m_overlayService->clearSelectedZone();
        m_overlayService->clearHighlight();
    }

    if (m_zoneDetector) {
        m_zoneDetector->clearHighlights();
    }
}

void WindowDragAdaptor::resetDragState()
{
    m_draggedWindowId.clear();
    m_originalGeometry = QRect();
    m_currentZoneId.clear();
    m_currentZoneGeometry = QRect();
    m_currentAdjacentZoneIds.clear();
    m_isMultiZoneMode = false;
    m_currentMultiZoneGeometry = QRect();
    m_snapCancelled = false;
    m_wasSnapped = false;
}

void WindowDragAdaptor::tryStorePreSnapGeometry(const QString& windowId)
{
    // Delegate to overload using current member variables
    tryStorePreSnapGeometry(windowId, m_wasSnapped, m_originalGeometry);
}

void WindowDragAdaptor::tryStorePreSnapGeometry(const QString& windowId, bool wasSnapped, const QRect& originalGeometry)
{
    // Store pre-snap geometry on FIRST snap only (for restore on unsnap)
    // The storePreSnapGeometry method handles the "already stored" case internally,
    // so calling it multiple times for A→B snaps is safe and won't overwrite.
    //
    // This overload accepts captured values to prevent race conditions when called
    // from dragStopped() - another drag could modify m_wasSnapped/m_originalGeometry
    // between the time dragStopped() is invoked and when it actually executes.
    if (!wasSnapped && m_windowTracking && originalGeometry.isValid()) {
        m_windowTracking->storePreSnapGeometry(windowId, originalGeometry.x(), originalGeometry.y(),
                                               originalGeometry.width(), originalGeometry.height());
    }
}

void WindowDragAdaptor::onLayoutChanged()
{
    // Clear cached zone state when layout changes mid-drag to prevent stale geometry
    // This handles the case where user changes layout via hotkey/GUI while dragging
    // On next dragMoved(), fresh geometry will be calculated from the new layout
    if (!m_draggedWindowId.isEmpty()) {
        qCDebug(lcDbusWindow) << "Layout changed mid-drag, clearing cached zone state";
        m_currentZoneId.clear();
        m_currentZoneGeometry = QRect();
        m_currentMultiZoneGeometry = QRect();
        m_currentAdjacentZoneIds.clear();
        m_isMultiZoneMode = false;

        // Clear highlight state since zones are now invalid
        if (m_zoneDetector) {
            m_zoneDetector->clearHighlights();
        }
        if (m_overlayService) {
            m_overlayService->clearHighlight();
        }
    }
}

} // namespace PlasmaZones
