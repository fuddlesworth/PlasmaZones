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
    ColumnWidth w;
    const int kind = obj.value(kKind()).toInt(static_cast<int>(ColumnWidth::Proportion));
    w.kind = (kind == ColumnWidth::Fixed || kind == ColumnWidth::Preset) ? static_cast<ColumnWidth::Kind>(kind)
                                                                         : ColumnWidth::Proportion;
    w.proportion = obj.value(kProportion()).toDouble(0.5);
    w.fixedPx = obj.value(kFixedPx()).toInt(0);
    w.presetIdx = obj.value(kPresetIdx()).toInt(0);
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
    h.weight = obj.value(kWeight()).toDouble(1.0);
    h.fixedPx = obj.value(kFixedPx()).toInt(0);
    h.presetIdx = obj.value(kPresetIdx()).toInt(0);
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
    // Stash entries first (mode-round-trip structure not yet re-adopted),
    // live strips second so a live strip wins its key — it is strictly
    // newer than any stash the same context might still hold.
    for (auto it = m_stripStash.cbegin(); it != m_stripStash.cend(); ++it) {
        if (!it.value().isEmpty()) {
            out.insert(keyToString(it.key()), stashToJson(it.value()));
        }
    }
    const auto& states = m_states.states();
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
                if (!tile.windowId.isEmpty()) {
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
        m_stripStash.insert(key, stash);
        m_stripStashConsumed.remove(key);
        ++restored;
    }
    if (restored > 0) {
        qCInfo(lcScrollEngine) << "restoreStripState: staged" << restored << "strip snapshot(s) for arrival restore";
    }
}

} // namespace PhosphorScrollEngine
