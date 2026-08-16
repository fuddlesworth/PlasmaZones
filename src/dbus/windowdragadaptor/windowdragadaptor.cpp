// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowdragadaptor.h"
#include <QGuiApplication>
#include <QKeySequence>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <optional>
#include "phosphor_i18n.h"
#include "config/configdefaults.h"
#include <PhosphorShortcuts/IAdhocRegistrar.h>
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/utils/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "core/types/zoneselectorlayout.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "core/types/constants.h"
#include "config/settings.h"
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

// Sampling interval of the drag edge auto-scroll heartbeat, ~60 Hz. Not a
// tunable: it sets how finely the strip is sampled, never how fast it
// travels, because the engine integrates against the real elapsed time.
static constexpr int kDragScrollTickMs = 16;

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
    // phase (shortcuts_wiring.cpp's snapAssistShown handler) and the layout picker
    // (shortcuts_wiring.cpp), and released via onSnapAssistDismissed / the picker path.

    // When snap assist is dismissed (selection, timeout, etc.), unregister the Escape shortcut
    // that shortcuts_wiring.cpp's snapAssistShown handler bound for the snap assist phase
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

PhosphorEngine::IPlacementEngine* WindowDragAdaptor::dragInsertEngineFor(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return nullptr;
    }
    if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId)) {
        return m_autotileEngine;
    }
    if (m_scrollEngine && m_scrollEngine->isActiveOnScreen(screenId)) {
        return m_scrollEngine;
    }
    return nullptr;
}

PhosphorEngine::IPlacementEngine* WindowDragAdaptor::dragInsertPreviewEngine() const
{
    if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
        return m_autotileEngine;
    }
    if (m_scrollEngine && m_scrollEngine->hasDragInsertPreview()) {
        return m_scrollEngine;
    }
    return nullptr;
}

bool WindowDragAdaptor::effectiveDragReorderModeFor(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return false;
    }
    if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId)) {
        return effectiveReorderMode(screenId);
    }
    if (m_scrollEngine && m_scrollEngine->isActiveOnScreen(screenId)) {
        // Scrolling's "always re-insert" is the AlwaysActive sentinel in its
        // trigger list — no DragBehavior enum, no rule resolve. Read from
        // the per-drag parsed cache (populated unconditionally by beginDrag,
        // ahead of every consumer). A sentinel paired with a mouse button is
        // NOT unconditional — anyTriggerHeld ANDs the button in per tick, so
        // treating it as always-on here would make policy and per-tick
        // behaviour disagree; only the bare sentinel means "always".
        return std::any_of(m_cachedScrollingDragInsertTriggers.cbegin(), m_cachedScrollingDragInsertTriggers.cend(),
                           [](const ParsedTrigger& pt) {
                               return pt.modifier == static_cast<int>(DragModifier::AlwaysActive)
                                   && pt.mouseButton == 0;
                           });
    }
    return false;
}

void WindowDragAdaptor::ensureDragScrollTimerRunning(const QString& windowId)
{
    if (!m_dragScrollTimer) {
        m_dragScrollTimer = new QTimer(this);
        // The engine integrates against the REAL elapsed time, so this
        // figure only sets how finely the strip is sampled, not how fast it
        // travels.
        m_dragScrollTimer->setInterval(kDragScrollTickMs);
        connect(m_dragScrollTimer, &QTimer::timeout, this, &WindowDragAdaptor::advanceDragScroll);
    }
    if (m_dragScrollWindowId != windowId) {
        // A different drag owns the timer now: restart the elapsed clock so
        // the first tick of this drag cannot integrate the gap since the
        // last one.
        m_dragScrollWindowId = windowId;
        m_dragScrollElapsed.invalidate();
    }
    if (!m_dragScrollTimer->isActive()) {
        m_dragScrollElapsed.invalidate();
        m_dragScrollTimer->start();
    }
}

void WindowDragAdaptor::stopDragScrollTimer()
{
    if (m_dragScrollTimer) {
        m_dragScrollTimer->stop();
    }
    m_dragScrollWindowId.clear();
    m_dragScrollElapsed.invalidate();
}

void WindowDragAdaptor::advanceDragScroll()
{
    PhosphorEngine::IPlacementEngine* engine = dragInsertPreviewEngine();
    // Self-terminating: five engine-side paths cancel a preview without
    // telling the adaptor, and the drag itself can end between ticks. Rather
    // than trust every caller to have stopped us, notice here.
    if (!engine || m_draggedWindowId.isEmpty() || m_draggedWindowId != m_dragScrollWindowId) {
        stopDragScrollTimer();
        return;
    }
    // A dt of zero on the first tick of an arming is not a special case, only
    // a zero-length interval: the engine still needs the tick to arm its
    // start delay, and it clamps dt into [0, ceiling] itself. Resolved and
    // dispatched once so both arms share the same screen id and the same
    // post-condition — an earlier split let the first tick silently drop a
    // repaint the engine had asked for.
    const qreal dt = m_dragScrollElapsed.isValid() ? qreal(m_dragScrollElapsed.nsecsElapsed()) / 1'000'000'000.0 : 0.0;
    m_dragScrollElapsed.restart();
    const QString screenId = engine->dragInsertPreviewScreenId();
    if (!engine->dragAutoScrollTick(screenId, m_lastDragCursorPos, dt)) {
        return;
    }
    // The engine owns the target while it scrolls (it writes the view's
    // leading/trailing new-column slot itself), so there is nothing to
    // hit-test here — only the rect that target resolves to, which moves
    // with the view.
    pushScrollDropIndicator(screenId, engine->dragInsertIndicatorRect(screenId), /*animate=*/false);
}

void WindowDragAdaptor::pushScrollDropIndicator(const QString& screenId, const QRect& rect, bool animate)
{
    if (!m_overlayService || screenId.isEmpty()) {
        return;
    }
    // Cross-screen drag: hide the old screen's indicator before lighting the
    // new one. Without this the departed screen keeps painting a target the
    // drop can no longer land in, and nothing else would clear it — the
    // teardown paths only know the screen recorded here.
    // screensMatch, not raw !=, defensively: the recorded id and an incoming
    // one can spell the same output as a physical id or a virtual one, and a
    // raw compare would read a spelling change as a screen change — pushing a
    // hide the very next line un-hides. Every sibling comparison in this file
    // already uses it.
    if (!m_dropIndicatorScreenId.isEmpty()
        && !PhosphorScreens::ScreenIdentity::screensMatch(m_dropIndicatorScreenId, screenId)) {
        // The departing screen's hide is never animated: there is no target
        // to make legible, only a rectangle that must stop being painted.
        m_overlayService->updateScrollDropIndicator(m_dropIndicatorScreenId, QRect(), /*animate=*/false);
    }
    m_overlayService->updateScrollDropIndicator(screenId, rect, animate);
    // An empty rect means the engine has no paintable target (autotile by
    // interface default, or a preview with nothing hit-tested yet). The
    // overlay treats that as a hide, so do not record the screen as lit —
    // otherwise the next clear would push a redundant second hide.
    m_dropIndicatorScreenId = rect.isValid() ? screenId : QString();
}

void WindowDragAdaptor::clearScrollDropIndicator()
{
    if (m_dropIndicatorScreenId.isEmpty()) {
        return;
    }
    if (m_overlayService) {
        m_overlayService->updateScrollDropIndicator(m_dropIndicatorScreenId, QRect(), /*animate=*/false);
    }
    m_dropIndicatorScreenId.clear();
}

void WindowDragAdaptor::cancelDragInsertIfActive()
{
    // Stopped FIRST, matching settleDragInsertPreviewAt: no scroll tick may
    // land between the decision to end the preview and the teardown below.
    // Neither call here spins the event loop today, so the order is currently
    // unobservable, but the two sites encoding opposite orders is how that
    // stops being true without anyone noticing.
    stopDragScrollTimer();
    // At most one engine holds a preview, but sweep both — a stale second
    // preview would otherwise be unreachable by every cleanup path.
    if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
        m_autotileEngine->cancelDragInsertPreview();
    }
    if (m_scrollEngine && m_scrollEngine->hasDragInsertPreview()) {
        m_scrollEngine->cancelDragInsertPreview();
    }
    clearScrollDropIndicator();
}

void WindowDragAdaptor::cancelDragInsertPreviews()
{
    cancelDragInsertIfActive();
}

void WindowDragAdaptor::cancelDragInsertPreviewsForScreen(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }
    // TARGET or PRIOR screen. Cancel restores the window to the screen it was
    // adopted from, so a cross-output drag whose SOURCE monitor is the one
    // going away is just as stranded as one whose target is: the preview
    // survives with a priorKey naming a context that no longer resolves, and
    // the cancel it exists to serve can never put the window back. The scroll
    // engine's own pruneStatesForRemovedScreen tests both, and this is the
    // only place autotile's previews get either test.
    const auto affected = [&screenId](PhosphorEngine::IPlacementEngine* engine) {
        if (!engine || !engine->hasDragInsertPreview()) {
            return false;
        }
        return PhosphorIdentity::VirtualScreenId::samePhysical(engine->dragInsertPreviewScreenId(), screenId)
            || PhosphorIdentity::VirtualScreenId::samePhysical(engine->dragInsertPreviewPriorScreenId(), screenId);
    };
    if (affected(m_autotileEngine)) {
        m_autotileEngine->cancelDragInsertPreview();
    }
    if (affected(m_scrollEngine)) {
        m_scrollEngine->cancelDragInsertPreview();
    }
    // Clear the indicator on the departing output regardless of whether any
    // engine cancel ran above. The engine may have self-cancelled its own
    // preview during a prune before this call arrives
    // (pruneStatesForRemovedScreen does exactly that), in which case
    // `affected` is false, nothing is cancelled here, and the rectangle would
    // stay painted on a screen that is going away.
    if (!m_dropIndicatorScreenId.isEmpty()
        && PhosphorIdentity::VirtualScreenId::samePhysical(m_dropIndicatorScreenId, screenId)) {
        clearScrollDropIndicator();
    }
    // Stop the heartbeat only when NO preview is left anywhere, never
    // unconditionally. This function is per-output and runs on paths (a
    // desktop switch, an output reconfigure) that routinely fire while a
    // preview is live on a DIFFERENT monitor, where `affected` is false and
    // nothing above cancelled anything. An unconditional stop there would
    // kill a scroll that is legitimately running on the other screen, and
    // nothing would restart it: the only arm site is inside dragMoved, so a
    // cursor parked in the band — the entire gesture this serves — never
    // gets another chance. Same non-interference rule as this function's own
    // TARGET-or-PRIOR test.
    if (!dragInsertPreviewEngine()) {
        stopDragScrollTimer();
    }
}

bool WindowDragAdaptor::settleDragInsertPreviewAt(int cursorX, int cursorY, const QString& windowId)
{
    PhosphorEngine::IPlacementEngine* engine = dragInsertPreviewEngine();
    const QString releaseScreenId = resolveScreenAt(QPointF(cursorX, cursorY)).screenId;

    // One last tick at the RELEASE position, before the timer stops. The
    // engine's ownership latch is cleared by the tick, not by pointer motion,
    // so a flick out of the band followed by a quick release could otherwise
    // land inside the gap between the two (dragMoved is throttled to ~32 ms,
    // the heartbeat is ~16 ms) with the latch still set — and a latched
    // target is the edge slot, not the slot under the cursor. Ticking here
    // runs the band test against where the button actually came up: still in
    // the band keeps the edge promise, out of it hands the target back and
    // repairs it. dt is zero, so this samples without advancing the scroll.
    //
    // Gated on the engine ALREADY owning the target. The tick is not a pure
    // sampler: with the cursor inside a band it arms, and once the delay has
    // elapsed it TAKES ownership and overwrites the target with an edge slot.
    // Running it from a state that does not already own would therefore
    // manufacture the very ownership the popup gate below honours, and the
    // drop would land at the strip edge instead of under the cursor. Gating
    // on the flag rather than on the timer being active closes that
    // completely instead of narrowing it: a delay that happens to elapse
    // between the last heartbeat and the release cannot promote here.
    // Ownership is exactly the precondition the repair needs anyway — with
    // nothing owned there is no edge slot to undo.
    if (engine && engine->dragAutoScrollActive()) {
        engine->dragAutoScrollTick(engine->dragInsertPreviewScreenId(), QPoint(cursorX, cursorY), 0.0);
    }
    // The drag is over: every arm below either commits, cancels or finds
    // nothing, and none of them wants a scroll tick landing between the
    // decision and the structural insert. Stopped before any arm can return
    // early, and after the settling tick above.
    stopDragScrollTimer();

    // A cancelled drag must stay cancelled. endDrag's cancel arm relies on
    // "the preview is gone, the settle finds nothing" — but the popup-only
    // arm below can BEGIN a fresh preview off a stored pick, which would
    // durably reorder the strip on a cancelled gesture. Gate on
    // m_dragExternallyCancelled ONLY: m_snapCancelled is not just Escape —
    // the policy-flip path sets it via cancelSnap while the drag-insert
    // preview deliberately keeps working for the rest of the drag
    // (drag.cpp's block-above-the-early-return note), so bailing on it here
    // would float a legitimately previewed cross-screen insert at drop.
    // Escape itself is covered anyway: cancelSnap already cancels the
    // previews and clears the stored pick, so neither arm below can fire.
    if (m_dragExternallyCancelled) {
        cancelDragInsertIfActive();
        return false;
    }

    // The strip popup's stored target, when it names the RELEASE screen. It
    // outranks the cursor-derived target: the popup highlight was the last
    // feedback the user saw for this drop. Cleared below on every commit
    // arm; the non-commit arms leave it for the shared drop teardown
    // (hideOverlayAndSelector's clearSelectedZone).
    PhosphorEngine::IPlacementEngine::DragInsertTarget popupTarget;
    bool popupOwns = false;
    if (m_overlayService && m_overlayService->hasSelectedStripTarget()
        && PhosphorScreens::ScreenIdentity::screensMatch(m_overlayService->selectedStripTargetScreenId(),
                                                         releaseScreenId)) {
        const auto strip = m_overlayService->selectedStripTarget();
        popupTarget.primary = strip.columnIndex;
        popupTarget.secondary = strip.tileIndex;
        popupTarget.newSlot = strip.newColumn;
        popupOwns = popupTarget.isValid();
    }

    if (!engine) {
        // Popup-only drop: hold-mode insert with the trigger never held has
        // no live preview, but a valid popup pick is still a committable
        // intent. Run the whole begin → update → commit here. The popup's
        // indices were computed against the detach-emulated snapshot, so
        // they are valid against the strip begin just produced. A failed
        // begin falls through to the caller's float-drop. Deliberately NO
        // isWindowTiled gate here, unlike the two per-tick begin sites: a
        // floating window dropped on a popup card is an EXPLICIT pick, and
        // adopting it into the strip is exactly what the pick asks for —
        // the tiled gate exists to keep passive drags of floating windows
        // free, not to veto an aimed drop.
        if (popupOwns && !windowId.isEmpty() && scrollSelectorScreen(releaseScreenId)
            && m_scrollEngine->beginDragInsertPreview(windowId, releaseScreenId)) {
            m_scrollEngine->updateDragInsertPreview(popupTarget);
            m_scrollEngine->commitDragInsertPreview();
            m_overlayService->clearSelectedZone();
            clearScrollDropIndicator();
            return true;
        }
        // No preview to settle — but the drag is ENDING here, so this is a
        // teardown path like the two below it. The engine may have dropped its
        // own preview after the last push (five engine-side self-cancel sites
        // do exactly that), leaving the indicator painted. It must go.
        clearScrollDropIndicator();
        return false;
    }
    // Screen-matched: a fast drop can land on another screen before any
    // dragMoved tick cancelled the departed preview, and committing then would
    // reorder the WRONG screen and swallow the real drop outcome.
    if (!PhosphorScreens::ScreenIdentity::screensMatch(engine->dragInsertPreviewScreenId(), releaseScreenId)) {
        engine->cancelDragInsertPreview();
        clearScrollDropIndicator();
        return false;
    }
    // The popup's stored pick loses to a live auto-scroll, exactly as it does
    // during the drag: dragMoved stops feeding the popup branch once the
    // engine owns the target, so letting it win here would commit a card the
    // user stopped steering with while they watched the edge slot instead.
    // The settling tick above has already given ownership back if the release
    // landed outside the band, so this only bites for a release still in it.
    if (popupOwns && engine->providesDragInsertSelector() && !engine->dragAutoScrollActive()) {
        engine->updateDragInsertPreview(popupTarget);
    }
    engine->commitDragInsertPreview(); // commit, not cancel — the drop finalizes the reorder
    if (m_overlayService) {
        m_overlayService->clearSelectedZone();
    }
    clearScrollDropIndicator();
    return true;
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
    // Clear the reorder latch alongside the other per-drag latches so cancelSnap
    // is symmetric with resetDragState (it doesn't route through resetDragState).
    m_dragReorderActive = false;
    // The drag-insert toggle pair too, or Escape cannot actually stop a
    // toggled-on preview: the drag-insert block runs ABOVE the
    // m_snapCancelled early return in dragMoved, so with the latch still
    // true the very next tick re-begins the preview Escape just cancelled.
    // prev is seeded true so a trigger still physically held is not read as
    // a fresh rising edge (beginDrag's seed contract).
    m_dragInsertToggled = false;
    m_prevDragInsertHeld = true;
    // Escape during a PENDING (never-activated) snap drag must stay
    // cancelled for the rest of the gesture: dragStarted unconditionally
    // clears m_snapCancelled on a later promotion, so kill the pending
    // state itself rather than racing that flag. The interactive-drag mark
    // is re-set afterwards — the compositor move CONTINUES after Escape,
    // and clearPendingSnapDragState's mark clear would let a scroll retile
    // fight the still-live move; the endDrag mismatch-with-no-live-drag
    // sweep clears the mark when the gesture really ends.
    if (m_draggedWindowId.isEmpty() && !m_pendingSnapDragWindowId.isEmpty()) {
        const QString pendingWindow = m_pendingSnapDragWindowId;
        clearPendingSnapDragState();
        if (m_scrollEngine) {
            m_scrollEngine->setInteractiveDragWindow(pendingWindow);
        }
    }
    m_currentZoneId.clear();
    m_currentZoneScreenId.clear();
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
    m_snapAssistPendingActivity.clear();
}

void WindowDragAdaptor::handleWindowClosed(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    // If this window was being dragged, clean up drag state
    if (windowId == m_draggedWindowId) {
        cancelDragInsertIfActive();
        if (m_scrollEngine) {
            m_scrollEngine->setInteractiveDragWindow(QString());
        }
        // Drag-teardown: only drop the shared Escape grab if no picker / snap
        // assist still needs it. A layout picker can be open over an in-flight
        // drag, and it holds kCancelOverlayId — an unconditional release here
        // would leave that still-visible picker un-Escape-able.
        releaseCancelOverlayShortcutIfIdle();
        hideOverlayAndClearZoneState();

        // Hide zone selector if shown
        if (m_zoneSelectorShown && m_overlayService) {
            m_zoneSelectorShown = false;
            m_zoneSelectorShownOn.clear();
            m_overlayService->hideZoneSelector();
            m_overlayService->clearSelectedZone();
        }

        // Route through the shared teardown rather than a hand-rolled subset:
        // the per-drag set/clear pairs (overlay active-drag id, drop-indicator
        // overrides, selector exclusion, reorder-abandoned) live there, and a
        // second reset list here is exactly how they went missing. The Escape
        // release already ran conditionally above, so keep it.
        resetDragState(/*keepEscapeShortcut=*/true);
        m_currentDragPolicy = {};
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
        m_snapAssistPendingActivity.clear();
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
    // no longer expressible: rebind() with an empty sequence releases the
    // backend grab inside the Registry (keeping only the inert entry), and
    // the cancel path always uses the explicit unregister call below.
    m_shortcutRegistrar->unregisterAdhocShortcut(kCancelOverlayId);
}

void WindowDragAdaptor::releaseCancelOverlayShortcutIfIdle()
{
    // kCancelOverlayId is the SHARED Escape grab. It is bound on behalf of the
    // layout picker (shortcuts_wiring.cpp, layoutPickerRequested) and the snap-assist
    // phase (shortcuts_wiring.cpp, snapAssistShown), each of which releases it on its own
    // dismiss. The drag itself never binds it (the kwin-effect's keyboard grab
    // handles Escape during a drag). So any teardown path that drops the grab
    // must first confirm no OTHER consumer is still showing — otherwise it
    // tears the grab out from under a visible picker or snap assist, leaving
    // that overlay un-dismissable by Escape. This is the single canonical guard
    // every normal release site routes through. Two sites deliberately bypass
    // it with an unconditional release: cancelSnap() (the explicit
    // Escape-pressed teardown) and clearForCompositorReconnect() (force-release
    // when the compositor that held the grab is already gone).
    if (m_overlayService && (m_overlayService->isLayoutPickerVisible() || m_overlayService->isSnapAssistVisible())) {
        return;
    }
    unregisterCancelOverlayShortcut();
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
    // One batch, one backend flush: six per-id registrations would issue six
    // Portal BindShortcuts requests, each superseding the prior in-flight
    // Response (see IAdhocRegistrar::registerAdhocShortcuts).
    using AdhocBinding = PhosphorShortcutsIntegration::IAdhocRegistrar::AdhocBinding;
    m_shortcutRegistrar->registerAdhocShortcuts(QVector<AdhocBinding>{
        {kLayoutPickerLeftId, QKeySequence(Qt::Key_Left), PhosphorI18n::tr("Layout Picker: Move Left"),
         [moveCb] {
             moveCb(-1, 0);
         }},
        {kLayoutPickerRightId, QKeySequence(Qt::Key_Right), PhosphorI18n::tr("Layout Picker: Move Right"),
         [moveCb] {
             moveCb(1, 0);
         }},
        {kLayoutPickerUpId, QKeySequence(Qt::Key_Up), PhosphorI18n::tr("Layout Picker: Move Up"),
         [moveCb] {
             moveCb(0, -1);
         }},
        {kLayoutPickerDownId, QKeySequence(Qt::Key_Down), PhosphorI18n::tr("Layout Picker: Move Down"),
         [moveCb] {
             moveCb(0, 1);
         }},
        {kLayoutPickerReturnId, QKeySequence(Qt::Key_Return), PhosphorI18n::tr("Layout Picker: Confirm"), confirmCb},
        {kLayoutPickerEnterId, QKeySequence(Qt::Key_Enter), PhosphorI18n::tr("Layout Picker: Confirm (Numpad Enter)"),
         confirmCb},
    });
}

void WindowDragAdaptor::releaseLayoutPickerNavShortcuts()
{
    if (!m_shortcutRegistrar) {
        return;
    }
    m_shortcutRegistrar->unregisterAdhocShortcuts({QString(kLayoutPickerLeftId), QString(kLayoutPickerRightId),
                                                   QString(kLayoutPickerUpId), QString(kLayoutPickerDownId),
                                                   QString(kLayoutPickerReturnId), QString(kLayoutPickerEnterId)});
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
    // A live drag-insert preview belongs to the dead session too, and nothing
    // else here reaches it. hideOverlayAndClearZoneState() does NOT cover it:
    // OverlayService::hide() only touches slots carrying the overlay's own
    // physical-screen sentinel and never the ScrollDropIndicator slot, so
    // without this the drop-indicator rectangle stays PAINTED after the
    // reconnect with no drag left to dismiss it, and the engine keeps a
    // preview whose window stays structurally detached. This is the call the
    // "every preview-end path" contract on cancelDragInsertIfActive's
    // declaration asks every new teardown to add.
    cancelDragInsertIfActive();
    // The zone selector popup and its stored selection belong to the dead
    // session too: resetDragState clears neither, and with the compositor
    // gone no drag-end ever will. A surviving shown-flag pair plus the
    // service-side strip target would mis-size the next session's
    // keep-visible band from tick one and could commit a pick nobody made
    // that session.
    if (m_overlayService) {
        m_overlayService->hideZoneSelector();
        m_overlayService->clearSelectedZone();
    }
    m_zoneSelectorShown = false;
    m_zoneSelectorShownOn.clear();
    resetDragState(/*keepEscapeShortcut=*/false);
    // Reconnect tears EVERYTHING down: the compositor that held the grabs is
    // gone. Force-release the shared cancel-overlay grab unconditionally (not
    // via releaseCancelOverlayShortcutIfIdle) because a stale isLayoutPicker/
    // SnapAssistVisible flag from the dead session must not keep the grab
    // bound. Mirrors the unconditional releaseLayoutPickerNavShortcuts() below.
    unregisterCancelOverlayShortcut();
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
    m_snapAssistPendingActivity.clear();
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
    // Scoped to exactly one endDrag. Every teardown path routes through here,
    // so the flag cannot survive into the next drag and suppress a real commit.
    m_dragExternallyCancelled = false;
    // The dragged window's drop-indicator colour overrides die with the drag
    // that resolved them. Left set, the NEXT drag would paint the previous
    // window's colours until beginDrag re-resolved — and a drag of a window
    // with no rule would inherit them outright, since an empty resolve writes
    // an empty map only if it runs.
    if (m_overlayService) {
        m_overlayService->setScrollDropIndicatorWindowOverrides({});
        // The strip popup's card exclusion dies with the drag too — left
        // set, the next popup show (layout picker path, or a fresh drag of
        // a DIFFERENT window) would silently drop a bystander's card.
        m_overlayService->setActiveDragWindowId(QString());
    }
    if (!keepEscapeShortcut) {
        // Drag-end: drop the shared Escape grab only if no picker / snap assist
        // still needs it. The drag never bound the grab itself, so an
        // unconditional release here would tear it out from under a layout
        // picker left open across the drop.
        releaseCancelOverlayShortcutIfIdle();
    }
    m_draggedWindowId.clear();
    m_originalGeometry = QRect();
    m_currentZoneId.clear();
    m_currentZoneScreenId.clear();
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
    // Per-drag reorder latch: cleared here alongside the sibling latches so the
    // shared teardown (cancelSnap, clearForCompositorReconnect) leaves no stale
    // true for the next drag. endDrag also clears it directly before its own
    // early-return branches that don't route through resetDragState.
    m_dragReorderActive = false;
    m_dragReorderAbandoned = false;
    m_dragWindowExcludedFromSelector = false;
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
    // An assignment write can flip a screen's suppression verdict, so the
    // per-drag memo must not outlive it (#724). Cleared unconditionally: the
    // signal also fires between drags, where the memo is stale by definition.
    m_suppressMemoScreenId.clear();
    m_suppressMemoDesktop = 0;
    m_suppressMemoActivity.clear();
    m_suppressMemoValue = false;

    // Clear cached zone state when layout changes mid-drag to prevent stale geometry
    // This handles the case where user changes layout via hotkey/GUI while dragging
    // On next dragMoved(), fresh geometry will be calculated from the new layout
    if (!m_draggedWindowId.isEmpty()) {
        qCInfo(lcDbusWindow) << "Layout changed mid-drag, clearing cached zone state";
        m_currentZoneId.clear();
        m_currentZoneScreenId.clear();
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
    // Snap assist is already hidden by the time this fires, so the shared
    // Escape grab (kCancelOverlayId) can be dropped — unless the layout picker
    // is still up and holding the same grab (e.g. a snap-assist auto-dismiss
    // while a picker is open). releaseCancelOverlayShortcutIfIdle() is the
    // canonical cross-consumer guard; cancelSnap() handles Escape-time
    // precedence between overlays.
    releaseCancelOverlayShortcutIfIdle();
}

} // namespace PlasmaZones
