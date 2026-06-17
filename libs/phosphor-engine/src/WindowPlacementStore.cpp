// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/WindowPlacementStore.h>

#include <QJsonArray>
#include <QLatin1Char>

namespace PhosphorEngine {

bool WindowPlacementStore::record(WindowPlacement incoming)
{
    if (incoming.windowId.isEmpty() || incoming.appId.isEmpty() || !incoming.isValid()) {
        return false;
    }

    // ONE record per window (NOT one per engine). A capture provides only the
    // CALLING engine's slot (in `engines`) plus any free-geometry update; record()
    // MERGES that into the window's existing record, leaving the OTHER engine's
    // slot and the other screens' free geometry untouched. This is what gives
    // per-mode state independence with a single shared free/float geometry: the
    // snap engine recording a "snapped" slot never wipes the autotile "floating"
    // slot, and updating the float position on one screen never drops another
    // screen's remembered free spot.
    //
    // Locate the existing record for the EXACT windowId, wherever it lives. windowId
    // is unique across buckets, so once found we stop — WITHOUT running the loop's
    // `++it` after a possible erase(it) (would increment an invalidated iterator).
    for (auto it = m_byApp.begin(); it != m_byApp.end();) {
        QList<WindowPlacement>& bucket = it.value();
        for (int i = 0; i < bucket.size(); ++i) {
            if (bucket.at(i).windowId != incoming.windowId) {
                continue;
            }
            // Merge incoming into a copy of the existing record. Context (screen /
            // desktop / activity / kind) is the OWNING engine's business, so only a
            // real engine capture updates it — a geometry-only write (no engine slot,
            // e.g. recordFreeGeometry) must NOT clobber the managed-context fields.
            WindowPlacement merged = bucket.at(i);
            if (!incoming.engines.isEmpty()) {
                merged.screenId = incoming.screenId;
                merged.virtualDesktop = incoming.virtualDesktop;
                merged.activity = incoming.activity;
                if (incoming.kind != WindowKind::Unknown) {
                    merged.kind = incoming.kind;
                }
            }
            for (auto e = incoming.engines.constBegin(); e != incoming.engines.constEnd(); ++e) {
                merged.engines.insert(e.key(), e.value());
            }
            for (auto g = incoming.freeGeometryByScreen.constBegin(); g != incoming.freeGeometryByScreen.constEnd();
                 ++g) {
                merged.freeGeometryByScreen.insert(g.key(), g.value());
            }

            const bool appIdChanged = (it.key() != incoming.appId);
            if (!appIdChanged && merged.sameContentAs(bucket.at(i))) {
                // Content-identical re-capture (sequence aside): leave the existing
                // record untouched and report "no change" so the save loop settles.
                return false;
            }
            merged.appId = incoming.appId;
            merged.sequence = ++m_sequence;

            if (!appIdChanged) {
                bucket[i] = merged; // same bucket — update in place
                return true;
            }
            // appId changed (mid-session rename): drop the stale entry here and
            // re-insert under the new appId bucket below.
            bucket.removeAt(i);
            if (bucket.isEmpty()) {
                m_byApp.erase(it); // iterator consumed — do not ++it
            }
            QList<WindowPlacement>& dst = m_byApp[merged.appId];
            while (dst.size() >= MaxPerApp) {
                dst.removeFirst();
            }
            dst.append(merged);
            return true;
        }
        // No match in this bucket — advance. (A match always returns above; the
        // appId-rename branch consumes the iterator via erase and returns, so we
        // only reach here when the inner loop fell through without a hit.)
        ++it;
    }

    // No existing record for this window — append a fresh one.
    incoming.sequence = ++m_sequence;
    QList<WindowPlacement>& bucket = m_byApp[incoming.appId];
    while (bucket.size() >= MaxPerApp) {
        bucket.removeFirst();
    }
    bucket.append(incoming);
    return true;
}

namespace {
/// True when @p p carries float-back geometry but NO managed (snapped/tiled)
/// engine slot — i.e. a pure floating placement whose only value is its
/// remembered free position. A record with a snapped/tiled slot is a managed
/// placement and is never a collapse candidate.
bool isPureFloatRecord(const WindowPlacement& p)
{
    if (p.engines.isEmpty()) {
        return false;
    }
    for (auto it = p.engines.constBegin(); it != p.engines.constEnd(); ++it) {
        if (it.value().state == WindowPlacement::stateSnapped() || it.value().state == WindowPlacement::stateTiled()) {
            return false;
        }
    }
    return true;
}
} // namespace

bool WindowPlacementStore::collapsePureFloatSiblings(const QString& appId, const QString& keepWindowId)
{
    if (appId.isEmpty() || keepWindowId.isEmpty()) {
        return false;
    }
    auto bit = m_byApp.find(appId);
    if (bit == m_byApp.end()) {
        return false;
    }
    QList<WindowPlacement>& bucket = bit.value();

    int keepIdx = -1;
    for (int i = 0; i < bucket.size(); ++i) {
        if (bucket.at(i).windowId == keepWindowId) {
            keepIdx = i;
            break;
        }
    }
    // Only collapse when the kept record is itself a pure float — a managed
    // (snapped/tiled) close has no business pruning float siblings.
    if (keepIdx < 0 || !isPureFloatRecord(bucket.at(keepIdx))) {
        return false;
    }
    // Snapshot the kept record's remembered float screens; a sibling sharing any
    // of them is a stale duplicate of the same per-app/per-screen float memory.
    const QList<QString> keptScreens = bucket.at(keepIdx).freeGeometryByScreen.keys();
    if (keptScreens.isEmpty()) {
        return false;
    }

    bool removedAny = false;
    for (int i = bucket.size() - 1; i >= 0; --i) {
        if (i == keepIdx) {
            continue;
        }
        const WindowPlacement& other = bucket.at(i);
        if (!isPureFloatRecord(other)) {
            continue; // never prune a managed placement
        }
        bool sharesScreen = false;
        for (const QString& screen : keptScreens) {
            if (other.freeGeometryByScreen.contains(screen)) {
                sharesScreen = true;
                break;
            }
        }
        if (sharesScreen) {
            // Absorb the sibling's float memory for any screen the kept record
            // does NOT already cover, so collapsing a same-screen duplicate never
            // silently drops a DIFFERENT-monitor position the sibling alone held.
            // The kept record is newest, so its own per-screen geometry wins; the
            // sibling only fills gaps. (keptScreens is the kept record's ORIGINAL
            // coverage snapshot, so a gap-filled screen does not retroactively make
            // an older sibling a "share" — its geometry for that screen is the same
            // memory and is correctly left untouched.)
            //
            // Copy the sibling's geometry out first: mutating bucket[keepIdx] below
            // could detach/reallocate the list and dangle the `other` reference.
            const QHash<QString, QRect> otherFree = other.freeGeometryByScreen;
            WindowPlacement& keep = bucket[keepIdx];
            for (auto git = otherFree.constBegin(); git != otherFree.constEnd(); ++git) {
                if (!keep.freeGeometryByScreen.contains(git.key())) {
                    keep.freeGeometryByScreen.insert(git.key(), git.value());
                }
            }
            bucket.removeAt(i);
            removedAny = true;
            if (i < keepIdx) {
                --keepIdx; // the kept record shifted down by the removal
            }
        }
    }
    return removedAny;
}

std::optional<WindowPlacement> WindowPlacementStore::take(const QString& windowId, const QString& appId,
                                                          const std::function<bool(const WindowPlacement&)>& accept,
                                                          const std::function<bool(const WindowPlacement&)>& preferred)
{
    const auto matches = [&](const WindowPlacement& p) {
        return !accept || accept(p);
    };

    // 1. Exact-windowId match first (daemon restart, uuid stable). A record
    //    whose windowId matches but whose `accept` predicate rejects it is NOT
    //    consumed here; the loop falls through to the appId FIFO below (the
    //    semantics are "consume the oldest restorable record", not "fail if the
    //    exact record is unrestorable").
    if (!windowId.isEmpty()) {
        for (auto it = m_byApp.begin(); it != m_byApp.end(); ++it) {
            QList<WindowPlacement>& bucket = it.value();
            for (int i = 0; i < bucket.size(); ++i) {
                if (bucket.at(i).windowId == windowId && matches(bucket.at(i))) {
                    WindowPlacement p = bucket.takeAt(i);
                    if (bucket.isEmpty()) {
                        m_byApp.erase(it);
                    }
                    return p;
                }
            }
        }
    }

    // 2. appId FIFO (close/reopen, new uuid) — oldest accepted entry, but a
    //    `preferred` entry (also accepted) outranks a merely-accepted older one.
    if (!appId.isEmpty()) {
        auto it = m_byApp.find(appId);
        if (it != m_byApp.end()) {
            QList<WindowPlacement>& bucket = it.value();
            const auto consumeAt = [&](int i) {
                WindowPlacement p = bucket.takeAt(i);
                if (bucket.isEmpty()) {
                    m_byApp.erase(it);
                }
                return p;
            };
            // First pass: oldest entry satisfying accept AND preferred.
            if (preferred) {
                for (int i = 0; i < bucket.size(); ++i) {
                    if (matches(bucket.at(i)) && preferred(bucket.at(i))) {
                        return consumeAt(i);
                    }
                }
            }
            // Second pass: oldest merely-accepted entry.
            for (int i = 0; i < bucket.size(); ++i) {
                if (matches(bucket.at(i))) {
                    return consumeAt(i);
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<WindowPlacement>
WindowPlacementStore::peek(const QString& windowId, const QString& appId,
                           const std::function<bool(const WindowPlacement&)>& accept) const
{
    const auto matches = [&](const WindowPlacement& p) {
        return !accept || accept(p);
    };

    // 1. Exact-windowId match first (same window, daemon restart).
    if (!windowId.isEmpty()) {
        for (auto it = m_byApp.constBegin(); it != m_byApp.constEnd(); ++it) {
            for (const WindowPlacement& p : it.value()) {
                if (p.windowId == windowId && matches(p)) {
                    return p;
                }
            }
        }
    }

    // 2. appId fallback (uuid changed across login): the NEWEST accepted record,
    //    since the most recent placement is the right one to read live.
    if (!appId.isEmpty()) {
        const auto it = m_byApp.constFind(appId);
        if (it != m_byApp.constEnd()) {
            const WindowPlacement* best = nullptr;
            for (const WindowPlacement& p : it.value()) {
                if (matches(p) && (!best || p.sequence > best->sequence)) {
                    best = &p;
                }
            }
            if (best) {
                return *best;
            }
        }
    }
    return std::nullopt;
}

bool WindowPlacementStore::contains(const QString& windowId, const QString& appId) const
{
    if (!appId.isEmpty()) {
        const auto it = m_byApp.constFind(appId);
        if (it != m_byApp.constEnd() && !it->isEmpty()) {
            return true;
        }
    }
    if (windowId.isEmpty()) {
        return false;
    }
    for (auto it = m_byApp.constBegin(); it != m_byApp.constEnd(); ++it) {
        for (const WindowPlacement& p : it.value()) {
            if (p.windowId == windowId) {
                return true;
            }
        }
    }
    return false;
}

bool WindowPlacementStore::clear(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    bool removed = false;
    for (auto it = m_byApp.begin(); it != m_byApp.end();) {
        QList<WindowPlacement>& bucket = it.value();
        for (int i = bucket.size() - 1; i >= 0; --i) {
            if (bucket.at(i).windowId == windowId) {
                bucket.removeAt(i);
                removed = true;
            }
        }
        if (bucket.isEmpty()) {
            it = m_byApp.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

bool WindowPlacementStore::clearFreeGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // windowId is unique across buckets (record() enforces this), so the first
    // match is the only match — return as soon as it's cleared.
    for (auto it = m_byApp.begin(); it != m_byApp.end(); ++it) {
        for (WindowPlacement& p : it.value()) {
            if (p.windowId == windowId && !p.freeGeometryByScreen.isEmpty()) {
                p.freeGeometryByScreen.clear();
                return true;
            }
        }
    }
    return false;
}

int WindowPlacementStore::transform(const std::function<bool(WindowPlacement&)>& fn)
{
    if (!fn) {
        return 0;
    }
    int changed = 0;
    for (auto it = m_byApp.begin(); it != m_byApp.end(); ++it) {
        for (WindowPlacement& p : it.value()) {
            if (fn(p)) {
                ++changed;
            }
        }
    }
    return changed;
}

int WindowPlacementStore::removeIf(const std::function<bool(const WindowPlacement&)>& pred)
{
    if (!pred) {
        return 0;
    }
    int removed = 0;
    for (auto it = m_byApp.begin(); it != m_byApp.end();) {
        QList<WindowPlacement>& bucket = it.value();
        for (int i = bucket.size() - 1; i >= 0; --i) {
            if (pred(bucket.at(i))) {
                bucket.removeAt(i);
                ++removed;
            }
        }
        if (bucket.isEmpty()) {
            it = m_byApp.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

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
    m_byApp.clear();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.key().isEmpty() || it.key().contains(QLatin1Char('|'))) {
            continue;
        }
        QList<WindowPlacement> bucket;
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
            if (p.sequence > m_sequence) {
                m_sequence = p.sequence;
            }
            bucket.append(p);
        }
        // Enforce the per-app cap by dropping the OLDEST (front) to keep the newest
        // MaxPerApp — same direction as record()'s eviction (record() uses `>=`
        // because it appends one afterward; here nothing is appended, so `>`).
        while (bucket.size() > MaxPerApp) {
            bucket.removeFirst();
        }
        if (!bucket.isEmpty()) {
            m_byApp[it.key()] = bucket;
        }
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
