// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-session screen and behaviour state for TilingHandler: which screens the
// daemon manages, which of those run the scrolling engine, the active layout
// per screen, the daemon-resolved per-screen scroll behaviours (focus-follows-
// mouse, straddler crop, strip axis), the focus-follows-mouse and wheel-focus
// settings, and the wheel shortcut registration those settings drive.
//
// What unites this file is that every member here is published BY the daemon
// and consumed by the effect, so all of it is per-session and all of it is
// dropped on daemon loss. That is also the shared hazard: the managed set, the
// scrolling set and the active layouts arrive on independent D-Bus signals with
// no ordering guarantee between them, so any predicate reading two of them can
// see a transient disagreement. Where that matters the reader takes the raw set
// deliberately rather than an intersection.
//
// Three concerns were split out of this file: monocle and windowed-fullscreen
// ownership (windowedfullscreen.cpp), pre-tile geometry (pretilegeometry.cpp),
// and eligibility plus float shed (floatcleanup.cpp).

#include "tilinghandler.h"
#include "compositor/scrollbehaviourparse.h"
#include "compositor/stripviewanimator.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>

#include <QAction>
#include <QDBusVariant>
#include <QHash>
#include <QList>
#include <QLoggingCategory>
#include <QMetaType>
#include <QVariant>

#include <optional>

namespace PlasmaZones {

void TilingHandler::clearTiledTracking()
{
    // Bookkeeping only. Physical title-bar restores are the
    // DecorationManager's job — teardown callers pair this with
    // DecorationManager::restoreAll().
    m_border.tiledWindowsByScreen.clear();
    // The screen set belongs to the daemon session that published it. Both
    // callers (daemon loss, effect teardown) mean that session is gone —
    // keeping the set let stale membership answer isAutotileScreen until the
    // next bringup reply, and left the bringup's fresh-set replacement with
    // no removed-screen delta to act on.
    //
    // This write takes the named teardown exemption from the
    // scrollingScreenIntersection snapshot/compare/invalidate contract (see
    // the header) — it is valid only while every caller is a teardown.
    m_managedScreens.clear();
}

void TilingHandler::setFocusFollowsMouse(bool enabled)
{
    m_focusFollowsMouse = enabled;
    if (ffmOffEverywhere()) {
        // handleCursorMoved bails before the suppression latch while FFM is
        // off everywhere, so a latch set just before the setting was turned
        // off would survive with a long-stale anchor and swallow the first
        // move after it is turned back on. The latch is shared across both
        // modes, so it clears only when NO screen can focus-follow — which is
        // the ffmOffEverywhere predicate, per-screen scrolling membership
        // included.
        m_ffmSuppressPending = false;
    }
}

void TilingHandler::setScrollingFocusFollowsMouse(bool enabled)
{
    m_scrollingFocusFollowsMouse = enabled;
    if (ffmOffEverywhere()) {
        // Same shared-latch reasoning as setFocusFollowsMouse.
        m_ffmSuppressPending = false;
    }
}

void TilingHandler::setWheelFocusEnabled(bool enabled)
{
    if (m_wheelFocusEnabled == enabled) {
        return;
    }
    m_wheelFocusEnabled = enabled;
    // Re-evaluate registration immediately: the flag is part of the want
    // predicate, and no screen-set change will fire on a settings save.
    updateScrollWheelShortcuts();
}

void TilingHandler::setWheelFocusInverted(bool inverted)
{
    if (m_wheelFocusInverted == inverted) {
        return;
    }
    m_wheelFocusInverted = inverted;
    // No re-registration pass, unlike setWheelFocusEnabled: the flag is not
    // part of the want predicate, only read at trigger time to pick a
    // direction.
}

bool TilingHandler::isManagedScreen(const QString& screenId) const
{
    return m_managedScreens.contains(screenId);
}

void TilingHandler::slotScrollEffectBehaviourChanged(const QVariantMap& behaviour)
{
    applyScrollEffectBehaviour(behaviour);
}

void TilingHandler::applyScrollEffectBehaviour(const QVariantMap& behaviour)
{
    // Any apply voids in-flight property replies, identical map or not — the
    // writer is always newer than a reply dispatched earlier (the
    // setScrollingScreens shape; see the m_scrollEffectBehaviourGeneration
    // doc). Before the change gate for the same reason the twin bumps first.
    ++m_scrollEffectBehaviourGeneration;
    // The daemon publishes the whole map every time (never a delta), so a
    // straight replace is correct even if a previous signal was missed.
    //
    // Boundary validation, mirroring slotActiveLayoutsChanged: this map
    // crosses D-Bus from another process, and every half of it decides
    // compositor behaviour (focus stealing, forced composition, which way the
    // strip slides). An a{sv} value arrives
    // either already demarshalled (the property Get path, which qdbus_cast
    // unwraps) or still wrapped in a QDBusVariant (a signal delivered without
    // a registered argument type) — unwrap one level before the type test, or
    // every live update silently clears all three sets. Empty screen ids are
    // dropped: no window resolves to one, and they only defeat the change
    // gate below. A wire regression is warned about rather than being
    // indistinguishable from a legitimately-off session.
    //
    // The return is OPTIONAL so the three keys can take different directions
    // on a MALFORMED value (as opposed to an absent one, which is a legitimate
    // publish and reads as an empty set for all three). Empty is the safe
    // direction for focus-follows-mouse and crop — both are behaviours that
    // simply stay off — but it is the WRONG direction for the axis, where it
    // silently re-lays every vertical strip horizontally on the next tile
    // batch. The axis arm below keeps the previous membership instead.
    //
    // The parse itself lives in compositor/scrollbehaviourparse.h so its
    // three-way contract is unit-tested — an on-screen failure here is
    // silent, which is why the contract must not rest on this file alone.
    QStringList parseWarnings;
    const auto toSet = [&parseWarnings](const QVariant& raw, QLatin1StringView key) {
        return ScrollBehaviourParse::parseScreenIdList(raw, key, parseWarnings);
    };
    // One spelling per key, shared with the daemon's producer through
    // PhosphorProtocol, so the lookup and its diagnostic label cannot drift.
    using PhosphorProtocol::Service::ScrollBehaviourKey::CropStraddlers;
    using PhosphorProtocol::Service::ScrollBehaviourKey::FocusFollowsMouse;
    using PhosphorProtocol::Service::ScrollBehaviourKey::VerticalAxis;
    const QSet<QString> ffm = toSet(behaviour.value(FocusFollowsMouse), FocusFollowsMouse).value_or(QSet<QString>());
    const QSet<QString> crop = toSet(behaviour.value(CropStraddlers), CropStraddlers).value_or(QSet<QString>());
    // Membership: a screen IN the list runs its strip vertically. An ABSENT
    // key reads as an empty set, which means horizontal everywhere — that is
    // what the daemon publishes on a session with no vertical strip.
    //
    // A MALFORMED value keeps the CURRENT membership rather than emptying it.
    // The two siblings can fail to an empty set because empty means their
    // behaviour is off; this one has no off, and empty means "horizontal",
    // which is an active claim about geometry the engine has already committed
    // the other way. Falling to it would shear every vertical strip for the
    // rest of the session with no batch coming to correct it, and the last
    // good membership is a strictly better guess than the historical default.
    const QSet<QString> verticalAxis =
        toSet(behaviour.value(VerticalAxis), VerticalAxis).value_or(m_scrollVerticalAxisScreens);
    // The parser collects rather than logs (it is a headless-tested pure
    // function); the warnings reach the journal here, once per apply.
    for (const QString& warning : std::as_const(parseWarnings)) {
        qCWarning(lcEffect).noquote() << warning;
    }
    // Seeded BEFORE the change gate, the m_activeLayoutsSeeded shape: an
    // identical map is still a real map, and the daemon's first publish is
    // legitimately all-empty on a session with no scrolling screen. Gating the
    // flag would leave blocksDirectScanout permanently falling back to the
    // global setting there.
    m_scrollEffectBehaviourSeeded = true;
    m_scrollFocusFollowsMouseScreens = ffm;
    // One of the five ffmOffEverywhere sites: this write is what can
    // take the LAST focus-follows-mouse screen away while both globals were
    // already off, and handleCursorMoved's bail (the latch's only other
    // disarm) sits behind the very predicate that just went true — so a latch
    // armed by an engine-driven strip move would survive here with a stale
    // anchor and swallow the first move after the rule turns FFM back on.
    if (ffmOffEverywhere()) {
        m_ffmSuppressPending = false;
    }
    // BOTH sets take part in the gate. Testing crop alone would drop an
    // axis-only change on the floor — which is exactly what a monitor rotation
    // produces when the crop membership happens not to move, and it would
    // leave the effect sliding the strip along the old axis with no batch
    // coming to correct it. (Only crop is painted state — the repaint below
    // explains why the axis half is not a painting concern.)
    const bool cropChanged = crop != m_scrollCropStraddlerScreens;
    const bool axisChanged = verticalAxis != m_scrollVerticalAxisScreens;
    if (!cropChanged && !axisChanged) {
        return;
    }
    // Screens whose axis MEMBERSHIP flipped, captured before the write below
    // consumes the old set. Consumed after the write, but derived here.
    const QSet<QString> axisFlipped =
        (verticalAxis - m_scrollVerticalAxisScreens) + (m_scrollVerticalAxisScreens - verticalAxis);
    m_scrollCropStraddlerScreens = crop;
    m_scrollVerticalAxisScreens = verticalAxis;
    // An axis flip landing MID-LEG must cancel that screen's view spring and
    // armed strip pass, the setScrollingScreens teardown shape: the painted
    // axis lives in StripViewAnimator's per-output stamp, which only
    // applyBatchDelta rewrites — and it early-returns on a ZERO delta, so
    // the flip's own re-layout batch (a reflow, not a scroll) never
    // restamps it. Without this the stale leg keeps sliding the strip along
    // the axis the screen no longer has until the next genuine scroll.
    // forgetOutput fires no repaint of its own, so damage the output too.
    for (const QString& flippedScreen : axisFlipped) {
        if (KWin::LogicalOutput* out = m_effect->outputForScreenId(flippedScreen)) {
            m_effect->m_stripTransition.outputRemoved(out);
            m_effect->m_stripViewAnimator->forgetOutput(out);
            if (KWin::effects) {
                KWin::effects->addRepaint(out->geometry());
            }
        }
    }
    // Crop is PAINTED state: a screen that just started (or stopped) cropping
    // has stale pixels on it, and nothing else will revisit them, since the
    // strip's geometry did not necessarily move and no tile batch is
    // guaranteed.
    //
    // The axis set is NOT read by the paint path — StripViewAnimator holds the
    // per-output stamp that offsetFor and the shader pass answer from. It is
    // damaged here anyway, defensively: an axis change means the daemon has
    // re-resolved a screen's layout, and a full repaint on that is cheap
    // against getting it wrong. Do not cite this repaint as evidence the set
    // is painted state; the teardown clear deliberately has no such bookend
    // for exactly that reason.
    //
    // The focus-follows-mouse set needs no bookend; it is read fresh on the
    // next pointer move.
    if (KWin::effects) {
        KWin::effects->addRepaintFull();
    }
}

void TilingHandler::clearScrollEffectBehaviourForTeardown()
{
    m_scrollEffectBehaviourSeeded = false;
    m_scrollFocusFollowsMouseScreens.clear();
    if (ffmOffEverywhere()) {
        // Same latch reasoning as the apply above — the dead session's set was
        // the last thing keeping FFM alive anywhere, and its anchor names a
        // cursor position from before the teardown.
        m_ffmSuppressPending = false;
    }
    // ABOVE the crop early return, not after it: cropping is off by default,
    // so an all-empty crop set is the common case and a clear appended at the
    // tail would never run there — the axis set would outlive the session that
    // published it and answer Vertical for a screen the next daemon may run
    // horizontally. No repaint bookend of its own, unlike the crop set: the
    // axis the paint path reads is StripViewAnimator's per-output copy, not
    // this set, and no batch lands with the daemon gone, so the painted half
    // is StripViewAnimator::reset()'s to undo.
    m_scrollVerticalAxisScreens.clear();
    if (m_scrollCropStraddlerScreens.isEmpty()) {
        return;
    }
    m_scrollCropStraddlerScreens.clear();
    // Painted state, so the same bookend the live apply takes: the clip stops
    // cutting on every screen that was cropping, and nothing else repaints
    // those outputs.
    if (KWin::effects) {
        KWin::effects->addRepaintFull();
    }
}

void TilingHandler::slotScrollingScreensChanged(const QStringList& screenIds)
{
    // Mode discriminator — no per-screen LIFECYCLE transitions here (the
    // union set arriving via slotScreensChanged owns those). But the set IS
    // an input to ruleQuery's Mode stamp, and rule verdicts are memoised per
    // window: on an autotile↔scrolling flip the union does not move, so
    // slotScreensChanged never invalidates anything and a `Mode Equals
    // "scrolling"` border/opacity/decoration rule would keep its stale
    // verdict indefinitely. Invalidate + sweep on a GENUINE change only
    // (identical-set desktop-switch re-emits stay free).
    setScrollingScreens(QSet<QString>(screenIds.cbegin(), screenIds.cend()));
}

void TilingHandler::setScrollingScreens(const QSet<QString>& newSet, bool announceFlipped)
{
    // Any authoritative write voids in-flight property replies, identical
    // set or not — the writer is always newer than a reply dispatched
    // earlier (see the m_scrollingScreensGeneration doc).
    ++m_scrollingScreensGeneration;
    if (newSet == m_scrollingScreens) {
        // Skipping updateScrollWheelShortcuts at the tail is deliberate and
        // stays correct only while its want predicate reads nothing but the
        // enable flag and the set's emptiness, neither of which an identical
        // set moves. A predicate that starts reading the set's CONTENTS would
        // have to be re-evaluated here.
        return;
    }
    const QSet<QString> oldSet = m_scrollingScreens;

    // Engine-flip re-announce. A screen that changes tiling ENGINE while
    // staying in the union (autotile↔scrolling) never transits
    // managedScreensChanged — the union is emit-on-change and does not move —
    // so slotScreensChanged cannot demote and re-announce its windows. The
    // daemon side has already torn the old engine's state down and the new
    // engine claims an EMPTY screen: windows keep their old rects and every
    // verb on the new engine refuses. Re-announce the flipped screens'
    // windows here; the daemon routes windowOpened by the screen's current
    // mode, so the receiving engine adopts them (order-seeded from the
    // capture the daemon took during the flip). Cross-union transitions
    // (snapping↔scrolling) still announce exactly once regardless of which
    // signal lands first: whichever handler sees the screen inside
    // m_managedScreens does the work, the other filters it out
    // (notifyWindowsAddedBatch drops screens outside the union, and
    // slotScreensChanged only processes union membership changes).
    QSet<QString> flipped = (newSet - oldSet) + (oldSet - newSet);
    flipped &= m_managedScreens;
    const bool announcing = announceFlipped && !flipped.isEmpty();

    // The re-announce's per-window screen ids are resolved HERE, under the
    // OLD scrolling set, and threaded into the batch. getWindowScreenId's
    // engine-authoritative override is gated on m_scrollingScreens membership
    // (via scrollTrackedScreenFor), so the moment the assignment below drops a
    // screen from the set, a parked strip column — which the strip places
    // ENTIRELY outside its own output — resolves POSITIONALLY onto the
    // neighbouring output. The batch would then filter it out against
    // `flipped` and never announce it, stranding the window at its parked rect
    // with neither engine owning it. The entering direction needs no such care
    // (those windows are on-canvas, so positional and override agree), but
    // resolving both under the old set keeps one rule for the whole batch.
    QList<KWin::EffectWindow*> announceWindows;
    QHash<KWin::EffectWindow*, QString> announceScreens;
    if (announcing && KWin::effects) {
        announceWindows = KWin::effects->stackingOrder();
        announceScreens.reserve(announceWindows.size());
        for (KWin::EffectWindow* w : std::as_const(announceWindows)) {
            // Close-grabbed dying windows linger in the stacking order and
            // resolving one re-pollutes the scrubbed id caches — the same bail
            // the batch itself takes before any id lookup.
            if (w && !w->isDeleted()) {
                announceScreens.insert(w, m_effect->getWindowScreenId(w));
            }
        }
    }

    // Windowed fullscreen ends for windows on screens leaving the scrolling
    // set: their strip is gone, and the new engine's batches (which would
    // otherwise un-flag them entry by entry) skip windows it floats, so the
    // batch path alone cannot be relied on. Collected against the
    // PRE-CAPTURED screen map — the engine-authoritative override dies with
    // the assignment below, and a live resolve after it lands parked
    // columns on the wrong output (the announceScreens comment above).
    // Membership keyed iteration: the hash value is a rect, so the window's
    // screen comes from the capture, never from the hash.
    //
    // DEPENDENCY, stated rather than removed: announceScreens is populated
    // only when `announcing` is true, so this release rides the RE-ANNOUNCE
    // and not the set shrinking on its own. An announceFlipped=false call, or
    // one whose flipped screens fall outside m_managedScreens, leaves the
    // enumeration empty and releases nothing. Both such callers compensate
    // deliberately and say so at their own site — the bring-up fetch
    // (wiring.cpp) runs before any batch has populated the membership hash,
    // and the bring-up drain (drainDeadSessionState) calls
    // restoreAllWindowedFullscreen immediately BEFORE its
    // setScrollingScreens({}, false). A future announceFlipped=false caller
    // that can reach here with live membership must do the same, or resolve
    // the leaving screens independently of the announce.
    QStringList windowedFsLeavingScrolling;
    if (!m_effect->m_windowedFullscreenWindows.isEmpty()) {
        const QSet<QString> leavingScrolling = oldSet - newSet;
        for (auto it = announceScreens.constBegin(); it != announceScreens.constEnd(); ++it) {
            if (!leavingScrolling.contains(it.value())) {
                continue;
            }
            const QString wid = m_effect->getWindowId(it.key());
            if (m_effect->m_windowedFullscreenWindows.contains(wid)) {
                forgetWindowedFullscreen(wid);
                windowedFsLeavingScrolling.append(wid);
            }
        }
    }

    m_scrollingScreens = newSet;
    for (const QString& wid : std::as_const(windowedFsLeavingScrolling)) {
        releaseWindowedFullscreenState(wid);
    }

    // A screen LEAVING the scrolling set mid-leg must take its view spring
    // and strip shader pass with it. The instant the set changes,
    // scrollManagedOutputFor answers null for every column on that screen,
    // so the paint path stops applying the offset (the columns snap to
    // committed geometry) — but the spring and the armed pass know nothing
    // of the set, so the pass would keep capturing and decorating a scene
    // that is no longer scrolling for the leg's remaining duration, with
    // the whole capture now classified as wallpaper-under-everything.
    // forgetOutput fires no repaint of its own, so damage the output too:
    // the last presented frame carries the dying offset/pass and nothing
    // else is scheduled to repaint it away.
    for (const QString& removedScreen : oldSet - newSet) {
        if (KWin::LogicalOutput* out = m_effect->outputForScreenId(removedScreen)) {
            m_effect->m_stripTransition.outputRemoved(out);
            m_effect->m_stripViewAnimator->forgetOutput(out);
            if (KWin::effects) {
                KWin::effects->addRepaint(out->geometry());
            }
        }
    }

    m_effect->invalidateAllRuleCaches();
    m_effect->scheduleBorderSweep();
    // Mode is a ruleQuery input too, so the same static-window problem the
    // active-layout write documents applies here: a `Mode Equals "scrolling"`
    // SetOpacity rule flips verdict on an engine swap, and an undamaged
    // window would keep its last-painted alpha until incidental damage.
    if (m_effect->m_shaderManager.hasOpacityRules() && KWin::effects) {
        KWin::effects->addRepaintFull();
    }

    if (announcing) {
        qCInfo(lcEffect) << "Scrolling flip within managed union — re-announcing windows on" << flipped;
        // A flipped screen's pending staggered applies were computed by the
        // OLD engine; void them per-screen before the re-announce drives the
        // new engine's batch. The new batch captures its generations at
        // build time, after this bump, so it is unaffected. (This is the
        // union-internal twin of slotScreensChanged's removed-screens bump —
        // the global epoch stays reserved for desktop switches.)
        for (const QString& screenId : std::as_const(flipped)) {
            ++m_tileStaggerGenByScreen[screenId];
        }
        // enteringAutotile=true: the flag is a MODE-ENTRY discriminator, not an
        // autotile-specific one. Left false, an already-minimized window on the
        // flipped screen took claimAlreadyMinimizedAsFloated's early return and
        // got neither the untiled-minimize marker nor the per-screen float
        // re-assert, so on unminimize it sat at the PRIOR engine's rect for the
        // animation grace and then visibly hopped into its new tile — the same
        // class as the minimized-window-on-mode-swap regression.
        notifyWindowsAddedBatch(announceWindows, flipped, /*resetNotified=*/true,
                                /*enteringAutotile=*/true, announceScreens);
    }
    updateScrollWheelShortcuts();
}

void TilingHandler::slotActiveLayoutsChanged(const QVariantMap& activeLayouts)
{
    // Boundary validation: this map crosses D-Bus from another process and
    // lands directly in a rule-match input. An empty key would be a screen id
    // no window can ever resolve to (dead weight that still defeats the
    // change gate), and a non-string value would silently stringify to
    // something no authored rule can match.
    QHash<QString, QString> next;
    next.reserve(activeLayouts.size());
    for (auto it = activeLayouts.cbegin(); it != activeLayouts.cend(); ++it) {
        if (it.key().isEmpty()) {
            qCWarning(lcEffect) << "activeLayouts: dropping entry with empty screen id";
            continue;
        }
        // a{sv} values can arrive either already demarshalled to their inner
        // type (the property Get path, which qdbus_cast unwraps) or still
        // wrapped in a QDBusVariant (a signal delivered without a registered
        // argument type). Unwrap one level before the type test so the guard
        // filters genuinely wrong types instead of silently discarding every
        // entry the moment the transport shape changes.
        QVariant value = it.value();
        if (value.typeId() == QMetaType::fromType<QDBusVariant>().id()) {
            value = qvariant_cast<QDBusVariant>(value).variant();
        }
        if (value.typeId() != QMetaType::QString) {
            qCWarning(lcEffect) << "activeLayouts: dropping non-string layout id for screen" << it.key() << "type"
                                << value.typeName();
            continue;
        }
        next.insert(it.key(), value.toString());
    }
    setActiveLayouts(next);
}

void TilingHandler::setActiveLayouts(const QHash<QString, QString>& activeLayouts)
{
    // Any authoritative write voids in-flight property replies, identical
    // map or not — the writer is always newer than a reply dispatched
    // earlier (see the m_activeLayoutsGeneration doc).
    ++m_activeLayoutsGeneration;
    // Seed BEFORE the change gate: an identical map is still a real map, and
    // the daemon's very first push is legitimately empty on a session with no
    // engine-managed screen. Gating the flag would leave the effect
    // permanently unseeded there, holding every ActiveLayout rule out of the
    // evaluator for the whole session.
    if (!m_activeLayoutsSeeded) {
        m_activeLayoutsSeeded = true;
        // Seeding edge: ActiveLayout-referencing rules were held out of every
        // effect-bound rule set while the map was unknown (see
        // activeLayoutsSeeded). Re-fetch the store so the admission filter
        // re-runs with the field admitted. Above the change gate because an
        // all-empty first map is still the edge; async, so it lands after
        // everything below regardless of where it sits here.
        //
        // Gated on the effect's withheld marker: the re-drive costs a
        // getAllRules round-trip, a full RuleSet parse and an
        // updateAllDecorations sweep, and it can only change an outcome when
        // the last admission pass actually dropped a rule for referencing
        // ActiveLayout. A pass that ran with the map already seeded leaves the
        // marker false, which is also the right answer: it admitted the field
        // itself. The other writer is clearActiveLayoutsForTeardown, which
        // sets the marker when its re-slice actually removed a rule — so an
        // unseed that withheld nothing does not arm this edge either. The
        // marker is consumed here so a later unseed→seed cycle re-drives only
        // on its own evidence.
        if (m_effect->m_activeLayoutRulesWithheld) {
            m_effect->m_activeLayoutRulesWithheld = false;
            m_effect->loadRuleAnimationsFromDbus();
        }
    }
    if (activeLayouts == m_activeLayouts) {
        return;
    }
    m_activeLayouts = activeLayouts;
    // Rule verdicts are memoised per window and ActiveLayout is a ruleQuery
    // input: a layout/template change on any screen can flip a border,
    // opacity, decoration, or animation-exclusion verdict, so the whole
    // cache goes (there is no per-screen invalidation surface) and the
    // sweep repaints borders that changed.
    m_effect->invalidateAllRuleCaches();
    m_effect->scheduleBorderSweep();
    // The sweep re-folds decorations, but a SetOpacity rule scoped on
    // ActiveLayout alters paint output for windows that are otherwise static
    // and undamaged — those never reach a paint pass to pick the new alpha
    // up. Same bookend loadRuleAnimationsFromDbus takes on a rule edit,
    // gated on rule PRESENCE so a session with no opacity rule pays nothing.
    // The KWin::effects term matches every sibling bookend in this file: the
    // teardown orderings that leave the handle null all reach a setter of one
    // kind or another, and this one is no less reachable than setScrollingScreens'.
    if (m_effect->m_shaderManager.hasOpacityRules() && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
}

void TilingHandler::clearActiveLayoutsForTeardown()
{
    ++m_activeLayoutsGeneration;
    m_activeLayouts.clear();
    m_activeLayoutsSeeded = false;
    // Take the rules that resolve against the map back out of the effect's
    // rule sets — see the header doc for why leaving them bound over the
    // daemon-down interval over-matches. This also arms the seed edge above
    // (it sets m_activeLayoutRulesWithheld when it removed anything), so the
    // rules are re-admitted from the live store on the next real map.
    //
    // No border sweep and no cache invalidation here, by the call-site
    // contract: both callers run invalidateAllRuleCaches (which drops the
    // verdicts these removals change, and carries the window-layer sweep)
    // immediately after, and each then rebuilds what those verdicts had baked
    // into decorations its own way — drainDeadSessionState with scheduleBorderSweep,
    // the serviceUnregistered teardown with clearAllDecorations, which tears
    // the decorations down outright and so needs no sweep. The re-slice does take
    // the SetOpacity repaint bookend, which neither caller covers on the
    // handover path — see its own doc.
    m_effect->sliceActiveLayoutRulesForUnseededMap();
}

void TilingHandler::updateScrollWheelShortcuts()
{
    // The enable setting folds into the want predicate so turning it off
    // genuinely releases the axis chords back to the compositor, rather
    // than swallowing them.
    const bool want = m_wheelFocusEnabled && !m_scrollingScreens.isEmpty();
    if (want == !m_scrollWheelActions.isEmpty()) {
        return;
    }
    if (!want) {
        // Destroying the QAction unregisters the axis shortcut (KWin's
        // shortcut manager erases entries on QAction::destroyed), releasing
        // the chord for any later registrant. deleteLater rather than a
        // manual delete: these are parented QObjects, and a delete here
        // would run inside whatever emitted the mode change. The sub-turn
        // window before the deferred delete lands is benign — KWin APPENDS
        // duplicate registrations and matches the FIRST, and a still-live
        // doomed action drives the same trigger as its replacement.
        for (QAction* action : std::as_const(m_scrollWheelActions)) {
            action->deleteLater();
        }
        m_scrollWheelActions.clear();
        qCInfo(lcEffect) << "Scroll wheel shortcuts unregistered (no scrolling screens)";
        return;
    }
    // niri's default Mod+wheel bindings: wheel down / right focuses the next
    // column along the screen's strip axis, wheel up / left the previous one.
    // The chord carries a signed delta only — which way that points on screen
    // is the engine's call, resolved against the screen's own axis — so one
    // registration serves a horizontal and a vertical strip alike. The
    // horizontal pair covers tilted wheels, and horizontal touchpad scrolls
    // once the accumulated delta clears KWin's 1.0 threshold (processAxis
    // only fires on |delta| >= 1.0).
    //
    // Meta ONLY for the focus quad — no Meta+Alt fallback, and the mechanics
    // matter (verified against KWin 6.7 source): KWin's
    // GlobalShortcutsManager APPENDS duplicate axis registrations and
    // match() returns the FIRST entry, so whoever registered earlier wins.
    // KWin core registers Meta+Alt+WheelUp/Down for Switch to Next/Previous
    // Desktop at init, before any effect loads — a Meta+Alt pair here could
    // only ever lose that match and sit dead. Plain Meta is free on a stock
    // setup: the zoom effect's axis modifiers default to Meta+Ctrl, not
    // Meta. A user who rebinds zoom onto plain Meta creates a duplicate
    // whose winner is whichever effect registered earlier in the session.
    //
    // Meta+Shift for the VIEW quad, one modifier step from the focus quad
    // for one step of difference in meaning (move focus along the strip /
    // move the view along it). KWin core registers nothing on Meta+Shift+
    // wheel and no stock effect defaults to it, so it sits in the same
    // free-on-a-stock-setup class as plain Meta, with the same rebind caveat.
    const auto add = [this](Qt::KeyboardModifiers mods, KWin::PointerAxisDirection axis, int delta, const QString& name,
                            void (TilingHandler::*trigger)(int)) {
        auto* action = new QAction(this);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, delta, trigger]() {
            (this->*trigger)(delta);
        });
        KWin::effects->registerAxisShortcut(mods, axis, action);
        m_scrollWheelActions.append(action);
    };
    add(Qt::MetaModifier, KWin::PointerAxisDown, 1, QStringLiteral("pz-scroll-column-next"),
        &TilingHandler::wheelFocusColumn);
    add(Qt::MetaModifier, KWin::PointerAxisUp, -1, QStringLiteral("pz-scroll-column-prev"),
        &TilingHandler::wheelFocusColumn);
    add(Qt::MetaModifier, KWin::PointerAxisRight, 1, QStringLiteral("pz-scroll-column-next-h"),
        &TilingHandler::wheelFocusColumn);
    add(Qt::MetaModifier, KWin::PointerAxisLeft, -1, QStringLiteral("pz-scroll-column-prev-h"),
        &TilingHandler::wheelFocusColumn);
    const Qt::KeyboardModifiers metaShift = Qt::MetaModifier | Qt::ShiftModifier;
    add(metaShift, KWin::PointerAxisDown, 1, QStringLiteral("pz-scroll-view-forward"), &TilingHandler::wheelScrollView);
    add(metaShift, KWin::PointerAxisUp, -1, QStringLiteral("pz-scroll-view-back"), &TilingHandler::wheelScrollView);
    add(metaShift, KWin::PointerAxisRight, 1, QStringLiteral("pz-scroll-view-forward-h"),
        &TilingHandler::wheelScrollView);
    add(metaShift, KWin::PointerAxisLeft, -1, QStringLiteral("pz-scroll-view-back-h"), &TilingHandler::wheelScrollView);
    qCInfo(lcEffect) << "Scroll wheel shortcuts registered"
                     << "(Meta+wheel focuses columns, Meta+Shift+wheel scrolls the view)";
}

QString TilingHandler::wheelTargetScreen(int& delta) const
{
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return QString();
    }
    // Re-gate on the enable flag: between setWheelFocusEnabled(false)'s
    // deleteLater and the deferred delete actually landing, the doomed
    // action is still registered and can fire one more tick.
    if (!m_wheelFocusEnabled) {
        return QString();
    }
    if (m_wheelFocusInverted) {
        delta = -delta;
    }
    // The strip that moves is the one under the CURSOR (a wheel chord is a
    // pointer gesture, not a focus verb): resolve the cursor's effective
    // screen — virtual subdivisions included — and only forward when it
    // actually runs the scrolling engine. On any other screen the chord is
    // consumed but inert; registration is per-session, not per-screen.
    const QPointF pos = KWin::effects->cursorPos();
    const QPoint rounded(qRound(pos.x()), qRound(pos.y()));
    const auto* output = KWin::effects->screenAt(rounded);
    if (!output) {
        return QString();
    }
    const QString screenId = m_effect->resolveEffectiveScreenId(rounded, output);
    // isScrollingScreen, not the raw set: it intersects with the managed union,
    // so a screen the union already dropped cannot still swallow the chord and
    // forward a verb the engine no longer owns.
    if (!isScrollingScreen(screenId)) {
        return QString();
    }
    return screenId;
}

void TilingHandler::wheelFocusColumn(int delta)
{
    const QString screenId = wheelTargetScreen(delta);
    if (screenId.isEmpty()) {
        return;
    }
    qCDebug(lcEffect) << "Wheel focus column: delta" << delta << "on" << screenId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Scrolling,
                                                   QStringLiteral("focusColumn"), {screenId, delta},
                                                   QStringLiteral("focusColumn"));
}

void TilingHandler::wheelScrollView(int delta)
{
    const QString screenId = wheelTargetScreen(delta);
    if (screenId.isEmpty()) {
        return;
    }
    qCDebug(lcEffect) << "Wheel scroll view: delta" << delta << "on" << screenId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Scrolling,
                                                   QStringLiteral("scrollView"), {screenId, delta},
                                                   QStringLiteral("scrollView"));
}

} // namespace PlasmaZones
