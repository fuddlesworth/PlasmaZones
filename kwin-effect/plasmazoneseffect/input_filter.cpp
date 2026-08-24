// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input_filter.h"
#include "plasmazoneseffect.h"
#include "tilinghandler/tilinghandler.h"
#include "compositor/effectlogging.h"

#include <input_event.h>
#include <pointer_input.h>
#include <window.h>
#include <workspace.h>

#include <QLoggingCategory>

#include <cmath>
#include <optional>

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

bool ScrollOverhangInputFilter::pointerMotion(KWin::PointerMotionEvent* event)
{
    if (!event) {
        return false;
    }
    // Hover for the compositor-drawn tab pills. Never consumes: motion must
    // keep flowing to whatever is under the cursor (the column, a dialog), the
    // pill only lights up on the way past. The handler gates on "some screen
    // has a payload" so a desktop with no tabbed column pays one branch.
    if (TilingHandler* tiling = m_effect->tilingHandler()) {
        tiling->updateScrollTabHover(event->position);
    }
    return false;
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
    // A press on a compositor-drawn tab pill activates that tab. Tested
    // BEFORE the overhang: the pills sit over the column they label, and a
    // pill at a straddler's visible edge must win over retargeting the click
    // to the column. Reached only when the pill interception is NOT held
    // (touch, or a press with no prior motion — see the header): a press
    // over a HOVERED pill is consumed by KWin's Effects filter ahead of this
    // one and lands on PlasmaZonesEffect::pointerButton instead. Left button
    // only here; on this path a right or middle press does fall through to
    // whatever is under it. The press is consumed (and its release paired
    // below), so the column never sees a click it did not get.
    if (event->button == Qt::LeftButton) {
        if (TilingHandler* tiling = m_effect->tilingHandler(); tiling && tiling->activateScrollTabAt(event->position)) {
            m_consumedButtons |= event->button;
            qCDebug(lcEffect) << "Overhang input filter: consumed press on tab pill at" << event->position;
            return true;
        }
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
    // Wheel chords FIRST, before the overhang test. The chord is a global
    // gesture over the strip, and the overhang is part of that strip: an
    // event landing on a straddler's clipped-away region is exactly the case
    // where the user is scrolling "the strip" rather than any one window, so
    // deferring to the overhang branch would make the chord dead in a band at
    // the screen edge.
    //
    // A matched chord is CONSUMED. This filter sits at Popup weight, below
    // KWin's global-shortcut filter, so a chord the user has also bound to a
    // compositor axis shortcut loses to that binding rather than to us; the
    // chord we do claim never reaches the app underneath, which would
    // otherwise scroll its own content at the same time as the strip.
    // event->modifiers, NOT modifiersRelevantForGlobalShortcuts, and that is
    // deliberate rather than an oversight. Verified against KWin 6.x source:
    // GlobalShortcutFilter::pointerAxis passes `event->modifiers` straight to
    // processAxis, so this is the same field the chords were matched against
    // while they were compositor axis shortcuts. Using the other one here
    // would be a behaviour CHANGE, not a fidelity fix.
    if (TilingHandler* tiling = m_effect->tilingHandler(); tiling
        && tiling->handleWheelChord(event->delta, event->deltaV120, event->orientation, event->modifiers,
                                    event->buttons)) {
        // A claimed chord ends whatever ScrollFactor stream was running: the
        // client never sees this tick, so its fractional remainder must not
        // survive to be applied to the next tick it does see.
        resetScrollFactorStream();
        return true;
    }
    // Scrolling over the invisible overhang must not reach the straddler;
    // consuming (rather than retargeting) matches how the region reads
    // visually — inert until clicked.
    if (overhangTargetAt(event->position) != nullptr) {
        // The consumed stream ends here, so drop the residues with it: the
        // next tick this filter DOES scale belongs to a different window (or
        // to this one after a gap), and either way it must start clean rather
        // than inherit a remainder from a stream the client never saw.
        resetScrollFactorStream();
        return true;
    }
    // ScrollFactor rule: rescale the event in place and pass it on. Only
    // events no strip chord claimed reach here (the branch above returns),
    // so a chord-driven strip move is never scaled by an app's rule.
    applyScrollFactor(event);
    return false;
}

void ScrollOverhangInputFilter::resetScrollFactorStream()
{
    m_scrollFactorWindow = nullptr;
    m_v120ResidueVertical = 0.0;
    m_v120ResidueHorizontal = 0.0;
}

void ScrollOverhangInputFilter::applyScrollFactor(KWin::PointerAxisEvent* event)
{
    // Fast path: no enabled ScrollFactor rule in the session. Checked BEFORE
    // any window lookup so the no-rules case pays two pointer reads per tick,
    // matching the effect's other has*Rules gates.
    if (!m_effect->m_shaderManager.hasScrollFactorRules()) {
        resetScrollFactorStream();
        return;
    }
    // POINTER FOCUS, not a positional hit test. Two reasons, and they point
    // the same way: the focused window is the one the forwarding filter will
    // actually deliver this event to (during an implicit grab — a button held
    // while the cursor leaves the window — that is NOT the window under the
    // cursor, so a positional lookup would scale by the wrong app's rule or
    // by none at all), and it costs no second hit test on top of the one
    // overhangTargetAt already performed for this same event.
    KWin::PointerInputRedirection* pointer = KWin::input()->pointer();
    KWin::Window* target = pointer ? pointer->focus() : nullptr;
    if (!target || target->isDeleted()) {
        resetScrollFactorStream();
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
        // A reversal within the same window and orientation abandons the
        // remainder it was accumulating: carrying it across the turn subtracts
        // from the first notch of the new direction (and, with a small factor,
        // can swallow it outright), so the wheel feels like it sticks when the
        // user changes their mind mid-scroll.
        if ((event->deltaV120 > 0 && residue < 0.0) || (event->deltaV120 < 0 && residue > 0.0)) {
            residue = 0.0;
        }
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
    // Same pill-first rule as pointerButton: a tap on a tab pill activates
    // it and the sequence is consumed through touchUp.
    if (TilingHandler* tiling = m_effect->tilingHandler(); tiling && tiling->activateScrollTabAt(event->pos)) {
        m_consumedTouchIds.insert(event->id);
        qCDebug(lcEffect) << "Overhang input filter: consumed touch on tab pill at" << event->pos;
        return true;
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

bool ScrollOverhangInputFilter::touchMotion(KWin::TouchMotionEvent* event)
{
    if (!event) {
        return false;
    }
    // A sequence whose down this filter consumed never reached the seat, so
    // its motion must not either: KWin's seat discards a motion for an id it
    // has no down for, but warns on every such event, and the client below
    // would otherwise see the rest of a gesture whose start it never got.
    return m_consumedTouchIds.contains(event->id);
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
