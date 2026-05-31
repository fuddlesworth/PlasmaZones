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
/// Holds AT MOST ONE WindowPlacement record per window, captured live by the
/// owning engine. Records are keyed two ways so they survive both a daemon
/// restart (full windowId / uuid is stable while the window stays open) and a
/// close→reopen (uuid changes, so the appId FIFO carries it).
///
/// The core invariant — `record()` REPLACES any prior record for the exact
/// windowId — is what makes states mutually exclusive: when a window snaps, the
/// snap record overwrites its stale "floated" record, so a floated→snapped
/// window can never resurrect its float on the next login.
class PHOSPHORENGINE_EXPORT WindowPlacementStore
{
public:
    WindowPlacementStore() = default;

    /// Record / replace this window's placement. If a record with the same exact
    /// windowId already exists (in any appId bucket) it is overwritten in place;
    /// otherwise the record is appended to its appId's FIFO. Stamps a fresh
    /// monotonic `sequence`. No-op on an invalid record.
    void record(WindowPlacement placement);

    /// Restore lookup: the first record whose `accept` predicate passes, trying
    /// the exact-windowId match before the appId FIFO (oldest first). The matched
    /// record is REMOVED (consumed) and returned. `accept` lets the caller reject
    /// cross-screen / disabled-context / wrong-kind candidates.
    std::optional<WindowPlacement> take(const QString& windowId, const QString& appId,
                                        const std::function<bool(const WindowPlacement&)>& accept = {});

    /// Non-consuming lookup (unlike take): the record for the exact windowId, else
    /// the NEWEST record in the appId bucket whose `accept` passes. Leaves the
    /// store unchanged — for live reads such as the float-back geometry lookup,
    /// where the record must stay put for the eventual restore/capture.
    std::optional<WindowPlacement> peek(const QString& windowId, const QString& appId,
                                        const std::function<bool(const WindowPlacement&)>& accept = {}) const;

    /// True if a record exists for the exact windowId, or (if @p appId non-empty)
    /// any record in that appId bucket.
    bool contains(const QString& windowId, const QString& appId = QString()) const;

    /// Drop any record for the exact windowId (and prune the empty bucket).
    void clear(const QString& windowId);

    /// Remove every record matching @p pred. Returns the count removed.
    int removeIf(const std::function<bool(const WindowPlacement&)>& pred);

    void clearAll();

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
