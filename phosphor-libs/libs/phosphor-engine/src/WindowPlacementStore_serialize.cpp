// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The store's on-disk shape: enumeration, serialize, deserialize, the format
// migrations and size.
// Split out of WindowPlacementStore.cpp, which crossed the file-size ceiling
// when record() gained the per-desktop zone merge. Pure I/O over the record
// list, sharing nothing with the claim and release machinery there but the
// members.

#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1Char>
#include <QLoggingCategory>
#include <QPair>

#include <algorithm>
#include <limits>
#include <utility>

namespace PhosphorEngine {

namespace {
Q_LOGGING_CATEGORY(lcPlacementFormat, "org.phosphor.engine.placementstore")

QLatin1String versionKey()
{
    return QLatin1String("_version");
}

/// Shift every free-geometry rect of every record in @p root by @p sign times its output's origin. With
/// @p dropUnknown, a rect whose output @p originFor does not know is removed instead; otherwise it counts as (0,0).
/// Returns the number of rects dropped.
int shiftFreeGeometry(QJsonObject& root, const WindowPlacementStore::OutputOrigin& originFor, int sign,
                      bool dropUnknown)
{
    int dropped = 0;
    for (auto bucket = root.begin(); bucket != root.end(); ++bucket) {
        if (!bucket.value().isArray()) {
            continue;
        }
        QJsonArray records = bucket.value().toArray();
        for (qsizetype i = 0; i < records.size(); ++i) {
            QJsonObject record = records.at(i).toObject();
            QJsonObject freeGeo = record.value(QLatin1String("freeGeo")).toObject();
            if (freeGeo.isEmpty()) {
                continue;
            }
            const QStringList screenIds = freeGeo.keys();
            for (const QString& screenId : screenIds) {
                const std::optional<QPoint> origin = originFor ? originFor(screenId) : std::optional<QPoint>{};
                if (!origin && dropUnknown) {
                    freeGeo.remove(screenId);
                    ++dropped;
                    continue;
                }
                const QPoint by = origin.value_or(QPoint()) * sign;
                QJsonObject rect = freeGeo.value(screenId).toObject();
                rect[QLatin1String("x")] = rect.value(QLatin1String("x")).toInt() + by.x();
                rect[QLatin1String("y")] = rect.value(QLatin1String("y")).toInt() + by.y();
                freeGeo[screenId] = rect;
            }
            if (freeGeo.isEmpty()) {
                record.remove(QLatin1String("freeGeo"));
            } else {
                record[QLatin1String("freeGeo")] = freeGeo;
            }
            records[i] = record;
        }
        bucket.value() = records;
    }
    return dropped;
}

/// Version 1 to 2: the absolute float-back rects become relative to their output, measured against the outputs
/// known now, which are the arrangement the last session saved them in unless the monitors moved while the daemon
/// was down. A rect on an output not known now cannot be placed in any frame and is dropped, once.
void migrateV1ToV2(QJsonObject& root, const WindowPlacementStore::OutputOrigin& originFor)
{
    const int dropped = shiftFreeGeometry(root, originFor, -1, /*dropUnknown=*/true);
    if (dropped > 0) {
        qCInfo(lcPlacementFormat) << "migrateV1ToV2: dropped" << dropped
                                  << "remembered floating position(s) on monitors not connected now";
    }
    root[versionKey()] = 2;
}

/// The format migrations, one step per version bump, each transforming the whole root in place and stamping the
/// version it produces (the ConfigSchemaVersion chain's shape, for the placements blob).
struct FormatMigration
{
    int from;
    void (*apply)(QJsonObject& root, const WindowPlacementStore::OutputOrigin& originFor);
};
constexpr FormatMigration kFormatMigrations[] = {{1, &migrateV1ToV2}};

/// The origin the plain serialize / deserialize measure every output from.
std::optional<QPoint> originAtZero(const QString&)
{
    return QPoint();
}
} // namespace

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
    return serialize(keep, &originAtZero);
}

void WindowPlacementStore::deserialize(const QJsonObject& obj)
{
    deserialize(obj, &originAtZero);
}

QJsonObject WindowPlacementStore::serialize(const std::function<bool(const WindowPlacement&)>& keep,
                                            const OutputOrigin& originFor) const
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
            arr.append(p.toJson());
        }
        if (!arr.isEmpty()) {
            root[it.key()] = arr;
        }
    }
    if (!root.isEmpty()) {
        shiftFreeGeometry(root, originFor, -1, /*dropUnknown=*/false);
        root[versionKey()] = FormatVersion;
    }
    return root;
}

int WindowPlacementStore::formatVersionOf(const QJsonObject& stored)
{
    return stored.value(versionKey()).toInt(1);
}

void WindowPlacementStore::deserialize(const QJsonObject& stored, const OutputOrigin& originFor)
{
    // Up to FormatVersion first (an unstamped root is version 1), then into
    // this session's absolute frame. A root from a newer build is read as the
    // current format, the best that can be done with it.
    QJsonObject obj = stored;
    const int version = formatVersionOf(stored);
    for (const FormatMigration& step : kFormatMigrations) {
        if (step.from >= version && step.from < FormatVersion) {
            step.apply(obj, originFor);
        }
    }
    if (version > FormatVersion) {
        qCWarning(lcPlacementFormat) << "deserialize: placements format" << version << "is newer than" << FormatVersion
                                     << "- reading it as" << FormatVersion;
    }
    obj.remove(versionKey());
    shiftFreeGeometry(obj, originFor, 1, /*dropUnknown=*/false);

    // WHOLE-STORE REPLACE. The daemon loads once per start, through
    // WindowTrackingAdaptor's constructor, which runs before any window opens,
    // so discarding what is here costs nothing. (The engines' load delegates
    // would load again, but nothing in the daemon calls them.) Calling this
    // mid-session would not be safe. It would drop every live capture and void
    // every open claim, so windows already placed this session would lose the
    // record their engines restore from.
    m_byApp.clear();
    m_sequence = 0;
    // Every claim named a record in the store being replaced.
    m_openPairing.clear();
    m_claimedBy.clear();
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
    // Every touch mark named a record in the store replaced, and the replay
    // marks a persisted duplicate it merges unchanged, which no live window
    // touched (F1103).
    m_touchedThisSession.clear();

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
