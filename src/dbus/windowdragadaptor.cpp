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
#include "../config/settings.h"
#include "../autotile/AutotileEngine.h"

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

    // When snap assist is dismissed (selection, timeout, etc.), unregister the Escape shortcut
    // that was kept alive during dragStopped() for the snap assist phase
    connect(overlay, &IOverlayService::snapAssistDismissed, this, &WindowDragAdaptor::onSnapAssistDismissed);
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

bool WindowDragAdaptor::anyTriggerHeld(const QVariantList& triggers, Qt::KeyboardModifiers mods, int mouseButtons) const
{
    for (const auto& t : triggers) {
        const auto map = t.toMap();
        const int mod = map.value(QStringLiteral("modifier"), 0).toInt();
        const int btn = map.value(QStringLiteral("mouseButton"), 0).toInt();
        // AND semantics: both modifier and mouse button must match when both are set.
        // A zero field means "don't care" (always matches). At least one must be non-zero.
        const bool modMatch = (mod == 0) || checkModifier(mod, mods);
        const bool btnMatch = (btn == 0) || (mouseButtons & btn) != 0;
        if (modMatch && btnMatch && (mod != 0 || btn != 0))
            return true;
    }
    return false;
}

QRectF WindowDragAdaptor::computeCombinedZoneGeometry(const QVector<Zone*>& zones, QScreen* screen,
                                                      Layout* layout) const
{
    if (zones.isEmpty()) {
        return QRectF();
    }
    QString screenId = Utils::screenIdentifier(screen);
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId);
    EdgeGaps outerGaps = GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId);
    bool useAvail = !(layout && layout->useFullScreenGeometry());
    QRectF combined = GeometryUtils::getZoneGeometryWithGaps(zones.first(), screen, zonePadding, outerGaps, useAvail);
    for (int i = 1; i < zones.size(); ++i) {
        combined =
            combined.united(GeometryUtils::getZoneGeometryWithGaps(zones[i], screen, zonePadding, outerGaps, useAvail));
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

void WindowDragAdaptor::cancelSnap()
{
    m_snapCancelled = true;
    m_triggerReleasedAfterCancel = false;
    m_activationToggled = false;
    m_prevTriggerHeld = false;
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

    // Also dismiss snap assist if visible (Escape pressed while snap assist is showing,
    // e.g. due to KGlobalAccel unregistration race with the snap assist Shortcut)
    if (m_overlayService && m_overlayService->isSnapAssistVisible()) {
        m_overlayService->hideSnapAssist();
    }
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
        // removeAllShortcuts() fully deregisters the action from the kglobalaccel daemon,
        // releasing the compositor-level key grab. The previous approach of setting an empty
        // QKeySequence left the action registered with a stale grab on Wayland, causing Escape
        // to remain intercepted system-wide after every window drag (see discussion #155).
        KGlobalAccel::self()->removeAllShortcuts(m_cancelOverlayAction);
    }
}

void WindowDragAdaptor::checkZoneSelectorTrigger(int cursorX, int cursorY)
{
    // Check if zone selector feature is enabled
    if (!m_settings || !m_settings->zoneSelectorEnabled()) {
        return;
    }

    QScreen* screen = screenAtPoint(cursorX, cursorY);
    if (screen && m_settings->isMonitorDisabled(Utils::screenIdentifier(screen))) {
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
    const ZoneSelectorConfig config = m_settings->resolvedZoneSelectorConfig(Utils::screenIdentifier(screen));
    const int triggerDistance = config.triggerDistance;
    const auto position = static_cast<ZoneSelectorPosition>(config.position);

    const QRect screenGeom = screen->geometry();
    // Use filtered layout count (matches what the zone selector popup actually displays)
    // so the keep-visible zone matches the real popup dimensions
    const int layoutCount = m_overlayService ? m_overlayService->visibleLayoutCount(Utils::screenIdentifier(screen))
                                             : (m_layoutManager ? m_layoutManager->layouts().size() : 0);

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

void WindowDragAdaptor::resetDragState(bool keepEscapeShortcut)
{
    if (!keepEscapeShortcut) {
        unregisterCancelOverlayShortcut();
    }
    m_draggedWindowId.clear();
    m_originalGeometry = QRect();
    m_currentZoneId.clear();
    m_currentZoneGeometry = QRect();
    m_currentAdjacentZoneIds.clear();
    m_isMultiZoneMode = false;
    m_currentMultiZoneGeometry = QRect();
    m_paintedZoneIds.clear();
    m_snapCancelled = false;
    m_triggerReleasedAfterCancel = false;
    m_activationToggled = false;
    m_prevTriggerHeld = false;
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

void WindowDragAdaptor::onSnapAssistDismissed()
{
    unregisterCancelOverlayShortcut();
}

} // namespace PlasmaZones
