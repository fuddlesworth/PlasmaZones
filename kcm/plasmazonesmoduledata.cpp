// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazonesmoduledata.h"
#include "common/daemoncontroller.h"

#include "pz_i18n.h"
#include <QAction>

namespace PlasmaZones {

PlasmaZonesModuleData::PlasmaZonesModuleData(QObject* parent)
    : KCModuleData(parent)
    , m_daemonController(new DaemonController(this))
    , m_toggleAction(new QAction(PzI18n::tr("Enable PlasmaZones", "@info:tooltip"), this))
{
    m_toggleAction->setCheckable(true);
    connect(m_toggleAction, &QAction::triggered, this, &PlasmaZonesModuleData::toggle);
    setAuxiliaryAction(m_toggleAction);

    connect(m_daemonController, &DaemonController::enabledChanged, this, &PlasmaZonesModuleData::updateAction);
    connect(m_daemonController, &DaemonController::runningChanged, this, &PlasmaZonesModuleData::updateAction);
    updateAction();
}

void PlasmaZonesModuleData::toggle(bool checked)
{
    m_daemonController->setEnabled(checked);
}

void PlasmaZonesModuleData::updateAction()
{
    m_toggleAction->setChecked(m_daemonController->isEnabled());
}

} // namespace PlasmaZones
