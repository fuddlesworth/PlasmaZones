// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>
#include <QObject>
#include <QVariantList>

namespace PlasmaZones {

class ISettings;

/**
 * @brief Helper that manages screen list, monitor disable, and D-Bus signal connections.
 *
 * Own one of these (via composition) in each KCM that needs screen awareness.
 * The KCM keeps its Q_PROPERTY and Q_INVOKABLE declarations for QML but
 * delegates the actual work here, eliminating duplicated implementations
 * across snapping, autotiling, layouts, and assignments KCMs.
 */
class ScreenHelper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool daemonUnavailable READ daemonUnavailable NOTIFY daemonUnavailableChanged)
public:
    explicit ScreenHelper(ISettings* settings, QObject* parent = nullptr);

    QVariantList screens() const;

    /**
     * @brief Whether the most recent screen-list refresh fell back to Qt.
     *
     * `true` when the daemon's `getScreens` / `getScreenInfo` D-Bus path
     * failed and the Qt-fallback ran. QML chrome can subscribe to the
     * NOTIFY signal and render a banner explaining the degraded view —
     * without this, the picker silently shows fewer columns and the user
     * has no way to tell apart "daemon hasn't started yet" from "this
     * screen genuinely has no EDID".
     */
    bool daemonUnavailable() const
    {
        return m_daemonUnavailable;
    }

    bool isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenName) const;
    void setMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenName, bool disabled);

    /// Call in KCM constructor to connect D-Bus screen change signals
    void connectToDaemonSignals();

public Q_SLOTS:
    void refreshScreens();

Q_SIGNALS:
    void screensChanged();
    // Disabled-monitor changes route via ISettings::disabledMonitorsChanged(Mode);
    // ScreenHelper only marks dirty via needsSave().
    void needsSave();
    /// Emitted when the screen list refresh transitions between daemon-served
    /// and Qt-fallback states. QML chrome should re-read daemonUnavailable.
    void daemonUnavailableChanged();
    /// Emitted when setMonitorDisabled() couldn't resolve the connector name
    /// to a canonical screen id and the toggle was rejected. QML toggle
    /// handlers should listen on this and revert their visual state.
    void monitorDisableFailed(const QString& screenName);

private:
    ISettings* m_settings = nullptr;
    QVariantList m_screens;
    bool m_daemonUnavailable = false;
};

} // namespace PlasmaZones
