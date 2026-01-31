// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QStringList>

class QDBusInterface;

namespace PlasmaZones {

class LayoutManager;

/**
 * @brief Manages virtual desktop changes and layout switching
 *
 * Handles virtual desktop changes and automatically switches layouts
 * based on assignments. Follows Single Responsibility Principle.
 */
class PLASMAZONES_EXPORT VirtualDesktopManager : public QObject
{
    Q_OBJECT

public:
    explicit VirtualDesktopManager(LayoutManager* layoutManager, QObject* parent = nullptr);
    ~VirtualDesktopManager() override;

    /**
     * @brief Initialize virtual desktop monitoring
     * @return true if successful
     */
    bool init();

    /**
     * @brief Start monitoring virtual desktops
     */
    void start();

    /**
     * @brief Stop monitoring virtual desktops
     */
    void stop();

    /**
     * @brief Get current virtual desktop number
     * @return Desktop number (1-based), or 0 if unable to determine
     */
    int currentDesktop() const;

    /**
     * @brief Switch to a specific virtual desktop
     * @param desktop Desktop number (1-based)
     */
    void setCurrentDesktop(int desktop);

    /**
     * @brief Get total number of virtual desktops
     * @return Number of desktops (queried via KWin D-Bus)
     */
    int desktopCount() const;

    /**
     * @brief Get names of all virtual desktops
     * @return List of desktop names (may be auto-generated like "Desktop 1", etc.)
     */
    QStringList desktopNames() const;

public Q_SLOTS:
    /**
     * @brief Update the active layout for the current desktop
     * Called when assignments change to refresh the overlay display
     */
    void updateActiveLayout();

Q_SIGNALS:
    /**
     * @brief Emitted when virtual desktop changes
     * @param desktop New desktop number (1-based)
     */
    void currentDesktopChanged(int desktop);

    /**
     * @brief Emitted when number of virtual desktops changes
     * @param count New desktop count
     */
    void desktopCountChanged(int count);

private Q_SLOTS:
    void onCurrentDesktopChanged(int desktop);
    void onNumberOfDesktopsChanged(int count);
    void refreshFromKWin();
    void onKWinCurrentChanged(const QString& desktopId);
    void onKWinDesktopCreated();
    void onKWinDesktopRemoved();

private:
    void connectSignals();
    void disconnectSignals();
    void initKWinDBus();

    LayoutManager* m_layoutManager = nullptr;
    QDBusInterface* m_kwinVDInterface = nullptr;
    bool m_running = false;
    bool m_useKWinDBus = false; // True if KWin D-Bus is available
    int m_currentDesktop = 1;
    int m_desktopCount = 1;
    QStringList m_desktopNames;
    QStringList m_desktopIds; // KWin desktop UUIDs (maps position to id)
};

} // namespace PlasmaZones
