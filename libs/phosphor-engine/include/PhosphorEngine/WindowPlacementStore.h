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
/// Holds AT MOST ONE WindowPlacement record PER WINDOW (not per engine), captured
/// live by the engines. Records are keyed two ways so they survive both a daemon
/// restart (full windowId / uuid is stable while the window stays open) and a
/// close→reopen (uuid changes, so the appId FIFO carries it).
///
/// The core invariant — `record()` MERGES into the single record for the exact
/// windowId — gives per-mode state independence with a shared free/float geometry:
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
    /// record for the same exact windowId already exists (in any appId bucket) the
    /// incoming engine slot(s) and free-geometry screen(s) are merged in, leaving
    /// the other engine's slot and other screens' free geometry intact. Otherwise
    /// the record is appended to its appId's FIFO. Stamps a fresh monotonic
    /// `sequence`. No-op on an invalid record. Returns true if the store actually
    /// changed — false when the merge produced a content-identical record (sequence
    /// aside), so callers can skip marking state dirty and avoid a self-perpetuating
    /// save loop.
    bool record(WindowPlacement placement);

    /// Restore lookup: the first record whose `accept` predicate passes, trying
    /// the exact-windowId match before the appId FIFO (oldest first). The matched
    /// record is REMOVED (consumed) and returned. `accept` lets the caller reject
    /// cross-screen / disabled-context / wrong-kind candidates.
    ///
    /// `preferred` (optional) ranks the appId-FIFO branch ONLY: when supplied, the
    /// oldest entry satisfying BOTH `accept` and `preferred` is consumed first, and
    /// only if none qualifies does the oldest merely-accepted entry win. The
    /// exact-windowId match is unaffected — a window's own record is always used in
    /// whatever state it holds. Lets a caller restore the most meaningful record
    /// (e.g. a snapped placement) ahead of a contentless free/floating sibling that
    /// is merely older in the FIFO.
    std::optional<WindowPlacement> take(const QString& windowId, const QString& appId,
                                        const std::function<bool(const WindowPlacement&)>& accept = {},
                                        const std::function<bool(const WindowPlacement&)>& preferred = {});

    /// Non-consuming lookup (unlike take): the record for the exact windowId, else
    /// the NEWEST record in the appId bucket whose `accept` passes. Leaves the
    /// store unchanged — for live reads such as the float-back geometry lookup,
    /// where the record must stay put for the eventual restore/capture.
    std::optional<WindowPlacement> peek(const QString& windowId, const QString& appId,
                                        const std::function<bool(const WindowPlacement&)>& accept = {}) const;

    /// Exact-windowId peek: branch 1 of peek() only, never the appId-FIFO
    /// fallback. For reads about a LIVE window mid-session (KWin uuids are
    /// stable until logout), where a same-app sibling's record must not
    /// answer for this window — the appId fallback exists for the relogin
    /// re-bind paths (take()), not for live per-window state. An empty appId
    /// disables peek's fallback branch structurally, so this cannot drift.
    std::optional<WindowPlacement> peekExact(const QString& windowId) const
    {
        return peek(windowId, QString());
    }

    /// True if a record exists for the exact windowId, or (if @p appId non-empty)
    /// any record in that appId bucket.
    bool contains(const QString& windowId, const QString& appId = QString()) const;

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
    /// are stale. Without this the oldest-first take() rotates a reopening window
    /// between the duplicates — it "opens in a different spot each time."
    ///
    /// Returns true if at least one sibling was removed, so the caller can mark
    /// its persistence dirty: the preceding record() may have been a
    /// content-identical no-op, in which case this prune is the only mutation and
    /// would otherwise not reach disk until an incidental save.
    bool collapsePureFloatSiblings(const QString& appId, const QString& keepWindowId);

    /// Drop any record for the exact windowId (and prune the empty bucket).
    /// Returns true if a record was actually removed.
    bool clear(const QString& windowId);

    /// Clear ONLY the shared free/float geometry for the exact windowId, leaving the
    /// engine slots and context intact. Returns true if anything was cleared. Used by
    /// the drag-out / layout-change paths that consume the float-back once.
    bool clearFreeGeometry(const QString& windowId);

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
    /// appId → FIFO list of records (preserves multi-instance + close/reopen order).
    QHash<QString, QList<WindowPlacement>> m_byApp;
    quint64 m_sequence = 0;

    static constexpr int MaxPerApp = 16;
};

} // namespace PhosphorEngine
