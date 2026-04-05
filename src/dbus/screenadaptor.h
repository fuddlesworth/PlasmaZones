// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QStringList>
#include <functional>

class QScreen;

namespace PlasmaZones {

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

public Q_SLOTS:
    // Screen queries
    QStringList getScreens();
    QString getScreenInfo(const QString& screenId);
    QString getPrimaryScreen();
    QString getScreenId(const QString& connectorName);
    void setPrimaryScreenFromKWin(const QString& connectorName);

    // Virtual screen management
    QString getVirtualScreenConfig(const QString& physicalScreenId);
    void setVirtualScreenConfig(const QString& physicalScreenId, const QString& configJson);
    QStringList getPhysicalScreens();
    QString getEffectiveScreenAt(int x, int y);

Q_SIGNALS:
    void screenAdded(const QString& screenId);
    void screenRemoved(const QString& screenId);
    void screenGeometryChanged(const QString& screenId);
    void virtualScreensChanged(const QString& physicalScreenId);

private:
    void handleScreenGeometryChanged(QScreen* screen, const QString& physId);

    /// Emit per-virtual-screen or fall back to physical screen ID.
    /// Returns true if virtual screen IDs were emitted, false if physical.
    bool emitForEffectiveScreens(const QString& physId, const std::function<void(const QString&)>& emitFn);

    QString m_primaryScreenOverride;

    /// Last effective screen ID list emitted by the deferred timer, used to
    /// suppress duplicate emissions during rapid hot-plug sequences.
    QStringList m_lastEmittedEffectiveIds;

    /// Per-physical-screen cached effective IDs for screenRemoved emission.
    /// Updated when virtualScreensChanged fires so removal signals stay current.
    QHash<QString, QStringList> m_cachedEffectiveIdsPerScreen;
};

} // namespace PlasmaZones
