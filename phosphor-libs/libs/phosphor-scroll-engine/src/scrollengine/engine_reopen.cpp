// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Cross-screen reclaim. Split out of engine_lifecycle.cpp, which had reached
// the file-size ceiling: KWin's session restore opens a window on a
// nondeterministic output, so a window recorded on monitor A routinely
// arrives on monitor B, and this is the arm that pulls it back to the strip
// its placement record homes it on.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>

#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

bool ScrollEngine::claimCrossScreenReopen(const QString& rawWindowId, const QString& openingScreenId, int minWidth,
                                          int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // openingScreenId validated here, at the library boundary: the adaptor's
    // dispatch cannot produce an empty one, but this is public engine API and
    // an empty opening screen would defeat the predicate's same-screen bail
    // (empty compares unequal to every recorded screen).
    // The reopen contract: this claims only a FIFO record (another instance
    // of the app) on another virtual screen of the OPENING output, in
    // scrolling mode, for a window opening on a scrolling screen; never a
    // record on another monitor, never the window's own record. Every decline
    // is LOGGED; the ones that fire on every ordinary open (a brand-new window
    // has no record) are debug so they cannot bury the rare ones.
    if (windowId.isEmpty() || openingScreenId.isEmpty() || !m_scrollingModeResolver || !m_windowTracker) {
        qCInfo(lcScrollEngine) << "claimCrossScreenReopen: declining" << windowId << "on" << openingScreenId
                               << "— preconditions unmet (id empty" << windowId.isEmpty() << "screen empty"
                               << openingScreenId.isEmpty() << "resolver" << bool(m_scrollingModeResolver) << "tracker"
                               << bool(m_windowTracker) << ")";
        return false;
    }
    // First observation only, by MEMBERSHIP: a window this engine already
    // holds anywhere is an in-session move or re-announce, never a session
    // restore — yanking it back to the record's screen would undo the very
    // move that re-announced it. Membership, not the raw reverse-map key: a
    // phantom key left by a refused earlier open must not veto a legitimate
    // claim.
    if (const ScrollState* tracked = stateForWindow(windowId); tracked && tracked->containsWindow(windowId)) {
        qCDebug(lcScrollEngine) << "claimCrossScreenReopen: declining" << windowId
                                << "— already held here, so this is an in-session re-announce, not a restore";
        return false;
    }
    // Same mode: the window opens on a screen this engine runs (F647).
    if (!m_scrollingScreens.contains(openingScreenId)) {
        qCDebug(lcScrollEngine) << "claimCrossScreenReopen: declining" << windowId << "— opening screen"
                                << openingScreenId << "is not a scrolling screen";
        return false;
    }
    // The window's own record is final: restored (or not) where it names,
    // never stood in for by a sibling's.
    if (const auto own = m_windowTracker->placementStore().peekExact(windowId); own && !own->engines.isEmpty()) {
        qCDebug(lcScrollEngine) << "claimCrossScreenReopen: declining" << windowId
                                << "— it has its own placement record";
        return false;
    }
    // Registry-aware appId, like autotile's twin and like every record
    // producer: parsing the frozen canonical string would look in the wrong
    // bucket after an Electron/CEF class mutation, and finds nothing at all
    // for a window KWin had not classed when the id was frozen. The engine's
    // own resolver rather than the tracker's — they are line-for-line
    // identical, and it is the same spelling the capture and float restore
    // use, so the three cannot drift apart.
    const QString appId = currentAppIdFor(windowId);
    if (!PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
        // Info, not debug: an unmatchable appId means this window can never be
        // reclaimed, which is a standing condition worth seeing once per open
        // rather than a per-open no-op.
        qCInfo(lcScrollEngine).nospace() << "claimCrossScreenReopen: declining " << windowId
                                         << " — no stable appId: " << appId
                                         << " (its placement records cannot be matched)";
        return false;
    }
    // peekForReclaim, not peek: the live-instance exclusion is what stops a
    // fresh second instance being pulled onto its OPEN sibling's virtual
    // screen on the strength of that sibling's live record, and it honours
    // the open claim. Non-consuming either way —
    // consumption stays with windowOpened's own restore machinery (the strip
    // stash claim and takeForReopen), which this claim funnels the window
    // into by re-entering the open path with the RECORDED screen. Only a
    // TILED slot earns the pull — a scroll-floating record is screen-local,
    // matching snap's float doctrine.
    const auto pending = m_windowTracker->placementStore().peekForReclaim(
        windowId, appId, [&](const PhosphorEngine::WindowPlacement& p) {
            return PhosphorEngine::pendingCrossScreenManagedRestore(
                p, PhosphorEngine::WindowPlacement::scrollingEngineId(), PhosphorEngine::WindowPlacement::stateTiled(),
                windowId, openingScreenId, [this](const QString& rec, int desktop, const QString& activity) {
                    return m_scrollingModeResolver(rec, desktop, activity);
                });
        });
    if (!pending) {
        // Debug: the ordinary-open case. "No usable record" covers all of the
        // store's null answers — no record at all, none whose scrolling slot
        // is tiled on another scrolling-mode screen, and one that qualifies
        // but belongs to a still-live sibling instance (peekForReclaim
        // excludes those). Naming only the middle case would misreport the
        // multi-instance one.
        qCDebug(lcScrollEngine) << "claimCrossScreenReopen: declining" << windowId << "appId" << appId << "on"
                                << openingScreenId << "— no usable cross-screen scrolling record";
        return false;
    }
    const QString homeScreen = pending->screenId;
    // The LIVE screen set must agree with the record-context verdict:
    // windowOpened's own m_scrollingScreens gate would otherwise refuse the
    // adoption AFTER this method already answered "claimed", stranding the
    // window untracked (a per-desktop mode override can make the resolver
    // and the live set disagree during a context switch).
    if (!m_scrollingScreens.contains(homeScreen)) {
        qCInfo(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "recorded home" << homeScreen
                               << "is scrolling-mode by record context but absent from the live set — declining";
        return false;
    }
    // Grant-context must be compatible with insert-context. The predicate
    // granted on the RECORD's (desktop, activity); windowOpened inserts into
    // the home screen's CURRENT key. When two CONCRETE contexts disagree,
    // adopting would splice the window into a strip on a desktop it is not
    // visible on (displacing that desktop's real windows) and miss its
    // stashed column outright — the conservative skip leaves the window to
    // the ordinary open path instead. Sticky / unknown-context records
    // (the 0 and empty sentinels) stay eligible; see
    // recordContextMatchesLive.
    const PhosphorEngine::PlacementStateKey homeKey = currentKeyForScreen(homeScreen);
    if (!PhosphorEngine::recordContextMatchesLive(*pending, homeKey.desktop, homeKey.activity)) {
        qCInfo(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "recorded context desktop"
                               << pending->virtualDesktop << "activity" << pending->activity << "differs from"
                               << homeScreen << "current context — declining";
        return false;
    }
    // A float is screen-local: the home open would mark the window floating
    // in the HOME state and move nothing, leaving it tracked on a monitor it
    // is not on and sized against that monitor's columns. The three verdicts
    // insertOpenedWindow floats on, evaluated for the home screen; the
    // arrival screen's open then floats it where it stands.
    const ScrollLayoutParams homeParams = layoutParamsForScreen(homeScreen);
    const int clampedMinWidth = qMax(0, minWidth);
    const int clampedMinHeight = qMax(0, minHeight);
    const bool oversized = homeParams.workArea.isValid()
        && (clampedMinWidth > homeParams.workArea.width() || clampedMinHeight > homeParams.workArea.height());
    const bool ruleFloated = m_floatPredicate && m_floatPredicate(windowId, homeScreen);
    const bool stickyExcluded =
        effectiveStickyWindowHandling(homeScreen) != PhosphorEngine::StickyWindowHandling::TreatAsNormal
        && m_windowTracker->isWindowSticky(windowId);
    if (oversized || ruleFloated || stickyExcluded) {
        qCInfo(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "would float on" << homeScreen
                               << "(oversized" << oversized << "rule" << ruleFloated << "sticky" << stickyExcluded
                               << ") — declining, a float has nothing to pull home for";
        return false;
    }
    windowOpened(windowId, homeScreen, minWidth, minHeight);
    // Return the REAL outcome, verified by membership (ScrollState-level: a
    // legitimately floated adoption is in the floating set, not the strip).
    // An optimistic true converted every silently-refused re-entry into a
    // window no engine manages — the caller hands a claimed window to no
    // other engine.
    const ScrollState* adopted = stateForWindow(windowId);
    if (!adopted || !adopted->containsWindow(windowId)) {
        qCWarning(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "adoption on" << homeScreen
                                  << "was refused — not claimed";
        return false;
    }
    // The ARRIVAL screen's mode-transition seed still names this id; the
    // arrival-screen windowOpened that would have consumed it never runs on
    // a successful claim (the dispatch returns), and one unconsumed id pins
    // that screen's seed for the whole session. Safe no-op for a screen with
    // no seed.
    consumePendingInitialOrder(openingScreenId, windowId);
    qCInfo(lcScrollEngine) << "claimCrossScreenReopen:" << windowId << "opened on" << openingScreenId
                           << "— reclaimed to recorded scrolling home" << homeScreen;
    return true;
}

void ScrollEngine::noteCrossScreenClaimsExhausted(const QString& windowId, bool exhausted)
{
    // INERT, kept for ABI: the mark told the cross-screen defer gate to adopt
    // instead of deferring again, and that gate is gone (the reopen contract).
    Q_UNUSED(windowId)
    Q_UNUSED(exhausted)
}

} // namespace PhosphorScrollEngine
