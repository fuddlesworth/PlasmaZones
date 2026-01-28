// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ILayoutService.h"

class QDBusInterface;

namespace PlasmaZones {

/**
 * @brief D-Bus implementation of ILayoutService
 *
 * Communicates with the PlasmaZones daemon via D-Bus
 * to load and save layouts.
 */
class DBusLayoutService : public ILayoutService
{
    Q_OBJECT

public:
    explicit DBusLayoutService(QObject* parent = nullptr);
    ~DBusLayoutService() override;

    QString loadLayout(const QString& layoutId) override;
    QString createLayout(const QString& jsonLayout) override;
    bool updateLayout(const QString& jsonLayout) override;
    QString getLayoutIdForScreen(const QString& screenName) override;
    void assignLayoutToScreen(const QString& screenName, const QString& layoutId) override;

private:
    /**
     * @brief Get or create the cached D-Bus interface
     * @return Pointer to valid interface, or nullptr if connection failed
     *
     * Performance optimization: Reuses the same QDBusInterface instance
     * instead of creating a new one for every D-Bus call.
     */
    QDBusInterface* getInterface();

    QString m_serviceName;
    QString m_objectPath;
    QString m_interfaceName;
    
    // Cached D-Bus interface (performance optimization)
    QDBusInterface* m_interface = nullptr;
};

} // namespace PlasmaZones
