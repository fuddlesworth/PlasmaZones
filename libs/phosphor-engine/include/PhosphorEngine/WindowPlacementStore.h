// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/WindowPlacement.h>
#include <phosphorengine_export.h>

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

namespace PhosphorEngine {

/// The single source of truth for window restore state in the unified model.
///
/// Holds AT MOST ONE WindowPlacement record PER WINDOW INSTANCE (not per engine),
/// captured live by the engines. Records are keyed two ways so they survive both a daemon
/// restart (the instance component is stable while the window stays open) and a
/// close→reopen (uuid changes, so the appId FIFO carries it).
///
/// The core invariant — `record()` MERGES into the single record for the same
/// instance — gives per-mode state independence with a shared free/float geometry:
/// each engine updates only its OWN slot (in `engines`, keyed by engineId()) plus
/// any free-geometry change, so a window may be `snapped` in the snap engine AND
/// `floating` in the autotile engine at once, each engine remembering the window's
/// state in its own mode, while the un-managed position lives once in
/// freeGeometryByScreen (shared across modes, keyed per screen).
class PHOSPHORENGINE_EXPORT WindowPlacementStore
{
public:
    WindowPlacementStore() = default;

    /// Record / MERGE this window's placement. The incoming record supplies only
    /// the calling engine's slot (in `engines`) and any free-geometry update; if a
    /// record for the same live instance already exists (in any appId bucket) the
    /// incoming engine slot(s) and free-geometry screen(s) are merged in, leaving
    /// the other engine's slot and other screens' free geometry intact. Otherwise
    /// the record is appended to its appId's FIFO. No-op on an invalid record.
    /// Returns true if the store actually changed — false when the merge produced
    /// a content-identical record, so callers can skip marking state dirty and
    /// avoid a self-perpetuating save loop. Stamps a fresh monotonic `sequence`
    /// whenever it changes anything (the content-identical short-circuit leaves
    /// the existing record, sequence included, untouched).
    bool record(WindowPlacement placement);

    /// Restore lookup: the first record whose `accept` predicate passes, trying
    /// the same-instance match before the appId FIFO (oldest first). The matched
    /// record is REMOVED (consumed) and returned. `accept` lets the caller reject
    /// cross-screen / disabled-context / wrong-kind candidates.
    ///
    /// `preferred` (optional) ranks the appId-FIFO branch ONLY: when supplied, the
    /// oldest entry satisfying BOTH `accept` and `preferred` is consumed first, and
    /// only if none qualifies does the oldest merely-accepted entry win. The
    /// same-instance match is unaffected — a window's own record is always used in
    /// whatever state it holds. Lets a caller restore the most meaningful record
    /// (e.g. a snapped placement) ahead of a contentless free/floating sibling that
    /// is merely older in the FIFO.
    std::optional<WindowPlacement> take(const QString& windowId, const QString& appId,
                                        const std::function<bool(const WindowPlacement&)>& accept = {},
                                        const std::function<bool(const WindowPlacement&)>& preferred = {});

    /// Reopen resolve: the shared consumption pattern the TILING engines'
    /// open-time restores use (SnapEngine::resolveWindowRestore keeps its own
    /// take + re-bind: its snapped records restore cross-screen and its accept
    /// depends on a mode-defer bypass, so rule 1 below does not fit it — and
    /// note the snap path therefore also keeps take()'s oldest-first order
    /// WITHOUT the live-instance exclusion below; that asymmetry is a
    /// documented property of the snap flow, not an oversight) —
    /// take() wrapped in the accept predicate both tiling engines share and
    /// the two rules that make a close/reopen (fresh uuid, appId-FIFO match)
    /// behave correctly.
    ///
    /// The accept predicate, hoisted here so the two engines cannot drift: a
    /// record whose @p engineId slot is FLOATING restores when its screen
    /// matches @p screenId (or is empty), and FIFO consumption by a DIFFERENT
    /// instance additionally requires a valid anyFreeGeometry (a geometry-less
    /// floating record is meaningful only same-instance — consumed by a
    /// sibling it floats a fresh window at its spawn rect for no reason while
    /// burning a FIFO slot). FLOATING slots only, deliberately: a TILED
    /// record is never consumed and restores no position — its role is the
    /// exact-final verdict below (the window closed tiled, so the reopen must
    /// not float it).
    ///
    ///   1. A REJECTED exact record is FINAL — no FIFO fallback past it — but
    ///      ONLY when that record carries a slot for the ASKING engine. The
    ///      fallback exists for a reopen, whose fresh uuid has no exact record
    ///      WITH A VERDICT: every open writes a geometry-only, slot-less
    ///      record under the live uuid (the pre-tile free-geometry capture)
    ///      before the engine's restore runs, and that stub says nothing about
    ///      this engine, so it must not veto the FIFO. A LIVE window whose own
    ///      record holds this engine's slot but was rejected on context (tiled
    ///      on another desktop, say) IS final: falling through would consume a
    ///      SIBLING's record, and the re-bind below would re-record it under
    ///      this window's id, where the merge overwrites the window's own
    ///      other-context slot.
    ///   2. The consumed record is RE-BOUND to the live @p windowId and
    ///      re-recorded, so the other engines' slots + per-screen free/float
    ///      geometry survive the reopen.
    ///
    /// The appId fallback consumes the NEWEST accepted record — matching
    /// peek()'s "the most recent placement is current truth" — and never one
    /// whose window instance is still LIVE (per the live-instance probe): the
    /// last close is the state the user expects back, and consuming oldest
    /// first handed a reopen whichever stale record had sat unconsumed
    /// longest (the octopi graveyard: eleven leftover tiled records shadowing
    /// the fresh floating one, and colliding months-old column ranks on a
    /// compositor-restart restore). The live exclusion is what makes
    /// newest-first safe for multi-instance apps: a record just re-bound to
    /// an OPEN sibling is the newest in the bucket, and without the probe the
    /// next reopen would steal it, leaving the sibling recordless.
    ///
    /// Returns the consumed record (already re-recorded), or nullopt when no
    /// record passed.
    ///
    /// Side effect, hit or miss: BURNS ONE RECLAIM CREDIT in the @p appId
    /// bucket (the newest reclaim-eligible record not bound to a live window
    /// and not this instance's own). This is what retires a session-restore
    /// credit per open — a window restored onto its CORRECT screen (where
    /// the reclaim's same-screen bail never consumes anything) spends its
    /// credit here instead of leaving it for a later same-app open to be
    /// teleported by. The exact-final early return burns nothing: it is a
    /// live window's own context-rejected verdict, not an open that used up
    /// a restore.
    ///
    /// The burn is per CALL, and the tiling engines do not call this exactly
    /// once per open — the accounting is approximate in both directions, on
    /// purpose, because both errors fail SAFE (a missed burn leaves a credit
    /// the close-time revoke will take instead; an extra burn only makes a
    /// reclaim less likely, never wrong):
    ///  - AutotileEngine::insertWindow calls this only when its earlier tiers
    ///    left the window uninserted AND the arrival is not a migration
    ///    re-add, so a pre-seeded open burns nothing.
    ///  - ScrollEngine::insertOpenedWindow has no migration guard, so a live
    ///    window changing screen or desktop context re-drives the open path
    ///    and burns again. It can only burn a NON-live record, so the cost is
    ///    at most one not-yet-reopened sibling's restore credit.
    /// A caller that needs exactly-once accounting must gate its own call;
    /// do not read the burn as a per-open counter.
    std::optional<WindowPlacement> takeForReopen(const QString& engineId, const QString& windowId, const QString& appId,
                                                 const QString& screenId);

    /// Non-consuming lookup (unlike take): the record for the same live instance, else
    /// the NEWEST record in the appId bucket whose `accept` passes. Leaves the
    /// store unchanged — for live reads such as the float-back geometry lookup,
    /// where the record must stay put for the eventual restore/capture.
    std::optional<WindowPlacement> peek(const QString& windowId, const QString& appId,
                                        const std::function<bool(const WindowPlacement&)>& accept = {}) const;

    /// Same-instance peek: branch 1 of peek() only, never the appId-FIFO
    /// fallback. The instance component remains stable if a live window's appId
    /// prefix changes, while still distinguishing same-app siblings. The appId
    /// fallback exists for close/reopen paths where the instance changes.
    std::optional<WindowPlacement> peekExact(const QString& windowId) const
    {
        return peek(windowId, QString());
    }

    /// Non-consuming lookup for the cross-screen reclaim
    /// (IPlacementEngine::claimCrossScreenReopen). Differs from peek() in the
    /// two ways that make a reclaim verdict sound:
    ///  - It applies the LIVE-INSTANCE exclusion takeForReopen's appId
    ///    fallback applies: a record bound to a still-OPEN sibling describes a
    ///    different, living window, never this window's history, so it must
    ///    not justify a cross-screen pull. peek() deliberately lacks the
    ///    exclusion (float-back geometry reads legitimately consult a live
    ///    sibling's record); a reclaim through plain peek() teleported a
    ///    fresh second instance onto its open sibling's monitor on EVERY
    ///    open, mid-session, repeatedly.
    ///  - The fallback additionally requires the record's reclaim CREDIT
    ///    (WindowPlacement::reclaimEligible): only a record whose window was
    ///    live at the last save — a session-restore candidate — may pull a
    ///    fresh window across monitors. Without it, the immortal tiled record
    ///    of the last-closed sibling homed every future same-app window (a
    ///    detached browser tab landed on the OPPOSITE monitor, bug #1017).
    ///    The same-instance branch ignores the credit: the window's own
    ///    record is its history, and the daemon-restart reclaim must work
    ///    regardless.
    ///  - It scans ONLY the @p appId bucket (the window's own record included
    ///    — a same-instance match wins outright, live or not, since the
    ///    window's own record IS its history). peek()'s same-instance branch
    ///    walks every bucket in the store; on the per-open reclaim path that
    ///    cost is paid per engine per open. The narrowing is safe because
    ///    record() RE-BUCKETS on an appId change, so a record follows the
    ///    window's current appId — if that ever stops holding, this scan
    ///    starts missing records peek() would find. It fails SAFE either
    ///    way: a miss means no reclaim, never a wrong one.
    /// Returns nullopt when @p appId is empty — a bare id has no bucket, so a
    /// reclaim verdict is impossible.
    std::optional<WindowPlacement> peekForReclaim(const QString& windowId, const QString& appId,
                                                  const std::function<bool(const WindowPlacement&)>& accept = {}) const;

    /// True if a record exists for the same live instance, or (if @p appId non-empty)
    /// any record in that appId bucket.
    bool contains(const QString& windowId, const QString& appId = QString()) const;

    /// The window CLOSED mid-session: revoke its records' reclaim credit
    /// (WindowPlacement::reclaimEligible) and stamp closedAtMsecs. From here
    /// on the records serve reopen restore only — never the cross-screen
    /// reclaim's appId fallback. Called from the daemon's close funnels AFTER
    /// the close-time captureWindowPlacement (record() preserves the stored
    /// credit on merge, so order only matters for the stamp's accuracy).
    /// Sweeps every matching record across buckets, the releaseEngineSlot
    /// appId-drift rationale. Returns true when any credit was revoked.
    ///
    /// @p graceEligible stamps closedAtMsecs with the current time, which is
    /// what lets serialize()'s ShutdownCloseGraceMs arm persist this close as
    /// restore evidence. Pass false when the close time is NOT known to be
    /// now — the alive-set prune backstop discovers a window that died at some
    /// unobserved earlier moment, and dating that death "now" would hand it a
    /// shutdown grace it never earned. The stamp is then left ALONE rather
    /// than zeroed — an unobserved close contributes no time, and clearing a
    /// stamp an earlier observed close wrote would strip the grace from a
    /// window that legitimately earned it (logout can push an alive report
    /// through the prune backstop before the final save). A record that was
    /// never stamped keeps its default 0, so it persists credit-less at the
    /// next save.
    bool markInstanceClosed(const QString& windowId, bool graceEligible = true);

    /// The window was RELEASED WHILE LIVE (a move, not a close): its next
    /// takeForReopen is a move return, not a session-restore open, so it must
    /// consume its record WITHOUT spending a reclaim credit.
    ///
    /// WHY THIS EXISTS. The daemon's live-release funnel
    /// (Tiling.releaseWindowTracking) untracks the window in its engine, so
    /// the re-announce that follows arrives as a FIRST OBSERVATION — the
    /// engine no longer holds it, and nothing downstream can tell that
    /// announce apart from a genuine open. Verified live: a desktop move of a
    /// floating scrolling window reached takeForReopen and retired a credit
    /// belonging to a sibling that had not reopened yet, which is exactly the
    /// window the credit exists to bring home.
    ///
    /// ONE-SHOT, consumed by the next takeForReopen for the instance, so a
    /// window moved twice is excused twice and a move never disarms the
    /// window's later genuine opens. Keyed by INSTANCE id, so a mid-session
    /// class rename cannot lose it. Cleared when the instance closes
    /// (markInstanceClosed / clear) and by deserialize's whole-store replace.
    ///
    /// Sibling of TilingAdaptor::m_moveReleasedInstances, armed from the same
    /// line for the same reason. They are deliberately NOT one flag: that one
    /// is consumed by the adaptor BEFORE engine dispatch (to suppress the
    /// cross-screen reclaim), this one during the engine's open path (to
    /// suppress the burn), so a single flag consumed at the first moment
    /// would already be gone by the second.
    void markInstanceMovedLive(const QString& windowId);

    /// Retire ONE reclaim credit in @p appId's bucket: the newest
    /// reclaim-eligible record not bound to a live window and not
    /// @p windowId's own instance. This is takeForReopen's per-open burn,
    /// exposed for the one open channel that never calls takeForReopen —
    /// the snap adaptor's open-path resolve on SNAP-mode screens — so a
    /// window restored onto a snap screen also spends its session-restore
    /// credit instead of leaving it dangling for a later same-app open.
    /// Returns true when a credit was burned.
    bool burnReclaimCredit(const QString& windowId, const QString& appId);

    /// Reserve, once per opening window instance, the record that instance owns.
    ///
    /// WHY THIS EXISTS. Two independent selectors read a record for the same
    /// opening window: the daemon's cross-desktop restore through peek() (newest
    /// first) and the engines' restore through take() / takeForReopen(). At login
    /// every uuid is fresh, so both always fall to their appId branch, and their
    /// orders differ — a multi-window app got one record's DESKTOP paired with a
    /// different record's ZONE. Worse, an already-home window could consume a
    /// SIBLING's record, so that sibling never returned to its desktop at all.
    ///
    /// The fix has to be a RESERVATION, never a predicate. "Skip armed records"
    /// or "skip non-same-instance records" both look right and both destroy the
    /// feature: after a logout EVERY record is persisted-armed and NONE is
    /// same-instance, so such a predicate refuses the whole bucket. Instead the
    /// first daemon-side touch of an opening window claims one record, and every
    /// later reader honours that claim.
    ///
    /// Non-consuming and idempotent: repeat calls for the same instance return
    /// the same record for the life of the claim. Selection is the same-instance
    /// record when one carries restorable content (the daemon-restart shape,
    /// uuid intact — never the slot-less geometry stub every open writes), else
    /// the NEWEST unclaimed record in the appId bucket that is not bound to a
    /// still-live sibling. Newest matches peek() and takeForReopen(); take()'s
    /// oldest-first is a starvation order, not a claim about ownership, and with
    /// N windows the SET consumed is the same either way.
    ///
    /// Returns the claimed record, or nullopt when nothing is claimable.
    std::optional<WindowPlacement> claimForOpen(const QString& windowId, const QString& appId);

    /// Drop @p windowId's open claim. Called when the window closes, so a claim
    /// never outlives the instance that made it. Consumption through take() /
    /// takeForReopen() releases it too — after the re-bind the record IS the
    /// window's own, and the claim is redundant identity.
    void releaseOpenClaim(const QString& windowId);

    /// Inject the live-window probe takeForReopen's appId fallback uses to
    /// skip records bound to a still-open window (see its doc). Answers per
    /// full windowId; evaluated at consume time. Unwired (tests) means no
    /// exclusion.
    void setLiveInstanceProbe(std::function<bool(const QString& windowId)> probe)
    {
        m_liveInstanceProbe = std::move(probe);
    }

    /// Collapse stale pure-float duplicates for an app, keeping @p keepWindowId.
    /// A "pure-float" record carries float-back geometry but NO managed
    /// (snapped/tiled) engine slot. When @p keepWindowId names a pure-float
    /// record, every OTHER pure-float record in the same @p appId bucket that
    /// remembers a float position on a screen @p keepWindowId also covers is
    /// removed. Records carrying a snapped/tiled slot are never touched (managed
    /// placements whose multi-instance distribution must survive). No-op when the
    /// kept record is absent or itself managed.
    ///
    /// Called ONLY from close-capture paths: a window closing floating is the
    /// freshest authority for its app's float-back on that screen, so duplicate
    /// siblings (left by rapid open/close or overlapping short-lived instances)
    /// are stale. Records bound to a still-OPEN window (per the live-instance
    /// probe) are never pruned, and a pruned sibling's engine slots and
    /// other-screen geometry are absorbed fill-gaps-only. Without the
    /// collapse, a consuming reopen — take()'s oldest-first for snap, or a
    /// probe-excluded tail for the tiling engines — can rotate a reopening
    /// window between the duplicates: it "opens in a different spot each
    /// time."
    ///
    /// Returns true if at least one sibling was removed, so the caller can mark
    /// its persistence dirty: the preceding record() may have been a
    /// content-identical no-op, in which case this prune is the only mutation and
    /// would otherwise not reach disk until an incidental save.
    bool collapsePureFloatSiblings(const QString& appId, const QString& keepWindowId);

    /// Drop any record for the same live instance (and prune the empty bucket).
    /// Returns true if a record was actually removed.
    bool clear(const QString& windowId);

    /// Clear ONLY the shared free/float geometry for the same live instance, leaving the
    /// engine slots and context intact. Returns true if anything was cleared. The
    /// all-screens form is for wholesale invalidation (virtual-screen remap); the
    /// screen-scoped overload is for consume-once paths (drag-out, drop-snap), which
    /// must not destroy the float-back remembered for other monitors.
    bool clearFreeGeometry(const QString& windowId);
    bool clearFreeGeometry(const QString& windowId, const QString& screenId);

    /// DOWNGRADE one engine's slot to WindowPlacement::stateReleased() on
    /// every record for the same live instance, leaving the other engines'
    /// slots, the context and the shared geometry intact. Returns true if any
    /// record changed.
    ///
    /// This is the release-path counterpart of record()'s merge-never-clear:
    /// slots accumulate as cross-mode memory, and for RESTORE that is right —
    /// but a slot on a window an engine has knowingly GIVEN UP (cross-mode
    /// handoff) is not memory, it is a stale ownership claim. Since the
    /// cross-screen reclaim (pendingCrossScreenManagedRestore) treats a
    /// managed slot plus the record-level screenId as evidence of a home to
    /// pull the window back to, a stale slot that outlives its engine's
    /// ownership can yank a window out from under its CURRENT engine whenever
    /// the new owner's capture misses (the record()-overwrite of screenId is
    /// not guaranteed on every path).
    ///
    /// DOWNGRADE, not remove, and the difference is load-bearing on both
    /// sides — see stateReleased()'s contract. Removing the slot would also
    /// risk emptying the engines map, which record()'s merge reads as "no
    /// managed context to adopt", freezing the record's screenId against
    /// every later geometry-only write.
    ///
    /// Sweeps ALL records matching the instance rather than stopping at the
    /// first: appId drift (the Electron/CEF case this codebase canonicalizes
    /// for) can leave records for one instance in two buckets, and the one
    /// carrying the stale slot is not necessarily the one QHash order
    /// reaches first.
    ///
    /// Callers: the tiling engines' handoffRelease, through
    /// WindowTrackingService::releaseEngineSlot (which marks the store
    /// dirty). NOT called on ordinary close — a window that CLOSED tiled
    /// keeps its slot; that persistence is exactly what login restore reads.
    bool releaseEngineSlot(const QString& windowId, const QString& engineId);

    /// Apply an in-place mutation to every record; @p fn returns true when it changed
    /// the record. Returns the number changed. For bulk rewrites that keep the appId
    /// bucketing (e.g. virtual-screen id remap of freeGeometryByScreen keys). Does NOT
    /// move records between buckets — only mutate fields other than appId.
    int transform(const std::function<bool(WindowPlacement&)>& fn);

    /// Remove every record matching @p pred. Returns the count removed.
    int removeIf(const std::function<bool(const WindowPlacement&)>& pred);

    /// All records, in no particular order. For read-only sweeps (e.g. building
    /// the effect's instant-restore cache from the snapped records).
    QList<WindowPlacement> records() const;

    /// JSON shape: { appId: [ record, ... ] }. @p keep filters out entries that
    /// should not persist (e.g. disabled-context). Empty buckets are dropped.
    /// When the live-instance probe is wired, each record's persisted
    /// "liveAtSave" is DERIVED here — live per the probe, or closed within
    /// ShutdownCloseGraceMs — rather than copied from the in-memory credit,
    /// so the cross-session graveyard is stripped of reclaim credit at every
    /// save. Unwired (tests), the in-memory value round-trips as-is.
    QJsonObject serialize(const std::function<bool(const WindowPlacement&)>& keep = {}) const;
    void deserialize(const QJsonObject& obj);

    int size() const;

private:
    /// Capacity eviction preferring contentless residue, then non-live
    /// records, over restorable live placements — see the implementation
    /// comment. Non-static: the middle tier consults m_liveInstanceProbe.
    void evictForCapacity(QList<WindowPlacement>& bucket);

    /// May the instance behind @p windowId read @p candidate?
    ///
    /// FAILS OPEN by design. A claim naming a record that is no longer in the
    /// store (evicted, collapsed, consumed elsewhere) is ignored rather than
    /// honoured, because honouring it would make the instance permanently
    /// unpairable and restore NOTHING — strictly worse than the disagreement
    /// this whole mechanism exists to fix.
    bool pairingAllows(const QString& windowId, const WindowPlacement& candidate) const;

    /// Drop any open claim naming @p recordWindowId, for use at the two sites
    /// that remove a record out from under a possible claim.
    void dropClaimsNaming(const QString& recordWindowId);

public:
    /// Per-app record cap (public so tests can pin the eviction contract).
    static constexpr int MaxPerApp = 16;

    /// serialize()'s shutdown-close grace: a window that closed within this
    /// many msecs of the save still persists reclaimEligible=true. Logout
    /// tears windows down moments before the daemon's final save, and
    /// persisting those closes as credit-less would break exactly the login
    /// reclaim the credit exists to serve. Mid-session closes are long past
    /// the grace by the next unrelated save, so their persisted credit
    /// converges to false. (Exposure: a daemon restart within the grace of a
    /// close treats that one record as restore evidence — bounded to a
    /// single reclaim by the per-open credit burn.)
    static constexpr qint64 ShutdownCloseGraceMs = 30000;

private:
    /// appId → list of records in positional FIFO order. The POSITION order
    /// governs take()'s oldest-first consumption (the snap paths) and the
    /// eviction's last-resort tier; takeForReopen's fallback consumes by
    /// SEQUENCE (newest first, live-excluded) instead, and multi-instance
    /// distribution there rests on the live-instance probe, not on position.
    QHash<QString, QList<WindowPlacement>> m_byApp;
    quint64 m_sequence = 0;
    std::function<bool(const QString&)> m_liveInstanceProbe;

    /// instanceId → the windowId of the record that instance claimed at open,
    /// and its inverse. TRANSIENT and never serialized: they describe which live
    /// window owns which record for the duration of one open, and mean nothing
    /// across a restart.
    ///
    /// Kept in lockstep, and kept TRUE: every path that removes a record drops
    /// the claim naming it, so a claim in these maps always names a record that
    /// is still in the store. That invariant is what lets the lookups be hash
    /// hits instead of a full-store scan on a per-open hot path. The lookup
    /// still fails open if the two ever disagree, because refusing an instance
    /// every record is worse than the disagreement the claim exists to fix.
    QHash<QString, QString> m_openPairing;
    QHash<QString, QString> m_claimedBy;

    /// Instance ids released while LIVE, each excusing exactly one
    /// takeForReopen from the reclaim-credit burn — see
    /// markInstanceMovedLive. TRANSIENT and never serialized: it describes
    /// moves in flight and means nothing across a restart.
    QSet<QString> m_movedLiveInstances;
};

} // namespace PhosphorEngine
