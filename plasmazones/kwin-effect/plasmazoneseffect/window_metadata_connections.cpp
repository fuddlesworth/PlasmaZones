// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-window identity and metadata wiring for PlasmaZonesEffect: the
// registry pushes on a class, desktop-file, caption or role change, the
// rule-cache invalidations those fields and a desktop or activity edit feed
// (that edit's push is runContextEdit's, window_desktop_connections.cpp),
// and the identity diagnostic dump. Called once per window from
// setupWindowConnections.

#include "plasmazoneseffect.h"
#include "desktopvisibility.h"
#include "tilinghandler/tilinghandler.h"

#include <effect/effecthandler.h>
#include <window.h>

#include <QPointer>

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
        // The registry only updates its record. It does NOT retroactively
        // unsnap, re-snap, or re-evaluate rules — that would surprise users.
        // Committed state stays committed.
        auto pushLatest = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                pushWindowMetadata(safeW);
            }
        };
        // Caption changes fire every frame for terminals / browsers; the extended
        // property snapshot (geometry / state flags) doesn't change on a title tick,
        // so send only the core fields (app id, desktop file, title, role, pid,
        // desktop, activity, window type, plus captionNormal) WITHOUT rebuilding
        // and marshalling the extended a{sv} each frame. The daemon applies the
        // core fields as sent and keeps the existing extended fields.
        auto pushCaptionOnly = [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            const QString caption = safeW->caption();
            const QString windowId = getWindowId(safeW.data());
            // A compositor-drawn tab pill shows the live caption in the CHIPS
            // style, so it is rebuilt on every change whatever the daemon has
            // been sent, skipping screens whose bar style never draws it (a
            // chatty terminal title must not re-raster a bar that cannot
            // change). One hash probe when it is not a tab anywhere, the
            // common case.
            m_tilingHandler->noteScrollTabTitleChanged(windowId);
            // Title and CaptionNormal are live rule fields. The eviction rides
            // the frame-geometry flush, one per window per 50 ms, so a title
            // rewritten every frame costs one re-resolve per flush (F864).
            if (m_hasCaptionScopedRules) {
                m_pendingCaptionEvictions.insert(windowId);
                if (!m_frameGeometryFlushTimer->isActive()) {
                    m_frameGeometryFlushTimer->start();
                }
            }
            // Skip a push of the caption the daemon already holds: KWin can
            // emit captionChanged without a net change, and the marshal (plus
            // the daemon-side upsert) should not ride those. pushWindowMetadata
            // records each caption it sends, full push or caption-only, so a
            // daemon that came back after a title change is compared against
            // what IT was sent (F170, F345). An unchanged caption is safe to
            // skip though the push carries more than the title: every other
            // core field has its own push (the class, desktop-file and role
            // connects below, and the desktop and activity edits), pid and the
            // window type do not change, and captionNormal derives from the
            // caption.
            if (const auto sent = m_lastPushedCaption.constFind(safeW.data());
                sent != m_lastPushedCaption.constEnd() && *sent == caption) {
                return;
            }
            pushWindowMetadata(safeW, /*includeExtended=*/false);
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
        // CAPTION is not cleared here: Title and CaptionNormal are live rule
        // fields, and pushCaptionOnly hands a changed caption to the throttled
        // per-window eviction (flushPendingFrameGeometry) when any rule reads
        // them, so a title that changes every frame never clears the global
        // caches.
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
        // Role changes also refresh the registry. The record-only contract
        // above holds for class, desktop file and role. A desktop or activity
        // change does drive the daemon's membership reconcile (#1076), and its
        // push is the first step of that axis's handler
        // (window_desktop_connections.cpp), so it reaches the daemon before the
        // handler's own notices.
        connect(kw, &KWin::Window::windowRoleChanged, this, pushLatest);
        // VirtualDesktop and Activity are matchable rule fields stamped live
        // into the per-window query, but the verdict caches key on
        // (windowId, ruleSet revision) — neither moves on a desktop or
        // activity move, so a `WHEN VirtualDesktop Equals N` exclusion or
        // appearance verdict would pin stale across the move. Enqueue the
        // coalesced per-window invalidation, mirroring the outputChanged
        // handler; the flush clears the caches and re-drives decoration /
        // title bar / layer for exactly this window.
        // A resize or a context hold runs it from its release instead.
        auto invalidateForContextMove = [this, safeW]() {
            if (safeW && !safeW->isDeleted() && m_resizeHold.window != safeW
                && !m_contextHold.holds.contains(safeW.data())) {
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
        // is deliberately excluded: terminals and browsers (and this very
        // tool's progress spinner) rewrite their title every frame, and
        // dumping on captionChanged floods the journal with identical blocks.
        // See logWindowDiagnostics().
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
