// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/WindowPlacement.h>
#include <phosphorengine_export.h>

#include <QHash>
#include <QJsonObject>
#include <QList>
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

    /// True if a record exists for the same live instance, or (if @p appId non-empty)
    /// any record in that appId bucket.
    bool contains(const QString& windowId, const QString& appId = QString()) const;

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
    QJsonObject serialize(const std::function<bool(const WindowPlacement&)>& keep = {}) const;
    void deserialize(const QJsonObject& obj);

    int size() const;

private:
    /// Capacity eviction preferring contentless residue, then non-live
    /// records, over restorable live placements — see the implementation
    /// comment. Non-static: the middle tier consults m_liveInstanceProbe.
    void evictForCapacity(QList<WindowPlacement>& bucket);

public:
    /// Per-app record cap (public so tests can pin the eviction contract).
    static constexpr int MaxPerApp = 16;

private:
    /// appId → list of records in positional FIFO order. The POSITION order
    /// governs take()'s oldest-first consumption (the snap paths) and the
    /// eviction's last-resort tier; takeForReopen's fallback consumes by
    /// SEQUENCE (newest first, live-excluded) instead, and multi-instance
    /// distribution there rests on the live-instance probe, not on position.
    QHash<QString, QList<WindowPlacement>> m_byApp;
    quint64 m_sequence = 0;
    std::function<bool(const QString&)> m_liveInstanceProbe;
};

} // namespace PhosphorEngine
