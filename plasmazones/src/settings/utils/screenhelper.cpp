// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenhelper.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "screenprovider.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusConnection>

namespace PlasmaZones {

namespace {

/// The same wait the layout reload coalesces over: long enough to fold the
/// per-screen burst one mode change or rotation produces, short enough that
/// the list still reads as live.
constexpr int ScreenRefreshCoalesceMs = 50;

/// screenInfoListToVariantList's maps with the device-pixel size added. The
/// library serialiser leaves the size out because every label it builds is
/// logical, so it is added here, beside the entry it belongs to. The
/// serialiser emits exactly one map per entry, in order.
QVariantList screenMaps(const QList<PhosphorScreens::ScreenInfo>& infos)
{
    QVariantList maps = PhosphorScreens::screenInfoListToVariantList(infos);
    for (qsizetype i = 0; i < maps.size() && i < infos.size(); ++i) {
        const PhosphorScreens::ScreenInfo& info = infos.at(i);
        if (info.pixelSize.width() <= 0 || info.pixelSize.height() <= 0) {
            continue;
        }
        QVariantMap map = maps.at(i).toMap();
        map[QStringLiteral("pixelWidth")] = info.pixelSize.width();
        map[QStringLiteral("pixelHeight")] = info.pixelSize.height();
        map[QStringLiteral("pixelSizeKnown")] = info.pixelSizeKnown;
        maps[i] = map;
    }
    return maps;
}

} // namespace

ScreenHelper::ScreenHelper(ISettings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(ScreenRefreshCoalesceMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ScreenHelper::refreshScreens);
}

QVariantList ScreenHelper::screens() const
{
    return m_screens;
}

void ScreenHelper::scheduleRefreshScreens()
{
    m_refreshTimer.start();
}

void ScreenHelper::refreshScreens()
{
    // A direct refresh supersedes one still waiting to coalesce.
    m_refreshTimer.stop();
    bool fallback = false;
    QVariantList fresh = screenMaps(fetchScreens(&fallback));
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
    // A mode change, a rotation, a scale settling and a new device-pixel size
    // from the compositor all arrive as screenGeometryChanged, once per
    // affected screen, and each one changes what the list shows. Coalesced
    // because the refresh is a blocking round trip per screen.
    const bool ok = QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Screen), QStringLiteral("screenGeometryChanged"), this,
        SLOT(scheduleRefreshScreens()));
    if (!ok) {
        qCWarning(lcConfig) << "ScreenHelper: failed to subscribe to screenGeometryChanged; the screen list will not"
                            << "follow a mode change or rotation";
    }
}

} // namespace PlasmaZones
