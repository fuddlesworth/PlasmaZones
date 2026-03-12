// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KCModuleData>

class QAction;

namespace PlasmaZones {

class DaemonController;

/**
 * @brief Module data for PlasmaZones KCM — provides sidebar toggle for daemon
 *
 * When X-KDE-System-Settings-Uses-ModuleData is true in the JSON metadata,
 * System Settings instantiates this class before the KCM is opened and uses
 * the auxiliaryAction to render a toggle switch in the sidebar.
 */
class PlasmaZonesModuleData : public KCModuleData
{
    Q_OBJECT

public:
    explicit PlasmaZonesModuleData(QObject* parent = nullptr);

private:
    void toggle(bool checked);
    void updateAction();

    DaemonController* m_daemonController = nullptr;
    QAction* m_toggleAction = nullptr;
};

} // namespace PlasmaZones
