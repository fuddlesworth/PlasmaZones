// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowdragadaptor.h"
#include <QGuiApplication>
#include <QKeySequence>
#include <QScreen>
#include <cmath>
#include "phosphor_i18n.h"
#include "../config/configdefaults.h"
#include <PhosphorShortcuts/IAdhocRegistrar.h>
#include "windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include "../core/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../core/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "../core/zoneselectorlayout.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../core/constants.h"
#include "../config/settings.h"
#include <PhosphorContext/ContextHandle.h>
#include <PhosphorContext/IContextResolver.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

// Stable id for the Escape cancel-overlay shortcut bound dynamically during
// a drag. Matches the pre-library object name so kglobalshortcutsrc entries
// created by earlier installs continue to resolve. QLatin1String is constexpr-
// constructible from a string literal in Qt 6, so we pay zero per-call
// conversion at the Integration::IAdhocRegistrar boundary (QString accepts it implicitly).
static constexpr auto kCancelOverlayId = QLatin1String("cancel_overlay_during_drag");

WindowDragAdaptor::WindowDragAdaptor(IOverlayService* overlay, PhosphorZones::IZoneDetector* detector,
                                     PhosphorZones::LayoutRegistry* layoutManager,
                                     PhosphorScreens::ScreenManager* screenManager, ISettings* settings,
                                     WindowTrackingAdaptor* windowTracking, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_overlayService(overlay)
    , m_zoneDetector(detector)
    , m_layoutManager(layoutManager)
    , m_screenManager(screenManager)
    , m_settings(settings)
    , m_windowTracking(windowTracking)
{
    // Every dep is mandatory — a misordered Daemon wiring that
    // constructs WindowDragAdaptor before its dependencies would
    // otherwise produce a silently-no-op (but D-Bus-callable) object
    // that crashes on the first slot dispatch instead of at
    // construction. qFatal aborts in both debug and release, matching
    // the sibling WindowTrackingAdaptor + SnapAdaptor defensive
    // pattern, so a wiring regression is loud and immediate.
    if (!overlay || !detector || !layoutManager || !settings || !windowTracking) {
        qFatal(
            "WindowDragAdaptor: null dependency at construction "
            "(overlay=%p, detector=%p, layoutManager=%p, settings=%p, windowTracking=%p) "
            "— daemon-wiring bug",
            static_cast<void*>(overlay), static_cast<void*>(detector), static_cast<void*>(layoutManager),
            static_cast<void*>(settings), static_cast<void*>(windowTracking));
        return; // See snapadaptor.cpp's matching `return` for the MSVC noreturn caveat.
    }

    // Connect to layout change signals to invalidate cached zone geometry mid-drag
    // Uses PhosphorZones::LayoutRegistry (concrete) because PhosphorZones::LayoutRegistry is a pure interface without
    // signals
    connect(m_layoutManager, &PhosphorZones::LayoutRegistry::activeLayoutChanged, this,
            &WindowDragAdaptor::onLayoutChanged);
    connect(m_layoutManager, &PhosphorZones::LayoutRegistry::layoutAssigned, this,
            [this](const QString&, int, PhosphorZones::Layout*) {
                onLayoutChanged();
            });

    // Escape-to-cancel during a drag is handled by the kwin-effect's keyboard
    // grab (grabbedKeyboardEvent -> callCancelSnap), not by a KGlobalAccel
    // binding — binding one per drag forced kwin to fsync kglobalshortcutsrc
    // at drag start/end and stuttered the compositor on slow disks (#167). The
    // kCancelOverlayId grab below is bound only for the grab-less snap-assist
    // phase (endDrag's requestSnapAssist branch) and the layout picker
    // (start.cpp), and released via onSnapAssistDismissed / the picker path.

    // When snap assist is dismissed (selection, timeout, etc.), unregister the Escape shortcut
    // that endDrag bound for the snap assist phase
    connect(overlay, &IOverlayService::snapAssistDismissed, this, &WindowDragAdaptor::onSnapAssistDismissed);
}

QScreen* WindowDragAdaptor::screenAtPoint(int x, int y) const
{
    return Utils::findScreenAtPosition(x, y);
}

QString WindowDragAdaptor::effectiveScreenIdAt(int x, int y) const
{
    return Utils::effectiveScreenIdAt(m_screenManager, QPoint(x, y));
}

WindowDragAdaptor::ScreenResolution WindowDragAdaptor::resolveScreenAt(const QPointF& globalPos) const
{
    ScreenResolution result;
    result.screenId = effectiveScreenIdAt(qRound(globalPos.x()), qRound(globalPos.y()));
    result.physicalId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(result.screenId);
    result.qscreen = m_screenManager ? m_screenManager->physicalScreenFor(result.physicalId).qscreen
                                     : PhosphorScreens::ScreenIdentity::findByIdOrName(result.physicalId);
    if (!result.qscreen) {
        result.qscreen = screenAtPoint(qRound(globalPos.x()), qRound(globalPos.y()));
        if (result.qscreen) {
            result.physicalId = PhosphorScreens::ScreenIdentity::identifierFor(result.qscreen);
            // Try virtual screen resolution before falling back to physical ID
            auto* mgr = m_screenManager;
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
                                       int mouseButtons, bool excludeSentinel) const
{
    const int alwaysActive = static_cast<int>(DragModifier::AlwaysActive);
    for (const auto& pt : triggers) {
        if (excludeSentinel && pt.modifier == alwaysActive)
            continue;
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
    QRectF combined = GeometryUtils::getZoneGeometryForScreenF(m_screenManager, zones.first(), screen, screenId, layout,
                                                               m_settings, m_layoutManager);
    for (int i = 1; i < zones.size(); ++i) {
        combined = combined.united(GeometryUtils::getZoneGeometryForScreenF(m_screenManager, zones[i], screen, screenId,
                                                                            layout, m_settings, m_layoutManager));
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
    // Layout picker takes precedence: Escape on a visible picker should
    // dismiss the picker, not also cancel any underlying drag. KGlobalAccel
    // routes one action per key, so the picker-open path piggybacks on this
    // same kCancelOverlayId binding rather than competing with a separate
    // Escape registration that the OS-level grab would silently no-op.
    if (m_overlayService && m_overlayService->isLayoutPickerVisible()) {
        m_overlayService->hideLayoutPicker();
        return;
    }

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
    // Clear any pending snap-assist payload scheduled by a prior endDrag.
    // QTimer::singleShot(0) for computeAndEmitSnapAssist fires on the next
    // event-loop tick; if Escape lands between scheduling and that tick,
    // computeAndEmitSnapAssist would still emit snapAssistReady for a snap
    // the user just cancelled. Clearing the pending IDs makes the deferred
    // call early-return on its empty-id guard. clearForCompositorReconnect
    // does the same two-line clear for the same reason.
    m_snapAssistPendingWindowId.clear();
    m_snapAssistPendingScreenId.clear();
    m_snapAssistPendingDesktop = 0;
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
        m_dragReorderActive = false;
    }

    // Drop pending snap-drag state if this window was the pending target
    // (beginDrag ran but activation was never held before close).
    if (windowId == m_pendingSnapDragWindowId) {
        clearPendingSnapDragState();
        m_currentDragPolicy = {};
    }

    // Drop any pending snap-assist payload addressed to this window — endDrag
    // schedules `computeAndEmitSnapAssist` via QTimer::singleShot(0); if the
    // window closes before that tick, the deferred call would emit
    // `snapAssistReady` for a window that no longer exists. cancelSnap and
    // clearForCompositorReconnect do the same two-line clear for the same
    // reason.
    if (windowId == m_snapAssistPendingWindowId) {
        m_snapAssistPendingWindowId.clear();
        m_snapAssistPendingScreenId.clear();
        m_snapAssistPendingDesktop = 0;
    }

    // NOTE: This slot is now driven by WTA::windowClosedNotification (wired in
    // daemon/signals.cpp), which is emitted at the END of WTA::windowClosed
    // after the canonical tracking-cleanup has already run. Re-invoking
    // m_windowTracking->windowClosed() here would re-enter WTA's slot, re-emit
    // the notification, and recurse infinitely. The drag-state teardown above
    // is the only work this slot owns.
}

void WindowDragAdaptor::registerCancelOverlayShortcut()
{
    if (!m_shortcutRegistrar) {
        return;
    }
    m_shortcutRegistrar->registerAdhocShortcut(kCancelOverlayId, QKeySequence(Qt::Key_Escape),
                                               PhosphorI18n::tr("Cancel Zone Overlay"), [this] {
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

namespace {
constexpr auto kLayoutPickerLeftId = QLatin1String("layout_picker_nav_left");
constexpr auto kLayoutPickerRightId = QLatin1String("layout_picker_nav_right");
constexpr auto kLayoutPickerUpId = QLatin1String("layout_picker_nav_up");
constexpr auto kLayoutPickerDownId = QLatin1String("layout_picker_nav_down");
constexpr auto kLayoutPickerReturnId = QLatin1String("layout_picker_confirm_return");
constexpr auto kLayoutPickerEnterId = QLatin1String("layout_picker_confirm_enter");
} // namespace

void WindowDragAdaptor::ensureLayoutPickerNavShortcutsRegistered(std::function<void(int, int)> moveCb,
                                                                 std::function<void()> confirmCb)
{
    if (!m_shortcutRegistrar || !moveCb || !confirmCb) {
        return;
    }
    m_shortcutRegistrar->registerAdhocShortcut(kLayoutPickerLeftId, QKeySequence(Qt::Key_Left),
                                               PhosphorI18n::tr("Layout Picker: Move Left"), [moveCb] {
                                                   moveCb(-1, 0);
                                               });
    m_shortcutRegistrar->registerAdhocShortcut(kLayoutPickerRightId, QKeySequence(Qt::Key_Right),
                                               PhosphorI18n::tr("Layout Picker: Move Right"), [moveCb] {
                                                   moveCb(1, 0);
                                               });
    m_shortcutRegistrar->registerAdhocShortcut(kLayoutPickerUpId, QKeySequence(Qt::Key_Up),
                                               PhosphorI18n::tr("Layout Picker: Move Up"), [moveCb] {
                                                   moveCb(0, -1);
                                               });
    m_shortcutRegistrar->registerAdhocShortcut(kLayoutPickerDownId, QKeySequence(Qt::Key_Down),
                                               PhosphorI18n::tr("Layout Picker: Move Down"), [moveCb] {
                                                   moveCb(0, 1);
                                               });
    m_shortcutRegistrar->registerAdhocShortcut(kLayoutPickerReturnId, QKeySequence(Qt::Key_Return),
                                               PhosphorI18n::tr("Layout Picker: Confirm"), confirmCb);
    m_shortcutRegistrar->registerAdhocShortcut(kLayoutPickerEnterId, QKeySequence(Qt::Key_Enter),
                                               PhosphorI18n::tr("Layout Picker: Confirm"), confirmCb);
}

void WindowDragAdaptor::releaseLayoutPickerNavShortcuts()
{
    if (!m_shortcutRegistrar) {
        return;
    }
    m_shortcutRegistrar->unregisterAdhocShortcut(kLayoutPickerLeftId);
    m_shortcutRegistrar->unregisterAdhocShortcut(kLayoutPickerRightId);
    m_shortcutRegistrar->unregisterAdhocShortcut(kLayoutPickerUpId);
    m_shortcutRegistrar->unregisterAdhocShortcut(kLayoutPickerDownId);
    m_shortcutRegistrar->unregisterAdhocShortcut(kLayoutPickerReturnId);
    m_shortcutRegistrar->unregisterAdhocShortcut(kLayoutPickerEnterId);
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
    // Disable gate via single resolver snapshot, mirroring the Pass 4
    // pattern in drop.cpp's zone-selector and layout-activation gates.
    // The legacy `isContextDisabled(..., AssignmentEntry::Snapping, ...)` had
    // two issues: (a) split-snapshot race — the (desktop, activity) reads
    // were independent of the mode lookup, so a virtual-desktop switch
    // between them decoupled them; (b) hard-coded `Snapping` consulted the
    // wrong disable list when the screen's live mode was autotile. Take
    // one `handleFor` snapshot so all three axes agree, override the mode
    // in place via the layout manager's per-(desktop, activity) lookup,
    // then gate via `isDisabled`.
    if (screen && m_contextResolver && m_layoutManager) {
        PhosphorContext::ContextHandle selectorCtx = m_contextResolver->handleFor(selectorScreenId);
        selectorCtx.mode =
            m_layoutManager->modeForScreen(selectorScreenId, selectorCtx.virtualDesktop, selectorCtx.activity);
        if (m_contextResolver->isDisabled(selectorCtx)) {
            if (m_zoneSelectorShown) {
                m_zoneSelectorShown = false;
                m_zoneSelectorShownOn.clear();
                m_overlayService->hideZoneSelector();
            }
            return;
        }
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
    const QString effectiveId = screenId.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(screen) : screenId;

    // Use per-screen resolved config (per-screen override > global default)
    const ZoneSelectorConfig config = m_settings->resolvedZoneSelectorConfig(effectiveId);
    const int triggerDistance = config.triggerDistance;
    const auto position = static_cast<ZoneSelectorPosition>(config.position);

    // Use virtual screen geometry when available
    auto* smgr = m_screenManager;
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
    // labels-texture hash cache skips the sparse glyph-tile payload rebuild
    // when inputs are unchanged. m_overlayShown stays true because the
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
        // next shader-timer tick (overlayservice/shader.cpp's
        // `updateShaderUniforms` m_zoneDataDirty branch) would then re-run
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
    // Drop any pending async snapAssistReady payload. Without this, a
    // compositor reconnect between endDrag and the QTimer::singleShot(0)
    // that emits snapAssistReady would deliver the prior session's
    // windowId/screenId to the freshly-registered effect. resetDragState
    // can NOT clear these — the normal drop path sets the pending IDs
    // immediately before calling resetDragState, so clearing there would
    // wipe the IDs the about-to-fire timer is meant to read, and
    // snapAssistReady would never be emitted.
    m_snapAssistPendingWindowId.clear();
    m_snapAssistPendingScreenId.clear();
    // Clear the desktop snapshot alongside the id pair so the (windowId,
    // screenId, desktop) triple is always cleared together. The id-empty
    // early-return in computeAndEmitSnapAssist makes a leftover desktop
    // value harmless today, but the symmetric clear matches the cancelSnap
    // and handleWindowClosed sites and survives a future refactor that
    // drops the id-empty guard.
    m_snapAssistPendingDesktop = 0;
    // Drop any pending snap-drag state — if a beginDrag landed snap-path
    // but activation never fired (no trigger held), the pending fields
    // would survive compositor reconnect and bleed into the next drag
    // until the next beginDrag's `clearPendingSnapDragState()` ran. Be
    // explicit here so the post-reconnect state is well-defined.
    clearPendingSnapDragState();
    // Clear the last-computed drag policy. Both code paths in
    // `handleWindowClosed` clear it on the equivalent "session torn down"
    // events; clearForCompositorReconnect should match.
    m_currentDragPolicy = {};
    // Clear the last-logged activation-transition state. Otherwise the
    // first dragMoved tick of the next drag may suppress its
    // transition log because the stale value matches the new tick.
    // beginDrag() resets this at the start of every drag, but the
    // reconnect path can leave a window where the next beginDrag has
    // not yet fired.
    m_lastLoggedActivationActive = false;
    // Drop any picker-nav lambda registrations: their captures
    // include OverlayService* which the compositor-reconnect path
    // may tear down before the next picker-show re-registers.
    // Leaving stale registrations alive would route an arrow keypress
    // into a freed pointer.
    releaseLayoutPickerNavShortcuts();
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
    // m_snapAssistPendingWindowId / m_snapAssistPendingScreenId are
    // intentionally NOT cleared here. The normal drop path in drop.cpp
    // sets those IDs immediately before calling resetDragState, then
    // schedules computeAndEmitSnapAssist via QTimer::singleShot(0). If we
    // cleared them here, the timer would read empty IDs and snapAssistReady
    // would never fire — snap assist would never show. The compositor-
    // reconnect concern is handled in clearForCompositorReconnect.
    // computeAndEmitSnapAssist consumes-and-clears the IDs after reading.
}

void WindowDragAdaptor::tryStorePreSnapGeometry(const QString& windowId, const QRect& originalGeometry)
{
    // Store pre-snap geometry for restore on unsnap/float (first-only: overwrite=false).
    // Single float-back store: the unified placement record's shared free geometry.
    if (m_windowTracking && m_windowTracking->service() && originalGeometry.isValid()) {
        QString screenId = effectiveScreenIdAt(originalGeometry.center().x(), originalGeometry.center().y());
        if (screenId.isEmpty()) {
            QScreen* screen = Utils::findScreenAtPosition(originalGeometry.center());
            if (screen) {
                screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
            }
        }
        m_windowTracking->service()->recordFreeGeometry(windowId, screenId, originalGeometry, false);
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
    // The Escape grab (kCancelOverlayId) is shared with two other consumers
    // that piggy-back on the same id (so KGlobalAccel routes to a single
    // action — see registerCancelOverlayShortcut's docstring): the layout
    // picker (registered by start.cpp on layoutPickerRequested) and the
    // snap-assist phase (registered by endDrag when it requests snap assist).
    // Releasing here unconditionally tears the grab out from under those
    // consumers — picker-still-up after a snap-assist auto-dismiss would
    // become un-Escape-able, and the same applies if the daemon thinks a
    // drag is in flight (defence-in-depth, snap-assist normally appears
    // post-drop). cancelSnap() already orchestrates precedence between
    // overlays at Escape time, so the only safe thing this slot can do is
    // release WHEN no other consumer still needs the grab.
    if (m_overlayService && m_overlayService->isLayoutPickerVisible()) {
        return;
    }
    if (isDragActive()) {
        return;
    }
    unregisterCancelOverlayShortcut();
}

} // namespace PlasmaZones
