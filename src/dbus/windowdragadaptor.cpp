// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowdragadaptor.h"
#include <QGuiApplication>
#include <QKeySequence>
#include <QScreen>
#include <cmath>
#include "pz_i18n.h"
#include "../config/configdefaults.h"
#include <PhosphorShortcuts/IAdhocRegistrar.h>
#include "windowtrackingadaptor.h"
#include "../core/interfaces.h"
#include "../core/layoutmanager.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../core/geometryutils.h"
#include "../core/screenmanagerservice.h"
#include "../core/zoneselectorlayout.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../core/constants.h"
#include "../config/settings.h"
#include "../autotile/AutotileEngine.h"

namespace PlasmaZones {

// Stable id for the Escape cancel-overlay shortcut bound dynamically during
// a drag. Matches the pre-library object name so kglobalshortcutsrc entries
// created by earlier installs continue to resolve. QLatin1String is constexpr-
// constructible from a string literal in Qt 6, so we pay zero per-call
// conversion at the Integration::IAdhocRegistrar boundary (QString accepts it implicitly).
static constexpr auto kCancelOverlayId = QLatin1String("cancel_overlay_during_drag");

WindowDragAdaptor::WindowDragAdaptor(IOverlayService* overlay, PhosphorZones::IZoneDetector* detector,
                                     LayoutManager* layoutManager, ISettings* settings,
                                     WindowTrackingAdaptor* windowTracking, QObject* parent)
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
    // Uses LayoutManager (concrete) because PhosphorZones::ILayoutManager is a pure interface without signals
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &WindowDragAdaptor::onLayoutChanged);
    connect(m_layoutManager, &LayoutManager::layoutAssigned, this, [this](const QString&, int, PhosphorZones::Layout*) {
        onLayoutChanged();
    });

    // Escape shortcut to cancel overlay during drag: bound into the Registry
    // on drag start (see registerCancelOverlayShortcut) and released on end
    // so the global Escape grab doesn't persist between drags.

    // When snap assist is dismissed (selection, timeout, etc.), unregister the Escape shortcut
    // that was kept alive during dragStopped() for the snap assist phase
    connect(overlay, &IOverlayService::snapAssistDismissed, this, &WindowDragAdaptor::onSnapAssistDismissed);
}

QScreen* WindowDragAdaptor::screenAtPoint(int x, int y) const
{
    return Utils::findScreenAtPosition(x, y);
}

QString WindowDragAdaptor::effectiveScreenIdAt(int x, int y) const
{
    return Utils::effectiveScreenIdAt(QPoint(x, y));
}

WindowDragAdaptor::ScreenResolution WindowDragAdaptor::resolveScreenAt(const QPointF& globalPos) const
{
    ScreenResolution result;
    result.screenId = effectiveScreenIdAt(qRound(globalPos.x()), qRound(globalPos.y()));
    result.physicalId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(result.screenId);
    result.qscreen = resolvePhysicalScreen(result.physicalId);
    if (!result.qscreen) {
        result.qscreen = screenAtPoint(qRound(globalPos.x()), qRound(globalPos.y()));
        if (result.qscreen) {
            result.physicalId = Utils::screenIdentifier(result.qscreen);
            // Try virtual screen resolution before falling back to physical ID
            auto* mgr = screenManager();
            if (mgr && mgr->hasVirtualScreens(result.physicalId)) {
                QString vsId = mgr->effectiveScreenAt(QPoint(qRound(globalPos.x()), qRound(globalPos.y())));
                result.screenId = vsId.isEmpty() ? result.physicalId : vsId;
            } else {
                result.screenId = result.physicalId;
            }
        }
    }
    return result;
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
        const int mod = map.value(ConfigDefaults::triggerModifierField(), 0).toInt();
        const int btn = map.value(ConfigDefaults::triggerMouseButtonField(), 0).toInt();
        // AND semantics: both modifier and mouse button must match when both are set.
        // A zero field means "don't care" (always matches). At least one must be non-zero.
        const bool modMatch = (mod == 0) || checkModifier(mod, mods);
        const bool btnMatch = (btn == 0) || (mouseButtons & btn) != 0;
        if (modMatch && btnMatch && (mod != 0 || btn != 0))
            return true;
    }
    return false;
}

QVector<WindowDragAdaptor::ParsedTrigger> WindowDragAdaptor::parseTriggers(const QVariantList& triggers)
{
    QVector<ParsedTrigger> result;
    result.reserve(triggers.size());
    for (const auto& t : triggers) {
        const auto map = t.toMap();
        ParsedTrigger pt;
        pt.modifier = map.value(ConfigDefaults::triggerModifierField(), 0).toInt();
        pt.mouseButton = map.value(ConfigDefaults::triggerMouseButtonField(), 0).toInt();
        result.append(pt);
    }
    return result;
}

bool WindowDragAdaptor::anyTriggerHeld(const QVector<ParsedTrigger>& triggers, Qt::KeyboardModifiers mods,
                                       int mouseButtons) const
{
    for (const auto& pt : triggers) {
        const bool modMatch = (pt.modifier == 0) || checkModifier(pt.modifier, mods);
        const bool btnMatch = (pt.mouseButton == 0) || (mouseButtons & pt.mouseButton) != 0;
        if (modMatch && btnMatch && (pt.modifier != 0 || pt.mouseButton != 0))
            return true;
    }
    return false;
}

QRectF WindowDragAdaptor::computeCombinedZoneGeometry(const QVector<PhosphorZones::Zone*>& zones, QScreen* screen,
                                                      PhosphorZones::Layout* layout, const QString& screenId) const
{
    if (zones.isEmpty()) {
        return QRectF();
    }
    QRectF combined = GeometryUtils::getZoneGeometryForScreenF(zones.first(), screen, screenId, layout, m_settings);
    for (int i = 1; i < zones.size(); ++i) {
        combined =
            combined.united(GeometryUtils::getZoneGeometryForScreenF(zones[i], screen, screenId, layout, m_settings));
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

void WindowDragAdaptor::cancelDragInsertIfActive()
{
    if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
        m_autotileEngine->cancelDragInsertPreview();
    }
}

void WindowDragAdaptor::cancelSnap()
{
    // Cancel any active autotile drag-insert preview so neighbours snap back
    // to their original order instead of sticking at the previewed position.
    cancelDragInsertIfActive();
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
        cancelDragInsertIfActive();
        unregisterCancelOverlayShortcut();
        hideOverlayAndClearZoneState();

        // Hide zone selector if shown
        if (m_zoneSelectorShown && m_overlayService) {
            m_zoneSelectorShown = false;
            m_zoneSelectorShownOn.clear();
            m_overlayService->hideZoneSelector();
            m_overlayService->clearSelectedZone();
        }

        // Reset all drag state
        m_draggedWindowId.clear();
        m_originalGeometry = QRect();
        m_snapCancelled = false;
        m_wasSnapped = false;
        m_currentDragPolicy = {};
    }

    // Drop pending snap-drag state if this window was the pending target
    // (beginDrag ran but activation was never held before close).
    if (windowId == m_pendingSnapDragWindowId) {
        clearPendingSnapDragState();
        m_currentDragPolicy = {};
    }

    // Delegate tracking cleanup to WindowTrackingAdaptor
    if (m_windowTracking) {
        m_windowTracking->windowClosed(windowId);
    }
}

void WindowDragAdaptor::registerCancelOverlayShortcut()
{
    if (!m_shortcutRegistrar) {
        return;
    }
    m_shortcutRegistrar->registerAdhocShortcut(kCancelOverlayId, QKeySequence(Qt::Key_Escape),
                                               PzI18n::tr("Cancel Zone Overlay"), [this] {
                                                   cancelSnap();
                                               });
}

void WindowDragAdaptor::unregisterCancelOverlayShortcut()
{
    if (!m_shortcutRegistrar) {
        return;
    }
    // unregisterAdhocShortcut() drops both the Registry entry and the
    // compositor-level key grab. Prior IShortcutBackend-era bug (discussion
    // #155) where setting an empty QKeySequence left a stale Wayland grab is
    // no longer expressible: rebind() with an empty sequence now routes
    // through unbind() inside the Registry, and the cancel path always uses
    // the explicit unregister call below.
    m_shortcutRegistrar->unregisterAdhocShortcut(kCancelOverlayId);
}

void WindowDragAdaptor::checkZoneSelectorTrigger(int cursorX, int cursorY)
{
    // Check if zone selector feature is enabled
    if (!m_settings || !m_settings->zoneSelectorEnabled()) {
        return;
    }

    // Resolve effective (virtual-aware) screen ID for disabled-monitor check
    auto resolved = resolveScreenAt(QPointF(cursorX, cursorY));
    QString selectorScreenId = resolved.screenId;
    QScreen* screen = resolved.qscreen;
    if (screen
        && isContextDisabled(m_settings, selectorScreenId, m_layoutManager->currentVirtualDesktop(),
                             m_layoutManager->currentActivity())) {
        if (m_zoneSelectorShown) {
            m_zoneSelectorShown = false;
            m_zoneSelectorShownOn.clear();
            m_overlayService->hideZoneSelector();
        }
        return;
    }

    bool nearEdge = isNearTriggerEdge(screen, cursorX, cursorY, selectorScreenId);

    if (nearEdge && m_zoneSelectorShown && m_zoneSelectorShownOn != selectorScreenId) {
        // Cursor moved into a different (virtual) screen's edge zone while the
        // selector was shown on the previous one. Hide + re-show on the new VS
        // so the popup follows the cursor instead of stranding on the old VS.
        m_overlayService->hideZoneSelector();
        m_zoneSelectorShown = false;
        m_zoneSelectorShownOn.clear();
    }

    if (nearEdge && !m_zoneSelectorShown) {
        // Show zone selector on the cursor's screen only
        m_zoneSelectorShown = true;
        m_zoneSelectorShownOn = selectorScreenId;
        m_overlayService->showZoneSelector(selectorScreenId);
    } else if (!nearEdge && m_zoneSelectorShown) {
        // Hide zone selector when cursor moves away from edge
        m_zoneSelectorShown = false;
        m_zoneSelectorShownOn.clear();
        m_overlayService->hideZoneSelector();
    }

    // Update selector position for hover effects
    if (m_zoneSelectorShown) {
        m_overlayService->updateSelectorPosition(cursorX, cursorY);
    }
}

bool WindowDragAdaptor::isNearTriggerEdge(QScreen* screen, int cursorX, int cursorY, const QString& screenId) const
{
    if (!m_settings || !screen) {
        return false;
    }

    // Use virtual-aware screen ID for config lookups (falls back to physical ID)
    const QString effectiveId = screenId.isEmpty() ? Utils::screenIdentifier(screen) : screenId;

    // Use per-screen resolved config (per-screen override > global default)
    const ZoneSelectorConfig config = m_settings->resolvedZoneSelectorConfig(effectiveId);
    const int triggerDistance = config.triggerDistance;
    const auto position = static_cast<ZoneSelectorPosition>(config.position);

    // Use virtual screen geometry when available
    auto* smgr = screenManager();
    QRect vsGeom = smgr ? smgr->screenGeometry(effectiveId) : QRect();
    const QRect screenGeom = vsGeom.isValid() ? vsGeom : screen->geometry();

    // Use filtered layout count (matches what the zone selector popup actually displays)
    // so the keep-visible zone matches the real popup dimensions
    const int layoutCount = m_overlayService ? m_overlayService->visibleLayoutCount(effectiveId)
                                             : (m_layoutManager ? m_layoutManager->layouts().size() : 0);

    // Use shared layout computation (same code as OverlayService)
    const ZoneSelectorLayout selectorLayout = computeZoneSelectorLayout(config, screenGeom, layoutCount);
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
    case ZoneSelectorPosition::Center: {
        // Trigger when cursor is within triggerDistance of screen center;
        // once shown, keep visible while cursor is inside the popup bounds
        const int centerX = screenGeom.x() + screenGeom.width() / 2;
        const int centerY = screenGeom.y() + screenGeom.height() / 2;
        if (m_zoneSelectorShown) {
            return std::abs(cursorX - centerX) <= barWidth / 2 && std::abs(cursorY - centerY) <= barHeight / 2;
        }
        return std::abs(cursorX - centerX) <= triggerDistance && std::abs(cursorY - centerY) <= triggerDistance;
    }
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
    // Drag-end: idle the shader overlay instead of destroying it.
    //
    // Destroying the overlay QQuickWindow here used to pay a ~QQuickWindow
    // teardown that routes through QRhi::~QRhi → vkDestroyDevice. On the
    // NVIDIA proprietary driver that call can deadlock in the driver's
    // internal mutex cycle between its Vulkan and GL/EGL backends (NVIDIA
    // devtalk 319139 / 195793 / 366254, wgpu discussion #9092) — the
    // symptom is an ~18 s main-thread stall per drop because
    // vkDestroyDevice blocks in pthread_cond_timedwait until the driver's
    // internal fence timeout fires. d797b9c3 (phase-a L1) already
    // eliminated the same stall for mid-drag modifier thrash by routing
    // trigger-release through setIdleForDragPause(); this extends the
    // same treatment to real drag-end so the shader overlay's lifetime
    // is bound to daemon lifetime, not drag lifetime.
    //
    // Next-drag resume: dragMoved's first activationActive tick sees
    // m_overlayIdled == true and calls refreshFromIdle() to re-push
    // zone data via updateZonesForAllWindows() — cheap because L2's
    // labels-texture hash cache skips the 23 MB QImage rebuild when
    // inputs are unchanged. m_overlayShown stays true because the
    // underlying QQuickWindow + wl_surface are still alive.
    //
    // Destructive teardown is still needed for real lifecycle events
    // (compositor reconnect, dragged-window-closed, context/autotile
    // disabled mid-drag) — those route through hideOverlayAndClearZoneState
    // instead, which still calls OverlayService::hide().
    bool didIdle = false;
    if (m_overlayShown && m_overlayService) {
        m_overlayService->setIdleForDragPause();
        m_overlayIdled = true;
        didIdle = true;
    }

    // PhosphorZones::Zone selector (different QQuickWindow, also Vulkan-backed) is
    // still destroyed on hide. It only shows when the user hovers a
    // configured selector trigger, so it's not in the drop-then-activate
    // hot path that triggers the NVIDIA deadlock. Revisit if selector
    // usage also hangs.
    if (m_zoneSelectorShown && m_overlayService) {
        m_zoneSelectorShown = false;
        m_zoneSelectorShownOn.clear();
        m_overlayService->hideZoneSelector();
    }
    if (m_overlayService) {
        m_overlayService->clearSelectedZone();
        // clearHighlight() is skipped on the idle path because
        // setIdleForDragPause() already wrote highlightedZoneId /
        // highlightedZoneIds / highlightedCount on every overlay window
        // AND set m_zoneDataDirty = false to protect the blank state.
        // Calling clearHighlight() here would redundantly re-write the
        // same properties AND flip m_zoneDataDirty back to true — the
        // next shader-timer tick (shader.cpp:245) would then re-run
        // updateZonesForAllWindows() and repopulate the zones, leaving
        // the overlay visibly showing zones after drag-end.
        if (!didIdle) {
            m_overlayService->clearHighlight();
        }
    }

    if (m_zoneDetector) {
        m_zoneDetector->clearHighlights();
    }
}

void WindowDragAdaptor::clearForCompositorReconnect()
{
    hideOverlayAndClearZoneState();
    resetDragState(/*keepEscapeShortcut=*/false);
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
    // m_overlayIdled is intentionally NOT cleared here. Each drag-end
    // hide helper sets it explicitly: hideOverlayAndSelector → true
    // (shader overlay idled, not destroyed — window alive across the
    // drag boundary so dragMoved's refreshFromIdle can repopulate it);
    // hideOverlayAndClearZoneState → false (destructive teardown).
    // Clearing it here would clobber the idled=true state set by the
    // drop path and break the next drag's refreshFromIdle trigger.
    // Drop any pending async snapAssistReady payload. Without this, a
    // compositor reconnect between endDrag and the QTimer::singleShot(0)
    // that emits snapAssistReady would deliver the prior session's
    // windowId/screenId to the freshly-registered effect.
    m_snapAssistPendingWindowId.clear();
    m_snapAssistPendingScreenId.clear();
}

void WindowDragAdaptor::tryStorePreSnapGeometry(const QString& windowId)
{
    // Delegate to overload - wasSnapped param is now unused but kept for compatibility
    tryStorePreSnapGeometry(windowId, m_wasSnapped, m_originalGeometry);
}

void WindowDragAdaptor::tryStorePreSnapGeometry(const QString& windowId, bool wasSnapped, const QRect& originalGeometry)
{
    Q_UNUSED(wasSnapped)
    // Store pre-tile geometry for restore on unsnap/float (first-only: overwrite=false).
    // The service handles the "already stored" case internally.
    if (m_windowTracking && originalGeometry.isValid()) {
        QString screenId = effectiveScreenIdAt(originalGeometry.center().x(), originalGeometry.center().y());
        if (screenId.isEmpty()) {
            QScreen* screen = Utils::findScreenAtPosition(originalGeometry.center());
            if (screen) {
                screenId = Utils::screenIdentifier(screen);
            }
        }
        m_windowTracking->storePreTileGeometry(windowId, originalGeometry.x(), originalGeometry.y(),
                                               originalGeometry.width(), originalGeometry.height(), screenId, false);
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
