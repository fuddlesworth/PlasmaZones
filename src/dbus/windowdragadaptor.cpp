// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowdragadaptor.h"
#include <QAction>
#include <QGuiApplication>
#include <QKeySequence>
#include <QScreen>
#include <cmath>
#include <KGlobalAccel>
#include <KLocalizedString>
#include "windowtrackingadaptor.h"
#include "../core/interfaces.h"
#include "../core/layoutmanager.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/zoneselectorlayout.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../core/constants.h"

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
        qCWarning(lcDbusWindow) << "One or more required dependencies are null!"
                                 << "overlay:" << (overlay != nullptr) << "detector:" << (detector != nullptr)
                                 << "layoutManager:" << (layoutManager != nullptr)
                                 << "settings:" << (settings != nullptr)
                                 << "windowTracking:" << (windowTracking != nullptr);
    }

    // Connect to layout change signals to invalidate cached zone geometry mid-drag
    // Uses LayoutManager (concrete) because ILayoutManager is a pure interface without signals
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &WindowDragAdaptor::onLayoutChanged);
    connect(m_layoutManager, &LayoutManager::layoutAssigned, this, &WindowDragAdaptor::onLayoutChanged);

    // Escape shortcut to cancel overlay during drag (registered when drag starts, unregistered when drag ends)
    m_cancelOverlayAction = new QAction(i18n("Cancel zone overlay"), this);
    m_cancelOverlayAction->setObjectName(QStringLiteral("cancel_overlay_during_drag"));
    connect(m_cancelOverlayAction, &QAction::triggered, this, &WindowDragAdaptor::cancelSnap);
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
    case static_cast<int>(DragModifier::Disabled):
        return false;
    case static_cast<int>(DragModifier::Shift):
        return shiftHeld;
    case static_cast<int>(DragModifier::Ctrl):
        return ctrlHeld;
    case static_cast<int>(DragModifier::Alt):
        return altHeld;
    case static_cast<int>(DragModifier::Meta):
        return metaHeld;
    case static_cast<int>(DragModifier::CtrlAlt):
        return ctrlHeld && altHeld;
    case static_cast<int>(DragModifier::CtrlShift):
        return ctrlHeld && shiftHeld;
    case static_cast<int>(DragModifier::AltShift):
        return altHeld && shiftHeld;
    case static_cast<int>(DragModifier::AlwaysActive):
        return true;
    case static_cast<int>(DragModifier::AltMeta):
        return altHeld && metaHeld;
    case static_cast<int>(DragModifier::CtrlAltMeta):
        return ctrlHeld && altHeld && metaHeld;
    default:
        return false;
    }
}

void WindowDragAdaptor::dragStarted(const QString& windowId, double x, double y, double width, double height,
                                    const QString& appName, const QString& windowClass, int mouseButtons)
{
    Q_UNUSED(mouseButtons); // Only used in dragMoved for dynamic activation
    // Check exclusion list - if window is excluded, don't allow snapping
    if (m_settings->isWindowExcluded(appName, windowClass)) {
        qCInfo(lcDbusWindow) << "Window excluded from snapping - appName:" << appName << "windowClass:" << windowClass;
        m_snapCancelled = true;
        m_draggedWindowId.clear();
        return;
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
    m_snapCancelled = false;
    m_mouseActivationLatched = false;
    m_overlayShown = false;
    m_overlayScreen = nullptr;
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
            auto* layout = m_layoutManager->resolveLayoutForScreen(screen->name());
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

QRectF WindowDragAdaptor::computeCombinedZoneGeometry(const QVector<Zone*>& zones, QScreen* screen, Layout* layout) const
{
    if (zones.isEmpty()) {
        return QRectF();
    }
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
    QRectF combined = GeometryUtils::getZoneGeometryWithGaps(zones.first(), screen, zonePadding, outerGap, true);
    for (int i = 1; i < zones.size(); ++i) {
        combined = combined.united(GeometryUtils::getZoneGeometryWithGaps(zones[i], screen, zonePadding, outerGap, true));
    }
    return combined;
}

QStringList WindowDragAdaptor::zoneIdsToStringList(const QVector<QUuid>& ids)
{
    QStringList result;
    result.reserve(ids.size());
    for (const QUuid& id : ids) {
        result.append(id.toString());
    }
    return result;
}

Layout* WindowDragAdaptor::prepareHandlerContext(int x, int y, QScreen*& outScreen)
{
    outScreen = screenAtPoint(x, y);
    if (!outScreen || (m_settings && m_settings->isMonitorDisabled(outScreen->name()))) {
        return nullptr;
    }

    if (!m_overlayShown) {
        m_overlayService->showAtPosition(x, y);
        m_overlayShown = true;
        m_overlayScreen = outScreen;
    } else if (m_settings && !m_settings->showZonesOnAllMonitors() && m_overlayScreen != outScreen) {
        // Cursor moved to different monitor - switch overlay to follow (fixes #136)
        m_overlayService->showAtPosition(x, y);
        m_overlayScreen = outScreen;
    }

    auto* layout = m_layoutManager->resolveLayoutForScreen(outScreen->name());
    if (!layout) {
        return nullptr;
    }

    layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(outScreen));
    return layout;
}

void WindowDragAdaptor::hideOverlayAndClearZoneState()
{
    if (m_overlayShown && m_overlayService) {
        m_overlayService->hide();
        m_overlayShown = false;
        m_overlayScreen = nullptr;
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
    auto* layout = prepareHandlerContext(x, y, screen);
    if (!layout) {
        return;
    }

    // Clear stale multi-zone state from proximity mode when transitioning to paint-to-span
    if (m_isMultiZoneMode && m_paintedZoneIds.isEmpty()) {
        m_currentAdjacentZoneIds.clear();
        m_isMultiZoneMode = false;
        m_currentMultiZoneGeometry = QRect();
    }

    // Convert cursor position to relative coordinates within available area
    QRectF availableGeom = ScreenManager::actualAvailableGeometry(screen);
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

    // Accumulate painted zones (never remove during a paint drag)
    if (foundZone) {
        m_paintedZoneIds.insert(foundZone->id());
    }

    // Build zone list and combined geometry from all painted zones
    if (!m_paintedZoneIds.isEmpty()) {
        QVector<Zone*> paintedZones;
        for (auto* zone : layout->zones()) {
            if (m_paintedZoneIds.contains(zone->id())) {
                paintedZones.append(zone);
            }
        }

        if (!paintedZones.isEmpty()) {
            QRectF combinedGeom = computeCombinedZoneGeometry(paintedZones, screen, layout);

            // Update multi-zone state
            QVector<QUuid> zoneIds;
            zoneIds.reserve(paintedZones.size());
            for (auto* zone : paintedZones) {
                zoneIds.append(zone->id());
            }

            m_currentZoneId = paintedZones.first()->id().toString();
            m_currentAdjacentZoneIds = zoneIds;
            m_isMultiZoneMode = (paintedZones.size() > 1);
            m_currentMultiZoneGeometry = combinedGeom.toRect();
            if (paintedZones.size() == 1) {
                m_currentZoneGeometry = combinedGeom.toRect();
            }

            // Highlight all painted zones
            m_zoneDetector->setLayout(layout);
            m_zoneDetector->highlightZones(paintedZones);
            m_overlayService->highlightZones(zoneIdsToStringList(zoneIds));
        }
    }
}

void WindowDragAdaptor::handleMultiZoneModifier(int x, int y, Qt::KeyboardModifiers mods)
{
    Q_UNUSED(mods);

    QScreen* screen = nullptr;
    auto* layout = prepareHandlerContext(x, y, screen);
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

            m_currentMultiZoneGeometry = computeCombinedZoneGeometry(zonesToHighlight, screen, layout).toRect();
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
    QScreen* screen = nullptr;
    auto* layout = prepareHandlerContext(x, y, screen);
    if (!layout) {
        return;
    }

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

void WindowDragAdaptor::dragMoved(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons)
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

    // KWin Effect provides modifiers via the mouseChanged signal.
    Qt::KeyboardModifiers mods;
    if (modifiers != 0) {
        mods = static_cast<Qt::KeyboardModifiers>(modifiers);
    } else {
        // Fallback: try Qt query (may not work on Wayland without focus)
        mods = QGuiApplication::queryKeyboardModifiers();
    }

    // Get modifier settings and current activation state (keyboard or mouse)
    int dragActivationMod = static_cast<int>(m_settings->dragActivationModifier());
    int multiZoneMod = static_cast<int>(m_settings->multiZoneModifier());
    int zoneSpanMod = static_cast<int>(m_settings->zoneSpanModifier());
    const bool multiZoneAlwaysOn = (multiZoneMod == static_cast<int>(DragModifier::AlwaysActive));
    const int activationButton = m_settings->dragActivationMouseButton();
    const bool activationByMouse = (activationButton != 0 && (mouseButtons & activationButton) != 0);
    if (activationByMouse) {
        m_mouseActivationLatched = true;
    }
    bool zoneActivationHeld = checkModifier(dragActivationMod, mods) || activationByMouse || m_mouseActivationLatched;
    // AlwaysActive should not independently activate the overlay;
    // it only enables proximity snap while the overlay is already open.
    bool multiZoneModifierHeld = multiZoneAlwaysOn ? false : checkModifier(multiZoneMod, mods);
    bool zoneSpanModifierHeld = checkModifier(zoneSpanMod, mods);

    // Warn once per drag about modifier conflicts (zone span shadows other modifiers)
    if (!m_modifierConflictWarned && zoneSpanMod != 0) {
        if (zoneSpanMod == multiZoneMod) {
            qCWarning(lcDbusWindow) << "zoneSpanModifier and multiZoneModifier are both set to"
                                    << zoneSpanMod << "- multi-zone proximity mode will be unreachable";
            m_modifierConflictWarned = true;
        } else if (zoneSpanMod == dragActivationMod) {
            qCWarning(lcDbusWindow) << "zoneSpanModifier and dragActivationModifier are both set to"
                                    << zoneSpanMod << "- single-zone mode will be unreachable";
            m_modifierConflictWarned = true;
        }
    }

    // Mutual exclusion: overlay (modifier-triggered) and zone selector (edge-triggered)
    // cannot be active simultaneously. Modifier takes priority as an explicit user action.
    // Priority: zone span > multi-zone > single zone > none
    if (zoneSpanModifierHeld || multiZoneModifierHeld || zoneActivationHeld) {
        // Modifier held: overlay takes priority — dismiss zone selector if open
        if (m_zoneSelectorShown) {
            m_zoneSelectorShown = false;
            m_overlayService->hideZoneSelector();
            m_overlayService->clearSelectedZone();
        }

        if (zoneSpanModifierHeld) {
            handleZoneSpanModifier(cursorX, cursorY);
        } else {
            // Transitioning away from zone span: clear painted zones
            if (!m_paintedZoneIds.isEmpty()) {
                m_paintedZoneIds.clear();
            }
            if (multiZoneModifierHeld || multiZoneAlwaysOn) {
                handleMultiZoneModifier(cursorX, cursorY, mods);
            } else {
                handleSingleZoneModifier(cursorX, cursorY);
            }
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
    // Only emit when geometry actually changes (per .cursorrules)
    QRect geom = m_isMultiZoneMode ? m_currentMultiZoneGeometry : m_currentZoneGeometry;
    if (geom.isValid()) {
        if (geom != m_lastEmittedZoneGeometry) {
            m_lastEmittedZoneGeometry = geom;
            m_restoreSizeEmittedDuringDrag = false;
            Q_EMIT zoneGeometryDuringDragChanged(windowId, geom.x(), geom.y(), geom.width(), geom.height());
        }
    } else {
        // Cursor left all zones: restore pre-snap size immediately if window was snapped
        if (m_wasSnapped && !m_restoreSizeEmittedDuringDrag && m_settings
            && m_settings->restoreOriginalSizeOnUnsnap() && m_windowTracking) {
            int origX, origY, origW, origH;
            if (m_windowTracking->getValidatedPreSnapGeometry(windowId, origX, origY, origW, origH)) {
                m_restoreSizeEmittedDuringDrag = true;
                m_lastEmittedZoneGeometry = QRect(); // Reset so re-entering zone will emit
                Q_EMIT restoreSizeDuringDragChanged(windowId, origW, origH);
            }
        }
    }
}

void WindowDragAdaptor::dragStopped(const QString& windowId, int cursorX, int cursorY, int& snapX, int& snapY,
                                    int& snapWidth, int& snapHeight, bool& shouldApplyGeometry,
                                    QString& releaseScreenNameOut, bool& restoreSizeOnlyOut,
                                    bool& snapAssistRequestedOut, QString& emptyZonesJsonOut)
{
    // Initialize output parameters
    // shouldApplyGeometry: true = KWin should set window to (snapX, snapY, snapWidth, snapHeight)
    // restoreSizeOnly: when true with shouldApplyGeometry, effect uses current position + returned size (drag-to-unsnap)
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
    releaseScreenNameOut = releaseScreenName;
    qCDebug(lcDbusWindow) << "dragStopped cursor= (" << cursorX << "," << cursorY << ") releaseScreen= "
                         << releaseScreenName;

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
    if (releaseScreen && m_settings && m_settings->isMonitorDisabled(releaseScreenName)) {
        useOverlayZone = false;
    }

    // Check if a zone was selected via the zone selector (takes priority)
    bool usedZoneSelector = false;
    if (!capturedSnapCancelled && capturedZoneSelectorShown && m_overlayService
        && m_overlayService->hasSelectedZone()) {
        QString selectedLayoutId = m_overlayService->selectedLayoutId();
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
                    m_windowTracking->windowSnapped(windowId, zoneUuid, releaseScreenName);
                    // Record user-initiated snap (not auto-snap)
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
                && m_windowTracking->getValidatedPreSnapGeometry(windowId, origX, origY, origW, origH)) {
                snapWidth = origW;
                snapHeight = origH;
                shouldApplyGeometry = true;
                restoreSizeOnlyOut = true;
            }
        }

        // Clear pre-snap geometry to prevent memory accumulation
        if (m_windowTracking) {
            m_windowTracking->clearPreSnapGeometry(windowId);
        }
    }

    // Snap Assist: only when we actually SNAPPED to a zone (not when restoring size on unsnap).
    // Empty zones are those with no windows AFTER windowSnapped (called above). The zone(s)
    // we just snapped to are now occupied, so they are excluded. Remaining empty zones
    // are offered for the user to fill via the window picker.
    const bool actuallySnapped = shouldApplyGeometry && !restoreSizeOnlyOut;
    if (actuallySnapped && m_settings && m_settings->snapAssistEnabled() && releaseScreen && m_layoutManager
        && m_windowTracking) {
        Layout* layout = m_layoutManager->resolveLayoutForScreen(releaseScreenName);
        if (layout) {
            QString emptyJson = GeometryUtils::buildEmptyZonesJson(layout, releaseScreen, m_settings,
                [this](const Zone* z) { return m_windowTracking->getWindowsInZone(z->id().toString()).isEmpty(); });
            if (!emptyJson.isEmpty() && emptyJson != QLatin1String("[]")) {
                snapAssistRequestedOut = true;
                emptyZonesJsonOut = emptyJson;
            }
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
    m_paintedZoneIds.clear();
    m_lastEmittedZoneGeometry = QRect();
    m_restoreSizeEmittedDuringDrag = false;

    unregisterCancelOverlayShortcut();
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
        unregisterCancelOverlayShortcut();
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
        m_mouseActivationLatched = false;
        m_wasSnapped = false;
    }

    // Delegate tracking cleanup to WindowTrackingAdaptor
    if (m_windowTracking) {
        m_windowTracking->windowClosed(windowId);
    }
}

void WindowDragAdaptor::registerCancelOverlayShortcut()
{
    if (m_cancelOverlayAction) {
        KGlobalAccel::setGlobalShortcut(m_cancelOverlayAction, QKeySequence(Qt::Key_Escape));
    }
}

void WindowDragAdaptor::unregisterCancelOverlayShortcut()
{
    if (m_cancelOverlayAction) {
        KGlobalAccel::setGlobalShortcut(m_cancelOverlayAction, QKeySequence());
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

    bool nearEdge = isNearTriggerEdge(screen, cursorX, cursorY);

    if (nearEdge && !m_zoneSelectorShown) {
        // Show zone selector on the cursor's screen only
        m_zoneSelectorShown = true;
        // Call directly - QDBusAbstractAdaptor signals don't work for internal Qt connections
        m_overlayService->showZoneSelector(screen);
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

bool WindowDragAdaptor::isNearTriggerEdge(QScreen* screen, int cursorX, int cursorY) const
{
    if (!m_settings || !screen) {
        return false;
    }

    // Use per-screen resolved config (per-screen override > global default)
    const ZoneSelectorConfig config = m_settings->resolvedZoneSelectorConfig(screen->name());
    const int triggerDistance = config.triggerDistance;
    const auto position = static_cast<ZoneSelectorPosition>(config.position);

    const QRect screenGeom = screen->geometry();
    const int layoutCount = m_layoutManager ? m_layoutManager->layouts().size() : 0;

    // Use shared layout computation (same code as OverlayService)
    const ZoneSelectorLayout selectorLayout = computeZoneSelectorLayout(config, screen, layoutCount);
    const int barHeight = selectorLayout.barHeight;
    const int barWidth = selectorLayout.barWidth;

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
        m_overlayScreen = nullptr;
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
    unregisterCancelOverlayShortcut();
    m_draggedWindowId.clear();
    m_originalGeometry = QRect();
    m_currentZoneId.clear();
    m_currentZoneGeometry = QRect();
    m_currentAdjacentZoneIds.clear();
    m_isMultiZoneMode = false;
    m_currentMultiZoneGeometry = QRect();
    m_paintedZoneIds.clear();
    m_snapCancelled = false;
    m_mouseActivationLatched = false;
    m_wasSnapped = false;
    m_lastEmittedZoneGeometry = QRect();
    m_restoreSizeEmittedDuringDrag = false;
}

void WindowDragAdaptor::tryStorePreSnapGeometry(const QString& windowId)
{
    // Delegate to overload - wasSnapped param is now unused but kept for compatibility
    tryStorePreSnapGeometry(windowId, m_wasSnapped, m_originalGeometry);
}

void WindowDragAdaptor::tryStorePreSnapGeometry(const QString& windowId, bool wasSnapped, const QRect& originalGeometry)
{
    Q_UNUSED(wasSnapped)
    // Store pre-snap geometry for restore on unsnap/float.
    // The storePreSnapGeometry method handles the "already stored" case internally
    // (only stores on FIRST snap, won't overwrite when moving A→B).
    //
    // BUG FIX: Previously we skipped this call if wasSnapped was true, but this caused
    // a race condition: when floating a window and quickly drag-snapping it, the async
    // D-Bus calls (windowUnsnappedForFloat, setWindowFloating, clearPreSnapGeometry)
    // might not have completed, causing wasSnapped to incorrectly be true. This meant
    // the floating geometry was never stored, so the second float couldn't restore it.
    // By always calling the daemon, we let the service's internal check handle it correctly.
    if (m_windowTracking && originalGeometry.isValid()) {
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
        qCInfo(lcDbusWindow) << "Layout changed mid-drag, clearing cached zone state";
        m_currentZoneId.clear();
        m_currentZoneGeometry = QRect();
        m_currentMultiZoneGeometry = QRect();
        m_currentAdjacentZoneIds.clear();
        m_isMultiZoneMode = false;
        m_paintedZoneIds.clear();

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
