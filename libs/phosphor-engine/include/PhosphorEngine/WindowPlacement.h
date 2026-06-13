// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PhosphorEngine {

/// One engine's view of a window: which managed slot it occupies (or that it is
/// floating / unmanaged) in THAT engine's mode. State is PER ENGINE — a window
/// can be `snapped` in the snap engine AND `floating` in the autotile engine at
/// the same time, each engine remembering the window's state in its OWN mode.
///
/// The slot is an IDENTIFIER, never a rectangle: `zoneIds` for the snap engine,
/// `order` for the autotile engine. A managed rect (the zone or tile geometry) is
/// NEVER stored here — that is what keeps the shared free/float geometry from
/// being poisoned by a zone/tile rect (the per-mode geometry leak this model
/// fixes). The window's actual free/float position lives once, shared, in
/// WindowPlacement::freeGeometryByScreen.
struct EngineSlot
{
    QString state; ///< engine-defined token: snap "snapped"/"floating"; autotile "tiled"/"floating" ("free" retired)
    QStringList zoneIds; ///< snap slot — zone UUIDs (first is primary); empty for autotile
    int order = -1; ///< autotile slot — tile index within the screen; -1 for snap

    bool operator==(const EngineSlot& o) const
    {
        return state == o.state && zoneIds == o.zoneIds && order == o.order;
    }
    bool operator!=(const EngineSlot& o) const
    {
        return !(*this == o);
    }
    bool isEmpty() const
    {
        return state.isEmpty();
    }
};

/// One window's single, authoritative placement record — the unit of the unified,
/// engine-agnostic restore model. The WindowPlacementStore holds at most ONE
/// record per window (NOT one per engine). The record carries:
///
///  - per-engine STATE in `engines` (keyed by IPlacementEngine::engineId()): each
///    engine's own slot + state, independent of the others (per-mode float
///    independence — floating in snap does not float in autotile);
///  - ONE SHARED free/float GEOMETRY in `freeGeometryByScreen`: the position the
///    window occupies when it is NOT engine-managed, shared across snap/autotile
///    but keyed PER SCREEN so a window remembers a distinct free spot per monitor.
///    Written ONLY from a genuine free/floating frame; FROZEN while the window is
///    snapped or tiled. A zone/tile rect never enters this map.
///
/// Extensibility: a new engine needs no change here — it keys its own slot under
/// its engineId() and reads/writes the shared geometry. Core code never
/// interprets an EngineSlot::state beyond equality.
struct WindowPlacement
{
    // ── Identity (the two store keys) ──
    QString windowId; ///< full `appId|uuid`; exact key for daemon-restart (uuid stable)
    QString appId; ///< window class; FIFO key for close/reopen (uuid changes)

    // ── Context (the universal restore + disabled-context gate) ──
    QString screenId; ///< last managed-context screen
    int virtualDesktop = 0; ///< 0 = all-desktops / sticky / unknown
    QString activity; ///< empty = all-activities / unknown
    WindowKind kind = WindowKind::Unknown;

    // ── Per-engine managed state, keyed by engineId() ──
    QHash<QString, EngineSlot> engines;

    // ── Shared free/float geometry, keyed by screenId ──
    QHash<QString, QRect> freeGeometryByScreen;

    // ── Recency (most-recent-wins ordering; stamped by the store) ──
    quint64 sequence = 0;

    bool isValid() const
    {
        return !windowId.isEmpty() && !appId.isEmpty();
    }

    /// The slot for @p engineId, or a default (empty) slot if this engine has no
    /// recorded state for the window.
    EngineSlot slotFor(const QString& engineId) const
    {
        return engines.value(engineId);
    }

    /// The shared free/float geometry for @p screenId, or an invalid rect if none
    /// has been captured on that screen yet.
    QRect freeGeometryFor(const QString& screenId) const
    {
        return freeGeometryByScreen.value(screenId);
    }

    /// Any captured free/float geometry (the first valid rect in unspecified hash
    /// order — there is no per-screen recency to pick a "newest"), used as a
    /// cross-screen fallback when the exact screen has none. Returns an invalid
    /// rect when the window has no free geometry on record at all.
    QRect anyFreeGeometry() const
    {
        for (auto it = freeGeometryByScreen.constBegin(); it != freeGeometryByScreen.constEnd(); ++it) {
            if (it.value().isValid()) {
                return it.value();
            }
        }
        return QRect();
    }

    /// Whether this record carries anything worth restoring. True when the window
    /// has a captured free/float geometry, OR a MANAGED slot (`snapped`/`tiled`,
    /// which restore by zone/tile reference), OR a slot that still references a zone
    /// (`zoneIds` — a floated-from-snap window's pre-float zones) or a tile order.
    ///
    /// A bare slot with no geometry, no zone reference, and no tile order — whether
    /// `floating` (a never-snapped floated window captured frame-less) or the retired
    /// `free` token — has NOTHING to restore. Snapping now defaults every unmanaged
    /// window to `floating`, so this geometry-less floated residue is exactly the
    /// case that must be rejected: it must NOT be persisted, and the snap restore's
    /// accept predicate must never CONSUME it from the per-app FIFO. At MaxPerApp
    /// entries per app, such residue would otherwise starve and even evict
    /// (removeFirst) the window's real placement, silently breaking geometry restore.
    bool hasRestorableContent() const
    {
        if (anyFreeGeometry().isValid()) {
            return true;
        }
        for (auto it = engines.constBegin(); it != engines.constEnd(); ++it) {
            const EngineSlot& s = it.value();
            if (s.state == stateSnapped() || s.state == stateTiled()) {
                return true;
            }
            if (!s.zoneIds.isEmpty() || s.order >= 0) {
                return true;
            }
        }
        return false;
    }

    /// Content equality IGNORING `sequence` (the store stamps a fresh sequence on
    /// every record(), so it must not count as a change). Lets the merge in
    /// record() short-circuit a re-capture that produced an identical placement —
    /// the save-time refreshOpenWindowPlacements() re-captures every open window
    /// each tick, and without this an unchanged window would re-mark dirty and
    /// reschedule the save timer forever (a self-perpetuating save/capture loop).
    bool sameContentAs(const WindowPlacement& o) const
    {
        return windowId == o.windowId && appId == o.appId && screenId == o.screenId
            && virtualDesktop == o.virtualDesktop && activity == o.activity && kind == o.kind && engines == o.engines
            && freeGeometryByScreen == o.freeGeometryByScreen;
    }

    /// Built-in engine-slot ids. Cross-engine readers (resnap, teardown
    /// capture, autotile's snap-defer gate) key slotFor() on these instead
    /// of spelling the id inline — same drift-prevention rationale as the
    /// state-token accessors below. Each engine's own engineId() override
    /// returns the same spelling.
    static QLatin1String snapEngineId()
    {
        return QLatin1String("snap");
    }
    static QLatin1String autotileEngineId()
    {
        return QLatin1String("autotile");
    }

    /// Common state-token vocabulary. Engines may define more; these cover the
    /// built-in snap/autotile states.
    /// Retired snap state — snapping is now two-state (snapped/floating). No engine
    /// produces it any more; a legacy `free` record persisted by an older build
    /// deserializes to this token and is restored as floating (see
    /// SnapEngine::resolveWindowRestore). Retained only for that legacy-record
    /// mapping and the tests that exercise it.
    static QLatin1String stateFree()
    {
        return QLatin1String("free");
    }
    static QLatin1String stateFloating()
    {
        return QLatin1String("floating");
    }
    static QLatin1String stateSnapped()
    {
        return QLatin1String("snapped");
    }
    static QLatin1String stateTiled()
    {
        return QLatin1String("tiled");
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = windowId;
        obj[QLatin1String("screen")] = screenId;
        obj[QLatin1String("desktop")] = virtualDesktop;
        if (!activity.isEmpty()) {
            obj[QLatin1String("activity")] = activity;
        }
        obj[QLatin1String("kind")] = static_cast<int>(kind);

        QJsonObject eng;
        for (auto it = engines.constBegin(); it != engines.constEnd(); ++it) {
            const EngineSlot& s = it.value();
            if (s.isEmpty()) {
                continue;
            }
            QJsonObject so;
            so[QLatin1String("state")] = s.state;
            if (!s.zoneIds.isEmpty()) {
                QJsonArray z;
                for (const QString& id : s.zoneIds) {
                    z.append(id);
                }
                so[QLatin1String("zoneIds")] = z;
            }
            if (s.order >= 0) {
                so[QLatin1String("order")] = s.order;
            }
            eng[it.key()] = so;
        }
        if (!eng.isEmpty()) {
            obj[QLatin1String("engines")] = eng;
        }

        QJsonObject fg;
        for (auto it = freeGeometryByScreen.constBegin(); it != freeGeometryByScreen.constEnd(); ++it) {
            const QRect& g = it.value();
            if (!g.isValid()) {
                continue;
            }
            QJsonObject go;
            go[QLatin1String("x")] = g.x();
            go[QLatin1String("y")] = g.y();
            go[QLatin1String("w")] = g.width();
            go[QLatin1String("h")] = g.height();
            fg[it.key()] = go;
        }
        if (!fg.isEmpty()) {
            obj[QLatin1String("freeGeo")] = fg;
        }

        obj[QLatin1String("seq")] = static_cast<double>(sequence);
        return obj;
    }

    static WindowPlacement fromJson(const QString& appId, const QJsonObject& obj)
    {
        WindowPlacement p;
        p.appId = appId;
        p.windowId = obj.value(QLatin1String("windowId")).toString();
        p.screenId = obj.value(QLatin1String("screen")).toString();
        p.virtualDesktop = obj.value(QLatin1String("desktop")).toInt();
        p.activity = obj.value(QLatin1String("activity")).toString();
        p.kind = clampWindowKindFromWire(obj.value(QLatin1String("kind")).toInt());

        const QJsonObject eng = obj.value(QLatin1String("engines")).toObject();
        for (auto it = eng.constBegin(); it != eng.constEnd(); ++it) {
            const QJsonObject so = it.value().toObject();
            EngineSlot s;
            s.state = so.value(QLatin1String("state")).toString();
            for (const QJsonValue& v : so.value(QLatin1String("zoneIds")).toArray()) {
                const QString z = v.toString();
                if (!z.isEmpty()) {
                    s.zoneIds.append(z);
                }
            }
            s.order = so.value(QLatin1String("order")).toInt(-1);
            if (!s.isEmpty()) {
                p.engines.insert(it.key(), s);
            }
        }

        const QJsonObject fg = obj.value(QLatin1String("freeGeo")).toObject();
        for (auto it = fg.constBegin(); it != fg.constEnd(); ++it) {
            const QJsonObject go = it.value().toObject();
            const QRect g(go.value(QLatin1String("x")).toInt(), go.value(QLatin1String("y")).toInt(),
                          go.value(QLatin1String("w")).toInt(), go.value(QLatin1String("h")).toInt());
            if (g.isValid()) {
                p.freeGeometryByScreen.insert(it.key(), g);
            }
        }

        p.sequence = static_cast<quint64>(obj.value(QLatin1String("seq")).toDouble());
        return p;
    }
};

} // namespace PhosphorEngine
