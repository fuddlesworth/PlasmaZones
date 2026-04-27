// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>
#include <PhosphorJsonLoader/ParsedEntry.h>

#include <QtCore/QHash>
#include <QtCore/QLoggingCategory>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <any>
#include <utility>

namespace PhosphorAnimation::detail {

/**
 * @brief Templatized base for "scan a directory of JSON files →
 *        produce a `Payload` per file → commit the diff to a registry"
 *        loaders.
 *
 * Both `CurveLoader::Sink` and `ProfileLoader::Sink` share the same
 * commit shape:
 *
 *   1. Reset a `lastBatchChanged` flag (consumed by the parent loader to
 *      gate its public `xxxChanged` signal — `DirectoryLoader` always
 *      emits `entriesChanged` on every rescan, so a no-op rescan must
 *      not propagate further than this layer).
 *   2. Walk `removedKeys`. For each key actually present in the
 *      previous-commit snapshot, evict it from the snapshot, flip the
 *      change flag, and let the subclass drop it from its own typed
 *      tracking (`onUntrackEntry`).
 *   3. Walk `currentEntries`. For each:
 *        - Diff the parsed `Payload` against the snapshot via the
 *          subclass-supplied `payloadEqual`. If different (or new),
 *          update the snapshot and add the key to a "changed-or-added"
 *          set so the subclass can re-register it.
 *        - Independently diff the entry's `sourcePath /
 *          systemSourcePath`. When the user copy is deleted and the
 *          system file re-emerges with byte-identical content, the
 *          payload diff alone misses it — settings UIs that show "this
 *          curve comes from /system/path" otherwise display stale data
 *          until the next genuine reload. This is the Phase 1c+1d
 *          metadata-source fix; it must survive any future refactor.
 *        - Always call `onTrackEntry` so the subclass's typed entry
 *          map mirrors the current scan.
 *   4. Hand the diff to `commitToRegistry(removedKeys, currentMap,
 *      changedOrAddedKeys)` exactly once. The subclass is the sole
 *      caller of its registry's mutate methods — the base does no
 *      registry I/O itself, which keeps the per-key vs. bulk asymmetry
 *      between Curve (per-key `registerFactory`) and Profile (bulk
 *      `reloadFromOwner`) cleanly localized to the subclass.
 *
 * @tparam Payload  Per-file value the sink's `parseFile` produces and
 *                  stuffs into `ParsedEntry::payload` via `std::any`.
 *                  Must be value-copy-constructible (held in the
 *                  snapshot map) and comparable via the subclass-
 *                  supplied `payloadEqual`.
 */
template<typename Payload>
class BatchedSink : public PhosphorJsonLoader::IDirectoryLoaderSink
{
public:
    /**
     * @param category  Reference to the consumer's `Q_LOGGING_CATEGORY`,
     *                  used for the payload-type-mismatch warning. Held
     *                  by reference for the sink's lifetime — Q_LOGGING_CATEGORY
     *                  produces a process-static instance, so the
     *                  reference is always valid.
     * @param ownerTag  Stable per-instance tag used by subclasses for
     *                  partitioned-ownership registry teardown. Stored
     *                  here (rather than per-subclass) since both
     *                  consumers always need it.
     */
    BatchedSink(const QLoggingCategory& category, QString ownerTag)
        : m_category(&category)
        , m_ownerTag(std::move(ownerTag))
    {
    }

    /// Parent-loader-visible flag — read by the lambda bound to
    /// `DirectoryLoader::entriesChanged` to decide whether the consumer
    /// signal (`curvesChanged` / `profilesChanged`) fires. Re-set to
    /// false at the start of every `commitBatch`; flipped to true on
    /// any tracked-state change (add / remove / payload diff /
    /// source-metadata diff).
    bool lastBatchChanged = false;

    /// Owner tag used for partitioned-ownership registry teardown.
    /// Exposed for parent-loader access (ownerTag() accessor + dtor's
    /// `unregisterByOwner` call).
    const QString& ownerTag() const
    {
        return m_ownerTag;
    }

    /**
     * @brief Final implementation of `IDirectoryLoaderSink::commitBatch`.
     *
     * Implements the diff-and-flag machinery; the per-schema registry
     * mutation is delegated to `commitToRegistry`. Marked `final` so
     * subclasses can't accidentally re-implement the diff logic and
     * silently drop the metadata-source check (Phase 1c+1d) or the
     * lastBatchChanged reset.
     */
    void commitBatch(const QStringList& removedKeys, const QList<PhosphorJsonLoader::ParsedEntry>& currentEntries) final
    {
        // Reset the per-batch change flag. Every add / remove /
        // payload-diff / source-metadata-diff flips it back to true.
        lastBatchChanged = false;

        // Walk removals first so a re-add of the same key on the same
        // pass (delete-then-recreate within one debounce window) sees
        // a clean snapshot and registers fresh.
        for (const QString& key : removedKeys) {
            const bool hadPayload = m_lastCommittedPayloads.remove(key) > 0;
            const bool hadSources = m_lastCommittedSources.remove(key) > 0;
            if (hadPayload || hadSources) {
                lastBatchChanged = true;
            }
            // Always notify the subclass — its typed entries map may
            // hold a stale entry even if our payload snapshot lost it
            // earlier (defensive; in practice both maps move together).
            onUntrackEntry(key);
        }

        // Build the post-rescan map and the "needs registry write" set
        // in a single pass. The subclass's `commitToRegistry` consumes
        // both: per-key consumers (Curve) iterate the changed set;
        // bulk-replace consumers (Profile) hand the full current map
        // straight to `reloadFromOwner`.
        QHash<QString, Payload> currentMap;
        currentMap.reserve(currentEntries.size());
        QStringList changedOrAddedKeys;
        changedOrAddedKeys.reserve(currentEntries.size());

        for (const auto& parsed : currentEntries) {
            const Payload* payload = std::any_cast<Payload>(&parsed.payload);
            if (!payload) {
                qCWarning(*m_category) << "commitBatch: payload type-mismatch for" << parsed.key;
                continue;
            }

            // Payload-diff: fresh registration if the key is new, or
            // the parsed payload differs from the snapshot per the
            // subclass's equality definition (wire-format string for
            // Curve, `Profile::operator==` for Profile).
            const auto snapshotIt = m_lastCommittedPayloads.constFind(parsed.key);
            const bool payloadChanged =
                snapshotIt == m_lastCommittedPayloads.constEnd() || !payloadEqual(*snapshotIt, *payload);

            // Source-metadata diff: PHASE 1c+1d FIX. When the user copy
            // is deleted and the system file re-emerges with byte-
            // identical content, payloadChanged is false but the
            // entry's sourcePath / systemSourcePath have shifted —
            // settings UIs that surface "this entry comes from /system/
            // path" otherwise display stale data until the next genuine
            // payload-different reload. Always check, independent of
            // the payload diff, so the consumer signal fires even when
            // only the source paths moved.
            const QPair<QString, QString> currentSources{parsed.sourcePath, parsed.systemSourcePath};
            const auto sourcesIt = m_lastCommittedSources.constFind(parsed.key);
            const bool sourcesChanged = sourcesIt == m_lastCommittedSources.constEnd() || *sourcesIt != currentSources;

            if (payloadChanged) {
                m_lastCommittedPayloads.insert(parsed.key, *payload);
                changedOrAddedKeys.append(parsed.key);
                lastBatchChanged = true;
            }
            if (sourcesChanged) {
                m_lastCommittedSources.insert(parsed.key, currentSources);
                lastBatchChanged = true;
            }

            // Mirror the parsed entry into the subclass's typed map so
            // the public `entries()` accessor stays current. Subclasses
            // store whatever extra fields (displayName, custom labels)
            // they extract from the payload here.
            onTrackEntry(parsed.key, *payload, parsed.sourcePath, parsed.systemSourcePath);

            currentMap.insert(parsed.key, *payload);
        }

        // Single delegation point. The subclass picks the registry-
        // write shape: per-key `registerFactory(key, ...)` for Curve,
        // bulk `reloadFromOwner(ownerTag, currentMap)` for Profile.
        // Either implementation gets the full current map (so bulk
        // consumers can hand it straight in) plus the changed/added
        // subset (so per-key consumers don't re-register unchanged
        // entries — that would round-trip the registry mutex and
        // serialize all consumer signal handlers for nothing).
        commitToRegistry(removedKeys, currentMap, changedOrAddedKeys);
    }

protected:
    /**
     * @brief Subclass equality predicate over `Payload`.
     *
     * Called once per current entry to decide whether the parsed
     * payload differs from the previous commit's snapshot. Curve
     * compares wire-format strings (the canonical Curve serialization,
     * stable across reloads); Profile delegates to `Profile::operator==`.
     */
    virtual bool payloadEqual(const Payload& a, const Payload& b) const = 0;

    /**
     * @brief Subclass-driven registry write.
     *
     * Called exactly once per `commitBatch`, after the diff is
     * computed. The subclass is the sole entity that touches its
     * target registry — the base does no registry I/O.
     *
     * @param removedKeys           Keys evicted in this batch. Per-key
     *                              consumers should call
     *                              `unregisterFactory(key)` for each
     *                              that the subclass actually held;
     *                              bulk consumers can ignore this (the
     *                              full-map replacement handles
     *                              removals implicitly).
     * @param currentMap            Full post-rescan map. Bulk consumers
     *                              hand this straight to a
     *                              `reloadFromOwner`-style call.
     * @param changedOrAddedKeys    Subset of `currentMap` whose payload
     *                              differs from the previous commit
     *                              (or whose key is new). Per-key
     *                              consumers iterate this list and
     *                              call `registerFactory(key,
     *                              currentMap[key])` to skip
     *                              re-registering unchanged entries.
     */
    virtual void commitToRegistry(const QStringList& removedKeys, const QHash<QString, Payload>& currentMap,
                                  const QStringList& changedOrAddedKeys) = 0;

    /**
     * @brief Subclass hook to mirror the parsed entry into its public
     *        typed `entries()` map.
     *
     * Called for every entry in `currentEntries`, regardless of
     * whether the payload or sources changed — the subclass's typed
     * entry struct may carry derived fields (e.g. CurveLoader's
     * `displayName`) that need to be refreshed from the parsed
     * payload on every commit.
     */
    virtual void onTrackEntry(const QString& key, const Payload& payload, const QString& sourcePath,
                              const QString& systemSourcePath) = 0;

    /**
     * @brief Subclass hook to drop an evicted entry from its public
     *        typed `entries()` map.
     */
    virtual void onUntrackEntry(const QString& key) = 0;

private:
    const QLoggingCategory* m_category; ///< pinned at ctor; logging category for type-mismatch warnings
    QString m_ownerTag; ///< stable per-instance tag for partitioned ownership

    /// Snapshot of the last-committed payload per key, used as the
    /// left-hand side of the payload diff in `commitBatch`. Removed
    /// keys are evicted before the corresponding `unregister` so a
    /// later re-add produces a miss and registers fresh.
    QHash<QString, Payload> m_lastCommittedPayloads;

    /// Snapshot of `(sourcePath, systemSourcePath)` per key, used as
    /// the left-hand side of the metadata diff in `commitBatch`. This
    /// is the Phase 1c+1d source-metadata fix made first-class — when
    /// the user copy is deleted and the system file re-emerges with
    /// byte-identical content, the source paths shift but the payload
    /// stays equal, so a metadata-only consumer-signal must still fire.
    QHash<QString, QPair<QString, QString>> m_lastCommittedSources;
};

} // namespace PhosphorAnimation::detail
