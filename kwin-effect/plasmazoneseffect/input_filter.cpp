// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input_filter.h"
#include "plasmazoneseffect.h"
#include "tilinghandler/tilinghandler.h"

#include <input_event.h>
#include <window.h>
#include <workspace.h>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace PlasmaZones {

ScrollOverhangInputFilter::ScrollOverhangInputFilter(PlasmaZonesEffect* effect)
    : KWin::InputEventFilter(KWin::InputFilterOrder::Popup)
    , m_effect(effect)
{
    KWin::input()->installInputEventFilter(this);
}

KWin::Window* ScrollOverhangInputFilter::overhangTargetAt(const QPointF& pos) const
{
    // Fast path for the overwhelmingly common case: no scrolling screens in
    // the session means no straddlers can exist, so never pay for a hit test.
    TilingHandler* tiling = m_effect ? m_effect->tilingHandler() : nullptr;
    if (!tiling || !tiling->hasScrollingScreens()) {
        return nullptr;
    }
    KWin::Window* target = KWin::input()->findToplevel(pos);
    if (!target) {
        return nullptr;
    }
    // The paint clip's own predicate: valid only for a scroll-managed,
    // non-floating, non-user-move straddler. A point outside the clip is on
    // the invisible overhang — exactly the pixels paintWindow never draws.
    const QRect clip = m_effect->scrollClipGeometryFor(target->effectWindow());
    if (!clip.isValid() || clip.contains(pos.toPoint())) {
        return nullptr;
    }
    return target;
}

void ScrollOverhangInputFilter::focusVisibleWindowAt(const QPointF& pos, KWin::Window* straddler)
{
    KWin::Workspace* ws = KWin::workspace();
    if (!ws) {
        return;
    }
    // stackingOrder() is bottom-to-top; walk from the top so the first hit is
    // what the user visually clicked. Skip other straddlers hit through their
    // own clipped overhang — they are equally invisible at this position.
    const QList<KWin::Window*> order = ws->stackingOrder();
    for (auto it = order.crbegin(); it != order.crend(); ++it) {
        KWin::Window* cand = *it;
        if (!cand || cand == straddler || cand->isDeleted() || cand->isMinimized() || cand->isHidden()
            || !cand->isOnCurrentDesktop() || !cand->hitTest(pos)) {
            continue;
        }
        const QRect clip = m_effect->scrollClipGeometryFor(cand->effectWindow());
        if (clip.isValid() && !clip.contains(pos.toPoint())) {
            continue;
        }
        ws->activateWindow(cand);
        ws->raiseWindow(cand);
        return;
    }
}

bool ScrollOverhangInputFilter::pointerButton(KWin::PointerButtonEvent* event)
{
    if (!event) {
        return false;
    }
    if (event->state == KWin::PointerButtonState::Released) {
        // Pair with the consumed press regardless of where the cursor is now
        // — the client below never saw the press, so it must not see the
        // release either.
        if (m_consumedButtons & event->button) {
            m_consumedButtons &= ~event->button;
            return true;
        }
        return false;
    }
    KWin::Window* target = overhangTargetAt(event->position);
    if (!target) {
        return false;
    }
    m_consumedButtons |= event->button;
    qCDebug(lcEffect) << "Overhang input filter: consumed press on" << m_effect->getWindowId(target->effectWindow())
                      << "at" << event->position;
    focusVisibleWindowAt(event->position, target);
    return true;
}

bool ScrollOverhangInputFilter::pointerAxis(KWin::PointerAxisEvent* event)
{
    if (!event) {
        return false;
    }
    // Scrolling over the invisible overhang must not reach the straddler;
    // consuming (rather than retargeting) matches how the region reads
    // visually — inert until clicked.
    return overhangTargetAt(event->position) != nullptr;
}

bool ScrollOverhangInputFilter::touchDown(KWin::TouchDownEvent* event)
{
    if (!event) {
        return false;
    }
    KWin::Window* target = overhangTargetAt(event->pos);
    if (!target) {
        return false;
    }
    m_consumedTouchIds.insert(event->id);
    qCDebug(lcEffect) << "Overhang input filter: consumed touch on" << m_effect->getWindowId(target->effectWindow())
                      << "at" << event->pos;
    focusVisibleWindowAt(event->pos, target);
    return true;
}

bool ScrollOverhangInputFilter::touchUp(KWin::TouchUpEvent* event)
{
    if (!event) {
        return false;
    }
    return m_consumedTouchIds.remove(event->id);
}

} // namespace PlasmaZones
