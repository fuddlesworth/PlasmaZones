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

    void assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                            int virtualDesktop);
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                             int virtualDesktop);
    void unassignWindow(const QString& windowId);

    QString zoneForWindow(const QString& windowId) const;
    QStringList zonesForWindow(const QString& windowId) const;
    QStringList windowsInZone(const QString& zoneId) const;
    QStringList snappedWindows() const;
    bool isWindowSnapped(const QString& windowId) const;

    QString screenForWindow(const QString& windowId) const;
    int desktopForWindow(const QString& windowId) const;

    const QHash<QString, QString>& screenAssignments() const
    {
        return m_windowScreenAssignments;
    }
    const QHash<QString, int>& desktopAssignments() const
    {
        return m_windowDesktopAssignments;
    }
    void setScreenAssignments(const QHash<QString, QString>& s)
    {
        if (m_windowScreenAssignments == s) {
            return;
        }
        m_windowScreenAssignments = s;
        Q_EMIT stateChanged();
    }
    void setDesktopAssignments(const QHash<QString, int>& d)
    {
        if (m_windowDesktopAssignments == d) {
            return;
        }
        m_windowDesktopAssignments = d;
        Q_EMIT stateChanged();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating State
    // ═══════════════════════════════════════════════════════════════════════════

    void setFloating(const QString& windowId, bool floating);

    /// Save zone assignment before floating for later restore.
    void unsnapForFloat(const QString& windowId);
    QString preFloatZone(const QString& windowId) const;
    QStringList preFloatZones(const QString& windowId) const;
    QString preFloatScreen(const QString& windowId) const;
    void clearPreFloatZone(const QString& windowId);

    const QHash<QString, QStringList>& preFloatZoneAssignments() const
    {
        return m_preFloatZoneAssignments;
    }
    const QHash<QString, QString>& preFloatScreenAssignments() const
    {
        return m_preFloatScreenAssignments;
    }
    void setPreFloatScreenAssignments(const QHash<QString, QString>& a)
    {
        if (m_preFloatScreenAssignments == a) {
            return;
        }
        m_preFloatScreenAssignments = a;
        Q_EMIT stateChanged();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Pre-Tile Geometry
    // ═══════════════════════════════════════════════════════════════════════════

    struct PreTileGeometry
    {
        QRect geometry;
        QString connectorName;

        bool operator==(const PreTileGeometry& other) const
        {
            return geometry == other.geometry && connectorName == other.connectorName;
        }
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
    bool isEmpty() const;
    void clear();

    // ═══════════════════════════════════════════════════════════════════════════
    // Rotation
    // ═══════════════════════════════════════════════════════════════════════════

    /// Rotate zone assignments: each window moves to the next/previous zone
    /// in zone-number order. Returns the list of window IDs affected.
    QStringList rotateAssignments(bool clockwise);

    // ═══════════════════════════════════════════════════════════════════════════
    // Last-Used Zone Tracking
    // ═══════════════════════════════════════════════════════════════════════════

    /// Update last-used zone and emit stateChanged.
    void updateLastUsedZone(const QString& zoneId, const QString& screenId, const QString& windowClass,
                            int virtualDesktop);

    /// Restore last-used zone fields from persistence without emitting stateChanged.
    void restoreLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass, int desktop);

    QString lastUsedZoneId() const
    {
        return m_lastUsedZoneId;
    }
    QString lastUsedScreenId() const
    {
        return m_lastUsedScreenId;
    }
    QString lastUsedZoneClass() const
    {
        return m_lastUsedZoneClass;
    }
    int lastUsedDesktop() const
    {
        return m_lastUsedDesktop;
    }
    void retagLastUsedZoneClass(const QString& newClass)
    {
        m_lastUsedZoneClass = newClass;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-Snap Bookkeeping
    // ═══════════════════════════════════════════════════════════════════════════

    void recordSnapIntent(const QString& windowClass, bool wasUserInitiated);
    const QSet<QString>& userSnappedClasses() const
    {
        return m_userSnappedClasses;
    }
    void setUserSnappedClasses(const QSet<QString>& classes)
    {
        if (m_userSnappedClasses == classes) {
            return;
        }
        m_userSnappedClasses = classes;
        Q_EMIT stateChanged();
    }

    void markAsAutoSnapped(const QString& windowId);
    bool isAutoSnapped(const QString& windowId) const;
    bool clearAutoSnapped(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Occupied Zone Queries
    // ═══════════════════════════════════════════════════════════════════════════

    /// Build the set of zone IDs currently occupied by snapped windows.
    /// Desktop 0 means "on all desktops" per KWin convention — windows with
    /// desktop 0 pass the filter and appear occupied on every desktop.
    QSet<QString> buildOccupiedZoneSet(const QString& screenFilter = {}, int desktopFilter = 0) const;

    /// Remove zone/screen/desktop assignments for windows not in the alive set.
    int pruneStaleAssignments(const QSet<QString>& aliveWindowIds);

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

    void setZoneAssignments(const QHash<QString, QStringList>& zones)
    {
        if (m_windowZoneAssignments == zones) {
            return;
        }
        m_windowZoneAssignments = zones;
        Q_EMIT stateChanged();
    }
    void setPreTileGeometries(const QHash<QString, PreTileGeometry>& geos)
    {
        if (m_preTileGeometries == geos) {
            return;
        }
        m_preTileGeometries = geos;
        Q_EMIT stateChanged();
    }
    void setFloatingWindows(const QSet<QString>& windows)
    {
        if (m_floatingWindows == windows) {
            return;
        }
        m_floatingWindows = windows;
        Q_EMIT stateChanged();
    }
    void setPreFloatZoneAssignments(const QHash<QString, QStringList>& a)
    {
        if (m_preFloatZoneAssignments == a) {
            return;
        }
        m_preFloatZoneAssignments = a;
        Q_EMIT stateChanged();
    }

Q_SIGNALS:
    void windowAssigned(const QString& windowId, const QString& zoneId);
    void windowUnassigned(const QString& windowId);
    void floatingChanged(const QString& windowId, bool floating);
    void stateChanged();

private:
    bool removeWindowData(const QString& windowId);

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
    QHash<QString, QString> m_windowScreenAssignments;
    QHash<QString, int> m_windowDesktopAssignments;
    QSet<QString> m_floatingWindows;
    QHash<QString, PreTileGeometry> m_preTileGeometries;
    QHash<QString, QStringList> m_preFloatZoneAssignments;
    QHash<QString, QString> m_preFloatScreenAssignments;

    QString m_lastUsedZoneId;
    QString m_lastUsedScreenId;
    QString m_lastUsedZoneClass;
    int m_lastUsedDesktop = 0;

    QSet<QString> m_userSnappedClasses;
    QSet<QString> m_autoSnappedWindows;
};

} // namespace PhosphorZones
