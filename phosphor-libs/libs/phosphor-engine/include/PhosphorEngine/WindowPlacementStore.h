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
    ///
    /// The appId branch never consumes a record bound to a still-LIVE sibling
    /// (per the live-instance probe), the same exclusion takeForReopen and
    /// claimForOpen apply. Such a record describes a different, open window,
    /// never this one's history: a second Dolphin opened beside a snapped
    /// first one used to take the first's record, snap-restore into the same
    /// zone and re-bind the record under its own id, stripping the live
    /// sibling of its float-back. Unwired (tests) means no exclusion.
    std::optional<WindowPlacement> take(const QString& windowId, const QString& appId,
                                        const std::function<bool(const WindowPlacement&)>& accept = {},
                                        const std::function<bool(const WindowPlacement&)>& preferred = {});

    /// The EARLIEST-RECORDED record in @p appId's bucket that is bound to a
    /// live window OTHER than @p windowId's instance and passes @p accept, or
    /// nullopt. Non-consuming. This is the record a fresh same-app window
    /// inherits its free SIZE from when nothing places it: KDE apps write
    /// their window size to their own config on every resize, a snap is a
    /// resize, so a second instance opened beside a snapped sibling comes up
    /// at the zone's size (discussion #1106). The sibling's record still
    /// carries the free geometry the sibling had before its snap.
    ///
    /// Earliest by bucket POSITION, not newest by sequence, and filtered by
    /// @p accept, which carries the real discrimination: the caller skips
    /// records with no usable rect for its screen (including a rect of a
    /// zone's size, the spawn frame of a sibling the auto-snap chain placed at
    /// open) instead of settling for the first live sibling. Position is only
    /// the tie-break: first-recorded order mid-session, the persisted bucket
    /// order for saved records after a restart, and re-announce order for
    /// anything the save missed, so it cannot be trusted on its own.
    /// Answers nullopt on an empty @p windowId or @p appId, without a
    /// live-instance probe, and for the window opened in the daemon-restart
    /// gap before the effect has re-announced its siblings: the production
    /// probe reads the registry, so until then no sibling reads as live. The
    /// last is unknowable from inside the store and is accepted.
    std::optional<WindowPlacement>
    peekLiveSibling(const QString& windowId, const QString& appId,
                    const std::function<bool(const WindowPlacement&)>& accept = {}) const;

    /// Reopen resolve: the shared consumption pattern the TILING engines'
    /// open-time restores use (SnapEngine::resolveWindowRestore keeps its own
    /// take + re-bind with take()'s oldest-first ORDER and its `preferred`
    /// ranking pass, and since #1106 shares the live-instance exclusion
    /// below) — take() wrapped in the accept predicate both tiling engines
    /// share and the two rules that make a close/reopen (fresh uuid,
    /// appId-FIFO match) behave correctly.
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
    /// record passed. It has no other side effect.
    std::optional<WindowPlacement> takeForReopen(const QString& engineId, const QString& windowId, const QString& appId,
                                                 const QString& screenId);

    /// Non-consuming lookup (unlike take): the record for the same live instance, else
    /// the NEWEST record in the appId bucket whose `accept` passes. Leaves the
    /// store unchanged — for live reads such as the float-back geometry lookup,
    /// where the record must stay put for the eventual restore/capture.
    ///
    /// @p excludeLiveSiblings makes the appId branch skip records bound to a
    /// still-open sibling, exactly as take() does. A caller that peeks to
    /// decide whether a later take() will find something (the snap engine's
    /// cross-screen ownership gate) must pass true, or the gate and the
    /// consume disagree on a live sibling's record. Off by default because
    /// float-back geometry reads legitimately consult a live sibling's record.
    std::optional<WindowPlacement> peek(const QString& windowId, const QString& appId,
                                        const std::function<bool(const WindowPlacement&)>& accept = {},
                                        bool excludeLiveSiblings = false) const;

    /// Same-instance peek: branch 1 of peek() only, never the appId-FIFO
    /// fallback. The instance component remains stable if a live window's appId
    /// prefix changes, while still distinguishing same-app siblings. The appId
    /// fallback exists for close/reopen paths where the instance changes.
    std::optional<WindowPlacement> peekExact(const QString& windowId) const
    {
        return peek(windowId, QString());
    }

    /// Non-consuming lookup for the tiling engines' reopen claim
    /// (IPlacementEngine::claimCrossScreenReopen), which restores a window
    /// on the output it opens on, onto another virtual screen there. Differs
    /// from peek() in three ways:
    ///  - It applies the LIVE-INSTANCE exclusion take() and takeForReopen's
    ///    appId fallback apply: a record bound to a still-OPEN sibling
    ///    describes a different, living window, never this window's history.
    ///    A claim through plain peek() moved a fresh second instance onto its
    ///    open sibling's screen on every open.
    ///  - It honours the open claim (claimForOpen): a record another opening
    ///    window reserved is read past, so the claim answers with the same
    ///    record the claimer's take consumes.
    ///  - It scans ONLY the @p appId bucket (the window's own record included
    ///    — a same-instance match wins outright, live or not, since the
    ///    window's own record IS its history). The narrowing is safe because
    ///    record() RE-BUCKETS on an appId change, so a record follows the
    ///    window's current appId. A miss means no claim, never a wrong one.
    /// Returns nullopt when @p appId is empty, since a bare id has no bucket.
    std::optional<WindowPlacement> peekForReclaim(const QString& windowId, const QString& appId,
                                                  const std::function<bool(const WindowPlacement&)>& accept = {}) const;

    /// True if a record exists for the same live instance, or (if @p appId non-empty)
    /// any record in that appId bucket.
    bool contains(const QString& windowId, const QString& appId = QString()) const;

    /// The window CLOSED: drop its open claim (releaseOpenClaim). The rest of
    /// the old contract retired with the cross-screen reclaim, so the records
    /// are left untouched and the answer is always false. @p graceEligible
    /// is ignored; both stay for ABI.
    bool markInstanceClosed(const QString& windowId, bool graceEligible = true);

    /// INERT, kept for ABI. It excused a live move from the reclaim-credit
    /// burn, and the credit is gone.
    void markInstanceMovedLive(const QString& windowId);

    /// INERT, kept for ABI. It retired one reclaim credit per open, and the
    /// credit is gone. Always returns false.
    bool burnReclaimCredit(const QString& windowId, const QString& appId);

    /// Reserve, once per opening window instance, the record that instance owns.
    ///
    /// WHY THIS EXISTS. The two open channels both select a record for the same
    /// opening window: the snap channel through SnapAdaptor::resolveWindowRestore
    /// and the tiling channel through TilingAdaptor::dispatchWindowOpened, which
    /// reaches take() / takeForReopen(). At login every uuid is fresh, so both
    /// always fall to their appId branch, and their orders differ. Without a
    /// reservation an already-home window could consume a SIBLING's record,
    /// leaving that sibling with nothing to restore from, and a window's later
    /// re-drives could answer from a different record than its own open used.
    ///
    /// The fix has to be a RESERVATION, never a predicate. "Skip non-same-instance
    /// records" looks right and destroys the behaviour: after a logout NO record
    /// is same-instance, so such a predicate refuses the whole bucket. Instead the
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

    /// Drop @p windowId's open claim, so a claim never outlives the instance
    /// that made it. The daemon calls it from the observed close, and the
    /// alive-set prune drops the claims of windows that died without one
    /// through releaseOpenClaimsExcept; the store calls it itself from
    /// clear(). Consumption through take() / takeForReopen() releases it
    /// too — after the re-bind the record IS the window's own, and the claim
    /// is redundant identity.
    void releaseOpenClaim(const QString& windowId);

    /// The reopen-contract claim: claimForOpen restricted to what the opening
    /// screen can restore. The window's OWN record (same instance) is final:
    /// when an engine has ever captured it, it is claimed if it carries
    /// restorable content and nothing is claimed otherwise, never a sibling's.
    /// Without one, the newest record that passes claimForOpen's sibling rules
    /// AND sits on the opening KWin output (or has no screen) AND satisfies
    /// @p restorableHere (the opening engine's "can I restore this here",
    /// unset meaning any) is claimed. An empty @p openingScreenId applies no
    /// output filter. Same idempotence as claimForOpen.
    std::optional<WindowPlacement> claimForOpen(const QString& windowId, const QString& appId,
                                                const QString& openingScreenId,
                                                const std::function<bool(const WindowPlacement&)>& restorableHere);

    /// Drop every open claim whose instance is not in @p aliveInstanceIds
    /// (instance ids, as PhosphorIdentity::WindowId::extractInstanceId gives
    /// them), so a claimer that died without a close signal stops reserving a
    /// record another instance could restore from. Returns how many went.
    int releaseOpenClaimsExcept(const QSet<QString>& aliveInstanceIds);

    /// Inject the live-window probe behind every live-sibling exclusion in the
    /// store (take, takeForReopen, claimForOpen, peekForReclaim,
    /// peekLiveSibling, peek's opt-in, the eviction and collapse tiers).
    /// Answers per full windowId; evaluated at lookup time. Unwired (tests)
    /// means no exclusion.
    void setLiveInstanceProbe(std::function<bool(const QString& windowId)> probe)
    {
        m_liveInstanceProbe = std::move(probe);
    }

    /// Whether the probe reports @p windowId's instance as a still-open
    /// window. False without a probe. For callers outside the store that
    /// enumerate records and must skip the ones describing a live window (the
    /// effect's instant-restore cache, which otherwise teleports a fresh
    /// second instance into its open sibling's zone).
    bool isLiveInstance(const QString& windowId) const
    {
        return m_liveInstanceProbe && m_liveInstanceProbe(windowId);
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
    /// probe) are never pruned. PRECONDITION: call this only from a close
    /// capture, where keepWindowId is the closing window. Every current call
    /// site enforces that by gating on a non-empty authoritative close screen.
    /// A pruned sibling's engine slots and other-screen geometry are absorbed
    /// fill-gaps-only.
    /// Without the collapse, a consuming reopen — take()'s live-excluded
    /// oldest-first for snap, or takeForReopen's live-excluded newest-first
    /// tail for the tiling engines — can rotate a
    /// reopening window between the duplicates: it "opens in a different spot
    /// each time."
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
    /// handoff, or a window that left the screen it was managed on) is not
    /// memory, it is a stale ownership claim. The reopen claim
    /// (pendingCrossScreenManagedRestore) reads a managed slot plus the
    /// record-level screenId as a restorable home, so a stale slot that
    /// outlives its engine's ownership would read as one.
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

    /// Drop one desktop's entry from an engine slot's zonesByDesktop map.
    ///
    /// The counterpart to that map being MERGED by record(): a capture that
    /// stops naming a desktop does not forget it, so a window that genuinely
    /// left one has to say so here. Without this the stale zone survives every
    /// later save and a restart puts the window back on a desktop it no longer
    /// occupies. Returns true when an entry was actually removed.
    bool forgetDesktopZones(const QString& windowId, const QString& engineId, int desktop);

    /// Drop @p removedDesktop from every slot's zonesByDesktop map and shift
    /// the entries above it down by one, and shift the record-level desktop
    /// the same way (a record ON the removed desktop reads as unknown),
    /// because Plasma renumbers x11 desktops when one in the middle is
    /// deleted. The persisted fields have to follow the same renumbering the
    /// engines apply to their live stores, or a restart seeds a zone under a
    /// number that now belongs to another desktop. Returns the number of
    /// records changed.
    int renumberDesktopZones(int removedDesktop);

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
    /// Each record persists as WindowPlacement::toJson writes it.
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

    /// Whether @p p is bound to a still-open window that is NOT the instance
    /// behind @p askingWindowId. The one predicate every live-SIBLING
    /// exclusion in the store applies (the eviction and collapse tiers, which
    /// protect ANY live record, and takeForReopen's fallback, where take()'s
    /// same-instance branch and the accept together already keep the asker's
    /// own record out, read the probe directly): a window's own record is its
    /// history, live or not (daemon restart: same uuid, window open), while
    /// an open sibling's record describes a different window and is never
    /// this one's. With an empty @p askingWindowId every live record is
    /// "other", which is what peek()'s opt-in exclusion relies on.
    bool boundToLiveOther(const QString& askingWindowId, const WindowPlacement& p) const;

public:
    /// Per-app record cap (public so tests can pin the eviction contract).
    static constexpr int MaxPerApp = 16;

    /// INERT, kept for source compatibility: the shutdown-close grace
    /// serialize() once applied to the reclaim credit, which is gone.
    static constexpr qint64 ShutdownCloseGraceMs = 30000;

private:
    /// appId → list of records in positional FIFO order. The POSITION order
    /// governs take()'s oldest-first consumption (the snap paths), the
    /// eviction's last-resort tier and peekLiveSibling's earliest-recorded
    /// pick; takeForReopen's fallback consumes by SEQUENCE (newest first)
    /// instead. Both consumers are live-excluded, so multi-instance
    /// distribution rests on the live-instance probe, not on position alone.
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
    /// is still in the store. That invariant is what lets pairingAllows answer
    /// from two hash hits per candidate; claimForOpen itself still walks the
    /// buckets once per open to find the record by id, which is bounded by
    /// MaxPerApp times the number of apps. The lookup still fails open if the
    /// two ever disagree, because refusing an instance every record is worse
    /// than the disagreement the claim exists to fix.
    QHash<QString, QString> m_openPairing;
    QHash<QString, QString> m_claimedBy;

    /// INERT, kept for ABI layout: the move excuses markInstanceMovedLive
    /// once armed. Never written.
    QSet<QString> m_movedLiveInstances;

    /// The one claim walk both claimForOpen overloads share. @p ownRecordFinal
    /// makes a captured own record final (claimed or nothing); otherwise a
    /// non-restorable own record falls through to the siblings, the original
    /// overload's rule. @p openingScreenId (empty: no filter) and
    /// @p restorableHere (unset: any) restrict the sibling step.
    std::optional<WindowPlacement> claimForOpenImpl(const QString& windowId, const QString& appId, bool ownRecordFinal,
                                                    const QString& openingScreenId,
                                                    const std::function<bool(const WindowPlacement&)>& restorableHere);
};

} // namespace PhosphorEngine
