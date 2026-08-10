// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input_filter.h"
#include "plasmazoneseffect.h"
#include "tilinghandler/tilinghandler.h"

#include <input_event.h>
#include <window.h>
#include <workspace.h>

#include <QLoggingCategory>

#include <cmath>
#include <optional>

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
    // (m_effect is the owning effect passed as `this` from its own
    // constructor, so it is an ownership invariant rather than a nullable
    // borrow — the sites below deref it unguarded for the same reason.)
    TilingHandler* tiling = m_effect->tilingHandler();
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
            || !cand->isOnCurrentDesktop() || !cand->isOnCurrentActivity() || !cand->hitTest(pos)) {
            continue;
        }
        // Structurally non-activatable surfaces are not what the user meant to
        // click. Without this the first hit over a bare region of the
        // neighbouring output is the desktop (wallpaper) window, and the filter
        // would activate and raise it; docks and unmanaged surfaces are equally
        // wrong targets for a click-to-focus retarget.
        if (cand->isDesktop() || cand->isDock() || cand->isUnmanaged() || !cand->wantsInput()) {
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
    // A fresh press proves the previous cycle for this button is over. Filters
    // ordered before this one (lock screen, drag-and-drop, tab box, global
    // shortcuts, interactive move/resize) can consume a release we were
    // waiting for; without this the bit stayed set for the rest of the session
    // and the next unrelated release of that button was swallowed, leaving a
    // client with a press it never saw released.
    m_consumedButtons &= ~event->button;
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
    if (overhangTargetAt(event->position) != nullptr) {
        return true;
    }
    // ScrollFactor rule: rescale the event in place and pass it on. Ordering
    // note: this runs at Popup weight, after the global-shortcut filter, so
    // the Meta+wheel strip binding is consumed before it can be scaled.
    applyScrollFactor(event);
    return false;
}

void ScrollOverhangInputFilter::applyScrollFactor(KWin::PointerAxisEvent* event)
{
    // Fast path: no enabled ScrollFactor rule in the session. Checked BEFORE
    // the toplevel hit test so the no-rules case pays two pointer reads per
    // tick, matching the effect's other has*Rules gates.
    if (!m_effect->m_shaderManager.hasScrollFactorRules()) {
        m_scrollFactorWindow = nullptr;
        return;
    }
    KWin::Window* target = KWin::input()->findToplevel(event->position);
    if (!target || target->isDeleted()) {
        m_scrollFactorWindow = nullptr;
        return;
    }
    if (target != m_scrollFactorWindow) {
        // Fresh stream: another window's fractional remainder must not leak
        // into this one's first tick.
        m_v120ResidueVertical = 0.0;
        m_v120ResidueHorizontal = 0.0;
        m_scrollFactorWindow = target;
    }
    const std::optional<qreal> factor = m_effect->ruleScrollFactorFor(target->effectWindow());
    if (!factor || qFuzzyCompare(*factor, 1.0)) {
        return;
    }
    // In-place mutation is the API's natural expression: returning false
    // hands the mutated struct to the remaining filters, ending at the
    // forwarding filter that delivers to the client. The smooth delta scales
    // directly; the discrete deltaV120 carries a per-orientation fractional
    // residue so a factor below 1 accumulates into full steps instead of
    // truncating every notch to zero.
    event->delta *= *factor;
    if (event->deltaV120 != 0) {
        qreal& residue = event->orientation == Qt::Vertical ? m_v120ResidueVertical : m_v120ResidueHorizontal;
        const qreal scaled = event->deltaV120 * *factor + residue;
        const qint32 emitted = qint32(std::trunc(scaled));
        residue = scaled - emitted;
        event->deltaV120 = emitted;
    }
}

bool ScrollOverhangInputFilter::touchDown(KWin::TouchDownEvent* event)
{
    if (!event) {
        return false;
    }
    KWin::Window* target = overhangTargetAt(event->pos);
    if (!target) {
        // libinput reuses small integer touch ids, so a stale entry would
        // swallow this sequence's up. A down we do NOT consume proves the id
        // is free again.
        m_consumedTouchIds.remove(event->id);
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

bool ScrollOverhangInputFilter::touchCancel()
{
    // A cancelled sequence delivers no touchUp, so every pending id would
    // otherwise leak. With libinput reusing small integer ids that stale entry
    // later swallows an unrelated touchUp, leaving the client with a down and
    // no up for the rest of the session. Never consume the cancel itself —
    // every other filter still needs it.
    m_consumedTouchIds.clear();
    return false;
}

} // namespace PlasmaZones
