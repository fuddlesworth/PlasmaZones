// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The zone-selector GATE concern of WindowDragAdaptor: which screens speak
// the strip vocabulary (scrollSelectorScreen), whether this cursor tick
// shows / keeps / hides the drag popup (checkZoneSelectorTrigger), and the
// trigger-edge / keep-visible band math (isNearTriggerEdge). Split out of
// windowdragadaptor.cpp by concern when it crossed the file-size ceiling,
// matching the existing drag.cpp / drag_protocol.cpp / drop.cpp split.

#include "windowdragadaptor.h"

#include <QScreen>
#include <cmath>

#include "config/settings.h"
#include "core/interfaces/interfaces.h"
#include "core/types/zoneselectorlayout.h"
#include "core/utils/utils.h"

#include <PhosphorContext/ContextHandle.h>
#include <PhosphorContext/IContextResolver.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

bool WindowDragAdaptor::scrollSelectorScreen(const QString& screenId) const
{
    // Engine-direct twin of OverlayService::isStripSelectorScreen (which
    // routes through Daemon::dragInsertSelectorForScreen and the screen-mode
    // router). The two agree by construction: the router's modeFor is
    // live-set-first on the SAME isActiveOnScreen set consulted here, so the
    // trigger-edge sizing contract (config half here, card-count half via
    // the overlay service) cannot pair a scrolling config with a layout-mode
    // count. Keep either side's predicate change mirrored in the other.
    return m_scrollEngine && m_scrollEngine->isActiveOnScreen(screenId) && m_scrollEngine->providesDragInsertSelector();
}

void WindowDragAdaptor::checkZoneSelectorTrigger(int cursorX, int cursorY)
{
    if (!m_settings) {
        return;
    }
    // Deliberately NO toggles-off early-out here. A SetDragSelectorEnabled
    // context rule outranks the selector toggle in BOTH directions, so a
    // return taken before the cursor screen — and with it the rule's context —
    // is known would make the rule's force-ON half unreachable on a user who
    // has both global toggles off. The per-screen gate below is the single
    // place the toggle and the rule are folded.

    // A rule-excluded dragged window never sees the popup, on EITHER
    // variant. The probe (SnapEngine::isWindowExcluded) is mode-neutral by
    // design: the Exclude rules and the minimum-window-size floor live in
    // the shared placement-exclusion vocabulary, and the scroll side
    // enforces the same rules effect-side before a strip ever adopts such
    // a window — so on a strip screen too, offering targets would promise
    // an insert the placement policy refuses. Evaluated once at drag start.
    if (m_dragWindowExcludedFromSelector) {
        if (m_zoneSelectorShown) {
            m_zoneSelectorShown = false;
            m_zoneSelectorShownOn.clear();
            m_overlayService->hideZoneSelector();
        }
        return;
    }

    // Layout-suppressed screens get NO CLASSIC selector (#724). It is
    // tempting to carve them out — the selector is a "pick a layout" UI and
    // a committed pick assigns one — but the classic commit lives inside
    // dragStopped, which a LayoutSuppressed drag never reaches (its policy
    // is a dead drag that returns NoOp from endDrag). Showing the popup
    // there would discard the user's pick on release, so the coherent answer
    // is not to offer it. STRIP screens are exempt below: EngineOwnedScreen
    // outranks LayoutSuppressed in computeDragPolicy, so a strip screen's
    // drop commits through settleDragInsertPreviewAt regardless — and a
    // mode-only Scrolling pin legitimately has no assignment id, which the
    // suppress feature would otherwise read as "suppressed" and silently
    // kill the strip popup on that monitor.

    // Resolve effective (virtual-aware) screen ID for disabled-monitor check
    auto resolved = resolveScreenAt(QPointF(cursorX, cursorY));
    QString selectorScreenId = resolved.screenId;
    QScreen* screen = resolved.qscreen;

    // Per-screen enable pick: a strip-selector screen is governed by the
    // SCROLLING selector switch; everywhere else the snapping one applies,
    // together with the snapping master switch — the classic selector is a
    // "pick a zone" UI whose pick is committed inside dragStopped, which a
    // snapping-disabled drag never reaches (endDrag returns NoOp), so
    // offering it would discard the choice on release. The strip popup is
    // exempt from the snapping master switch: its drop commits through the
    // scroll engine, not a snap. Both arms hide an already-shown popup on a
    // disable flip mid-drag rather than bare-returning, so the popup cannot
    // strand when the cursor crosses onto a screen whose variant is off.
    const bool cursorOnStripSelectorScreen = scrollSelectorScreen(selectorScreenId);
    // ONE context snapshot for both the rule resolve here and the disable gate
    // below, so the (desktop, activity) axes the two read cannot decouple
    // across a virtual-desktop switch mid-tick — the split-snapshot hazard the
    // disable gate's own comment describes.
    const PhosphorContext::ContextHandle selectorCtx =
        m_contextResolver ? m_contextResolver->handleFor(selectorScreenId) : PhosphorContext::ContextHandle{};
    // A SetDragSelectorEnabled context rule layers over the SELECTOR toggle of
    // whichever variant the screen hosts, and outranks it in both directions:
    // false hides the popup here even with the toggle on, true offers it even
    // with the toggle off. That is the daemon's navigationOsdAllowed layering
    // for SetOsdEnabled (`rule.value_or(setting)`), applied verbatim. The
    // snapping master switch is NOT something the rule can override: it is one
    // of the "a pick here could not be committed" gates, like the exclusion
    // check above, so it stays ANDed onto the classic arm.
    const std::optional<bool> selectorRule = (m_contextResolver && m_layoutManager)
        ? m_layoutManager->resolveContextDragSelectorEnabled(selectorScreenId, selectorCtx.virtualDesktop,
                                                             selectorCtx.activity)
        : std::nullopt;
    const bool selectorToggle =
        cursorOnStripSelectorScreen ? m_settings->scrollingZoneSelectorEnabled() : m_settings->zoneSelectorEnabled();
    const bool variantEnabled =
        selectorRule.value_or(selectorToggle) && (cursorOnStripSelectorScreen || m_settings->snappingEnabled());
    if (!variantEnabled) {
        if (m_zoneSelectorShown) {
            m_zoneSelectorShown = false;
            m_zoneSelectorShownOn.clear();
            m_overlayService->hideZoneSelector();
        }
        return;
    }
    // Disable gate via single resolver snapshot, mirroring the Pass 4
    // pattern in drop.cpp's zone-selector and layout-activation gates.
    // The legacy `isContextDisabled(..., AssignmentEntry::Snapping, ...)` had
    // two issues: (a) split-snapshot race — the (desktop, activity) reads
    // were independent of the mode lookup, so a virtual-desktop switch
    // between them decoupled them; (b) hard-coded `Snapping` consulted the
    // wrong disable list when the screen's live mode was autotile. Reuse the
    // one `selectorCtx` snapshot taken above so all three axes agree (the rule
    // resolve reads the same one), override the mode on a copy via the layout
    // manager's per-(desktop, activity) lookup, then gate via `isDisabled`.
    // Suppression is evaluated on its own, NOT nested in the resolver-dependent
    // block below: a wired layout manager with no context resolver would
    // otherwise skip the whole gate and show the selector on a screen that
    // cannot host it.
    const bool selectorSuppressed = !cursorOnStripSelectorScreen && isActiveLayoutSuppressedForScreen(selectorScreenId);
    if (screen && (selectorSuppressed || (m_contextResolver && m_layoutManager))) {
        bool refuse = selectorSuppressed;
        if (!refuse && m_contextResolver && m_layoutManager) {
            PhosphorContext::ContextHandle modeCtx = selectorCtx;
            modeCtx.mode = m_layoutManager->modeForScreen(selectorScreenId, modeCtx.virtualDesktop, modeCtx.activity);
            refuse = m_contextResolver->isDisabled(modeCtx);
        }
        if (refuse) {
            if (m_zoneSelectorShown) {
                m_zoneSelectorShown = false;
                m_zoneSelectorShownOn.clear();
                m_overlayService->hideZoneSelector();
            }
            return;
        }
    }

    // An engine-owned cursor screen gets no zone selector: the autotile stack
    // or the scrolling strip owns placement there, so a manual drag-snap out of
    // the selector would fight it. dragMoved reaches this on EVERY bypass drag
    // (it sits outside prepareHandlerContext, which is where the other overlay
    // paths are suppressed), so without this gate edge-hovering during a drag on
    // a scrolling screen popped the selector on a screen the strip owns — and
    // endDrag's non-snap exits do not tear the popup down, leaving it stranded
    // with no further cursor ticks to hide it. Mirrors drop.cpp's useOverlayZone.
    const bool engineOwnsSelectorScreen = (m_autotileEngine && m_autotileEngine->isActiveOnScreen(selectorScreenId))
        || (m_scrollEngine && m_scrollEngine->isActiveOnScreen(selectorScreenId));
    // Capability carve-out: a scroll screen whose engine provides the
    // drag-insert selector renders the STRIP popup here — the popup is the
    // engine surface, not a competitor to it. Autotile screens (and scroll
    // screens without the capability) keep the historical force-hide.
    if (engineOwnsSelectorScreen && !cursorOnStripSelectorScreen) {
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
        // The selection is a SERVICE-level singleton, not per screen, and
        // showZoneSelector does not reset it. Carried across the hop it names
        // a zone in the OLD screen's layout, which the drop then applies —
        // the window lands in a zone belonging to a monitor the cursor left.
        // OverlayService::destroyWindowsForPhysicalScreen clears it for the
        // same reason when a VS reconfigure invalidates the old geometry.
        m_overlayService->clearSelectedZone();
    }

    if (nearEdge && !m_zoneSelectorShown) {
        // Show zone selector on the cursor's screen only. Latch the shown
        // flag from the service's actual visibility, not from intent:
        // showZoneSelector can silently refuse (no showable slot, recreation
        // pending), and a latched-true flag against a refused show blocks
        // the re-show arm above until the cursor leaves and re-enters the
        // edge, while updateSelectorPosition polls a popup that never was.
        m_overlayService->showZoneSelector(selectorScreenId);
        m_zoneSelectorShown = m_overlayService->isZoneSelectorVisible();
        m_zoneSelectorShownOn = m_zoneSelectorShown ? selectorScreenId : QString();
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

    // Use per-screen resolved config (per-screen override > global default).
    // Strip-selector screens resolve the scrolling variant — the same pick
    // OverlayService::updateZoneSelectorWindow makes, so the edge math and
    // the rendered bar agree on position/size.
    const ZoneSelectorConfig config = scrollSelectorScreen(effectiveId)
        ? m_settings->resolvedScrollingZoneSelectorConfig(effectiveId)
        : m_settings->resolvedZoneSelectorConfig(effectiveId);
    const int triggerDistance = config.triggerDistance;
    const auto position = static_cast<ZoneSelectorPosition>(config.position);

    // Use virtual screen geometry when available
    auto* smgr = m_screenManager;
    QRect vsGeom = smgr ? smgr->screenGeometry(effectiveId) : QRect();
    const QRect screenGeom = vsGeom.isValid() ? vsGeom : screen->geometry();

    // Use the selector card count (matches what the popup actually displays,
    // strip cards included) so the keep-visible zone matches the real popup
    // dimensions.
    const int layoutCount = m_overlayService ? m_overlayService->selectorCardCount(effectiveId)
                                             : (m_layoutManager ? m_layoutManager->layouts().size() : 0);
    // Variable-width strip cards: the fractions reproduce the real bar
    // width (empty on layout-mode screens and for an empty strip, where the
    // uniform cell math applies).
    const QList<qreal> stripFractions =
        m_overlayService ? m_overlayService->selectorStripFractions(effectiveId) : QList<qreal>();

    // Use shared layout computation (same code as OverlayService). The axis
    // rides along for the same reason it does there: a bar rect computed on
    // the horizontal assumption is the transpose of the popup actually
    // painted, so the keep-visible band would stop matching what the cursor
    // is over and the popup would hide while it is still under the pointer.
    const bool stripVerticalAxis = m_overlayService ? m_overlayService->selectorStripVerticalAxis(effectiveId) : false;
    const ZoneSelectorLayout selectorLayout =
        computeZoneSelectorLayout(config, screenGeom, layoutCount, stripFractions, stripVerticalAxis);
    const int barHeight = selectorLayout.barHeight;
    const int barWidth = selectorLayout.barWidth;

    if (m_zoneSelectorShown) {
        // Keep-visible: the UNION of the pre-show trigger region (computed
        // by the fall-through below) and the popup's REAL rect (barWidth x
        // barHeight anchored at the configured position, the same anchoring
        // ZoneSelectorContent's selectorPosition state applies), inflated
        // for hysteresis. Both halves are load-bearing:
        //  - The bar rect is what lets the popup HIDE again once the cursor
        //    leaves it. The old single-axis distance test
        //    (distance-from-edge <= barWidth) degenerated on a strip popup,
        //    whose bar spans nearly the whole work area, so
        //    barWidth-of-an-axis covered the entire screen and the popup
        //    could never hide for Left / Right / Center once shown.
        //  - The pre-show region keeps the shown region a SUPERSET of the
        //    region that triggers the show. Without it, a cursor sitting in
        //    the full-width edge band but off to the side of a centred bar
        //    would show on one tick and hide on the next, oscillating per
        //    cursor tick for the four single-edge positions.
        // The inflation adds kQmlMarginSlack on top of triggerDistance: the
        // QML clamps the container margins to ceil(gridUnit * 1.25) (~23)
        // while this layout struct carries 10, so the rendered bar sits up
        // to ~13 px further from the edge than barRect claims.
        constexpr int kQmlMarginSlack = 16;
        const int inflate = triggerDistance + kQmlMarginSlack;
        const int cx = screenGeom.x() + (screenGeom.width() - barWidth) / 2;
        const int cy = screenGeom.y() + (screenGeom.height() - barHeight) / 2;
        QRect barRect(0, 0, barWidth, barHeight);
        switch (position) {
        case ZoneSelectorPosition::TopLeft:
            barRect.moveTo(screenGeom.left(), screenGeom.top());
            break;
        case ZoneSelectorPosition::Top:
            barRect.moveTo(cx, screenGeom.top());
            break;
        case ZoneSelectorPosition::TopRight:
            barRect.moveTo(screenGeom.right() - barWidth + 1, screenGeom.top());
            break;
        case ZoneSelectorPosition::Left:
            barRect.moveTo(screenGeom.left(), cy);
            break;
        case ZoneSelectorPosition::Center:
            barRect.moveTo(cx, cy);
            break;
        case ZoneSelectorPosition::Right:
            barRect.moveTo(screenGeom.right() - barWidth + 1, cy);
            break;
        case ZoneSelectorPosition::BottomLeft:
            barRect.moveTo(screenGeom.left(), screenGeom.bottom() - barHeight + 1);
            break;
        case ZoneSelectorPosition::Bottom:
            barRect.moveTo(cx, screenGeom.bottom() - barHeight + 1);
            break;
        case ZoneSelectorPosition::BottomRight:
            barRect.moveTo(screenGeom.right() - barWidth + 1, screenGeom.bottom() - barHeight + 1);
            break;
        }
        if (barRect.adjusted(-inflate, -inflate, inflate, inflate).contains(cursorX, cursorY)) {
            return true;
        }
        // Fall through to the pre-show predicate for the union.
    }

    const int distanceFromTop = cursorY - screenGeom.top();
    const int distanceFromBottom = screenGeom.bottom() - cursorY;
    const int distanceFromLeft = cursorX - screenGeom.left();
    const int distanceFromRight = screenGeom.right() - cursorX;

    const bool nearTop = distanceFromTop >= 0 && distanceFromTop <= triggerDistance;
    const bool nearBottom = distanceFromBottom >= 0 && distanceFromBottom <= triggerDistance;
    const bool nearLeft = distanceFromLeft >= 0 && distanceFromLeft <= triggerDistance;
    const bool nearRight = distanceFromRight >= 0 && distanceFromRight <= triggerDistance;

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
        // Trigger when cursor is within triggerDistance of screen center.
        const int centerX = screenGeom.x() + screenGeom.width() / 2;
        const int centerY = screenGeom.y() + screenGeom.height() / 2;
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

} // namespace PlasmaZones
