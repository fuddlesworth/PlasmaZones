// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>

namespace PlasmaZones {

class Settings;

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
public:
    explicit ScreenHelper(Settings* settings, QObject* parent = nullptr);

    QVariantList screens() const;

    bool isMonitorDisabled(const QString& screenName) const;
    void setMonitorDisabled(const QString& screenName, bool disabled);

    /// Call in KCM constructor to connect D-Bus screen change signals
    void connectToDaemonSignals();

public Q_SLOTS:
    void refreshScreens();

Q_SIGNALS:
    void screensChanged();
    void disabledMonitorsChanged();
    void needsSave();

private:
    Settings* m_settings = nullptr;
    QVariantList m_screens;
};

} // namespace PlasmaZones
