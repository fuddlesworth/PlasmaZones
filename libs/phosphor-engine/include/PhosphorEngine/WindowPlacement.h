// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QJsonObject>
#include <QLatin1String>
#include <QRect>
#include <QString>

namespace PhosphorEngine {

/// One window's single, authoritative placement state — the unit of the
/// unified, engine-agnostic restore model. A window is in exactly one state at
/// a time (free / floated / snapped / autotiled / ...); the WindowPlacementStore
/// holds at most one record per window, captured live by the owning engine and
/// re-applied on reopen / daemon restart.
///
/// Extensibility: a NEW engine or state needs no change here. `engineId` selects
/// the engine that restores the record; `stateId` is that engine's own token
/// (cross-engine policy gates read it as an opaque string); and `engineData` is
/// an opaque per-engine payload (zone ids for snap, tile position for autotile,
/// scroll column for a future scrolling engine). Core code never inspects
/// `engineData` or interprets `stateId` beyond equality.
struct WindowPlacement
{
    // ── Identity (the two store keys) ──
    QString windowId; ///< full `appId|uuid`; exact key for daemon-restart (uuid stable)
    QString appId; ///< window class; FIFO key for close/reopen (uuid changes)

    // ── State routing ──
    QString stateId; ///< engine-defined token, e.g. "free"/"floated"/"snapped"/"autotiled"
    QString engineId; ///< restore dispatch target — IPlacementEngine::engineId()

    // ── Context (the universal restore + disabled-context gate) ──
    QString screenId;
    int virtualDesktop = 0; ///< 0 = all-desktops / sticky / unknown
    QString activity; ///< empty = all-activities / unknown
    WindowKind kind = WindowKind::Unknown;

    // ── Geometry (floated/free frame; last-known for managed states) ──
    QRect geometry;

    // ── Recency (most-recent-wins ordering; stamped by the store) ──
    quint64 sequence = 0;

    // ── Opaque per-engine restore payload ──
    QJsonObject engineData;

    bool isValid() const
    {
        return !windowId.isEmpty() && !engineId.isEmpty() && !stateId.isEmpty();
    }

    /// Common state-token vocabulary. Engines may define more; these four cover
    /// the built-in snap/autotile states and keep the effect's apply switch
    /// state-agnostic.
    static QLatin1String stateFree()
    {
        return QLatin1String("free");
    }
    static QLatin1String stateFloated()
    {
        return QLatin1String("floated");
    }
    static QLatin1String stateSnapped()
    {
        return QLatin1String("snapped");
    }
    static QLatin1String stateAutotiled()
    {
        return QLatin1String("autotiled");
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = windowId;
        obj[QLatin1String("state")] = stateId;
        obj[QLatin1String("engine")] = engineId;
        obj[QLatin1String("screen")] = screenId;
        obj[QLatin1String("desktop")] = virtualDesktop;
        if (!activity.isEmpty()) {
            obj[QLatin1String("activity")] = activity;
        }
        obj[QLatin1String("kind")] = static_cast<int>(kind);
        if (geometry.isValid()) {
            QJsonObject g;
            g[QLatin1String("x")] = geometry.x();
            g[QLatin1String("y")] = geometry.y();
            g[QLatin1String("w")] = geometry.width();
            g[QLatin1String("h")] = geometry.height();
            obj[QLatin1String("geo")] = g;
        }
        obj[QLatin1String("seq")] = static_cast<double>(sequence);
        if (!engineData.isEmpty()) {
            obj[QLatin1String("data")] = engineData;
        }
        return obj;
    }

    static WindowPlacement fromJson(const QString& appId, const QJsonObject& obj)
    {
        WindowPlacement p;
        p.appId = appId;
        p.windowId = obj.value(QLatin1String("windowId")).toString();
        p.stateId = obj.value(QLatin1String("state")).toString();
        p.engineId = obj.value(QLatin1String("engine")).toString();
        p.screenId = obj.value(QLatin1String("screen")).toString();
        p.virtualDesktop = obj.value(QLatin1String("desktop")).toInt();
        p.activity = obj.value(QLatin1String("activity")).toString();
        p.kind = clampWindowKindFromWire(obj.value(QLatin1String("kind")).toInt());
        const QJsonObject g = obj.value(QLatin1String("geo")).toObject();
        if (!g.isEmpty()) {
            p.geometry = QRect(g.value(QLatin1String("x")).toInt(), g.value(QLatin1String("y")).toInt(),
                               g.value(QLatin1String("w")).toInt(), g.value(QLatin1String("h")).toInt());
        }
        p.sequence = static_cast<quint64>(obj.value(QLatin1String("seq")).toDouble());
        p.engineData = obj.value(QLatin1String("data")).toObject();
        return p;
    }
};

} // namespace PhosphorEngine
