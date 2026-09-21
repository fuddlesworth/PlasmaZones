// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The store's on-disk shape: enumeration, serialize, deserialize and size.
// Split out of WindowPlacementStore.cpp, which crossed the file-size ceiling
// when record() gained the per-desktop zone merge. Pure I/O over the record
// list, sharing nothing with the reclaim, claim and release machinery there
// but the members.

#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1Char>
#include <QPair>

#include <algorithm>
#include <limits>
#include <utility>

namespace PhosphorEngine {

QList<WindowPlacement> WindowPlacementStore::records() const
{
    QList<WindowPlacement> out;
    for (auto it = m_byApp.constBegin(); it != m_byApp.constEnd(); ++it) {
        out.append(it.value());
    }
    return out;
}

QJsonObject WindowPlacementStore::serialize(const std::function<bool(const WindowPlacement&)>& keep) const
{
    QJsonObject root;
    for (auto it = m_byApp.constBegin(); it != m_byApp.constEnd(); ++it) {
        // appIds never contain '|' (that delimits appId|uuid) — a key that does is
        // a corrupt identity; skip rather than persist poison.
        if (it.key().isEmpty() || it.key().contains(QLatin1Char('|'))) {
            continue;
        }
        QJsonArray arr;
        for (const WindowPlacement& p : it.value()) {
            if (keep && !keep(p)) {
                continue;
            }
            // Reclaim credit is DERIVED at save time when the probe is wired
            // (header contract): live windows persist as restore evidence, a
            // close within the shutdown grace still counts (the logout save),
            // and everything else — the cross-session graveyard included,
            // whatever its in-memory credit says — persists credit-less.
            if (m_liveInstanceProbe) {
                WindowPlacement stamped = p;
                stamped.reclaimEligible = m_liveInstanceProbe(p.windowId)
                    || (p.closedAtMsecs > 0
                        && QDateTime::currentMSecsSinceEpoch() - p.closedAtMsecs <= ShutdownCloseGraceMs);
                arr.append(stamped.toJson());
                continue;
            }
            arr.append(p.toJson());
        }
        if (!arr.isEmpty()) {
            root[it.key()] = arr;
        }
    }
    return root;
}

void WindowPlacementStore::deserialize(const QJsonObject& obj)
{
    // WHOLE-STORE REPLACE, and it runs more than once per startup: the daemon
    // loads through WindowTrackingAdaptor's constructor and again through the
    // engines' load delegate at finalizeStartup. Both are bringup, before any
    // window opens, so discarding what is here costs nothing. Calling this
    // mid-session would not be safe. It would drop every live capture and void
    // every open claim, so windows already placed this session would lose the
    // record their engines restore from.
    m_byApp.clear();
    m_sequence = 0;
    // Every claim named a record in the store being replaced.
    m_openPairing.clear();
    m_claimedBy.clear();
    // Likewise the in-flight move excuses: they describe this session's moves,
    // and both deserialize callers are startup-time (see the note above).
    m_movedLiveInstances.clear();
    QList<WindowPlacement> loaded;
    // Persisted ARRAY positions, keyed per (bucket, instance) — NOT a global
    // index into the flattened list: a renamed duplicate persisted under an
    // old bucket would carry that bucket's low global index into its NEW
    // bucket and sort ahead of the new bucket's genuinely older entries,
    // inverting the FIFO head take() consumes. Per-bucket positions keep each
    // bucket's order self-referential; a record merged in from ANOTHER bucket
    // (rename with no entry in the destination) has no key here and sorts
    // last, i.e. newest — matching the runtime rename-append behaviour.
    QHash<QPair<QString, QString>, int> firstPersistedPos;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.key().isEmpty() || it.key().contains(QLatin1Char('|'))) {
            continue;
        }
        int posInBucket = 0;
        const QJsonArray arr = it->toArray();
        for (const QJsonValue& v : arr) {
            WindowPlacement p = WindowPlacement::fromJson(it.key(), v.toObject());
            if (!p.isValid()) {
                continue;
            }
            // Drop a structureless windowId (no `appId|uuid` separator) — a forged
            // or truncated identity that no live window can exact-match. Do NOT
            // require the windowId prefix to equal the bucket appId: the stored
            // appId comes from the identity registry, which legitimately drifts
            // from the windowId's embedded class (e.g. Electron/CEF apps re-broadcast
            // their WM_CLASS mid-session). The appId-FIFO lookup keys on the bucket,
            // not the prefix, so such a record is still restorable.
            if (!p.windowId.contains(QLatin1Char('|'))) {
                continue;
            }
            const QPair<QString, QString> posKey{it.key(), PhosphorIdentity::WindowId::extractInstanceId(p.windowId)};
            if (!firstPersistedPos.contains(posKey)) {
                firstPersistedPos.insert(posKey, posInBucket);
            }
            ++posInBucket;
            loaded.append(p);
        }
    }
    // Positions exist because record()'s in-place merge keeps a record's
    // bucket POSITION while restamping its sequence, so position order and
    // sequence order legitimately diverge — replaying by sequence alone would
    // reorder buckets across a reload, flipping take()'s oldest-first head and
    // the eviction order the header documents as FIFO.

    // Replay oldest to newest through record() so persisted duplicates from an
    // appId-prefix mutation are merged into one live-instance record (sequence
    // decides MERGE precedence only). This also applies the normal per-app cap
    // in the same direction as runtime inserts.
    std::stable_sort(loaded.begin(), loaded.end(), [](const WindowPlacement& lhs, const WindowPlacement& rhs) {
        return lhs.sequence < rhs.sequence;
    });
    for (WindowPlacement& placement : loaded) {
        record(std::move(placement));
    }

    // Restore each bucket to its persisted array order (merged duplicates sit
    // at their earliest persisted position within THIS bucket — FIFO
    // semantics; cross-bucket merge arrivals sort last, i.e. newest).
    for (auto it = m_byApp.begin(); it != m_byApp.end(); ++it) {
        const QString bucketKey = it.key();
        std::stable_sort(it->begin(), it->end(),
                         [&firstPersistedPos, &bucketKey](const WindowPlacement& a, const WindowPlacement& b) {
                             const int pa = firstPersistedPos.value(
                                 {bucketKey, PhosphorIdentity::WindowId::extractInstanceId(a.windowId)},
                                 std::numeric_limits<int>::max());
                             const int pb = firstPersistedPos.value(
                                 {bucketKey, PhosphorIdentity::WindowId::extractInstanceId(b.windowId)},
                                 std::numeric_limits<int>::max());
                             return pa < pb;
                         });
    }
}

int WindowPlacementStore::size() const
{
    int n = 0;
    for (auto it = m_byApp.constBegin(); it != m_byApp.constEnd(); ++it) {
        n += it->size();
    }
    return n;
}

} // namespace PhosphorEngine
