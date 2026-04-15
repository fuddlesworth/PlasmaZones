// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QHash>
#include <QRect>
#include <QStringList>
#include <functional>

class QScreen;

namespace PlasmaZones {

class Settings;

/**
 * @brief D-Bus adaptor for screen management operations
 *
 * Provides D-Bus interface: org.plasmazones.Screen
 *  Screen information and monitoring
 *
 * NOTE: Interface name must match dbus/org.plasmazones.Screen.xml and
 * DBus::Interface::Screen constant for KCM signal connections to work.
 */
class PLASMAZONES_EXPORT ScreenAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Screen")

public:
    explicit ScreenAdaptor(QObject* parent = nullptr);
    ~ScreenAdaptor() override = default;

    /// Wire the authoritative Settings instance for VS config writes.
    /// Late-wired after construction because the adaptor is instantiated
    /// before Settings in the daemon init sequence. Methods that need
    /// m_settings null-check it and log a warning if invoked unwired,
    /// so this is a soft contract — the daemon calls it exactly once
    /// during init, external callers generally don't need to. D-Bus VS
    /// mutations are persisted to Settings, which then drives ScreenManager
    /// via the daemon's virtualScreenConfigsChanged → refreshVirtualConfigs
    /// bridge.
    void setSettings(Settings* settings);

public Q_SLOTS:
    // Screen queries
    QStringList getScreens();
    QString getScreenInfo(const QString& screenId);
    QString getPrimaryScreen();
    QString getScreenId(const QString& connectorName);
    void setPrimaryScreenFromKWin(const QString& connectorName);
    QRect getAvailableGeometry(const QString& screenId);
    QRect getScreenGeometry(const QString& screenId);

    // Virtual screen management
    QString getVirtualScreenConfig(const QString& physicalScreenId);
    void setVirtualScreenConfig(const QString& physicalScreenId, const QString& configJson);
    QStringList getPhysicalScreens();
    QString getEffectiveScreenAt(int x, int y);

    /// Swap the region of @p currentVirtualScreenId with the adjacent sibling
    /// VS in the given @p direction (left/right/up/down) within the same
    /// physical monitor. No-op if the current id is not virtual or no sibling
    /// lies in that direction. All per-VS state (windows, layout, autotile)
    /// follows the new geometry automatically.
    /// @return Empty string on success, otherwise a stable rejection token
    ///         from VirtualScreenSwapper::reasonString() so callers can
    ///         distinguish failure modes without parsing logs.
    QString swapVirtualScreenInDirection(const QString& currentVirtualScreenId, const QString& direction);

    /// Rotate every VS region on @p physicalScreenId through a spatial
    /// clockwise ring order. Rotation direction matches the window rotate
    /// convention: with @p clockwise=true each VS moves forward in the ring.
    /// No-op if the physical monitor has fewer than two VSs.
    /// @return Empty string on success, otherwise a stable rejection token
    ///         from VirtualScreenSwapper::reasonString().
    QString rotateVirtualScreens(const QString& physicalScreenId, bool clockwise);

Q_SIGNALS:
    void screenAdded(const QString& screenId);
    void screenRemoved(const QString& screenId);
    void screenGeometryChanged(const QString& screenId);
    void virtualScreensChanged(const QString& physicalScreenId);

private:
    void handleScreenGeometryChanged(QScreen* screen, const QString& physId);

    /// Handle physical screen removal: emit screenRemoved for each cached
    /// effective (virtual) screen ID, or fall back to the physical screen ID.
    void handleScreenRemoved(QScreen* removedScreen, QScreen* targetScreen, const QString& cachedId);

    /// Emit per-virtual-screen or fall back to physical screen ID.
    /// Returns true if virtual screen IDs were emitted, false if physical.
    bool emitForEffectiveScreens(const QString& physId, const std::function<void(const QString&)>& emitFn);

    /// Drop every cached getScreenInfo JSON entry. Called from every
    /// invalidation edge: geometry change, virtual-screen add/remove,
    /// region reconfigure, screen add/remove. Cheap — just clears a QHash.
    void invalidateScreenInfoCache();

    QString m_primaryScreenOverride;
    Settings* m_settings = nullptr;

    /// Last effective screen ID list emitted by the deferred timer, used to
    /// suppress duplicate emissions during rapid hot-plug sequences.
    QStringList m_lastEmittedEffectiveIds;

    /// Per-physical-screen cached effective IDs for screenRemoved emission.
    /// Updated when virtualScreensChanged fires so removal signals stay current.
    QHash<QString, QStringList> m_cachedEffectiveIdsPerScreen;

    /// Cache of getScreenInfo(screenId) JSON responses. Populated lazily on
    /// first request; invalidated globally on any screen topology change
    /// (geometry, add, remove, virtual screen reconfigure). KCM re-reads
    /// this info on every monitor page refresh, so memoizing saves a full
    /// QScreen walk + JSON serialization per query.
    QHash<QString, QString> m_cachedScreenInfoJson;
};

} // namespace PlasmaZones
