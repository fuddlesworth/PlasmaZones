// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenhelper.h"
#include "screenprovider.h"
#include "../../src/config/settings.h"

namespace PlasmaZones {

ScreenHelper::ScreenHelper(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QVariantList ScreenHelper::screens() const
{
    return m_screens;
}

void ScreenHelper::refreshScreens()
{
    m_screens = screenInfoListToVariantList(fetchScreens());
    Q_EMIT screensChanged();
}

bool ScreenHelper::isMonitorDisabled(const QString& screenName) const
{
    return isMonitorDisabledFor(m_settings, screenName);
}

void ScreenHelper::setMonitorDisabled(const QString& screenName, bool disabled)
{
    setMonitorDisabledFor(m_settings, screenName, disabled, [this]() {
        Q_EMIT disabledMonitorsChanged();
        Q_EMIT needsSave();
    });
}

void ScreenHelper::connectToDaemonSignals()
{
    connectScreenChangeSignals(this);
}

} // namespace PlasmaZones
