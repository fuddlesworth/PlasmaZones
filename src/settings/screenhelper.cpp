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

bool ScreenHelper::isMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenName) const
{
    return isMonitorDisabledFor(m_settings, mode, screenName);
}

void ScreenHelper::setMonitorDisabled(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenName,
                                      bool disabled)
{
    // Settings::setDisabledMonitors fires ISettings::disabledMonitorsChanged(mode)
    // when the canonicalised list actually changes. SettingsController forwards
    // that signal to QML, so ScreenHelper only needs to mark the page dirty.
    setMonitorDisabledFor(m_settings, mode, screenName, disabled, [this]() {
        Q_EMIT needsSave();
    });
}

void ScreenHelper::connectToDaemonSignals()
{
    connectScreenChangeSignals(this);
}

} // namespace PlasmaZones
