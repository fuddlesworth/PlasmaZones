// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QDBusInterface;

namespace PlasmaZones {

/**
 * @brief Service for managing virtual screen configurations via D-Bus
 *
 * Communicates with the PlasmaZones daemon's Screen interface to
 * query and modify virtual screen subdivisions on physical monitors.
 */
class VirtualScreenService : public QObject
{
    Q_OBJECT

public:
    explicit VirtualScreenService(QObject* parent = nullptr);
    ~VirtualScreenService() override;

    /// Get list of physical screen IDs
    Q_INVOKABLE QStringList physicalScreens() const;

    /// Get screen info (geometry, name) for a physical screen
    Q_INVOKABLE QVariantMap screenInfo(const QString& screenId) const;

    /// Get virtual screen config for a physical screen
    Q_INVOKABLE QVariantList virtualScreensFor(const QString& physicalScreenId) const;

    /// Apply a virtual screen configuration
    /// @param physicalScreenId Physical screen to configure
    /// @param screens Array of {displayName, x, y, width, height} objects
    Q_INVOKABLE void applyConfig(const QString& physicalScreenId, const QVariantList& screens);

    /// Remove all virtual screen subdivisions for a physical screen
    Q_INVOKABLE void removeConfig(const QString& physicalScreenId);

    /// Apply a preset split
    /// @param physicalScreenId Physical screen
    /// @param preset "50-50", "60-40", "33-33-33", "40-20-40"
    Q_INVOKABLE void applyPreset(const QString& physicalScreenId, const QString& preset);

Q_SIGNALS:
    void configChanged(const QString& physicalScreenId);
    void errorOccurred(const QString& error);

private:
    /**
     * @brief Get or create the cached D-Bus interface for the Screen adaptor
     * @return Pointer to valid interface, or nullptr if connection failed
     */
    QDBusInterface* getInterface() const;

    mutable QDBusInterface* m_interface = nullptr;
};

} // namespace PlasmaZones
