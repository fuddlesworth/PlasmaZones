// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Durable strip-structure snapshots. serializeStripState captures every live
// strip plus the un-consumed mode-round-trip stash as one JSON blob the
// daemon persists through the WTA KConfig layer; restoreStripState loads a
// blob back INTO THE STASH so the existing arrival-restore path
// (restoreFromStripStash) rebuilds each strip as its windows are announced —
// there is deliberately no second restore mechanism.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "scrollenginelogging.h"

#include <QJsonArray>
#include <QJsonObject>

namespace PhosphorScrollEngine {

namespace {

// JSON keys — QLatin1String per the Qt6 string rules. Schema is
// version-free by the no-ad-hoc-migration policy: absent key = empty,
// malformed entry = skipped.
inline QLatin1String kColumns()
{
    return QLatin1String("columns");
}
inline QLatin1String kFocused()
{
    return QLatin1String("focusedWindow");
}
inline QLatin1String kViewAnchor()
{
    return QLatin1String("viewAnchor");
}
inline QLatin1String kUnclaimedSessions()
{
    return QLatin1String("unclaimedSessions");
}
/// Logins an entry may be staged without a single tile being claimed before it
/// is dropped. Small on purpose: the entry only survives at all because its app
/// might come back, and three cold starts is generous evidence that it will not.
constexpr int kMaxUnclaimedSessions = 3;
inline QLatin1String kTiles()
{
    return QLatin1String("tiles");
}
inline QLatin1String kDisplay()
{
    return QLatin1String("display");
}
inline QLatin1String kWindowId()
{
    return QLatin1String("windowId");
}
inline QLatin1String kMinimized()
{
    return QLatin1String("minimized");
}
inline QLatin1String kKind()
{
    return QLatin1String("kind");
}
inline QLatin1String kProportion()
{
    return QLatin1String("proportion");
}
inline QLatin1String kWeight()
{
    return QLatin1String("weight");
}
inline QLatin1String kFixedPx()
{
    return QLatin1String("fixedPx");
}
inline QLatin1String kPresetIdx()
{
    return QLatin1String("presetIdx");
}
inline QLatin1String kWidth()
{
    return QLatin1String("width");
}
inline QLatin1String kHeight()
{
    return QLatin1String("height");
}

QJsonObject widthToJson(const ColumnWidth& w)
{
    QJsonObject obj;
    obj.insert(kKind(), static_cast<int>(w.kind));
    obj.insert(kProportion(), w.proportion);
    obj.insert(kFixedPx(), w.fixedPx);
    obj.insert(kPresetIdx(), w.presetIdx);
    return obj;
}

ColumnWidth widthFromJson(const QJsonObject& obj)
{
    // Persisted config is user-writable, so this is a system boundary: every
    // numeric field is bounded to the range its producers already enforce
    // (refreshConfigFromSettings, the open-rule override and
    // effectiveDefaultColumnWidth all clamp proportion to [0.05, 1.0]).
    // Unbounded, a hand-edited or corrupt value reaches proportionalPx, where
    // qRound() of a huge double to int is undefined, and a zero or negative
    // proportion pins the column at 1px for the rest of the session.
    ColumnWidth w;
    const int kind = obj.value(kKind()).toInt(static_cast<int>(ColumnWidth::Proportion));
    w.kind = (kind == ColumnWidth::Fixed || kind == ColumnWidth::Preset) ? static_cast<ColumnWidth::Kind>(kind)
                                                                         : ColumnWidth::Proportion;
    w.proportion = qBound<qreal>(0.05, obj.value(kProportion()).toDouble(0.5), 1.0);
    w.fixedPx = qMax(0, obj.value(kFixedPx()).toInt(0));
    w.presetIdx = qMax(0, obj.value(kPresetIdx()).toInt(0));
    return w;
}

QJsonObject heightToJson(const WindowHeight& h)
{
    QJsonObject obj;
    obj.insert(kKind(), static_cast<int>(h.kind));
    obj.insert(kWeight(), h.weight);
    obj.insert(kFixedPx(), h.fixedPx);
    obj.insert(kPresetIdx(), h.presetIdx);
    return obj;
}

WindowHeight heightFromJson(const QJsonObject& obj)
{
    WindowHeight h;
    const int kind = obj.value(kKind()).toInt(static_cast<int>(WindowHeight::Auto));
    h.kind = (kind == WindowHeight::Fixed || kind == WindowHeight::Preset) ? static_cast<WindowHeight::Kind>(kind)
                                                                           : WindowHeight::Auto;
    // Same boundary hardening as widthFromJson: a non-positive weight would
    // divide the auto-height share by zero-or-negative, and a negative
    // fixedPx/presetIdx indexes out of range.
    h.weight = qBound<qreal>(0.01, obj.value(kWeight()).toDouble(1.0), 100.0);
    h.fixedPx = qMax(0, obj.value(kFixedPx()).toInt(0));
    h.presetIdx = qMax(0, obj.value(kPresetIdx()).toInt(0));
    return h;
}

QString keyToString(const PhosphorEngine::PlacementStateKey& key)
{
    return key.screenId + QLatin1Char('|') + QString::number(key.desktop) + QLatin1Char('|') + key.activity;
}

// Right-anchored parse: activity is the last '|' segment, desktop the one
// before it, the rest is the screen id (screen ids never contain '|', but
// anchoring from the right keeps this true even if one ever did).
bool keyFromString(const QString& s, PhosphorEngine::PlacementStateKey* out)
{
    const int actSep = s.lastIndexOf(QLatin1Char('|'));
    if (actSep < 0) {
        return false;
    }
    const int deskSep = s.lastIndexOf(QLatin1Char('|'), actSep - 1);
    if (deskSep < 0) {
        return false;
    }
    bool ok = false;
    const int desktop = s.mid(deskSep + 1, actSep - deskSep - 1).toInt(&ok);
    if (!ok || s.left(deskSep).isEmpty()) {
        return false;
    }
    out->screenId = s.left(deskSep);
    out->desktop = desktop;
    out->activity = s.mid(actSep + 1);
    return true;
}

} // namespace

QJsonObject ScrollEngine::serializeStripState() const
{
    QJsonObject out;
    const auto stashToJson = [](const StashedStrip& stash) {
        QJsonArray columns;
        for (const StashedColumn& col : stash.columns) {
            QJsonArray tiles;
            for (const StashedTile& tile : col.tiles) {
                QJsonObject t;
                t.insert(kWindowId(), tile.windowId);
                t.insert(kHeight(), heightToJson(tile.height));
                t.insert(kMinimized(), tile.minimized);
                // PER-TILE lease. A tile still flagged staged-from-persistence
                // has not been claimed this session, so it ages by one; a
                // claimed tile (flag cleared, count zeroed) and a fresh
                // mode-exit stash both write 0. Per tile so a returning
                // co-tenant app cannot keep a dead sibling's tile alive.
                t.insert(kUnclaimedSessions(), tile.stagedFromPersistence ? tile.unclaimedSessions + 1 : 0);
                tiles.append(t);
            }
            QJsonObject c;
            c.insert(kTiles(), tiles);
            c.insert(kWidth(), widthToJson(col.width));
            c.insert(kDisplay(), static_cast<int>(col.display));
            columns.append(c);
        }
        QJsonObject obj;
        obj.insert(kColumns(), columns);
        obj.insert(kFocused(), stash.focusedWindowId);
        obj.insert(kViewAnchor(), stash.viewAnchor);
        return obj;
    };
    // Every window id that is LIVE on some strip right now. Writing the stash
    // first and the live strips second makes a live strip win its own KEY, but
    // a window that migrated screen or desktop after a mode exit is duplicated
    // across two DIFFERENT keys, which is not a collision and so is not
    // resolved by write order at all. restoreStripState's cross-key dedup then
    // keeps whichever key it meets first — alphabetical by "screen|desktop|
    // activity", not newest — so a stale stash tile could displace the
    // authoritative live one and restore the window to its old screen's slot.
    // Dropping the stale copy at WRITE time removes the ambiguity entirely.
    const auto& states = m_states.states();
    QSet<QString> liveWindowIds;
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        const StashedStrip live = buildStashFromState(it.value());
        for (const StashedColumn& col : live.columns) {
            for (const StashedTile& tile : col.tiles) {
                liveWindowIds.insert(tile.windowId);
            }
        }
    }
    // Stash entries first (mode-round-trip structure not yet re-adopted),
    // live strips second so a live strip also wins its own key.
    for (auto it = m_stripStash.cbegin(); it != m_stripStash.cend(); ++it) {
        StashedStrip pruned = it.value();
        for (auto colIt = pruned.columns.begin(); colIt != pruned.columns.end();) {
            colIt->tiles.removeIf([&liveWindowIds](const StashedTile& tile) {
                return liveWindowIds.contains(tile.windowId);
            });
            colIt = colIt->tiles.isEmpty() ? pruned.columns.erase(colIt) : std::next(colIt);
        }
        if (!pruned.isEmpty()) {
            out.insert(keyToString(it.key()), stashToJson(pruned));
        }
    }
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        const StashedStrip live = buildStashFromState(it.value());
        if (!live.isEmpty()) {
            out.insert(keyToString(it.key()), stashToJson(live));
        }
    }
    return out;
}

void ScrollEngine::restoreStripState(const QJsonObject& state)
{
    int restored = 0;
    // A window id may appear in exactly ONE staged tile, across every key.
    // restoreFromStripStash completes an entry when the count of distinct
    // consumed ids reaches its tile count, so a repeat inside one key makes
    // that entry permanently unconsumable (it is then re-consulted on every
    // later open of the context). A repeat ACROSS keys is not hypothetical —
    // serializeStripState legitimately writes a live strip for one key beside
    // an un-consumed stash for another that still lists the same window — and
    // would let two contexts stage it, so whichever announces second splices
    // it into a second strip.
    QSet<QString> claimedWindowIds;
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        PhosphorEngine::PlacementStateKey key;
        if (!keyFromString(it.key(), &key) || !it.value().isObject()) {
            continue;
        }
        // Additive and conservative: an existing stash entry is newer
        // in-session state, and a live populated strip means the context
        // already adopted its windows — re-staging stale structure over
        // either would re-position later unrelated opens.
        if (m_stripStash.contains(key)) {
            continue;
        }
        if (const ScrollState* live = m_states.stateForKey(key); live && live->windowCount() > 0) {
            continue;
        }
        const QJsonObject obj = it.value().toObject();
        StashedStrip stash;
        stash.focusedWindowId = obj.value(kFocused()).toString();
        stash.viewAnchor = obj.value(kViewAnchor()).toInt(0);
        const QJsonArray columns = obj.value(kColumns()).toArray();
        for (const QJsonValue& colVal : columns) {
            if (!colVal.isObject()) {
                continue;
            }
            const QJsonObject colObj = colVal.toObject();
            StashedColumn col;
            col.width = widthFromJson(colObj.value(kWidth()).toObject());
            col.display = colObj.value(kDisplay()).toInt(0) == static_cast<int>(ColumnDisplay::Tabbed)
                ? ColumnDisplay::Tabbed
                : ColumnDisplay::Normal;
            const QJsonArray tiles = colObj.value(kTiles()).toArray();
            for (const QJsonValue& tileVal : tiles) {
                if (!tileVal.isObject()) {
                    continue;
                }
                const QJsonObject tileObj = tileVal.toObject();
                StashedTile tile;
                tile.windowId = tileObj.value(kWindowId()).toString();
                tile.height = heightFromJson(tileObj.value(kHeight()).toObject());
                tile.minimized = tileObj.value(kMinimized()).toBool(false);
                // PER-TILE lease, bounded at the system boundary like every
                // other numeric in this file (persisted config is
                // user-writable). Absent key reads 0, so an older blob gets a
                // full fresh lease. A tile at the cap has gone that many
                // logins without any claim; its app is not coming back, and
                // the aliveness sweep can never reach it (pruneStaleWindows
                // fires once per session, at bring-up, while the entry is
                // still sweep-exempt), so dropping it here is what stops it
                // living forever and eventually handing an unrelated same-app
                // window a long-dead slot. Per tile so a returning co-tenant
                // in the same key cannot renew a dead sibling's lease.
                tile.unclaimedSessions = qBound(0, tileObj.value(kUnclaimedSessions()).toInt(0), kMaxUnclaimedSessions);
                if (tile.unclaimedSessions >= kMaxUnclaimedSessions) {
                    qCInfo(lcScrollEngine) << "restoreStripState: dropping stashed tile" << tile.windowId << "for"
                                           << it.key() << "after" << tile.unclaimedSessions << "sessions with no claim";
                    continue;
                }
                tile.stagedFromPersistence = true;
                if (!tile.windowId.isEmpty() && !claimedWindowIds.contains(tile.windowId)) {
                    claimedWindowIds.insert(tile.windowId);
                    col.tiles.append(tile);
                }
            }
            if (!col.tiles.isEmpty()) {
                stash.columns.append(col);
            }
        }
        if (stash.isEmpty()) {
            continue;
        }
        // The focus is read from JSON before the tiles are filtered, and the
        // cross-key duplicate claim above can drop the very tile it names. A
        // stash whose focus belongs to no surviving tile never hands the focus
        // over on restore, so the round trip silently re-anchors on whichever
        // window arrives first — the regression the focus carry exists to fix.
        // Fall back to the first surviving tile rather than leaving it dangling.
        if (!stash.focusedWindowId.isEmpty()) {
            bool focusSurvives = false;
            for (const StashedColumn& col : std::as_const(stash.columns)) {
                for (const StashedTile& tile : col.tiles) {
                    if (tile.windowId == stash.focusedWindowId) {
                        focusSurvives = true;
                        break;
                    }
                }
                if (focusSurvives) {
                    break;
                }
            }
            if (!focusSurvives) {
                // Every surviving column has at least one tile (empty ones are
                // dropped above), and stash.isEmpty() was just checked.
                stash.focusedWindowId = stash.columns.first().tiles.first().windowId;
            }
        }
        // Mark the entry as staged-from-persistence so pruneStaleWindows'
        // aliveness sweep leaves it alone until a tile is actually claimed.
        // These ids are last session's; no live alive-set can contain them.
        stash.stagedFromPersistence = true;
        m_stripStash.insert(key, stash);
        m_stripStashConsumed.remove(key);
        ++restored;
    }
    if (restored > 0) {
        qCInfo(lcScrollEngine) << "restoreStripState: staged" << restored << "strip snapshot(s) for arrival restore";
    }
}

} // namespace PhosphorScrollEngine
