// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineApi/IPlacementState.h>
#include <phosphorzones_export.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace PhosphorZones {

/// Per-screen snap placement state.
///
/// Owns the mutable state for manual zone-based snapping: which window
/// is assigned to which zone, floating state, pre-tile geometry for
/// restore, and pre-float zone memory for unfloat. Analogous to
/// PhosphorTiles::TilingState for automatic tiling.
///
/// Both SnapState and TilingState implement PhosphorEngineApi::IPlacementState
/// so the daemon's persistence layer and D-Bus adaptor can serialize state
/// uniformly without branching on mode.
class PHOSPHORZONES_EXPORT SnapState : public QObject, public PhosphorEngineApi::IPlacementState
{
    Q_OBJECT

public:
    explicit SnapState(const QString& screenId, QObject* parent = nullptr);
    ~SnapState() override;

    SnapState(const SnapState&) = delete;
    SnapState& operator=(const SnapState&) = delete;

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementState
    // ═══════════════════════════════════════════════════════════════════════════

    QString screenId() const override;
    int windowCount() const override;
    QStringList managedWindows() const override;
    bool containsWindow(const QString& windowId) const override;
    bool isFloating(const QString& windowId) const override;
    QStringList floatingWindows() const override;
    QString placementIdForWindow(const QString& windowId) const override;
    QJsonObject toJson() const override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Assignment CRUD
    // ═══════════════════════════════════════════════════════════════════════════

    void assignWindowToZone(const QString& windowId, const QString& zoneId);
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds);
    void unassignWindow(const QString& windowId);

    QString zoneForWindow(const QString& windowId) const;
    QStringList zonesForWindow(const QString& windowId) const;
    QStringList windowsInZone(const QString& zoneId) const;
    QStringList snappedWindows() const;
    bool isWindowSnapped(const QString& windowId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating State
    // ═══════════════════════════════════════════════════════════════════════════

    void setFloating(const QString& windowId, bool floating);

    /// Save zone assignment before floating for later restore.
    void unsnapForFloat(const QString& windowId);
    QString preFloatZone(const QString& windowId) const;
    QStringList preFloatZones(const QString& windowId) const;
    void clearPreFloatZone(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Pre-Tile Geometry
    // ═══════════════════════════════════════════════════════════════════════════

    struct PreTileGeometry
    {
        QRect geometry;
        QString connectorName;
    };

    void storePreTileGeometry(const QString& windowId, const QRect& geometry, const QString& connectorName = {},
                              bool overwrite = false);
    std::optional<QRect> preTileGeometry(const QString& windowId) const;
    bool hasPreTileGeometry(const QString& windowId) const;
    void clearPreTileGeometry(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    void windowClosed(const QString& windowId);
    void clear();

    // ═══════════════════════════════════════════════════════════════════════════
    // Rotation
    // ═══════════════════════════════════════════════════════════════════════════

    /// Rotate zone assignments: each window moves to the next/previous zone
    /// in zone-number order. Returns the list of window IDs affected.
    QStringList rotateAssignments(bool clockwise);

    // ═══════════════════════════════════════════════════════════════════════════
    // Deserialization
    // ═══════════════════════════════════════════════════════════════════════════

    static SnapState* fromJson(const QJsonObject& json, QObject* parent = nullptr);

    // ═══════════════════════════════════════════════════════════════════════════
    // State Access (for persistence layer)
    // ═══════════════════════════════════════════════════════════════════════════

    const QHash<QString, QStringList>& zoneAssignments() const
    {
        return m_windowZoneAssignments;
    }
    const QHash<QString, PreTileGeometry>& preTileGeometries() const
    {
        return m_preTileGeometries;
    }
    const QHash<QString, QStringList>& preFloatZoneAssignments() const
    {
        return m_preFloatZoneAssignments;
    }

    void setZoneAssignments(const QHash<QString, QStringList>& zones)
    {
        m_windowZoneAssignments = zones;
    }
    void setPreTileGeometries(const QHash<QString, PreTileGeometry>& geos)
    {
        m_preTileGeometries = geos;
    }
    void setFloatingWindows(const QSet<QString>& windows)
    {
        m_floatingWindows = windows;
    }
    void setPreFloatZoneAssignments(const QHash<QString, QStringList>& a)
    {
        m_preFloatZoneAssignments = a;
    }

Q_SIGNALS:
    void windowAssigned(const QString& windowId, const QString& zoneId);
    void windowUnassigned(const QString& windowId);
    void floatingChanged(const QString& windowId, bool floating);
    void stateChanged();

private:
    QSet<QString> allManagedWindowIds() const
    {
        QSet<QString> all;
        all.reserve(m_windowZoneAssignments.size() + m_floatingWindows.size());
        for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
            all.insert(it.key());
        }
        all.unite(m_floatingWindows);
        return all;
    }

    QString m_screenId;

    QHash<QString, QStringList> m_windowZoneAssignments;
    QSet<QString> m_floatingWindows;
    QHash<QString, PreTileGeometry> m_preTileGeometries;
    QHash<QString, QStringList> m_preFloatZoneAssignments;
};

} // namespace PhosphorZones
