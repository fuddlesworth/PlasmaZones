// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The compositor-bridge registration watchdog's timeout arm: if the KWin
// effect has not registered within BRIDGE_WATCHDOG_TIMEOUT_MS of start(),
// diagnose why (plugin missing, or built against another KWin) and warn the
// user once. start() arms the watchdog; init_adaptors.cpp connects it here.

#include "daemon/daemon.h"
#include "helpers.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QPluginLoader>
#include <QRegularExpression>

#include "dbus/compositorbridgeadaptor.h"
#include "phosphor_i18n.h"

namespace PlasmaZones {

namespace {
// Locate the installed PlasmaZones KWin effect plugin and read the KWin
// version embedded in its plugin interface ID. The KWin effect is a compiled
// C++ plugin; KWin bakes its exact version into the IID it accepts
// (EffectPluginFactory_iid = "org.kde.kwin.EffectPluginFactory" + the KWin
// version string), and silently rejects any plugin built against a different
// KWin. metaData() reads only the static metadata section — no dlopen — so it
// works even on the version-mismatched plugin KWin itself refuses to load.
// `installed` is set to whether the plugin file was found at all. Returns the
// KWin version the effect was built against, or empty when the plugin is
// missing or its IID is not a recognizable KWin effect IID.
QString probeEffectKWinVersion(bool& installed)
{
    static const QLatin1String iidPrefix("org.kde.kwin.EffectPluginFactory");
    const QString effectRelPath = QStringLiteral("kwin/effects/plugins/kwin_effect_plasmazones.so");

    installed = false;
    const QStringList libraryPaths = QCoreApplication::libraryPaths();
    for (const QString& base : libraryPaths) {
        const QString candidate = base + QLatin1Char('/') + effectRelPath;
        if (!QFile::exists(candidate)) {
            continue;
        }
        installed = true;
        const QString iid = QPluginLoader(candidate).metaData().value(QLatin1String("IID")).toString();
        return iid.startsWith(iidPrefix) ? iid.mid(iidPrefix.size()) : QString();
    }
    return QString();
}
} // anonymous namespace

void Daemon::warnCompositorBridgeMissing()
{
    // Stay silent during shutdown. The watchdog may still be armed when the
    // session ends, and a warning/notification raised on the way out is just
    // noise — mirrors the OSD suppression gated on m_running/m_shuttingDown.
    if (m_shuttingDown) {
        return;
    }

    // Defensive only: bridgeRegistered stops the watchdog through a direct
    // connection (init_adaptors.cpp) and start() arms it only while unregistered,
    // so neither a registered nor a null bridge reaches here today.
    if (!m_compositorBridge || m_compositorBridge->isBridgeRegistered()) {
        return;
    }

    // Inspect the installed effect plugin (synchronous, cheap). The most common
    // silent failure is a stale effect build whose IID no longer matches the
    // running KWin, so KWin's effect loader rejects it without surfacing an
    // error and the effect never registers.
    bool effectInstalled = false;
    const QString effectKWinVersion = probeEffectKWinVersion(effectInstalled);

    if (!effectInstalled) {
        emitBridgeMissingWarning(
            PhosphorI18n::tr("The PlasmaZones KWin effect plugin is not installed where KWin can find it. "
                             "Reinstall PlasmaZones."));
        return;
    }
    if (effectKWinVersion.isEmpty()) {
        // Plugin present but its IID is not a recognizable KWin effect IID —
        // nothing specific to report, fall back to the generic guidance.
        emitBridgeMissingWarning(QString());
        return;
    }

    // Compare the effect's build-time KWin version against the running KWin.
    // supportInformation() is the only reliable D-Bus source for KWin's
    // version; query it asynchronously so this degraded startup path never
    // blocks the daemon's event loop (mirrors the fire-and-forget notification
    // call in emitBridgeMissingWarning).
    QDBusMessage req =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                       QStringLiteral("org.kde.KWin"), QStringLiteral("supportInformation"));
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(req, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, effectKWinVersion](QDBusPendingCallWatcher* call) {
                call->deleteLater();

                // The 3s round-trip widens the window in which a late effect
                // registration can land; stay silent if shutdown began or the
                // bridge registered after all.
                if (m_shuttingDown || (m_compositorBridge && m_compositorBridge->isBridgeRegistered())) {
                    return;
                }

                QString diagnosis;
                const QDBusPendingReply<QString> reply = *call;
                if (!reply.isError()) {
                    const QRegularExpressionMatch match =
                        QRegularExpression(QStringLiteral("KWin version:\\s*(\\S+)")).match(reply.value());
                    if (match.hasMatch()) {
                        const QString runningKWinVersion = match.captured(1);
                        if (runningKWinVersion != effectKWinVersion) {
                            diagnosis = PhosphorI18n::tr(
                                            "The PlasmaZones KWin effect was built for KWin %1 but "
                                            "KWin %2 is running, so KWin will not load it. Rebuild and "
                                            "reinstall PlasmaZones against the running KWin.")
                                            .arg(effectKWinVersion, runningKWinVersion);
                        }
                    }
                }
                emitBridgeMissingWarning(diagnosis);
            });
}

void Daemon::emitBridgeMissingWarning(const QString& diagnosis)
{
    if (diagnosis.isEmpty()) {
        qCWarning(lcDaemon) << "Compositor bridge did not register within" << (BRIDGE_WATCHDOG_TIMEOUT_MS / 1000)
                            << "s of startup — the PlasmaZones KWin effect is not running or"
                            << "failed to register. Window dragging, keyboard shortcuts, and"
                            << "snapping will not work. Enable the PlasmaZones effect in System"
                            << "Settings > Desktop Effects, then restart the Plasma session so"
                            << "KWin loads it.";
    } else {
        qCWarning(lcDaemon) << "Compositor bridge did not register within" << (BRIDGE_WATCHDOG_TIMEOUT_MS / 1000)
                            << "s of startup — window control is dead." << diagnosis;
    }

    const QString body = diagnosis.isEmpty()
        ? PhosphorI18n::tr(
              "The PlasmaZones KWin effect has not registered with the daemon, so window "
              "dragging and shortcuts will not work. Make sure it is enabled in System "
              "Settings > Desktop Effects, then restart the Plasma session.")
        : diagnosis;

    // Raise a desktop notification via the freedesktop spec so the user sees
    // the problem without having to read the journal. A direct method call
    // (rather than QDBusInterface) keeps this off the main thread's critical
    // path: QDBusInterface's constructor does a blocking Introspect round-trip,
    // whereas createMethodCall + asyncCall is genuinely fire-and-forget. A
    // missing notification server just makes the async call error out, which
    // is fine. Mirrors the createMethodCall pattern used elsewhere in daemon.
    QDBusMessage notify = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    notify << QStringLiteral("PlasmaZones") // app_name
           << 0u // replaces_id
           << QStringLiteral("plasmazones") // app_icon
           << PhosphorI18n::tr("Window manager integration is inactive") // summary
           << body // body
           << QStringList() // actions
           << QVariantMap() // hints
           << -1; // timeout (server default)
    QDBusConnection::sessionBus().asyncCall(notify);
}

} // namespace PlasmaZones
