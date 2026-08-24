// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <input.h>

#include <QPointF>
#include <QPointer>
#include <QSet>

namespace KWin {
class Window;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Makes the invisible overhang of scrolling-strip straddler windows
 *        non-interactive.
 *
 * A strip edge column keeps its TRUE rect while its drawing is clipped at the
 * monitor boundary (paint_pipeline.cpp's boundary clip). KWin's input
 * hit-testing knows nothing of that clip, so pointer events on the adjacent
 * output would focus and click the invisible window. This filter consumes
 * pointer button, axis, and touch events whose would-be target is a straddler
 * hit only through its clipped-away overhang, and on the press edge focuses
 * the topmost window the user actually sees at that position instead.
 *
 * Weight: InputFilterOrder::Popup — strictly below Decoration and
 * WindowAction, so the invisible window's decoration handling and
 * click-to-focus never see the event; above the compositor-global filters
 * (lock screen, global shortcuts), which must keep winning. Relative order
 * against KWin's own popup filter at the same weight is unspecified, and both
 * outcomes are benign (an open popup either dismisses first or stays open
 * while focus moves); an equal weight with Decoration would instead risk the
 * invisible decoration swallowing the click, hence not Decoration.
 *
 * Uses KWin private API (input.h) — the same version-locked ABI contract the
 * effect already accepts for Window / Workspace / scene items. Deleting the
 * filter uninstalls it (InputEventFilter dtor contract).
 *
 * This filter ALSO owns part of the pointer input for the compositor-drawn
 * scrolling TAB INDICATORS (ScrollTabIndicatorPainter): pointerMotion hovers
 * the pill under the cursor and pointerButton / touchDown activate it. The
 * pills are not windows, so no window-level input path sees them; a filter
 * at this weight is the one place the press can be claimed before
 * click-to-focus hands it to the column underneath. The OTHER part is the
 * effect's own pointer hooks: once the hover has armed KWin's mouse
 * interception (for the pointing-hand cursor), KWin's Effects filter runs
 * BEFORE this one and consumes every pointer event for the effect, so a
 * pointer press over a hovered pill reaches PlasmaZonesEffect::pointerButton
 * and never this filter. This filter's pill press branch therefore covers
 * touch (no hover, no interception) and a press that arrives with no prior
 * motion. Hover updates gate on "some payload exists"
 * (TilingHandler::updateScrollTabHover), so a desktop with no tabbed column
 * pays one branch per motion.
 *
 * DOCUMENTED GAPS. Two input classes still reach the invisible overhang:
 *  - Pointer FOCUS (enter/leave, focus-follows-mouse). KWin recomputes pointer
 *    focus inside the device handler before filters run, so no filter can
 *    retarget it. Cosmetic: no button, wheel or touch lands there. (Pointer
 *    MOTION itself is observable — pointerMotion is how the pills hover.)
 *  - TABLET tool tip/axis events. The tablet hooks are deliberately not
 *    overridden; a stylus is a pointing device the strip has no story for yet,
 *    and guessing one here would be worse than the honest gap. Named so the
 *    omission reads as a decision rather than an oversight.
 *
 * The press that retargets focus is CONSUMED, not replayed, so activating a
 * control on the newly-focused window costs a second click. Replaying it would
 * mean synthesising an event against a window whose surface never saw the
 * press; the one-click cost is the deliberate trade. Named here for the same
 * reason as the gaps above.
 */
class ScrollOverhangInputFilter : public KWin::InputEventFilter
{
public:
    explicit ScrollOverhangInputFilter(PlasmaZonesEffect* effect);
    ~ScrollOverhangInputFilter() override = default;

    bool pointerMotion(KWin::PointerMotionEvent* event) override;
    bool pointerButton(KWin::PointerButtonEvent* event) override;
    bool pointerAxis(KWin::PointerAxisEvent* event) override;
    bool touchDown(KWin::TouchDownEvent* event) override;
    bool touchMotion(KWin::TouchMotionEvent* event) override;
    bool touchUp(KWin::TouchUpEvent* event) override;
    bool touchCancel() override;

private:
    /// The straddler whose clipped-away overhang covers @p pos, or null when
    /// the event should pass through untouched (fast path: no scrolling
    /// screens at all).
    KWin::Window* overhangTargetAt(const QPointF& pos) const;
    /// Focus + raise the topmost visible window at @p pos that is not
    /// @p straddler and not itself hit through a clipped overhang.
    void focusVisibleWindowAt(const QPointF& pos, KWin::Window* straddler);

    /// Rescale @p event's delta/deltaV120 in place when a ScrollFactor rule
    /// matches the window that will RECEIVE it. Returns nothing — the caller
    /// always forwards the (possibly mutated) event; scaling never consumes.
    ///
    /// The mutated event continues down the filter chain, so the Decoration,
    /// WindowAction and XWayland filters below this one see the scaled delta
    /// as well as the forwarding filter that delivers it to the client. That
    /// is the intended reading of the action: "this app's scroll moves this
    /// much per notch" applies wherever that app's scroll is consumed,
    /// including its own title bar. Filters ABOVE this one (global shortcuts,
    /// interactive move/resize) are ordered earlier and always see the raw
    /// delta. The strip's own wheel chords are matched in pointerAxis BEFORE
    /// this runs and return early, so a chord-driven strip move is never
    /// scaled by the underlying app's rule either.
    void applyScrollFactor(KWin::PointerAxisEvent* event);

    /// Forget the window the v120 residues belong to and zero them. Called on
    /// every axis tick that does not scale (no rule, no target, consumed
    /// overhang) so one window's fractional remainder can never be applied to
    /// the next stream the filter does scale.
    void resetScrollFactorStream();

    PlasmaZonesEffect* m_effect;
    /// Fractional deltaV120 remainder carried between ticks so a factor
    /// below 1 still accumulates into full v120 steps instead of rounding
    /// every tick to zero. One residue per axis orientation, reset when the
    /// pointer-FOCUS window changes, when the stream ends, and on a direction
    /// reversal within one stream — so neither another window's remainder nor
    /// an abandoned one from the opposite direction ever eats a notch.
    qreal m_v120ResidueVertical = 0.0;
    qreal m_v120ResidueHorizontal = 0.0;
    /// The window the residues belong to. QPointer, not a raw pointer: the
    /// comparison is by identity, and a destroyed KWin::Window's address can
    /// be reused by the next one allocated — a raw pointer would then compare
    /// EQUAL to an unrelated window and hand it the dead window's fractional
    /// remainder instead of resetting the stream. A QPointer reads null after
    /// the window dies, so the reset fires.
    QPointer<KWin::Window> m_scrollFactorWindow;
    /// Buttons whose PRESS this filter consumed — their releases are consumed
    /// too, even if the cursor has left the overhang, so the client never
    /// sees an unpaired release.
    Qt::MouseButtons m_consumedButtons = Qt::NoButton;
    /// Touch ids whose DOWN this filter consumed, for the matching ups.
    QSet<qint32> m_consumedTouchIds;
};

} // namespace PlasmaZones
