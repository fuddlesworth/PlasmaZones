// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Interface for layout persistence operations
 *
 * Abstracts layout loading/saving to support different backends
 * (D-Bus, file system, etc.)
 */
class ILayoutService : public QObject
{
    Q_OBJECT

public:
    explicit ILayoutService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    virtual ~ILayoutService() = default;

    /**
     * @brief Loads a layout by ID
     * @param layoutId The unique identifier of the layout
     * @return JSON string representation of the layout, or empty string on failure
     */
    virtual QString loadLayout(const QString& layoutId) = 0;

    /**
     * @brief Creates a new layout from JSON
     * @param jsonLayout JSON string representation of the layout
     * @return Layout ID of the created layout, or empty string on failure
     */
    virtual QString createLayout(const QString& jsonLayout) = 0;

    /**
     * @brief Updates an existing layout with JSON
     * @param jsonLayout JSON string representation of the layout
     * @return true on success, false on failure
     */
    virtual bool updateLayout(const QString& jsonLayout) = 0;

    /**
     * @brief Gets the layout ID assigned to a screen
     * @param screenName The screen/monitor name
     * @return Layout ID for the screen, or empty string if no assignment
     */
    virtual QString getLayoutIdForScreen(const QString& screenName) = 0;

    /**
     * @brief Assigns a layout to a screen
     * @param screenName The screen/monitor name
     * @param layoutId The layout ID to assign
     */
    virtual void assignLayoutToScreen(const QString& screenName, const QString& layoutId) = 0;

Q_SIGNALS:
    /**
     * @brief Emitted when a layout operation fails
     * @param error Error message describing what went wrong
     */
    void errorOccurred(const QString& error);
};

} // namespace PlasmaZones
