// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenhelper.h"
#include "core/interfaces/isettings.h"
#include "screenprovider.h"

namespace PlasmaZones {

ScreenHelper::ScreenHelper(ISettings* settings, QObject* parent)
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
    bool fallback = false;
    QVariantList fresh = PhosphorScreens::screenInfoListToVariantList(fetchScreens(&fallback));
    // Emit the fallback-state transition independently of the list-changed
    // gate below. A repeated `screenAdded` for an already-known screen
    // shouldn't rebuild the QML model, but a daemon recovery (fallback
    // true → false) MUST still propagate so the banner clears even when
    // the list payload is identical.
    if (fallback != m_daemonUnavailable) {
        m_daemonUnavailable = fallback;
        Q_EMIT daemonUnavailableChanged();
    }
    if (fresh == m_screens) {
        // Hot-plug events that don't actually change the list (e.g. a
        // repeated `screenAdded` for an already-known screen) shouldn't
        // fan out to a full QML model rebuild.
        return;
    }
    m_screens = std::move(fresh);
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
    const bool ok = setMonitorDisabledFor(m_settings, mode, screenName, disabled, [this]() {
        Q_EMIT needsSave();
    });
    if (!ok) {
        // The free function already logged the precise reason at warning
        // level. Surface it to QML so the toggle can revert visual state
        // rather than silently lying about the persisted value.
        Q_EMIT monitorDisableFailed(screenName);
    }
}

void ScreenHelper::connectToDaemonSignals()
{
    connectScreenChangeSignals(this);
}

} // namespace PlasmaZones
