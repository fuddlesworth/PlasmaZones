// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>
#include <PhosphorEngine/EngineTypes.h>

#include <functional>

#include <QHash>
#include <QString>

namespace PhosphorEngine {

/// Outcome of a context mutation on ScreenContextTracker.
///
/// The tracker owns the "was a context ever established" arming subtleties
/// (which distinguish the daemon's startup push from a genuine desktop/activity
/// switch), but the engine-specific reaction to a switch — logging, arming an
/// engine's own "this pass is a context switch" flag — stays in the engine.
/// The engine ORs `armSwitch` into its own flag and logs when `changed`.
struct ContextChange
{
    /// The tracked current value actually changed (a real switch, not a
    /// same-value re-push that only establishes context).
    bool changed = false;
    /// Value the engine should OR into its own context-switch flag: true only
    /// when a context had already been established before this change.
    bool armSwitch = false;
};

/// Tracks the "current context" of each screen for a placement engine: the
/// global current virtual desktop, per-output desktop overrides (Plasma 6.7
/// "switch desktops independently per screen", #648), the sticky-desktop pin
/// (KWin "virtualdesktopsonlyonprimary" model), and the current activity.
///
/// Resolves a screen's owning PlacementStateKey via currentKeyForScreen() with
/// the precedence: sticky-pin override > per-output desktop > global desktop;
/// activity is always the current activity.
///
/// Extracted verbatim (in behaviour) from AutotileEngine so the snap engine can
/// share it. Plain value/helper type — no QObject, no signals; engines read it
/// synchronously when resolving keys.
class PHOSPHORENGINE_EXPORT ScreenContextTracker
{
public:
    ScreenContextTracker() = default;

    /// Construct the owning key for a screen in the current context.
    ///
    /// Precedence (highest first):
    ///   1. sticky-pin override — a CORRECTNESS constraint: sticky on-all-desktops
    ///      windows must keep their state on the desktop where they live;
    ///   2. per-output virtual desktop (#648) — the normal per-screen input;
    ///   3. the global current desktop — fallback.
    /// The activity dimension is always the current activity.
    ///
    /// Tier 3 is a STARTUP SEED, not a live input, and reading it as one is the
    /// mistake to avoid here. Under per-output desktops the daemon pushes the
    /// global desktop once during startup and every later change arrives
    /// through setCurrentDesktopForScreen, unconditionally, for every screen —
    /// so a screen that reaches tier 3 after startup is resolving against a
    /// value frozen at login. That is why a per-output entry must never be
    /// dropped while its screen still exists: the fallback below it does not
    /// track anything, and every output falling through to it lands on one
    /// shared desktop.
    PlacementStateKey currentKeyForScreen(const QString& screenId) const;

    // ── Getters ──────────────────────────────────────────────────────────────
    int currentDesktop() const noexcept
    {
        return m_currentDesktop;
    }
    const QString& currentActivity() const noexcept
    {
        return m_currentActivity;
    }
    bool desktopContextEverSet() const noexcept
    {
        return m_desktopContextEverSet;
    }
    bool activityContextEverSet() const noexcept
    {
        return m_activityContextEverSet;
    }
    /// The screen's PER-OUTPUT desktop: its own entry if set, else the global
    /// current desktop.
    ///
    /// NOT the effective desktop, and the difference matters: this deliberately
    /// ignores the sticky pin, which currentKeyForScreen ranks ABOVE both of
    /// these. On a pinned screen the two disagree, and the key the engine
    /// actually resolves is the one currentKeyForScreen returns. Ask this only
    /// when you specifically want the per-output value; ask currentKeyForScreen
    /// when you want the desktop a lookup will use.
    ///
    /// Kept excluding the pin on purpose — releaseScreenOwnership's contract is
    /// written in terms of this falling back to the global value, and the
    /// tracker's tests pin that behaviour.
    int screenDesktop(const QString& screenId) const
    {
        return m_screenCurrentDesktop.value(screenId, m_currentDesktop);
    }

    // ── Context mutators ─────────────────────────────────────────────────────
    ContextChange setCurrentDesktop(int desktop);
    ContextChange setCurrentDesktopForScreen(const QString& screenId, int desktop);
    /// Drop a screen's per-output desktop entry, falling it back to the global.
    ///
    /// No production caller, and deliberately so. Turning Plasma's per-output
    /// desktop setting OFF does not strand these entries: the effect's
    /// desktopChanged handler fans a global switch out to EVERY output, so the
    /// pushes keep arriving and simply become uniform. There is no "per-output
    /// mode ended" event to hook, and none is needed. A screen that genuinely
    /// goes away is handled by removeScreen / removeScreensIf / pruneDesktop.
    ///
    /// Wiring a caller here would drop an entry that currentKeyForScreen's
    /// contract says must survive while its screen exists.
    void clearCurrentDesktopForScreen(const QString& screenId)
    {
        m_screenCurrentDesktop.remove(screenId);
    }
    ContextChange setCurrentActivity(const QString& activity);

    // ── Sticky-pin override (m_screenDesktopOverride) ────────────────────────
    bool hasStickyPin(const QString& screenId) const
    {
        return m_screenDesktopOverride.contains(screenId);
    }
    /// Pin a screen to a desktop, outranking both the per-output and the global
    /// value in currentKeyForScreen.
    ///
    /// Rejects desktop < 1, symmetric with setCurrentDesktop and
    /// setCurrentDesktopForScreen — and the guard matters MORE here, because a
    /// poisoned pin outranks both of the values those two protect. Unreachable
    /// from the in-tree callers, which pass a desktop off a live state key.
    void setStickyPin(const QString& screenId, int desktop)
    {
        if (desktop < 1) {
            return;
        }
        m_screenDesktopOverride.insert(screenId, desktop);
    }
    /// The sticky-pin desktop for a screen, or 0 when it is not pinned.
    ///
    /// Non-consuming, unlike takeStickyPin. 0 is unambiguous as "not pinned"
    /// because setStickyPin rejects anything below 1.
    int stickyPinnedDesktop(const QString& screenId) const
    {
        return m_screenDesktopOverride.value(screenId, 0);
    }
    /// Remove and return the sticky-pin desktop for a screen (default-constructed
    /// int == 0 when absent, mirroring QHash::take).
    int takeStickyPin(const QString& screenId)
    {
        return m_screenDesktopOverride.take(screenId);
    }

    // ── Bulk per-screen cleanup ──────────────────────────────────────────────
    /// Drop both per-screen maps' entries for a screen leaving the engine's set.
    ///
    /// For an OUTPUT that is going away. A screen that merely leaves this
    /// engine's mode set must use releaseScreenOwnership instead — see its
    /// doc for why dropping the per-output desktop there is a correctness bug.
    ///
    /// No production caller today: real output removal goes through
    /// removeScreensIf, which every engine drives from its own
    /// pruneStatesForRemovedScreen, and orphaned virtual-screen ids are swept
    /// the same way. Kept as the single-screen form of that operation and
    /// exercised by the tracker's tests. Reach for removeScreensIf first.
    void removeScreen(const QString& screenId)
    {
        m_screenDesktopOverride.remove(screenId);
        m_screenCurrentDesktop.remove(screenId);
    }
    /// Drop only the ENGINE-OWNED half for a screen leaving this engine's mode
    /// set, keeping the per-output desktop.
    ///
    /// Operationally this clears the sticky pin and nothing else — the name says
    /// what the CALLER is doing rather than what the call does, because the
    /// point of it is the half deliberately left alone. Read it as
    /// clearStickyPin with a mandatory rationale attached.
    ///
    /// The sticky pin is this engine's own bookkeeping and is meaningless once
    /// the engine stops managing the screen. The per-output desktop is not: it
    /// is compositor truth about which desktop the screen is showing, true
    /// whoever manages it, and the engine cannot reconstruct it.
    ///
    /// Dropping it made screenDesktop() silently fall back to the GLOBAL
    /// desktop — which the daemon sets exactly once at startup and never
    /// updates, because every later change arrives through the per-output
    /// screenDesktopChanged path. So a screen that left an engine's set and
    /// came back keyed EVERY context to the startup desktop until its next
    /// per-output change: on the scrolling engine that merged every virtual
    /// desktop's strip into one, and windows from other desktops rode along in
    /// its batches.
    ///
    /// The pin is NOT rebuilt on re-entry, and the consequence is worth knowing
    /// before changing either half. Both engines' updateStickyScreenPins iterate
    /// their OWN managed-screen set, and their only callers are the daemon's two
    /// context-switch handlers — so an all-sticky screen that leaves the mode and
    /// returns with no intervening desktop or activity switch comes back
    /// unpinned, resolves to its per-output desktop, and finds no state there.
    /// Its pinned-desktop state is orphaned until a prune reaps it.
    ///
    /// Preserving the pin here instead would trade that for the mirror case: the
    /// unpin arm iterates the same managed-screen set, so a pin kept for a screen
    /// OUTSIDE the mode goes stale if its windows stop being sticky. That case
    /// needs one extra state change to reach, which is why it is the narrower of
    /// the two, but the migration path for a stale pin is not traced.
    void releaseScreenOwnership(const QString& screenId)
    {
        m_screenDesktopOverride.remove(screenId);
    }
    /// Drop entries from both per-screen maps whose SCREEN key matches `pred`
    /// (e.g. an orphaned virtual-screen id that no longer exists).
    void removeScreensIf(const std::function<bool(const QString&)>& pred);
    /// Drop entries from both per-screen maps whose DESKTOP value equals
    /// `removedDesktop` (a virtual desktop was destroyed / renumbered).
    void pruneDesktop(int removedDesktop);
    /// Rewrite every tracked desktop int per `oldToNew` (1-based; absent =
    /// unchanged): the global current, per-screen currents, and sticky pins.
    /// The dynamic-workspaces renumber pass drives this alongside the engines'
    /// state-map rewrite so pins stay keyed to the desktop they meant.
    void renumberDesktops(const QHash<int, int>& oldToNew);

private:
    /// Current desktop/activity context. `*ContextEverSet` carry "a context was
    /// established" for the switch-arming logic: there is no reserved unset value
    /// for the desktop (defaults to 1, KWin desktops are >= 1), so the daemon's
    /// initial startup push must be told apart from a genuine switch by the flag,
    /// not a value comparison.
    int m_currentDesktop = 1;
    QString m_currentActivity;
    bool m_desktopContextEverSet = false;
    bool m_activityContextEverSet = false;

    /// Per-screen sticky-desktop pin. When the KWin script
    /// "virtualdesktopsonlyonprimary" pins all secondary-screen windows to all
    /// desktops, the desktop dimension is meaningless for those screens; this map
    /// pins such screens to their original desktop so currentKeyForScreen()
    /// returns the key of the existing state after a desktop switch.
    QHash<QString, int> m_screenDesktopOverride;

    /// Per-screen current virtual desktop under Plasma 6.7 "switch desktops
    /// independently for each screen" (#648). Distinct from the sticky pin: the
    /// pin is a correctness constraint and wins; this is the normal per-screen
    /// input. Empty when per-output desktops aren't in use, so every screen falls
    /// back to the global current desktop.
    QHash<QString, int> m_screenCurrentDesktop;
};

} // namespace PhosphorEngine
