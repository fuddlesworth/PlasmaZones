// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenprovider.h"
#include "dbusutils.h"
#include "core/platform/logging.h"
#include <QDBusConnection>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include "core/types/constants.h"
#include "core/interfaces/isettings.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

QList<PhosphorScreens::ScreenInfo> fetchScreens(bool* daemonUnavailable)
{
    QList<PhosphorScreens::ScreenInfo> result;
    if (daemonUnavailable)
        *daemonUnavailable = false;

    // Get primary screen name from daemon
    QString primaryScreenName;
    QDBusMessage primaryReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                       QStringLiteral("getPrimaryScreen"));
    if (primaryReply.type() == QDBusMessage::ReplyMessage && !primaryReply.arguments().isEmpty()) {
        primaryScreenName = primaryReply.arguments().first().toString();
    }

    // Get screens from daemon via D-Bus
    QDBusMessage screenReply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen), QStringLiteral("getScreens"));

    // Track whether ANY per-screen getScreenInfo() reply succeeded. If every
    // per-screen probe fails (daemon up enough to answer getScreens but
    // unable to answer getScreenInfo — transient mid-startup or partial
    // crash), `result` would otherwise be populated with zero-geometry
    // entries that mislead the picker UI. Falling back to Qt's screen list
    // in that case yields a useful (even if D-Bus-poor) view.
    bool anyInfoReplySucceeded = false;
    if (screenReply.type() == QDBusMessage::ReplyMessage && !screenReply.arguments().isEmpty()) {
        const QStringList screenNames = screenReply.arguments().first().toStringList();

        for (const QString& screenName : screenNames) {
            PhosphorScreens::ScreenInfo info;
            info.name = screenName;
            // Compare physical parent for virtual screens (primary is always a physical ID)
            QString physName = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenName);
            // For virtual screens, only the first child (vs:0) is considered primary
            // to avoid showing multiple "Primary" badges in the monitor selector.
            if (PhosphorIdentity::VirtualScreenId::isVirtual(screenName)) {
                info.isPrimary =
                    (physName == primaryScreenName && PhosphorIdentity::VirtualScreenId::extractIndex(screenName) == 0);
            } else {
                info.isPrimary = (physName == primaryScreenName);
            }

            QDBusMessage infoReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                            QStringLiteral("getScreenInfo"), {screenName});

            if (infoReply.type() == QDBusMessage::ReplyMessage && !infoReply.arguments().isEmpty()) {
                anyInfoReplySucceeded = true;
                QString infoJson = infoReply.arguments().first().toString();
                QJsonDocument doc = QJsonDocument::fromJson(infoJson.toUtf8());
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject jsonObj = doc.object();

                    info.screenId =
                        jsonObj.contains(JsonKeys::ScreenId) ? jsonObj[JsonKeys::ScreenId].toString() : screenName;

                    if (jsonObj.contains(JsonKeys::Manufacturer))
                        info.manufacturer = jsonObj[JsonKeys::Manufacturer].toString();
                    if (jsonObj.contains(JsonKeys::Model))
                        info.model = jsonObj[JsonKeys::Model].toString();

                    if (jsonObj.contains(JsonKeys::Geometry)) {
                        QJsonObject geom = jsonObj[JsonKeys::Geometry].toObject();
                        info.width = geom[::PhosphorZones::ZoneJsonKeys::Width].toInt();
                        info.height = geom[::PhosphorZones::ZoneJsonKeys::Height].toInt();
                        // Position (top-left in the compositor's global space)
                        // drives the proportional multi-monitor map. 0 is a
                        // valid coordinate, so no sentinel check here.
                        info.x = geom[::PhosphorZones::ZoneJsonKeys::X].toInt();
                        info.y = geom[::PhosphorZones::ZoneJsonKeys::Y].toInt();
                    }
                    // Surface a corrupted reply so it doesn't silently produce a
                    // 0×0 picker tile. width/height stay 0 when the geometry
                    // object — or the whole key — is absent (QJsonValue::toInt()
                    // of an absent key is 0); a legitimately-0 dimension would
                    // mean a dimensionless screen, which the daemon never
                    // reports. Runs whether or not the key was present so an
                    // entirely missing `geometry` is caught too.
                    if (info.width <= 0 || info.height <= 0) {
                        qCWarning(lcConfig) << "ScreenProvider: daemon screen" << screenName
                                            << "returned non-positive geometry width=" << info.width
                                            << "height=" << info.height << "— picker tile will render as 0×0";
                    }
                    // Connector name is optional (the label falls back to
                    // vendor/model or the raw id), so unlike geometry/virtual-id
                    // a missing key is not warned.
                    if (jsonObj.contains(::PhosphorZones::ZoneJsonKeys::Name))
                        info.connectorName = jsonObj[::PhosphorZones::ZoneJsonKeys::Name].toString();
                    if (jsonObj.value(JsonKeys::IsVirtualScreen).toBool()) {
                        const int idx = PhosphorIdentity::VirtualScreenId::extractIndex(screenName);
                        // Daemon claimed virtual but the screenName isn't a
                        // parseable virtual id — treat as physical rather
                        // than persisting a sentinel index that would render
                        // as garbage in the picker.
                        if (idx >= 0) {
                            info.isVirtualScreen = true;
                            info.virtualIndex = idx;
                            info.virtualDisplayName = jsonObj.value(JsonKeys::VirtualDisplayName).toString();
                        } else {
                            // Symmetric warning to the non-positive-geometry
                            // branch above so wire-format drift surfaces in
                            // the journal instead of silently dropping
                            // virtual-screen identity.
                            qCWarning(lcConfig) << "ScreenProvider: daemon claimed virtual but screenName" << screenName
                                                << "is not a parseable virtual id — demoting to physical";
                        }
                    }
                } else {
                    info.screenId = screenName;
                }
            } else {
                info.screenId = screenName;
            }

            result.append(info);
        }
    }

    // Fallback: if no screens from daemon (getScreens returned empty or
    // errored), OR every per-screen getScreenInfo() call failed (we have
    // names but no usable metadata), get from Qt. The second arm catches
    // a partial-daemon-failure where surfacing zero-geometry entries would
    // mislead the picker.
    if (result.isEmpty() || !anyInfoReplySucceeded) {
        // Surface the degraded-fallback state to callers so a settings UI
        // can render a banner explaining why screen metadata (EDID,
        // virtual-screen subdivisions, etc.) is missing. Without this hint,
        // the picker silently shows a Qt-only view that the user can't
        // tell apart from a daemon-served view that happens to have less
        // metadata than usual.
        if (daemonUnavailable)
            *daemonUnavailable = true;
        result.clear();
        QScreen* primaryScreen = QGuiApplication::primaryScreen();
        for (QScreen* screen : QGuiApplication::screens()) {
            PhosphorScreens::ScreenInfo info;
            info.name = screen->name();
            info.isPrimary = (screen == primaryScreen);
            info.manufacturer = screen->manufacturer();
            info.model = screen->model();
            info.width = screen->geometry().width();
            info.height = screen->geometry().height();
            info.x = screen->geometry().x();
            info.y = screen->geometry().y();
            info.connectorName = screen->name();
            info.screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
            result.append(info);
        }
    }

    return result;
}

bool isMonitorDisabledFor(const ISettings* settings, PhosphorZones::AssignmentEntry::Mode mode,
                          const QString& screenName)
{
    return settings && settings->isMonitorDisabled(mode, screenName);
}

bool setMonitorDisabledFor(ISettings* settings, PhosphorZones::AssignmentEntry::Mode mode, const QString& screenName,
                           bool disabled, const std::function<void()>& onChanged)
{
    if (!settings || screenName.isEmpty()) {
        qCWarning(lcConfig) << "setMonitorDisabledFor: refusing to act —"
                            << "settings null:" << (settings == nullptr) << "screenName empty:" << screenName.isEmpty();
        return false;
    }

    QString id = PhosphorScreens::ScreenIdentity::idForName(screenName);
    // Empty id means the connector name couldn't be canonicalised — bail
    // rather than inserting an empty string into the disabled list (which
    // would never round-trip back to a real screen). Symmetric to the
    // warnings emitted by fetchScreens() above so a stale screenName
    // (referring to a screen that's since been unplugged, or never existed)
    // surfaces in the journal instead of silently no-op'ing — the QML
    // toggle should revert its visual state when this returns false.
    if (id.isEmpty()) {
        qCWarning(lcConfig) << "setMonitorDisabledFor: unknown screen name" << screenName
                            << "— could not canonicalise to a screen id, refusing to write empty entry";
        return false;
    }
    QStringList list = settings->disabledMonitors(mode);

    if (disabled) {
        bool changed = false;
        // A monitor can be referenced by either its connector name or its
        // canonical screen id; drop any entry under the connector form so a
        // user toggling disable/enable on the same row doesn't end up with the
        // monitor listed twice under both identifier forms.
        if (id != screenName)
            changed = list.removeAll(screenName) > 0;
        if (!list.contains(id)) {
            list.append(id);
            changed = true;
        }
        if (changed) {
            settings->setDisabledMonitors(mode, list);
            if (onChanged)
                onChanged();
        }
    } else {
        bool changed = list.removeAll(id) > 0;
        if (id != screenName) {
            changed |= list.removeAll(screenName) > 0;
        }
        if (changed) {
            settings->setDisabledMonitors(mode, list);
            if (onChanged)
                onChanged();
        }
    }
    return true;
}

bool connectScreenChangeSignals(QObject* receiver)
{
    auto bus = QDBusConnection::sessionBus();
    const bool a = bus.connect(QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
                               QString(PhosphorProtocol::Service::Interface::Screen), QStringLiteral("screenAdded"),
                               receiver, SLOT(refreshScreens()));
    const bool b = bus.connect(QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
                               QString(PhosphorProtocol::Service::Interface::Screen), QStringLiteral("screenRemoved"),
                               receiver, SLOT(refreshScreens()));
    if (!a || !b) {
        qCWarning(lcConfig) << "connectScreenChangeSignals: failed to subscribe to screen change broadcasts —"
                            << "screenAdded:" << a << "screenRemoved:" << b
                            << "— screen list will not auto-refresh on hot-plug";
    }
    return a && b;
}

} // namespace PlasmaZones
