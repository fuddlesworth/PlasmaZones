// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineApi/IPlacementEngine.h>
#include <phosphorengineapi_export.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>

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

protected:
    explicit PlacementEngineBase(QObject* parent = nullptr);
    ~PlacementEngineBase() override;

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

private:
    QHash<QString, UnmanagedEntry> m_unmanagedGeometries;
    QHash<QString, WindowState> m_windowStates;
};

} // namespace PhosphorEngineApi
