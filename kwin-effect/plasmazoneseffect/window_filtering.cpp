// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleEvaluator.h>

#include <core/output.h>
#include <effect/effecthandler.h>
#include <window.h>
#include <workspace.h>

#include <QLoggingCategory>
#include <QSet>

#include <optional>

#include "tilinghandler/tilinghandler.h"
#include "handlers/navigationhandler.h"
#include "window_query.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)
Q_DECLARE_LOGGING_CATEGORY(lcEffectDiag)

namespace {
// Record a filter rejection: when @p out is non-null, store @p reason into it.
// Lets each filter early-exit read as a single `return rejectedBecause(out,
// "...");`, keeping the reason text co-located with the clause that produced
// it — so logWindowDiagnostics never has to re-derive (and risk drifting from)
// the filter logic.
bool rejectedBecause(QString* out, const char* reason)
{
    if (out) {
        *out = QString::fromLatin1(reason);
    }
    return false;
}
} // namespace

QHash<QString, KWin::EffectWindow*> PlasmaZonesEffect::buildWindowMap() const
{
    const auto windows = KWin::effects->stackingOrder();
    QHash<QString, KWin::EffectWindow*> windowMap;
    windowMap.reserve(windows.size());
    for (KWin::EffectWindow* w : windows) {
        // Close-shader grabs keep deleted windows in the stacking order;
        // mapping them would re-pollute the just-scrubbed id caches via
        // getWindowId and let a dying sibling shadow a live window in
        // appId-fallback lookups.
        if (w && !w->isDeleted() && shouldHandleWindow(w)) {
            windowMap[getWindowId(w)] = w;
        }
    }
    return windowMap;
}

QRectF PlasmaZonesEffect::freeGeometryForCapture(KWin::EffectWindow* w, const QRectF& fallback) const
{
    // A maximized or fullscreen window's frameGeometry() is the full-monitor rect.
    // Capturing THAT as a window's pre-tile / pre-snap / float-back geometry makes it
    // float back at a maximized size. Substitute the pre-maximize / pre-fullscreen
    // RESTORE rect (a genuine free size). EffectWindow exposes neither; go through the
    // underlying KWin::Window (mirrors window_query.cpp's maximizeMode read).
    if (!w) {
        return fallback;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return fallback;
    }
    // A windowed-fullscreen strip column has NO free geometry to offer: its
    // live frame is the column rect, and because the effect flipped the
    // fullscreen state while the window sat at its previous column rect,
    // KWin's fullscreenGeometryRestore is a column rect too — both are tile
    // rects, the exact poison this function exists to keep out of the
    // free-geometry store. Invalid is the honest answer; every caller
    // already treats it as "skip the capture".
    if (!m_windowedFullscreenWindows.isEmpty() && !w->isDeleted()
        && m_windowedFullscreenWindows.contains(getWindowId(w))) {
        return QRectF();
    }
    // Off-screen poison guard: the scrolling engine parks off-viewport
    // columns and hidden tabs ENTIRELY outside every screen rect, so a
    // capture that runs while the frame is parked (float toggle on a parked
    // tile, unminimize-unfloat, cross-screen transfer) would persist an
    // off-screen rect as the window's restore geometry and the window would
    // later restore off-screen. A rect intersecting no screen is never a
    // legitimate free geometry — return invalid so the caller skips the
    // capture entirely (this is the shared chokepoint for both engines'
    // free-geometry capture paths). The restore rects below get the same
    // check: KWin records geometryRestore from the live frame, so a
    // maximize while parked snapshots the parked rect too.
    const auto onAnyScreen = [](const QRectF& rect) {
        if (!KWin::effects) {
            // No compositor context to verify against (teardown); treat as
            // on-screen rather than discarding a possibly-good capture.
            return true;
        }
        for (const auto* screen : KWin::effects->screens()) {
            if (rect.intersects(QRectF(screen->geometry()))) {
                return true;
            }
        }
        return false;
    };
    bool restoreBranchEntered = false;
    if (kw->isFullScreen()) {
        restoreBranchEntered = true;
        // A window maximized and THEN made fullscreen has a fullscreenGeometryRestore()
        // equal to the maximized (full work-area) rect, so it would still float back
        // maximized-sized. When the pre-fullscreen state was itself maximized, prefer
        // the un-maximized geometryRestore() (the true free size), then the fullscreen
        // restore rect, before giving up to the fallback.
        if (kw->maximizeMode() != KWin::MaximizeRestore) {
            const QRectF unmaximized(kw->geometryRestore());
            if (unmaximized.width() > 0 && unmaximized.height() > 0 && onAnyScreen(unmaximized)) {
                return unmaximized;
            }
        }
        const QRectF restore(kw->fullscreenGeometryRestore());
        if (restore.width() > 0 && restore.height() > 0 && onAnyScreen(restore)) {
            return restore;
        }
    } else if (kw->maximizeMode() != KWin::MaximizeRestore) {
        restoreBranchEntered = true;
        const QRectF restore(kw->geometryRestore());
        if (restore.width() > 0 && restore.height() > 0 && onAnyScreen(restore)) {
            return restore;
        }
    }
    // A maximized/fullscreen window whose restore rects were all rejected
    // must NOT fall through to the live frame — that IS the full-monitor
    // rect this function exists to keep out of the free-geometry store.
    if (restoreBranchEntered) {
        return QRectF();
    }
    if (!onAnyScreen(fallback)) {
        return QRectF();
    }
    return fallback;
}

bool PlasmaZonesEffect::isWindowFloating(const QString& windowId) const
{
    return m_navigationHandler->isWindowFloating(windowId);
}

bool PlasmaZonesEffect::isWindowSnapped(const QString& windowId) const
{
    return m_navigationHandler->isWindowSnapped(windowId);
}

QString PlasmaZonesEffect::zoneForWindow(const QString& windowId) const
{
    return m_navigationHandler->zoneForWindow(windowId);
}

void PlasmaZonesEffect::clearWindowZone(const QString& windowId)
{
    m_navigationHandler->clearWindowZone(windowId);
}

PhosphorRules::WindowQuery PlasmaZonesEffect::ruleQuery(KWin::EffectWindow* w) const
{
    const QString windowId = getWindowId(w);
    // Id-taking overload: the scroll override resolves off the window id, and
    // this funnel already holds it.
    const QString screenId = getWindowScreenId(w, windowId);
    PhosphorRules::WindowQuery query = ruleQueryFor(w, screenId, isWindowFloating(windowId), isWindowSnapped(windowId),
                                                    m_tilingHandler->isTiledWindow(windowId), zoneForWindow(windowId));
    // Scroll-managed windows ride the same tile-request pipeline as autotile,
    // so ruleQueryFor derives "tiling" from the tiled bit; re-stamp from the
    // per-screen engine discriminator so a `Mode Equals "scrolling"` window
    // rule matches strip-managed windows.
    //
    // Bring-up hazard, accepted deliberately: m_scrollingScreens is empty until
    // the daemon's scrollingScreens reply lands, so a scroll-managed window
    // resolved in that window stamps "tiling". Mode gets no seeded twin like
    // ActiveLayout's — holding every Mode rule out of the evaluator until the
    // set arrives would also disarm them for genuinely floating and snapped
    // windows, whose Mode is correct from the first frame, and the field is
    // engaged either way so no negation hazard is opened. The self-correction
    // is setScrollingScreens' invalidateAllRuleCaches plus its border sweep,
    // which drop and rebuild every verdict memoised against the empty set once
    // the reply lands.
    if (query.mode == PhosphorRules::ModeToken::Tiling && m_tilingHandler->isScrollingScreen(screenId)) {
        query.mode = QString(PhosphorRules::ModeToken::Scrolling);
    }
    // Stamp the rules-visible active layout the daemon pushed for this
    // window's screen (snapping UUID / "autotile:<algo>" /
    // "scrolling:<templateUuid>" / bare sentinel) so window-domain rules can
    // match Field::ActiveLayout with the same vocabulary as the daemon's
    // context rules. Replaces the engaged-empty stamp ruleQueryFor left,
    // which made every negated ActiveLayout rule fire for every window.
    // Unconditional by design: the bring-up window where the map is not yet
    // seeded is covered one layer up, by holding ActiveLayout-referencing
    // rules out of the evaluator entirely (TilingHandler::activeLayoutsSeeded).
    // A gate here could not help — the field resolves engaged either way.
    query.activeLayout = m_tilingHandler->activeLayoutForScreen(screenId);
    // Stamp the screen orientation from the resolved screen id. ruleQueryFor
    // leaves the field to us whenever it is handed an id: its own derivation
    // is centre-based, which answers for the wrong monitor (or not at all) for
    // a scroll strip's windows parked off-screen beside their column. The id
    // is authoritative for those — it comes from the engine's own per-window
    // screen override. An id that resolves to no output (a screen that just
    // disconnected) falls back to the centre-derived answer.
    QString orientation;
    if (const KWin::LogicalOutput* output = outputForScreenId(screenId)) {
        if (const QRect g = output->geometry(); g.isValid()) {
            orientation = g.height() > g.width() ? QStringLiteral("portrait") : QStringLiteral("landscape");
        }
    }
    // The centre-derived fallback is skipped when ruleQueryFor already stamped
    // one: it takes that branch for exactly the empty-screenId case, from the
    // same helper on the same frame, so re-running it would recompute a value
    // that is already in the query (a screenAt walk per rule query).
    if (orientation.isEmpty() && query.screenOrientation.isEmpty()) {
        orientation = centreScreenOrientation(w);
    }
    if (!orientation.isEmpty()) {
        query.screenOrientation = orientation;
    }
    applyOwnLayerFlags(query, windowId);
    return query;
}

void PlasmaZonesEffect::applyOwnLayerFlags(PhosphorRules::WindowQuery& query, const QString& windowId) const
{
    // KeepAbove / KeepBelow must report the window's OWN flags, not
    // rule-written state. SetWindowLayer is the one action that mutates a
    // matchable field: while a layer rule owns the pair, the live KWin flags
    // are the rule's output, and feeding them back into the query would make
    // a `WHEN KeepAbove` predicate read its own effect — a self-feeding match
    // that flips on every cache flush and thrashes the snapshot restore. The
    // pre-rule snapshot holds the app/user-set values, so substitute those
    // while the rule owns the layer. Applied to BOTH rule-input boundaries:
    // ruleQuery (effect-side evaluation) and pushWindowMetadata (the daemon's
    // KeepAbove/KeepBelow match inputs).
    // The windowed-fullscreen layer demotion is a SECOND writer of the same
    // KWin pair (keepAbove=false, keepBelow=true for the hold), so its
    // snapshot substitutes too — otherwise every flagged tile reads
    // keepBelow=true at the rule boundary and a `WHEN KeepBelow` rule fires
    // off the feature's own output. Rule-snapshot-first: both maps can hold
    // an entry for one window (the demotion snapshots whatever the rule had
    // already written), and only the rule snapshot holds the pre-RULE value.
    if (m_ruleWindowLayerSnapshots.isEmpty() && m_windowedFsLayerSnapshots.isEmpty()) {
        return;
    }
    if (const auto it = m_ruleWindowLayerSnapshots.constFind(windowId); it != m_ruleWindowLayerSnapshots.cend()) {
        query.keepAbove = it->keepAbove;
        query.keepBelow = it->keepBelow;
    } else if (const auto fsIt = m_windowedFsLayerSnapshots.constFind(windowId);
               fsIt != m_windowedFsLayerSnapshots.cend()) {
        query.keepAbove = fsIt->keepAbove;
        query.keepBelow = fsIt->keepBelow;
    }
}

bool PlasmaZonesEffect::windowOwnKeepAbove(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }
    // The window's OWN keep-above flag — the app/user-set state, excluding
    // rule-written values. The keep-above overlay gates (shouldHandleWindow /
    // shouldDecorateWindow / isTileableWindow) exist to reject external
    // overlay tools (Spectacle, colour pickers) that set keep-above on
    // themselves; a window raised by a SetWindowLayer rule must NOT be
    // misclassified as one of those, or the headline "floating windows above
    // tiled windows" rule would silently strip the matched window's
    // decoration and drop it from snap/tile management. While a layer rule
    // owns the pair, the pre-rule snapshot holds the window's own flag.
    //
    // Empty-map fast path: with no window rule-raised, the own flag IS the
    // live flag. Keeps the gates getWindowId-free for the common no-layer-rule
    // session (the "no-rules case pays nothing" invariant).
    // The windowed-fullscreen demotion writes the same pair, so its snapshot
    // substitutes here too (rule-snapshot-first, same reasoning as
    // applyOwnLayerFlags) — a window the user genuinely keep-above'd must
    // keep being rejected by the keep-above gates through the hold.
    if (m_ruleWindowLayerSnapshots.isEmpty() && m_windowedFsLayerSnapshots.isEmpty()) {
        return w->keepAbove();
    }
    // Close-grabbed corpse: its flags are frozen and slotWindowClosed already
    // dropped any snapshot it had, so answer from the live flag WITHOUT
    // getWindowId — several gate callers don't pre-check isDeleted(), and
    // getWindowId on a dying window would re-insert the reverse-map entry
    // buildWindowMap deliberately skips. Unconditional (not folded into the
    // fast path above) so the no-repollution guarantee holds even while a
    // layer rule owns some other window.
    if (w->isDeleted()) {
        return w->keepAbove();
    }
    const QString windowId = getWindowId(w);
    if (const auto it = m_ruleWindowLayerSnapshots.constFind(windowId); it != m_ruleWindowLayerSnapshots.cend()) {
        return it->keepAbove;
    }
    if (const auto fsIt = m_windowedFsLayerSnapshots.constFind(windowId); fsIt != m_windowedFsLayerSnapshots.cend()) {
        return fsIt->keepAbove;
    }
    return w->keepAbove();
}

PhosphorRules::ResolvedActions
PlasmaZonesEffect::resolveRuleActions(KWin::EffectWindow* w, const QString& windowId,
                                      std::optional<PhosphorRules::WindowQuery>* sharedQuery) const
{
    const PhosphorRules::RuleEvaluator& evaluator = m_shaderManager.animationRuleEvaluator();
    // An empty windowId can't key the per-window cache; nothing to resolve.
    if (windowId.isEmpty()) {
        return {};
    }
    // Cache hit → skip the ≈30-accessor ruleQuery(w) build entirely. The
    // cached verdict already reflects whatever query produced it, and resolveCached
    // ignores the query on a hit anyway.
    if (std::optional<PhosphorRules::ResolvedActions> cached = evaluator.resolveCachedIfPresent(windowId)) {
        return std::move(*cached);
    }
    // Miss → build the query once and resolve (caching the result), reusing the
    // caller's slot when it already holds this window's query (see the header
    // doc) and filling it otherwise. Defensive guard
    // against a windowless query (no engaged window attribute): it can't fill any
    // slot, so return empty actions WITHOUT caching to avoid a useless cache entry.
    // In practice a non-null w always engages placement/state attributes, so this
    // only ever covers the already-handled empty-windowId case — kept as a belt.
    std::optional<PhosphorRules::WindowQuery> localQuery;
    std::optional<PhosphorRules::WindowQuery>& slot = sharedQuery ? *sharedQuery : localQuery;
    if (!slot) {
        slot = ruleQuery(w);
    }
    if (!slot->hasWindow()) {
        return {};
    }
    return evaluator.resolveCached(windowId, *slot);
}

PhosphorRules::ResolvedActions PlasmaZonesEffect::resolveRuleVerdictActions(KWin::EffectWindow* w,
                                                                            const QString& windowId) const
{
    // Structurally identical to resolveRuleActions above, against the
    // effect-VERDICT evaluator instead. The two caches are keyed the same way
    // but live on different evaluators, so a hit in one says nothing about the
    // other — which is the point: the verdict walk must not stop at an
    // ExcludeAnimations action the appearance walk correctly honours.
    const PhosphorRules::RuleEvaluator& evaluator = m_shaderManager.effectVerdictRuleEvaluator();
    if (windowId.isEmpty()) {
        return {};
    }
    if (std::optional<PhosphorRules::ResolvedActions> cached = evaluator.resolveCachedIfPresent(windowId)) {
        return std::move(*cached);
    }
    const PhosphorRules::WindowQuery query = ruleQuery(w);
    if (!query.hasWindow()) {
        return {};
    }
    return evaluator.resolveCached(windowId, query);
}

bool PlasmaZonesEffect::isStructurallyUnmanageableWindowType(KWin::EffectWindow* w, QString* rejectReason,
                                                             bool exemptFullscreen) const
{
    // Single source of truth for the window-TYPE rejection set shared by
    // shouldHandleWindow() (snap/zone filter), notifyWindowActivated()
    // (focus-tracking filter) and classifyWindowKind() (window_identity.cpp).
    // They must reject the exact same structural
    // types: a window kind that can never legitimately be a snap/autotile
    // target must also never be reported as the active window, or the daemon's
    // focus tracking gets pinned to a popup. Discussion #461 item 11 (Steam
    // image popups) was a missed sync between two hand-maintained copies of
    // this list — keeping it in one function makes that class of drift
    // unrepresentable.
    //
    // isTileableWindow() deliberately keeps its own, narrower list (it gates
    // on !isNormalWindow()) and is NOT folded in here.

    // Null is structurally unmanageable. Every caller null-checks before
    // reaching here, so this is a defensive guard that keeps the precondition
    // enforced rather than merely documented for any future caller.
    if (!w) {
        if (rejectReason) {
            *rejectReason = QStringLiteral("null window");
        }
        return true;
    }

    // Special / non-manageable window types (inherently effect-side — KWin metadata).
    //
    // isFullScreen() is the one REVERSIBLE state in an otherwise type-based
    // set, and that is deliberate: a fullscreen window must not be a snap or
    // autotile target while it is fullscreen, and every consumer of this
    // predicate wants that. The consequence to know about is that it also
    // suppresses activation reporting and classifies a fullscreen window as
    // Transient for as long as the state lasts; both revert when the window
    // leaves fullscreen. Do not "clean up" the state check out of here without
    // re-adding an equivalent rejection at the snap/tile call sites.
    //
    // Exempt from the fullscreen half only: a scrolling WINDOWED-FULLSCREEN
    // window. Its fullscreen state is the effect's own doing and it stays a
    // managed strip tile — dropping it here would stop activation reporting
    // and tile routing for exactly the window the strip still lays out.
    // Note this also flips two focus-follows-mouse behaviours on purpose (a
    // flagged column is hovered-to-focus like any column, and it no longer
    // pauses FFM as the active window — both the niri behaviour), and it
    // makes Exclude-family rules scoped `WHEN IsFullscreen` newly observable
    // for flagged columns (previously the structural reject here fired
    // first).
    //
    // The isEmpty() term keeps the no-feature session at zero cost, and the
    // isDeleted() term preserves this predicate's documented contract of
    // never calling getWindowId on a deleted window (shouldHandleWindow's
    // ordering note): a cache-missing lookup would re-insert the reverse-map
    // entry buildWindowMap deliberately skips for corpses.
    const bool fullScreenUnmanageable = !exemptFullscreen && w->isFullScreen()
        && (m_windowedFullscreenWindows.isEmpty() || w->isDeleted()
            || !m_windowedFullscreenWindows.contains(getWindowId(w)));
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || fullScreenUnmanageable || w->isSkipSwitcher()) {
        if (rejectReason) {
            *rejectReason = QStringLiteral("special/desktop/dock/fullscreen/skipSwitcher window type");
        }
        return true;
    }

    // Transient / dialog / menu family. transientFor() catches child surfaces
    // that Electron/CEF apps (Steam, Discord, VS Code) spawn for image
    // previews, context menus and popups: these frequently fail to report an
    // accurate KWin window type (isDialog/isPopupWindow stay false) but always
    // set the transient-parent relationship. Without this the popup passes the
    // filter and gets snapped to a zone (discussion #461 item 11).
    // The bare transientFor() term shares the fullscreen exemption: Wine and
    // Proton toplevels routinely carry transient_for on the real game
    // window, and the exemption exists precisely so a fullscreen game on a
    // scrolling screen keeps reporting its focus (the original live bug).
    // Every EXPLICIT type term stays authoritative — a fullscreen dialog,
    // splash or popup is still rejected.
    if (w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isCriticalNotification()
        || w->isOnScreenDisplay() || w->isModal() || w->isPopupWindow() || w->isPopupMenu() || w->isDropdownMenu()
        || w->isMenu() || w->isTooltip() || (!exemptFullscreen && w->transientFor())) {
        if (rejectReason) {
            // Coarse reason — logWindowDiagnostics() dumps every flag in this
            // clause individually, so the caller can pinpoint which one fired.
            *rejectReason =
                QStringLiteral("transient/dialog/utility/splash/notification/osd/modal/popup/menu/tooltip window type");
        }
        return true;
    }

    return false;
}

bool PlasmaZonesEffect::isShowingDesktop()
{
    const auto* ws = KWin::Workspace::self();
    return ws && ws->showingDesktop();
}

bool PlasmaZonesEffect::shouldHandleWindow(KWin::EffectWindow* w, QString* rejectReason, bool exemptFullscreen) const
{
    if (rejectReason) {
        rejectReason->clear();
    }

    if (!w) {
        return rejectedBecause(rejectReason, "null window");
    }

    // Never snap our own overlay/editor windows (but allow the settings app).
    // Shared with the FFM stacking-order walk — see isOwnOverlayClass().
    const QString windowClass = w->windowClass();
    if (isOwnOverlayClass(windowClass)) {
        return rejectedBecause(rejectReason, "own overlay/editor window class");
    }

    // Exclude XDG desktop portal windows (file dialogs, color pickers, etc.)
    if (isXdgDesktopPortalSurface(windowClass)) {
        return rejectedBecause(rejectReason, "xdg-desktop-portal window class");
    }

    // Plasma shell layer-shell surfaces — see isPlasmaShellSurface() for rationale.
    if (isPlasmaShellSurface(windowClass)) {
        return rejectedBecause(rejectReason, "Plasma shell layer-shell surface");
    }

    // Skip structural / transient / dialog / menu window types BEFORE the
    // rule evaluation below: this is a cheap type check, while the rule slice
    // builds the full ~30-accessor ruleQuery, and hot callers (buildWindowMap,
    // the stacking walks) hit this filter for every tooltip/popup/menu. The
    // predicate is shared verbatim with the other structural filters so they
    // can never drift — see isStructurallyUnmanageableWindowType().
    // exemptFullscreen threads through untouched: the caller that sets it
    // (isEligibleForTilingNotify, for a fullscreen window on a scrolling
    // screen) needs the structural fullscreen reject waived here, because at
    // effect bring-up the membership hash is empty and this reject would
    // otherwise fire 30 lines before the eligibility function's own
    // scrolling-screen exemption could — making the documented re-adoption
    // of a windowed-fullscreen column unreachable.
    if (isStructurallyUnmanageableWindowType(w, rejectReason, exemptFullscreen)) {
        return false;
    }

    // Close-grabbed corpse: reject BEFORE the rule slice below, which builds a
    // ruleQuery and therefore calls getWindowId(w). That re-inserts the
    // reverse-map entry buildWindowMap deliberately skips for deleted windows
    // — the very pollution windowOwnKeepAbove's own isDeleted guard exists to
    // prevent, which is inert here because ruleQuery runs first. A dying
    // window is never a snap target regardless.
    if (w->isDeleted()) {
        return rejectedBecause(rejectReason, "deleted window");
    }

    // Keep-above overlays (Spectacle, color pickers, screen rulers, screenshot
    // tools that linger after capture) shouldn't be snapped to a zone — same
    // rationale as isTileableWindow's keep-above gate. Consults the window's
    // OWN flag (see windowOwnKeepAbove) so a SetWindowLayer-raised window
    // stays manageable. Checked BEFORE the rule slice below: this is a flag
    // read, while a rule-cache miss builds the full ~30-accessor ruleQuery,
    // and both are pure rejects so the order is behaviour-neutral.
    if (windowOwnKeepAbove(w)) {
        return rejectedBecause(rejectReason, "keep-above window");
    }

    // Check user-authored / migrated exclusion rules (needed for drag gating —
    // daemon also enforces these for keyboard navigation, but the effect
    // must filter for drag operations and lifecycle reporting).
    // `m_snappingExclusionRuleSet` mirrors the placement-exclusion slice
    // (Exclude ∪ ExcludePlacement) of the unified Rule store, refreshed on
    // every rulesChanged via loadRuleAnimationsFromDbus (see
    // shader_config_dbus.cpp).
    if (isExcludedBySnappingRule(w)) {
        return rejectedBecause(rejectReason, "user exclusion rule match");
    }

    return true;
}

bool PlasmaZonesEffect::isExcludedBySnappingRule(KWin::EffectWindow* w,
                                                 std::optional<PhosphorRules::WindowQuery>* sharedQuery) const
{
    // The `isEmpty()` fast path keeps a no-exclusions user at two pointer
    // reads — same cost as the prior list-derived check.
    if (m_snappingExclusionRuleSet.isEmpty()) {
        return false;
    }
    // Per-window verdict cache, mirroring resolveRuleActions: the hot callers
    // (buildWindowMap per batch, hasOtherWindowOfClassWithDifferentPid's
    // stacking-order sweep per open) would otherwise pay a full ~30-accessor
    // ruleQuery build per window per consult — O(N^2) query builds across a
    // login burst. Freshness matches the animation verdicts: the cache is
    // revision-keyed for rule edits and cleared by the same placement /
    // class-swap invalidation paths (flushPendingRuleInvalidations,
    // invalidateAllRuleCaches, the metadataChanged lambda).
    // Caller-owned memoisation slot (see the header doc): a miss builds the query
    // into it so the caller's own resolver pass reuses the build instead of
    // walking the ~30 accessors a second time for the same window.
    std::optional<PhosphorRules::WindowQuery> localQuery;
    std::optional<PhosphorRules::WindowQuery>& slot = sharedQuery ? *sharedQuery : localQuery;
    const auto query = [&]() -> const PhosphorRules::WindowQuery& {
        if (!slot) {
            slot = ruleQuery(w);
        }
        return *slot;
    };
    const QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return m_snappingExclusionEvaluator.resolve(query()).isExcluded();
    }
    if (std::optional<PhosphorRules::ResolvedActions> cached =
            m_snappingExclusionEvaluator.resolveCachedIfPresent(windowId)) {
        return cached->isExcluded();
    }
    return m_snappingExclusionEvaluator.resolveCached(windowId, query()).isExcluded();
}

bool PlasmaZonesEffect::isExcludedByDecorationRule(KWin::EffectWindow* w) const
{
    // Mirrors isExcludedBySnappingRule over the decoration slice (Exclude ∪
    // ExcludeDecorations): empty-slice fast path, then the per-window verdict
    // cache. The cache shares the snapping evaluator's freshness contract —
    // revision-keyed for rule edits, cleared by the same placement /
    // class-swap invalidation paths (flushPendingRuleInvalidations,
    // invalidateAllRuleCaches, the metadataChanged lambda).
    if (m_decorationExclusionRuleSet.isEmpty()) {
        return false;
    }
    const QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return m_decorationExclusionEvaluator.resolve(ruleQuery(w)).isExcluded();
    }
    if (std::optional<PhosphorRules::ResolvedActions> cached =
            m_decorationExclusionEvaluator.resolveCachedIfPresent(windowId)) {
        return cached->isExcluded();
    }
    return m_decorationExclusionEvaluator.resolveCached(windowId, ruleQuery(w)).isExcluded();
}

bool PlasmaZonesEffect::shouldAnimateWindow(KWin::EffectWindow* w,
                                            std::optional<PhosphorRules::WindowQuery>* sharedQuery) const
{
    if (!w) {
        return false;
    }

    const QString windowClass = w->windowClass();

    // NOTE: unlike shouldHandleWindow and shouldDecorateWindow, this gate
    // deliberately has NO isDeleted() bail. slotWindowClosed calls
    // tryBeginShaderForEvent BEFORE the id-cache scrub, and a window.close
    // animation genuinely needs the id — adding the guard here would kill
    // close transitions. Do not "complete the refactor" by adding one.
    //
    // Structural non-window surfaces — panels (docks), the desktop,
    // plasmoid / Plasma-shell surfaces, and other special or
    // skip-switcher windows are never application windows; a
    // window-event shader on them is always wrong. Hard-excluded with
    // no toggle and ahead of the rule-override path. This set deliberately
    // DIVERGES from isStructurallyUnmanageableWindowType: it omits
    // isFullScreen() (a fullscreen window closing should still animate) and
    // the transient/dialog family (handled toggleably below). Do NOT fold the
    // two into one predicate — shouldDecorateWindow carries the same warning.
    //
    // Our OWN surfaces (the daemon's layer-shell overlays, the editor
    // toplevel) and portal dialogs are rejected here for the same reason
    // shouldHandleWindow() and shouldDecorateWindow() reject them: animating
    // our own UI redirects it through OffscreenEffect and makes the overlay
    // that is drawing the animation itself animate. shouldAnimateWindow is
    // the ONLY filter on the tryBeginShaderForEvent path (window_lifecycle),
    // so omitting them here is not covered elsewhere.
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isSkipSwitcher()
        || isPlasmaShellSurface(windowClass) || isOwnOverlayClass(windowClass)
        || isXdgDesktopPortalSurface(windowClass)) {
        return false;
    }

    // Lazy per-window query — built at most once across the rule-override
    // gate AND the exclusion gate below. Both gates take the same full-
    // context WindowQuery (AppId / WindowClass / Title / WindowRole /
    // DesktopFile / WindowType / Pid / state flags), and `ruleQueryFor`
    // walks ~30 KWin accessors plus several QString copies — wasted work
    // when both rule sets fire (same note as on `resolveRuleActions`
    // above). The std::optional memoises so the function
    // pays at most one build no matter how many gates consult it, while
    // the `!isEmpty()` fast paths below keep the no-rules user's cost at
    // two pointer reads (query never built).
    std::optional<PhosphorRules::WindowQuery> cachedQuery;
    auto query = [&]() -> const PhosphorRules::WindowQuery& {
        // Prefer the caller's memoisation slot (see the header doc) so the
        // build survives this call for the caller's own resolver pass.
        std::optional<PhosphorRules::WindowQuery>& slot = sharedQuery ? *sharedQuery : cachedQuery;
        if (!slot) {
            slot = ruleQuery(w);
        }
        return *slot;
    };

    // Structural type exclusions (notification / OSD and the transient /
    // popup family) are AUTHORITATIVE over a coincidental class match. A
    // per-app rule like "firefox → dissolve" matches by windowClass, which
    // also catches Firefox's tooltips/popups (they share the class) — but the
    // user enabling "ignore transient" / "ignore notifications-OSD" must not
    // be silently overridden just because such a rule happens to match. A
    // rule re-enables an excluded window ONLY when it *deliberately targets
    // the window type*: its match expression references a type field. This
    // mirrors the global toggle — the user opts into transients via the
    // setting; a rule opts in via an explicit type predicate. The probe runs
    // against the animation rule set only (via the evaluator bound to it), so
    // a snapping/float rule that merely carries a windowType clause never
    // re-enables animation. These checks sit BEFORE the generic rule-override
    // gate so a class-only match cannot bypass them.
    const PhosphorRules::RuleEvaluator& animationEvaluator = m_shaderManager.animationRuleEvaluator();
    const bool haveAnimationRules = !m_shaderManager.animationRuleSet().isEmpty();

    if (m_animationExcludeNotificationsAndOsd
        && (w->isNotification() || w->isCriticalNotification() || w->isOnScreenDisplay())) {
        // IsNotification covers notification/critical/OSD; WindowType lets a
        // rule target a specific NET type. `!haveAnimationRules` short-circuits
        // so the WindowQuery is never built when there are no rules to probe.
        static const QSet<PhosphorRules::Field> kOsdTypeFields = {PhosphorRules::Field::IsNotification,
                                                                  PhosphorRules::Field::WindowType};
        if (!haveAnimationRules || !animationEvaluator.hasMatchTargetingFields(query(), kOsdTypeFields)) {
            return false;
        }
    }

    // Transient-window filter — dialogs / popups / tooltips / dropdowns /
    // menus / utility windows, plus any window with a transient parent.
    // Overridable only by a rule referencing IsTransient (the same
    // dialog/utility/popup/menu/tooltip/splash/transient-parent family),
    // WindowType, or IsModal.
    if (m_animationExcludeTransientWindows
        && (w->isDialog() || w->isUtility() || w->isPopupWindow() || w->isPopupMenu() || w->isDropdownMenu()
            || w->isTooltip() || w->isMenu() || w->isSplash() || w->transientFor())) {
        static const QSet<PhosphorRules::Field> kTransientTypeFields = {
            PhosphorRules::Field::IsTransient, PhosphorRules::Field::WindowType, PhosphorRules::Field::IsModal};
        if (!haveAnimationRules || !animationEvaluator.hasMatchTargetingFields(query(), kTransientTypeFields)) {
            return false;
        }
    }

    // Generic rule-override gate. A window that cleared the structural type
    // exclusions above and matches ANY animation rule is force-animated even
    // when the min-size / user-exclusion filters below would otherwise drop
    // it — the user's act of authoring a matching rule is the opt-in signal.
    // (Type exclusions are handled above and are NOT bypassable here.)
    //
    // The gate keys on the ANIMATION/APPEARANCE set only, which is what
    // narrows it: `m_shaderManager.animationRuleSet()` admits every rule
    // carrying a Tag::Effect action (shader_config_dbus.cpp's
    // `hasTag(type, Tag::Effect)` loop; the tag assignments in ruleaction.cpp
    // are the authoritative membership list), and Tag::Effect is now the
    // APPEARANCE family alone — the animation overrides, SetOpacity, the
    // border / title-bar / decoration-chain actions and SetWindowLayer. Each
    // of those changes how the window is drawn, so authoring one is a
    // legitimate opt-in signal for animating it too.
    // The two VERDICT actions (OpenFullscreen, ScrollFactor) carry
    // Tag::EffectVerdict instead and are bound to a different rule set, so a
    // rule whose only action is a scroll multiplier or an open-fullscreen
    // decision no longer force-animates its matches past the min-size and
    // user-exclusion filters below — neither action is an appearance change.
    // `hasAnyMatch` never surfaces a rule whose actions are EXCLUSIVELY
    // `ExcludeAnimations` — those route through the exclusion gate below.
    if (haveAnimationRules && animationEvaluator.hasAnyMatch(query())) {
        return true;
    }

    // Min-size filter — windows narrower or shorter than the threshold
    // are excluded. Zero (the default) disables each axis independently
    // so a user can set just one bound. Frame geometry is read live —
    // during minimize/close lifecycle a window may already be collapsed
    // when this fires, which is acceptable: the user opted into the
    // size threshold so a transient sub-threshold frame should suppress
    // the animation consistently with explicit-size cases.
    const QRectF frame = w->frameGeometry();
    if (m_animationMinWindowWidth > 0 && frame.width() < m_animationMinWindowWidth) {
        return false;
    }
    if (m_animationMinWindowHeight > 0 && frame.height() < m_animationMinWindowHeight) {
        return false;
    }

    // User-configured exclusion lists — routed through the unified
    // RuleEvaluator over the animation exclusion rule set, the same path
    // `shouldHandleWindow` uses for the snapping exclusions. Both filter
    // sets walk the full WindowQuery match expression (AppId / WindowClass /
    // Title / WindowRole / DesktopFile / WindowType / Pid / state flags),
    // so the two are in lockstep on match semantics even though their rule
    // sets are independent. The `!isEmpty()` fast path keeps a no-exclusions
    // user free.
    if (!m_animationExclusionRuleSet.isEmpty()) {
        if (m_animationExclusionEvaluator.resolve(query()).isExcluded()) {
            return false;
        }
    }

    return true;
}

bool PlasmaZonesEffect::shouldDecorateWindow(KWin::EffectWindow* w,
                                             std::optional<PhosphorRules::WindowQuery>* sharedQuery) const
{
    if (!w) {
        return false;
    }
    // Same reason as shouldHandleWindow: the rule slice below builds a
    // ruleQuery, whose getWindowId(w) would re-pollute the id caches for a
    // close-grabbed corpse. A dying window is never a decoration target.
    if (w->isDeleted()) {
        return false;
    }

    const QString windowClass = w->windowClass();

    // Always-wrong surfaces — never draw a border here regardless of any
    // toggle. Our own overlay/editor windows and xdg-portal surfaces mirror the
    // structural rejects in shouldHandleWindow; plasma-shell + the special/
    // desktop/dock/skipSwitcher set is shouldAnimateWindow's structural clause
    // PLUS isFullScreen(), which is decoration-only — do NOT fold the two into
    // one predicate. (Fullscreen is also rejected earlier in
    // updateWindowDecoration, but keep it here so the gate stands alone.)
    // The fullscreen reject deliberately carries NO windowed-fullscreen
    // membership exemption: a flagged strip tile presents as fullscreen, and
    // fullscreen surfaces wear no chrome — borderless-through-the-hold is
    // the intended presentation, and the un-flag paths' decoration re-drives
    // bring the chrome back when the hold ends.
    if (isOwnOverlayClass(windowClass) || isXdgDesktopPortalSurface(windowClass) || isPlasmaShellSurface(windowClass)) {
        return false;
    }
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()) {
        return false;
    }

    // Notification / OSD surfaces — hard-excluded with no toggle. A border on a
    // notification popup or a volume OSD is never sensible, so these are split
    // off from the transient family below (which IS toggleable) and always
    // rejected. There is no decoration NotificationsAndOsd knob.
    if (w->isNotification() || w->isCriticalNotification() || w->isOnScreenDisplay()) {
        return false;
    }

    // Keep-above overlays (Spectacle, colour pickers, screen rulers) — same
    // rejection shouldHandleWindow applies, preserved so upgrading doesn't
    // start bordering these lingering utility windows. Consults the window's
    // OWN flag (see windowOwnKeepAbove) so a SetWindowLayer-raised window
    // keeps its decoration. Checked before the rule slice for the same reason
    // as in shouldHandleWindow: a flag read beats a ruleQuery build, and both
    // are pure rejects.
    if (windowOwnKeepAbove(w)) {
        return false;
    }

    // User exclusion rules — the dedicated decoration slice (Exclude ∪
    // ExcludeDecorations). Blanket Exclude still strips decorations, keeping
    // the behavior from when this gate reused the snapping slice; the scoped
    // ExcludeDecorations strips only decorations, and the scoped
    // ExcludePlacement (which lives in the SNAPPING slice, not this one)
    // deliberately leaves decorations alone.
    if (isExcludedByDecorationRule(w)) {
        return false;
    }

    // Transient-window filter — dialogs / popups / tooltips / dropdowns /
    // menus / utility / splash windows, plus any window with a transient
    // parent. Unlike shouldHandleWindow (which rejects these unconditionally),
    // this is a user toggle: with it off the effect draws borders onto
    // transients. Defaults on, so today's behavior (no borders on transients)
    // is preserved.
    if (m_decorationExcludeTransientWindows
        && (w->isDialog() || w->isUtility() || w->isSplash() || w->isModal() || w->isPopupWindow() || w->isPopupMenu()
            || w->isDropdownMenu() || w->isMenu() || w->isTooltip() || w->transientFor())) {
        return false;
    }

    // Min-size filter — windows narrower or shorter than the threshold are not
    // decorated. Zero (the default) disables each axis independently. Frame
    // geometry is read live, consistent with the animation min-size gate.
    const QRectF frame = w->frameGeometry();
    if (m_decorationMinWindowWidth > 0 && frame.width() < m_decorationMinWindowWidth) {
        return false;
    }
    if (m_decorationMinWindowHeight > 0 && frame.height() < m_decorationMinWindowHeight) {
        return false;
    }

    return true;
}

bool PlasmaZonesEffect::isTileableWindow(KWin::EffectWindow* w, QString* rejectReason) const
{
    if (rejectReason) {
        rejectReason->clear();
    }

    if (!w) {
        return rejectedBecause(rejectReason, "null window");
    }

    // Reject menus, popups, tooltips, modals, and transient children.
    // Electron apps (Vesktop, VS Code, Discord) create separate windows
    // for context menus and dropdowns that pass shouldHandleWindow() but
    // must never enter the autotile tree.
    if (!w->isNormalWindow() || w->isModal() || w->isPopupWindow() || w->isDropdownMenu() || w->isPopupMenu()
        || w->isTooltip() || w->isMenu() || w->transientFor()) {
        // Coarse reason — logWindowDiagnostics() dumps every flag individually.
        return rejectedBecause(rejectReason, "non-normal/modal/popup/dropdown/menu/tooltip/transient window type");
    }
    // Reject keep-above windows — overlay/utility tools (Spectacle, color
    // pickers, screen rulers, etc.) set keep-above and should not enter the
    // autotile tree or receive auto-focus. Without this guard, opening
    // Spectacle while focusNewWindows is enabled disrupts the tiled layout.
    // Consults the window's OWN flag (see windowOwnKeepAbove) so a
    // SetWindowLayer-raised window stays tileable.
    if (windowOwnKeepAbove(w)) {
        return rejectedBecause(rejectReason, "keep-above window");
    }
    return true;
}

void PlasmaZonesEffect::logWindowDiagnostics(KWin::EffectWindow* w, const char* context) const
{
    // Silent-unless-enabled, as documented: the verdict computation below
    // walks both filter chains, which is real work to do just to hand
    // qCDebug strings it will discard.
    if (!lcEffectDiag().isDebugEnabled()) {
        return;
    }
    if (!w) {
        qCDebug(lcEffectDiag) << "[window-diag]" << context << "— null window";
        return;
    }

    QString handleReason;
    QString tileReason;
    const bool handle = shouldHandleWindow(w, &handleReason);
    const bool tileable = isTileableWindow(w, &tileReason);

    KWin::Window* kw = w->window();

    qCDebug(lcEffectDiag) << "[window-diag]" << context << "— class:" << w->windowClass() << "role:" << w->windowRole()
                          << "caption:" << w->caption() << "desktopFile:" << (kw ? kw->desktopFileName() : QString())
                          << "pid:" << w->pid();
    qCDebug(lcEffectDiag) << "[window-diag]   verdict — shouldHandleWindow:" << handle
                          << (handle ? QString() : QStringLiteral("[rejected: %1]").arg(handleReason))
                          << "| isTileableWindow:" << tileable
                          << (tileable ? QString() : QStringLiteral("[rejected: %1]").arg(tileReason));
    qCDebug(lcEffectDiag) << "[window-diag]   type — normal:" << w->isNormalWindow()
                          << "special:" << w->isSpecialWindow() << "dialog:" << w->isDialog()
                          << "utility:" << w->isUtility() << "splash:" << w->isSplash() << "modal:" << w->isModal()
                          << "toolbar:" << w->isToolbar() << "menu:" << w->isMenu()
                          << "popupWindow:" << w->isPopupWindow() << "popupMenu:" << w->isPopupMenu()
                          << "dropdownMenu:" << w->isDropdownMenu() << "tooltip:" << w->isTooltip()
                          << "notification:" << w->isNotification()
                          << "criticalNotification:" << w->isCriticalNotification()
                          << "onScreenDisplay:" << w->isOnScreenDisplay() << "appletPopup:" << w->isAppletPopup()
                          << "desktop:" << w->isDesktop() << "dock:" << w->isDock();
    qCDebug(lcEffectDiag) << "[window-diag]   state — managed:" << w->isManaged() << "x11:" << w->isX11Client()
                          << "wayland:" << w->isWaylandClient() << "fullScreen:" << w->isFullScreen()
                          << "minimized:" << w->isMinimized() << "skipSwitcher:" << w->isSkipSwitcher()
                          << "keepAbove:" << w->keepAbove() << "ownKeepAbove:" << windowOwnKeepAbove(w)
                          << "hasDecoration:" << w->hasDecoration() << "onCurrentDesktop:" << w->isOnCurrentDesktop()
                          << "onCurrentActivity:" << w->isOnCurrentActivity()
                          << "onAllDesktops:" << w->isOnAllDesktops();
    qCDebug(lcEffectDiag) << "[window-diag]   geometry — frame:" << w->frameGeometry()
                          << "minSize:" << (kw && !kw->isInternal() ? kw->minSize() : QSizeF());

    if (KWin::EffectWindow* parent = w->transientFor()) {
        qCDebug(lcEffectDiag) << "[window-diag]   transientFor — YES — parent class:" << parent->windowClass()
                              << "caption:" << parent->caption() << "normal:" << parent->isNormalWindow()
                              << "special:" << parent->isSpecialWindow() << "pid:" << parent->pid()
                              << "frame:" << parent->frameGeometry();
    } else {
        qCDebug(lcEffectDiag) << "[window-diag]   transientFor — none";
    }
}

bool PlasmaZonesEffect::hasOtherWindowOfClassWithDifferentPid(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    QString windowClass = w->windowClass();
    const pid_t windowPid = w->pid();
    // KWin reports -1 when the pid is unknown, notably during session restore
    // before the client reattaches (see window_identity.cpp, which clamps it for
    // the same reason). Comparing that sentinel as if it were a real pid makes
    // `-1 != <real pid>` true and reports "another app spawned this class" for
    // what is the same app — and session restore is exactly when the pid is
    // unknown AND several same-class windows appear together.
    //
    // "Cannot discriminate" is reported as false. Note the two callers read that
    // answer with OPPOSITE polarity, so this is a real behaviour choice rather
    // than a neutral one:
    //   * window_lifecycle.cpp, canSnapRestore = ... && !hasOther...  — false
    //     ALLOWS the restore. This is the case the fix targets: a window
    //     restored at login was being denied its snap position because a
    //     same-class sibling reported a real pid against its unknown one.
    //   * kwin-effect/handlers/snaphandler.cpp, snap-all skips when !hasOther... && the appId
    //     is already snapped — false can now SKIP a pid-unknown window that the
    //     old sentinel comparison would have snapped. Narrow: it needs an
    //     unknown pid, a same-class sibling, and the appId already in the
    //     snapped set. Accepted deliberately, because the login case is the
    //     common one and a wrongly-denied restore is the more visible failure.
    if (windowPid <= 0) {
        return false;
    }

    // Check all existing windows for same class but different PID
    // This detects when another app (e.g., Cachy Update) spawns a window
    // of a class that the user has previously snapped (e.g., Ghostty)
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* other : windows) {
        if (other == w) {
            continue; // Skip self
        }
        if (!other || other->isDeleted()) {
            // A close-grabbed dying window of the same class (quit-and-relaunch,
            // app auto-restart) must not suppress the new instance's snap restore.
            continue;
        }
        // Cheap discriminator FIRST: the class/pid compare is two flag
        // reads, while a verdict-cache miss in shouldHandleWindow builds
        // the full ~30-accessor ruleQuery — and this sweep runs per
        // window-open over the whole stacking order (the O(N²) query-build
        // case the cache comment names). Both are pure rejects, so the
        // order is behaviour-neutral.
        if (other->pid() <= 0) {
            continue; // Unknown-pid sentinel — cannot discriminate against it either.
        }
        if (other->windowClass() != windowClass || other->pid() == windowPid) {
            continue;
        }
        if (!shouldHandleWindow(other)) {
            continue; // Skip non-managed windows
        }
        // Found another managed window of the same class with a different
        // PID — the new window was likely spawned by a different app.
        return true;
    }

    return false;
}

bool PlasmaZonesEffect::isDaemonReady(const char* methodName) const
{
    if (!m_daemonGate.serviceRegistered) {
        qCDebug(lcEffect) << "Cannot" << methodName << "- daemon not ready";
        return false;
    }
    return true;
}

KWin::EffectWindow* PlasmaZonesEffect::getActiveWindow() const
{
    // Prefer KWin's active (focused) window when it is manageable and on current
    // desktop. Skip a close-grabbed dying window here for the same reason the
    // fallback loop does — it must not become the navigation / snap-assist anchor.
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    if (active && !active->isDeleted() && active->isOnCurrentActivity() && active->isOnCurrentDesktop()
        && !active->isMinimized() && shouldHandleWindow(active)) {
        return active;
    }
    // Fallback: topmost manageable window on current desktop (e.g. when activeWindow() is
    // null or refers to a dialog/utility we don't handle)
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        // Skip close-grabbed dying windows — a topmost close animation must
        // not become the navigation / snap-assist anchor.
        if (w && !w->isDeleted() && w->isOnCurrentActivity() && w->isOnCurrentDesktop() && !w->isMinimized()
            && shouldHandleWindow(w)) {
            return w;
        }
    }
    return nullptr;
}

bool PlasmaZonesEffect::isWindowSticky(KWin::EffectWindow* w) const
{
    return w && w->isOnAllDesktops();
}

void PlasmaZonesEffect::updateWindowStickyState(KWin::EffectWindow* w)
{
    if (!w || !m_daemonGate.serviceRegistered) {
        return;
    }

    QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }

    bool sticky = isWindowSticky(w);
    // Use fire-and-forget instead of QDBusInterface to avoid synchronous D-Bus
    // introspection. slotWindowAdded → updateWindowStickyState fires for every
    // window during login; QDBusInterface creation blocks the compositor thread
    // for ~25s if the daemon hasn't entered app.exec() yet (daemonReady is
    // emitted before the event loop starts).
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("setWindowSticky"), {windowId, sticky},
                                                   QStringLiteral("setWindowSticky"));
}

} // namespace PlasmaZones
