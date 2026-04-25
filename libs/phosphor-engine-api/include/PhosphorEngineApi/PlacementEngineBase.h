// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineApi/IPlacementEngine.h>
#include <phosphorengineapi_export.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVariantMap>

namespace PhosphorEngineApi {

enum class WindowState {
    Unmanaged,
    EngineOwned,
    Floated
};

/// Abstract base class for placement engines.
///
/// Handles universal mechanics that every engine needs identically:
/// window state FSM (unmanaged / engine-owned / floated), unmanaged
/// geometry capture/restore, and float geometry bookkeeping.
///
/// Engines subclass this and implement only the placement-specific
/// hooks: where to put a window, how to remember placement for
/// unfloat, etc. The base class ensures consistent state transitions
/// regardless of engine type.
class PHOSPHORENGINEAPI_EXPORT PlacementEngineBase : public QObject, public IPlacementEngine
{
    Q_OBJECT

public:
    struct UnmanagedEntry
    {
        QRect geometry;
        QString screenId;
    };

    WindowState windowState(const QString& windowId) const;

    void claimWindow(const QString& windowId, const QRect& geometry, const QString& screenId, bool overwrite = false);
    void releaseWindow(const QString& windowId);
    void floatWindow(const QString& windowId);
    void unfloatWindow(const QString& windowId);

    QRect unmanagedGeometry(const QString& windowId) const;
    QString unmanagedScreen(const QString& windowId) const;
    bool hasUnmanagedGeometry(const QString& windowId) const;
    void clearUnmanagedGeometry(const QString& windowId);
    void forgetWindow(const QString& windowId);
    void storeUnmanagedGeometry(const QString& windowId, const QRect& geometry, const QString& screenId,
                                bool overwrite = false);

    const QHash<QString, UnmanagedEntry>& unmanagedGeometries() const
    {
        return m_unmanagedGeometries;
    }
    void setUnmanagedGeometries(const QHash<QString, UnmanagedEntry>& geos);
    virtual int pruneStaleWindows(const QSet<QString>& aliveWindowIds);

    QJsonObject serializeBaseState() const;
    void deserializeBaseState(const QJsonObject& state);

    // ═══════════════════════════════════════════════════════════════════════════
    // Settings — universal pattern for all engines
    //
    // The daemon calls setEngineSettings() once at startup with a QObject*
    // that implements the engine's specific settings interface (e.g.
    // IAutotileSettings, ISnapSettings). Engines qobject_cast at point of
    // use to their interface type. No caching, no bridge, no signal wiring
    // inside the engine — the daemon handles change signals externally.
    // ═══════════════════════════════════════════════════════════════════════════

    void setEngineSettings(QObject* settings);
    QObject* engineSettings() const
    {
        return m_engineSettings;
    }

    // Public dtor required for unique_ptr<PlacementEngineBase> in Daemon.
    ~PlacementEngineBase() override;

protected:
    explicit PlacementEngineBase(QObject* parent = nullptr);

    virtual void onWindowClaimed(const QString& windowId) = 0;
    virtual void onWindowReleased(const QString& windowId) = 0;
    virtual void onWindowFloated(const QString& windowId) = 0;
    virtual void onWindowUnfloated(const QString& windowId) = 0;

Q_SIGNALS:
    void geometryRestoreRequested(const QString& windowId, const QRect& geometry, const QString& screenId);
    void windowStateTransitioned(const QString& windowId, WindowState oldState, WindowState newState);

    void navigationFeedback(bool success, const QString& action, const QString& reason, const QString& sourceId,
                            const QString& targetId, const QString& screenId);
    void windowFloatingChanged(const QString& windowId, bool floating, const QString& screenId);
    void activateWindowRequested(const QString& windowId);

    /// Emitted to sync floating state without restoring geometry.
    /// Passive state-sync: engine-internal divergence correction.
    void windowFloatingStateSynced(const QString& windowId, bool floating, const QString& screenId);

    /// Emitted when overflow windows are batch-floated during applyTiling.
    void windowsBatchFloated(const QStringList& windowIds, const QString& screenId);

    /// Emitted when the active tiling algorithm changes.
    void algorithmChanged(const QString& algorithmId);

    /// Emitted when the placement layout changes for a screen.
    void placementChanged(const QString& screenId);

    /// Emitted when windows are released from engine management.
    void windowsReleased(const QStringList& windowIds, const QSet<QString>& releasedScreenIds);

    /// Emitted when the engine wants to persist changed settings values.
    /// The QVariantMap contains key-value pairs the daemon should write
    /// to its settings store (e.g. {"autotileSplitRatio": 0.6}).
    void settingsWriteBackRequested(const QVariantMap& values);

private:
    QHash<QString, UnmanagedEntry> m_unmanagedGeometries;
    QHash<QString, WindowState> m_windowStates;
    QPointer<QObject> m_engineSettings;
};

} // namespace PhosphorEngineApi
