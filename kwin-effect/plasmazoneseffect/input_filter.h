// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <input.h>

#include <QPointF>
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
 * DOCUMENTED GAPS. Two input classes still reach the invisible surface:
 *  - Pointer HOVER (enter/leave, focus-follows-mouse). KWin recomputes pointer
 *    focus inside the device handler before filters run, so no filter can
 *    intercept it. Cosmetic: no button, wheel or touch lands there.
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

    bool pointerButton(KWin::PointerButtonEvent* event) override;
    bool pointerAxis(KWin::PointerAxisEvent* event) override;
    bool touchDown(KWin::TouchDownEvent* event) override;
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
    /// matches the hovered window. Returns nothing — the caller always
    /// forwards the (possibly mutated) event; scaling never consumes.
    void applyScrollFactor(KWin::PointerAxisEvent* event);

    PlasmaZonesEffect* m_effect;
    /// Fractional deltaV120 remainder carried between ticks so a factor
    /// below 1 still accumulates into full v120 steps instead of rounding
    /// every tick to zero. One residue per axis orientation, reset when the
    /// hovered window changes so one window's remainder never leaks into
    /// another's stream.
    qreal m_v120ResidueVertical = 0.0;
    qreal m_v120ResidueHorizontal = 0.0;
    /// The window the residues belong to (by pointer identity; only ever
    /// compared, never dereferenced).
    const KWin::Window* m_scrollFactorWindow = nullptr;
    /// Buttons whose PRESS this filter consumed — their releases are consumed
    /// too, even if the cursor has left the overhang, so the client never
    /// sees an unpaired release.
    Qt::MouseButtons m_consumedButtons = Qt::NoButton;
    /// Touch ids whose DOWN this filter consumed, for the matching ups.
    QSet<qint32> m_consumedTouchIds;
};

} // namespace PlasmaZones
