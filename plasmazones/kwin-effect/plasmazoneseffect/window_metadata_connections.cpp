// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-window identity and metadata wiring for PlasmaZonesEffect: the
// registry pushes on a class, desktop-file, caption, desktop, activity or
// role change, the rule-cache invalidations those fields feed, and the
// identity diagnostic dump. Called once per window from
// setupWindowConnections.

#include "plasmazoneseffect.h"
#include "desktopvisibility.h"

#include <effect/effecthandler.h>
#include <window.h>

#include <QPointer>

#include "tilinghandler/tilinghandler.h"

namespace PlasmaZones {

void PlasmaZonesEffect::wireMetadataHandlers(KWin::EffectWindow* w)
{
    KWin::Window* kw = w->window();
    if (kw) {
        QPointer<KWin::EffectWindow> safeW = w;
        // Metadata mutations: KWin fires these when an app swaps its class or
        // desktop file after the surface is already mapped. Electron/CEF apps
        // (Emby, some Discord forks) do this mid-session and silently break any
        // daemon state keyed to the first-seen class. Push the latest metadata
        // to the WindowRegistry so consumers query the current value.
        //
        // Per feedback_class_change_exclusion.md: the registry only updates its
        // record. It does NOT retroactively unsnap, re-snap, or re-evaluate
        // rules — that would surprise users. Committed state stays committed.
        auto pushLatest = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                pushWindowMetadata(safeW);
            }
        };
        // Caption changes fire every frame for terminals / browsers; the extended
        // property snapshot (geometry / state flags) doesn't change on a title tick,
        // so refresh the registry's core metadata (title) WITHOUT rebuilding and
        // marshalling the ~20-entry a{sv} each frame. The daemon preserves the
        // existing extended fields when none are sent.
        auto pushCaptionOnly = [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // Skip content-identical pushes: KWin can emit captionChanged
            // without a net caption change, and the marshal (plus the
            // daemon-side upsert) should not ride those. captionNormal
            // derives from the caption, so an unchanged caption implies an
            // unchanged push.
            const QString caption = safeW->caption();
            QString& last = m_lastPushedCaption[safeW.data()];
            if (last == caption) {
                return;
            }
            // Stamped only when the push can actually go out, mirroring BOTH of
            // pushWindowMetadata's early returns. Recording the caption
            // regardless made the de-dupe suppress every LATER push of that same
            // caption — so a title that settled while the daemon was down, or
            // while the window had no resolvable instance id, stayed stale in
            // the registry until something else re-pushed it. The bringup sweep
            // does re-push every live window, so this was bounded rather than
            // permanent, but the de-dupe should record what was sent, not what
            // was attempted. The cost of not recording is that a chatty title
            // re-runs this lambda's tail per tick while the daemon is absent,
            // which is a hash probe and a push that returns at its own gate.
            if (m_daemonGate.serviceRegistered && !getWindowInstanceId(safeW.data()).isEmpty()) {
                last = caption;
            }
            pushWindowMetadata(safeW, /*includeExtended=*/false);
            // A compositor-drawn tab pill shows this caption in the CHIPS
            // style; rebuild the strips that name the window, skipping screens
            // whose bar style never draws it (a chatty terminal title must not
            // re-raster a bar that cannot change). One hash probe when it is
            // not a tab anywhere, which is the common case.
            m_tilingHandler->noteScrollTabTitleChanged(getWindowId(safeW.data()));
        };
        // Class / desktop-file mutations invalidate the animation rule
        // evaluator's per-window match cache. The cache is keyed on the
        // window's frozen composite id but the cascade resolves against
        // the LIVE windowClass — so without invalidation, a SetOpacity /
        // OverrideAnimation* rule for the post-rename class silently
        // never applies (Electron/CEF/Steam family). pushLatest already
        // refreshes the daemon's WindowRegistry record; mirror that
        // refresh on the effect's local resolver cache. Desktop / activity /
        // role changes get their own invalidation connects below.
        //
        // CAPTION is the deliberate exception: Title and CaptionNormal ARE
        // matchable fields stamped live into the query, so a Title-scoped
        // verdict IS knowingly left stale until the next natural invalidation
        // (focus change, placement change, rule edit). A per-caption clear is
        // strictly worse than the staleness: terminals and browsers rewrite
        // their title every frame, and each clear drops the GLOBAL per-window
        // cache — one noisy terminal would cold-start every other window's
        // verdict at title-tick rate.
        auto invalidateRuleCache = [this, safeW]() {
            // Gate each clear on its own rule set, mirroring the sibling
            // invalidation in slotWindowActivated: the no-rules case pays
            // nothing on a class swap.
            if (!m_shaderManager.animationRuleSet().isEmpty()) {
                m_shaderManager.animationRuleEvaluator().clearCache();
            }
            // The verdict cache keys on the same frozen id and matches on
            // WindowClass / AppId just as readily (an Electron/CEF class swap
            // is exactly how a per-app scroll multiplier starts or stops
            // applying), so it takes the same clear.
            if (!m_shaderManager.effectVerdictRuleSet().isEmpty()) {
                m_shaderManager.effectVerdictRuleEvaluator().clearCache();
            }
            // The exclusion verdict caches key on the same frozen id and the
            // WindowClass matcher — a class swap can flip an Exclude verdict.
            if (!m_snappingExclusionRuleSet.isEmpty()) {
                m_snappingExclusionEvaluator.clearCache();
            }
            if (!m_decorationExclusionRuleSet.isEmpty()) {
                m_decorationExclusionEvaluator.clearCache();
            }
            // The cache drop alone revives nothing: appearance slots (opacity,
            // tint, border colour) bake into the decoration at
            // updateWindowDecoration time, and the stacking layer is
            // EVENT-driven. Both applied eagerly at window-added against the
            // pre-swap placeholder class — re-drive them here so a rule keyed
            // to the real class applies (and one keyed to the placeholder
            // releases) without waiting for an incidental focus / placement
            // sweep. Decoration re-folds only for a window on the desktop its OWN
            // OUTPUT shows, matching updateAllDecorations; off-desktop rides the sweep.
            if (safeW && !safeW->isDeleted()) {
                const QString wid = getWindowId(safeW);
                if (isOnOwnOutputCurrentDesktop(safeW)) {
                    updateWindowDecoration(wid, safeW);
                }
                // Title-bar override rides the same appearance resolve as the
                // decoration re-fold, but updateWindowDecoration deliberately
                // does not resolve it (decorations.cpp documents the split) —
                // without this, a SetHideTitleBar rule keyed to the real
                // post-swap class waits for the next focus-driven sweep.
                // Outside the desktop gate, matching updateAllDecorations:
                // title-bar state is persistent and survives desktop switches.
                reconcileRuleHiddenTitleBar(wid, safeW);
                reconcileRuleWindowLayer(wid, safeW);
            }
        };
        connect(kw, &KWin::Window::windowClassChanged, this, pushLatest);
        connect(kw, &KWin::Window::windowClassChanged, this, invalidateRuleCache);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, pushLatest);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, invalidateRuleCache);
        connect(kw, &KWin::Window::captionChanged, this, pushCaptionOnly);
        // Per-window virtual-desktop / activity / role changes also refresh the
        // registry so context-aware rule resolution sees current values. Same
        // record-only contract: no retroactive re-evaluation of committed state.
        connect(kw, &KWin::Window::desktopsChanged, this, pushLatest);
        connect(kw, &KWin::Window::activitiesChanged, this, pushLatest);
        connect(kw, &KWin::Window::windowRoleChanged, this, pushLatest);
        // VirtualDesktop and Activity are matchable rule fields stamped live
        // into the per-window query, but the verdict caches key on
        // (windowId, ruleSet revision) — neither moves on a desktop or
        // activity move, so a `WHEN VirtualDesktop Equals N` exclusion or
        // appearance verdict would pin stale across the move. Enqueue the
        // coalesced per-window invalidation, mirroring the outputChanged
        // handler; the flush clears the caches and re-drives decoration /
        // title bar / layer for exactly this window.
        auto invalidateForContextMove = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                invalidateRuleCacheForStateChange(getWindowId(safeW));
            }
        };
        connect(kw, &KWin::Window::desktopsChanged, this, invalidateForContextMove);
        connect(kw, &KWin::Window::activitiesChanged, this, invalidateForContextMove);
        // WindowRole is likewise matchable and stamped live; role changes are
        // rare (X11 clients setting WM_WINDOW_ROLE post-map), so the heavier
        // immediate class-swap invalidation is fine here and keeps the
        // identity-change family on one code path.
        connect(kw, &KWin::Window::windowRoleChanged, this, invalidateRuleCache);

        // Diagnostic dump on identity change — but ONLY for class / desktop-file,
        // never caption. CEF/Electron apps (Steam included) map with a
        // placeholder class and swap in the real one here, so re-dumping on
        // those catches the final classification the filters act on. Caption
        // is deliberately excluded: it feeds no filter, and terminals /
        // browsers (and this very tool's progress spinner) rewrite their title
        // every frame — dumping on captionChanged floods the journal with
        // identical blocks. See logWindowDiagnostics().
        auto logIdentityChange = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                logWindowDiagnostics(safeW, "identityChanged");
            }
        };
        connect(kw, &KWin::Window::windowClassChanged, this, logIdentityChange);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, logIdentityChange);
    }
}

} // namespace PlasmaZones
