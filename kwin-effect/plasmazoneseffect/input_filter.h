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

    PlasmaZonesEffect* m_effect;
    /// Buttons whose PRESS this filter consumed — their releases are consumed
    /// too, even if the cursor has left the overhang, so the client never
    /// sees an unpaired release.
    Qt::MouseButtons m_consumedButtons;
    /// Touch ids whose DOWN this filter consumed, for the matching ups.
    QSet<qint32> m_consumedTouchIds;
};

} // namespace PlasmaZones
