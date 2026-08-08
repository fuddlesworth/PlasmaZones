// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>

#include <QJsonArray>
#include <QLatin1Char>
#include <QLoggingCategory>

#include <algorithm>
#include <limits>
#include <utility>

namespace PhosphorEngine {

namespace {
Q_LOGGING_CATEGORY(lcPlacementStore, "org.phosphor.engine.placementstore")

// Instance-identity match for STORE keys. Contract note: ids without a '|'
// separator only match EXACTLY here — unlike the registry's
// extractInstanceId, which treats a bare string AS the instance id. The store
// is only ever keyed with full appId|uuid composites (capture and restore
// both build them), so a bare id reaching this predicate is foreign input
// that must not fuzzy-match a composite's uuid component.
bool sameWindowInstance(const QString& lhs, const QString& rhs)
{
    if (lhs == rhs) {
        return true;
    }
    if (!lhs.contains(QLatin1Char('|')) || !rhs.contains(QLatin1Char('|'))) {
        return false;
    }
    const QString lhsInstance = PhosphorIdentity::WindowId::extractInstanceId(lhs);
    return !lhsInstance.isEmpty() && lhsInstance == PhosphorIdentity::WindowId::extractInstanceId(rhs);
}
} // namespace

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
    // Locate the existing record for the same live instance, wherever it lives.
    // Instance uniqueness holds for ids sameWindowInstance can relate (composite
    // `app|uuid` forms) — two BARE-id entries under different buckets fall
    // outside that guarantee (see clearFreeGeometry's all-bucket sweep) — so
    // once found we stop, WITHOUT running the loop's `++it` after a possible
    // erase(it) (would increment an invalidated iterator).
    for (auto it = m_byApp.begin(); it != m_byApp.end();) {
        QList<WindowPlacement>& bucket = it.value();
        for (int i = 0; i < bucket.size(); ++i) {
            const WindowPlacement& stored = bucket.at(i);
            if (!sameWindowInstance(stored.windowId, incoming.windowId)) {
                continue;
            }
            // Merge incoming into a copy of the existing record. Context (screen /
            // desktop / activity / kind) is the OWNING engine's business, so only a
            // real engine capture updates it — a geometry-only write (no engine slot,
            // e.g. recordFreeGeometry) must NOT clobber the managed-context fields.
            WindowPlacement merged = bucket.at(i);
            merged.windowId = incoming.windowId;
            if (!incoming.engines.isEmpty()) {
                // Never blank a known managed screen: an engine capture with
                // an EMPTY screenId (a floating window whose engine lost its
                // screen assignment) must not make the record screen-agnostic
                // forever — the reopen accept and the cross-screen downgrade
                // both key off a real screen value.
                if (!incoming.screenId.isEmpty()) {
                    merged.screenId = incoming.screenId;
                }
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
            evictForCapacity(dst);
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
    evictForCapacity(bucket);
    bucket.append(incoming);
    return true;
}

void WindowPlacementStore::evictForCapacity(QList<WindowPlacement>& bucket)
{
    // Evict contentless residue FIRST (oldest such entry), then the oldest
    // record NOT bound to a still-open window, and only as the last resort
    // the positionally-oldest record outright. The first tier exists because
    // a bare floating slot with no geometry must never push a real snapped or
    // tiled placement out of the FIFO (WindowPlacement::hasRestorableContent
    // documents this exact starvation hazard); the second because deleting a
    // LIVE window's record leaves that window recordless — the same harm the
    // live-instance probe guards takeForReopen's fallback against, reached by
    // the eviction door instead.
    while (bucket.size() >= MaxPerApp) {
        int victim = -1;
        for (int i = 0; i < bucket.size(); ++i) {
            if (!bucket.at(i).hasRestorableContent()) {
                victim = i;
                break;
            }
        }
        if (victim < 0 && m_liveInstanceProbe) {
            for (int i = 0; i < bucket.size(); ++i) {
                if (!m_liveInstanceProbe(bucket.at(i).windowId)) {
                    victim = i;
                    break;
                }
            }
        }
        bucket.removeAt(victim >= 0 ? victim : 0);
    }
}

namespace {
/// True when @p p carries float-back geometry but NO managed (snapped/tiled)
/// engine slot — i.e. a pure floating placement whose only value is its
/// remembered free position. A record with a snapped/tiled slot is a managed
/// placement and is never a collapse candidate.
bool isPureFloatRecord(const WindowPlacement& p)
{
    // A slot-LESS record with remembered free geometry (the shape
    // recordFreeGeometry produces) is the purest float duplicate there is —
    // exactly what the collapse exists to converge. Only an empty record with
    // neither slots nor geometry is excluded (nothing to keep or prune).
    if (p.engines.isEmpty()) {
        return !p.freeGeometryByScreen.isEmpty();
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

    const auto findKeep = [&]() -> int {
        for (int i = 0; i < bucket.size(); ++i) {
            // Instance match, not exact-id: every other lookup in this file
            // matches through sameWindowInstance so a live window whose appId
            // prefix drifted still resolves — an exact compare here silently
            // no-ops the collapse for exactly the renamed-window case.
            if (sameWindowInstance(bucket.at(i).windowId, keepWindowId)) {
                return i;
            }
        }
        return -1;
    };
    int keepIdx = findKeep();
    // Only collapse when the kept record is itself a pure float — a managed
    // (snapped/tiled) close has no business pruning float siblings — and only when
    // it actually remembers a float position.
    if (keepIdx < 0 || !isPureFloatRecord(bucket.at(keepIdx)) || bucket.at(keepIdx).freeGeometryByScreen.isEmpty()) {
        return false;
    }

    // Fixpoint prune: remove every pure-float sibling that shares a screen the kept
    // record currently covers, absorbing the sibling's OTHER-screen geometry first
    // (so a different-monitor position the sibling alone held is never dropped). The
    // kept record's coverage grows as it absorbs, so re-scanning until stable
    // collapses the WHOLE set of float records transitively connected by a shared
    // screen — regardless of FIFO order — into the single kept record, leaving no
    // residual same-screen duplicate to rotate to. bucket <= MaxPerApp, so the
    // repeat is cheap. Wholly different-monitor records (no shared screen) are never
    // pruned: distinct-monitor float memory is preserved as its own record.
    bool removedAny = false;
    bool changed = true;
    while (changed) {
        changed = false;
        keepIdx = findKeep();
        if (keepIdx < 0) {
            break; // defensive — the kept record itself is never removed
        }
        for (int i = bucket.size() - 1; i >= 0; --i) {
            if (i == keepIdx) {
                continue;
            }
            const WindowPlacement& other = bucket.at(i);
            if (!isPureFloatRecord(other)) {
                continue; // never prune a managed placement
            }
            if (m_liveInstanceProbe && m_liveInstanceProbe(other.windowId)) {
                // A still-OPEN sibling's record is not a stale duplicate: it
                // is that window's live float-back, and pruning it leaves the
                // sibling recordless — the same harm the live-instance probe
                // guards the reopen fallback against, reached via the close
                // collapse instead.
                continue;
            }
            bool sharesScreen = false;
            for (auto git = other.freeGeometryByScreen.constBegin(); git != other.freeGeometryByScreen.constEnd();
                 ++git) {
                if (bucket.at(keepIdx).freeGeometryByScreen.contains(git.key())) {
                    sharesScreen = true;
                    break;
                }
            }
            if (!sharesScreen) {
                continue; // wholly different-monitor record — distinct memory, kept
            }
            // Copy the sibling's data out before mutating bucket[keepIdx]:
            // operator[] may detach/reallocate the list and dangle `other`.
            const QHash<QString, QRect> otherFree = other.freeGeometryByScreen;
            const QHash<QString, EngineSlot> otherEngines = other.engines;
            WindowPlacement& keep = bucket[keepIdx];
            for (auto git = otherFree.constBegin(); git != otherFree.constEnd(); ++git) {
                if (!keep.freeGeometryByScreen.contains(git.key())) {
                    keep.freeGeometryByScreen.insert(git.key(), git.value()); // kept (newest) wins; fill gaps only
                }
            }
            // Absorb the sibling's engine slots the same fill-gaps-only way.
            // Since the synthesized close slot is keyed per OWNING engine, two
            // pure-float siblings can carry float verdicts for DIFFERENT
            // engines (a scrolling-mode close and an autotile-mode close of
            // the same app); dropping the sibling without absorbing its slot
            // silently lost the other mode's float verdict. Safe by
            // construction: isPureFloatRecord guarantees the sibling carries
            // no snapped/tiled slot, so this can only add floating slots.
            for (auto eit = otherEngines.constBegin(); eit != otherEngines.constEnd(); ++eit) {
                if (!keep.engines.contains(eit.key())) {
                    keep.engines.insert(eit.key(), eit.value());
                }
            }
            bucket.removeAt(i);
            removedAny = true;
            changed = true;
            break; // indices + keepIdx shifted; restart the scan
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

    // 1. Same-instance match first (daemon restart, uuid stable). A record
    //    whose windowId matches but whose `accept` predicate rejects it is NOT
    //    consumed here; the loop falls through to the appId FIFO below (the
    //    semantics are "consume the oldest restorable record", not "fail if the
    //    same-instance record is unrestorable").
    if (!windowId.isEmpty()) {
        for (auto it = m_byApp.begin(); it != m_byApp.end(); ++it) {
            QList<WindowPlacement>& bucket = it.value();
            for (int i = 0; i < bucket.size(); ++i) {
                if (sameWindowInstance(bucket.at(i).windowId, windowId) && matches(bucket.at(i))) {
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

namespace {
/// The shared reopen accept — see the takeForReopen header doc. Hoisted into
/// the store (rather than per-engine lambdas) so autotile and scroll cannot
/// drift apart, and so the exact-final gate can reason about the SAME
/// predicate it applies.
bool acceptsReopen(const WindowPlacement& p, const QString& engineId, const QString& windowId, const QString& screenId)
{
    const EngineSlot s = p.slotFor(engineId);
    if (s.state == WindowPlacement::stateFloating()) {
        // A geometry-less floating record is meaningful for the SAME instance
        // (restore floating in place), but consumed by a FIFO sibling it
        // floats a fresh window at its spawn rect for no user-visible reason
        // while burning a slot a real placement may need. sameWindowInstance,
        // not a raw extractInstanceId compare: a bare id (no '|') must not
        // fuzzy-match a composite's uuid component — the file's contract note
        // on sameWindowInstance documents exactly that trap.
        const bool sameInstance = sameWindowInstance(p.windowId, windowId);
        if (!sameInstance && !p.anyFreeGeometry().isValid()) {
            return false;
        }
        return p.screenId.isEmpty() || p.screenId == screenId;
    }
    // FLOATING slots only: a TILED record is never consumed — it stands as
    // the exact-final verdict that the window closed tiled (see the header).
    return false;
}
} // namespace

std::optional<WindowPlacement> WindowPlacementStore::takeForReopen(const QString& engineId, const QString& windowId,
                                                                   const QString& appId, const QString& screenId)
{
    const auto accept = [&](const WindowPlacement& p) {
        return acceptsReopen(p, engineId, windowId, screenId);
    };
    // Exact-record rejection is FINAL — see the header contract — but only a
    // record carrying a slot FOR THE ASKING ENGINE is a verdict. Every fresh
    // open writes a geometry-only, slot-less record under the live uuid (the
    // pre-tile free-geometry capture) before any engine's restore runs, so an
    // exact hit alone proves nothing; gating on that stub vetoed the FIFO
    // fallback and lost every close/reopen float and column restore.
    if (const auto own = peekExact(windowId); own && own->engines.contains(engineId) && !accept(*own)) {
        qCDebug(lcPlacementStore) << "takeForReopen:" << engineId << "exact record for" << windowId
                                  << "rejected (slot state" << own->slotFor(engineId).state << "screen" << own->screenId
                                  << ") — final, no FIFO fallback";
        return std::nullopt;
    }
    // Same-instance match first (daemon restart, uuid stable) — take()'s
    // branch 1, scoped by the empty appId.
    std::optional<WindowPlacement> rec = take(windowId, QString(), accept);
    if (!rec && !appId.isEmpty()) {
        // appId fallback: the NEWEST accepted record not bound to a live
        // window — see the header doc. (take()'s oldest-first FIFO stays as
        // is for the snap paths that consume through it directly.)
        const auto it = m_byApp.find(appId);
        if (it != m_byApp.end()) {
            QList<WindowPlacement>& bucket = it.value();
            int best = -1;
            for (int i = 0; i < bucket.size(); ++i) {
                const WindowPlacement& p = bucket.at(i);
                if (!accept(p)) {
                    continue;
                }
                if (m_liveInstanceProbe && m_liveInstanceProbe(p.windowId)) {
                    continue; // an open sibling's record is not up for grabs
                }
                if (best < 0 || p.sequence > bucket.at(best).sequence) {
                    best = i;
                }
            }
            if (best >= 0) {
                rec = bucket.takeAt(best);
                if (bucket.isEmpty()) {
                    m_byApp.erase(it);
                }
            }
        }
    }
    if (rec) {
        qCDebug(lcPlacementStore) << "takeForReopen:" << engineId << "consumed" << rec->windowId << "for" << windowId
                                  << "slot state" << rec->slotFor(engineId).state << "order"
                                  << rec->slotFor(engineId).order << "screen" << rec->screenId;
        // Re-bind to the live windowId and re-record — header contract rule 2.
        rec->windowId = windowId;
        record(*rec);
    } else {
        qCDebug(lcPlacementStore) << "takeForReopen:" << engineId << "no restorable record for" << windowId << "appId"
                                  << appId << "on" << screenId;
    }
    return rec;
}

std::optional<WindowPlacement>
WindowPlacementStore::peek(const QString& windowId, const QString& appId,
                           const std::function<bool(const WindowPlacement&)>& accept) const
{
    const auto matches = [&](const WindowPlacement& p) {
        return !accept || accept(p);
    };

    // 1. Same-instance match first (same window, daemon restart).
    if (!windowId.isEmpty()) {
        for (auto it = m_byApp.constBegin(); it != m_byApp.constEnd(); ++it) {
            for (const WindowPlacement& p : it.value()) {
                if (sameWindowInstance(p.windowId, windowId) && matches(p)) {
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

std::optional<WindowPlacement>
WindowPlacementStore::peekForReclaim(const QString& windowId, const QString& appId,
                                     const std::function<bool(const WindowPlacement&)>& accept) const
{
    if (appId.isEmpty()) {
        return std::nullopt;
    }
    const auto it = m_byApp.constFind(appId);
    if (it == m_byApp.constEnd()) {
        return std::nullopt;
    }
    const auto matches = [&](const WindowPlacement& p) {
        return !accept || accept(p);
    };
    const WindowPlacement* best = nullptr;
    for (const WindowPlacement& p : it.value()) {
        if (!matches(p)) {
            continue;
        }
        // The window's OWN record is its history and wins outright — the
        // probe answering "live" for the asking window itself is not an
        // exclusion (daemon-restart case: same uuid, window open).
        if (sameWindowInstance(p.windowId, windowId)) {
            return p;
        }
        if (m_liveInstanceProbe && m_liveInstanceProbe(p.windowId)) {
            continue; // an open sibling's record is not evidence about THIS window
        }
        if (!best || p.sequence > best->sequence) {
            best = &p;
        }
    }
    if (best) {
        return *best;
    }
    return std::nullopt;
}

bool WindowPlacementStore::releaseEngineSlot(const QString& windowId, const QString& engineId)
{
    bool changed = false;
    // EVERY matching record, not the first: appId drift can file one
    // instance's records in two buckets, and QHash order does not promise the
    // one carrying the stale slot comes first — an early return there
    // silently no-ops and leaves the false home standing.
    for (auto it = m_byApp.begin(); it != m_byApp.end(); ++it) {
        for (WindowPlacement& p : it.value()) {
            if (!sameWindowInstance(p.windowId, windowId)) {
                continue;
            }
            const auto slotIt = p.engines.find(engineId);
            if (slotIt == p.engines.end() || slotIt->state == WindowPlacement::stateReleased()) {
                continue;
            }
            // Downgrade in place: the slot stays present (so takeForReopen's
            // exact-final gate still recognises this instance as one the
            // engine has seen) while ceasing to be managed (so the
            // cross-screen reclaim no longer reads it as a home).
            slotIt->state = QString(WindowPlacement::stateReleased());
            slotIt->zoneIds.clear();
            slotIt->order = -1;
            changed = true;
        }
    }
    return changed;
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
            if (sameWindowInstance(p.windowId, windowId)) {
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
            if (sameWindowInstance(bucket.at(i).windowId, windowId)) {
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
    // Sweep ALL buckets rather than returning on the first hit: record()
    // enforces instance uniqueness only for ids sameWindowInstance can relate,
    // and two bare-id entries under different buckets fall outside that
    // guarantee — an early return would leave the second one holding stale
    // geometry.
    bool cleared = false;
    for (auto it = m_byApp.begin(); it != m_byApp.end(); ++it) {
        for (WindowPlacement& p : it.value()) {
            if (sameWindowInstance(p.windowId, windowId) && !p.freeGeometryByScreen.isEmpty()) {
                p.freeGeometryByScreen.clear();
                cleared = true;
            }
        }
    }
    return cleared;
}

bool WindowPlacementStore::clearFreeGeometry(const QString& windowId, const QString& screenId)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return false;
    }
    // Screen-scoped consume: the drag-out/drop paths consume exactly one
    // screen's float-back, and wiping the whole map would destroy the
    // window's remembered free position on every OTHER monitor — the
    // distinct-monitor float memory collapsePureFloatSiblings deliberately
    // preserves.
    bool cleared = false;
    for (auto it = m_byApp.begin(); it != m_byApp.end(); ++it) {
        for (WindowPlacement& p : it.value()) {
            if (sameWindowInstance(p.windowId, windowId) && p.freeGeometryByScreen.remove(screenId) > 0) {
                cleared = true;
            }
        }
    }
    return cleared;
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
    m_sequence = 0;
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
