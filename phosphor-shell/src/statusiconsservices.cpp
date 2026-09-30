// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StatusIconsSource.h"
#include "StatusIconsController.h"
#include "AirplaneController.h"
#include "NotificationController.h"
#include "PhosphorShellI18n.h"
#include "QuickSettingsController.h"

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>
#include <PhosphorServiceNetwork/AccessPoint.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkHost.h>
#include <PhosphorServicePipeWire/PipeWireHost.h>
#include <PhosphorServicePipeWire/PwNode.h>
#include <PhosphorServiceUPower/UPowerHost.h>

#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <optional>

namespace PhosphorShellApp {
namespace {
using NetworkHost = PhosphorServiceNetwork::NetworkHost;
using NetworkDevice = PhosphorServiceNetwork::NetworkDevice;
using AccessPoint = PhosphorServiceNetwork::AccessPoint;
using BluetoothHost = PhosphorServiceBluetooth::BluetoothHost;
using BluetoothAdapter = PhosphorServiceBluetooth::BluetoothAdapter;
using BluetoothDevice = PhosphorServiceBluetooth::BluetoothDevice;
using UPowerHost = PhosphorServiceUPower::UPowerHost;
using UPowerDevice = PhosphorServiceUPower::UPowerDevice;
using PipeWireHost = PhosphorServicePipeWire::PipeWireHost;
using PwNode = PhosphorServicePipeWire::PwNode;
using PipeWireConnection = PhosphorServicePipeWire::PipeWireConnection;

QString nodeName(const PwNode* node)
{
    return !node                         ? QString{}
        : !node->description().isEmpty() ? node->description()
        : !node->nick().isEmpty()        ? node->nick()
                                         : node->name();
}
int volumeOf(const PwNode* node)
{
    if (!node || node->volumes().isEmpty() || !std::isfinite(node->volumes().constFirst()))
        return 0;
    return qRound(qBound(0.0, node->volumes().constFirst(), 1.0) * 100);
}
QString duration(qreal seconds)
{
    const auto minutes = qRound(qBound(1.0, seconds / 60, 525600.0));
    return minutes >= 60 ? PhosphorI18n::tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60)
                         : PhosphorI18n::tr("%1 min").arg(minutes);
}

class LiveStatusIconsSource final : public StatusIconsSource
{
public:
    LiveStatusIconsSource(QuickSettingsController* quickSettings, AirplaneController* airplane,
                          NotificationController* notifications, QObject* parent)
        : StatusIconsSource(parent)
        , m_quick(quickSettings)
        , m_airplane(airplane)
        , m_notifications(notifications)
        , m_network(new NetworkHost(this))
        , m_bluetooth(new BluetoothHost(this))
        , m_battery(new UPowerHost(this))
    {
        if (m_quick) {
            connect(m_quick, &QuickSettingsController::changed, this, &LiveStatusIconsSource::update);
            connect(m_quick, &QObject::destroyed, this, &LiveStatusIconsSource::update);
        }
        if (m_airplane) {
            connect(m_airplane, &AirplaneController::changed, this, &LiveStatusIconsSource::update);
            connect(m_airplane, &QObject::destroyed, this, &LiveStatusIconsSource::update);
        }
        if (m_notifications) {
            connect(m_notifications, &NotificationController::doNotDisturbChanged, this,
                    &LiveStatusIconsSource::update);
            connect(m_notifications, &QObject::destroyed, this, &LiveStatusIconsSource::update);
        }
        for (auto signal : {&NetworkHost::availableChanged, &NetworkHost::wirelessEnabledChanged,
                            &NetworkHost::wirelessHardwareEnabledChanged, &NetworkHost::connectivityChanged})
            connect(m_network, signal, this, &LiveStatusIconsSource::update);
        connect(m_network, &NetworkHost::deviceAdded, this, [this](NetworkDevice* device) {
            for (auto signal : {&NetworkDevice::deviceTypeChanged, &NetworkDevice::stateChanged,
                                &NetworkDevice::interfaceNameChanged, &NetworkDevice::activeAccessPointPathChanged})
                connect(device, signal, this, &LiveStatusIconsSource::update);
            update();
        });
        connect(m_network, &NetworkHost::deviceRemoved, this, &LiveStatusIconsSource::update);
        connect(m_bluetooth, &BluetoothHost::adapterAdded, this, [this](BluetoothAdapter* adapter) {
            connect(adapter, &BluetoothAdapter::poweredChanged, this, &LiveStatusIconsSource::update);
            update();
        });
        connect(m_bluetooth, &BluetoothHost::adapterRemoved, this, &LiveStatusIconsSource::update);
        connect(m_bluetooth, &BluetoothHost::deviceAdded, this, [this](BluetoothDevice* device) {
            for (auto signal :
                 {&BluetoothDevice::connectedChanged, &BluetoothDevice::nameChanged, &BluetoothDevice::aliasChanged})
                connect(device, signal, this, &LiveStatusIconsSource::update);
            update();
        });
        connect(m_bluetooth, &BluetoothHost::deviceRemoved, this, &LiveStatusIconsSource::update);
        connect(m_battery, &UPowerHost::availabilityChanged, this, &LiveStatusIconsSource::update);
        connect(m_battery, &UPowerHost::displayDeviceChanged, this, [this] {
            if (m_display)
                m_display->disconnect(this);
            m_display = m_battery->displayDevice();
            if (m_display) {
                for (auto signal : {&UPowerDevice::percentageChanged, &UPowerDevice::stateChanged,
                                    &UPowerDevice::isPresentChanged, &UPowerDevice::timeToEmptyChanged,
                                    &UPowerDevice::timeToFullChanged, &UPowerDevice::iconNameChanged})
                    connect(m_display, signal, this, &LiveStatusIconsSource::update);
            }
            update();
        });
        m_deadline.setSingleShot(true);
        m_deadline.setInterval(5000);
        connect(&m_deadline, &QTimer::timeout, this, [this] {
            clearAudioRequests();
            m_audioError =
                PhosphorI18n::tr("The audio change was not confirmed. Check the audio service and try again.");
            update();
        });
        m_reconnect.setSingleShot(true);
        connect(&m_reconnect, &QTimer::timeout, this, [this] {
            if (m_audio && !m_audio->isConnected()) {
                m_audio->connectToDaemon();
                m_retryDelay = qMin(30000, m_retryDelay * 2);
                m_reconnect.start(m_retryDelay);
            }
        });
    }

    void setAudioHost(PipeWireHost* host) override
    {
        if (m_audio == host)
            return;
        if (m_audio) {
            m_audio->disconnect(this);
            m_audio->connection()->disconnect(this);
            for (auto* node : m_audio->connection()->nodes())
                node->disconnect(this);
        }
        m_audio = host;
        m_audioConnected = false;
        m_reconnect.stop();
        clearAudioRequests();
        m_audioError.clear();
        if (host) {
            for (auto signal : {&PipeWireHost::connectedChanged, &PipeWireHost::defaultSinkChanged,
                                &PipeWireHost::defaultSourceChanged})
                connect(host, signal, this, &LiveStatusIconsSource::update);
            connect(host, &QObject::destroyed, this, &LiveStatusIconsSource::update);
            connect(host, &PipeWireHost::nodeAdded, this, [this](PwNode* node) {
                watchNode(node);
                update();
            });
            connect(host, &PipeWireHost::nodeRemoved, this, &LiveStatusIconsSource::update);
            connect(host, &PipeWireHost::error, this, [this](const QString&) {
                m_audioError = PhosphorI18n::tr("The audio service is unavailable.");
                update();
            });
            connect(host->connection(), &PipeWireConnection::operationFailed, this, [this](const QString&) {
                clearAudioRequests();
                m_audioError = PhosphorI18n::tr("Could not change the audio settings.");
                update();
            });
            for (auto* node : host->connection()->nodes())
                watchNode(node);
        }
        update();
    }

    StatusIconsSnapshot snapshot() const override
    {
        StatusIconsSnapshot result;
        StatusIconState wifi;
        wifi.available = m_airplane && m_airplane->wifiAvailable();
        wifi.off = !m_network->wirelessEnabled();
        wifi.pending = m_airplane && m_airplane->pending();
        wifi.error = m_airplane ? m_airplane->error() : QString{};
        QStringList connections;
        for (auto* device : m_network->devices()) {
            if (device->deviceType() != NetworkDevice::Wifi || device->state() != NetworkDevice::Activated)
                continue;
            wifi.active = m_network->available() && !wifi.off;
            const auto ap = m_accessPoints.value(device->activeAccessPointPath());
            const auto name = ap && !ap->ssid().isEmpty() ? ap->ssid() : device->interfaceName();
            if (!name.isEmpty() && !connections.contains(name))
                connections.append(name);
        }
        connections.sort();
        wifi.attention = wifi.active && m_network->connectivity() == NetworkHost::Portal;
        wifi.summary = !wifi.available              ? PhosphorI18n::tr("Wi-Fi unavailable")
            : !m_network->wirelessHardwareEnabled() ? PhosphorI18n::tr("Wi-Fi is blocked by the hardware switch")
            : wifi.off                              ? PhosphorI18n::tr("Wi-Fi off")
            : wifi.attention                        ? PhosphorI18n::tr("Sign in to the network")
            : wifi.active
            ? (connections.isEmpty() ? PhosphorI18n::tr("Connected") : connections.join(QStringLiteral(", ")))
            : PhosphorI18n::tr("Not connected");
        result.states.insert(QStringLiteral("wifi"), wifi);

        StatusIconState bluetooth;
        bluetooth.available = m_airplane && m_airplane->bluetoothAvailable();
        bluetooth.off = !m_airplane || !m_airplane->bluetoothEnabled();
        bluetooth.pending = m_airplane && m_airplane->pending();
        bluetooth.error = m_airplane ? m_airplane->error() : QString{};
        QStringList devices;
        for (auto* device : m_bluetooth->devices()) {
            if (device->connected())
                devices.append(!device->alias().isEmpty() ? device->alias() : device->name());
        }
        devices.sort();
        bluetooth.active = bluetooth.available && !bluetooth.off && !devices.isEmpty();
        bluetooth.summary = !bluetooth.available ? PhosphorI18n::tr("Bluetooth unavailable")
            : bluetooth.off                      ? PhosphorI18n::tr("Bluetooth off")
            : !devices.isEmpty()                 ? devices.join(QStringLiteral(", "))
                                                 : PhosphorI18n::tr("No connected devices");
        result.states.insert(QStringLiteral("bluetooth"), bluetooth);

        StatusIconState battery;
        battery.available = m_battery->available();
        battery.active = battery.available && m_display && m_display->isPresent();
        battery.pending = m_battery->pending();
        battery.error = m_battery->error();
        battery.off = !battery.active;
        battery.summary = !battery.available ? PhosphorI18n::tr("Battery service unavailable")
                                             : PhosphorI18n::tr("No system battery");
        if (battery.active) {
            if (std::isfinite(m_display->percentage()))
                battery.percent = qRound(qBound(0.0, m_display->percentage(), 100.0));
            battery.attention =
                battery.percent >= 0 && battery.percent <= 15 && m_display->state() == UPowerDevice::Discharging;
            battery.iconName = m_display->iconName();
            const QString charge =
                battery.percent < 0 ? PhosphorI18n::tr("Charge unknown") : PhosphorI18n::tr("%1%").arg(battery.percent);
            if (m_display->state() == UPowerDevice::Charging)
                battery.summary = m_display->timeToFull() > 0 && std::isfinite(m_display->timeToFull())
                    ? PhosphorI18n::tr("%1, charging, %2 until full").arg(charge, duration(m_display->timeToFull()))
                    : PhosphorI18n::tr("%1, charging").arg(charge);
            else if (m_display->state() == UPowerDevice::FullyCharged)
                battery.summary = PhosphorI18n::tr("Fully charged");
            else if (m_display->timeToEmpty() > 0 && std::isfinite(m_display->timeToEmpty()))
                battery.summary = PhosphorI18n::tr("%1, %2 remaining").arg(charge, duration(m_display->timeToEmpty()));
            else
                battery.summary = charge;
        }
        result.states.insert(QStringLiteral("battery"), battery);

        addAudio(result);
        StatusIconState night;
        if (m_quick) {
            night.available = m_quick->nightLightAvailable();
            night.active = night.available && m_quick->nightLightRunning();
            night.off = !m_quick->nightLightEnabled() || m_quick->nightLightInhibited();
            night.pending = m_quick->nightLightPending();
            night.error = m_quick->nightLightError();
            night.summary = !night.available     ? PhosphorI18n::tr("Night light unavailable")
                : m_quick->nightLightPaused()    ? PhosphorI18n::tr("Night light paused")
                : m_quick->nightLightInhibited() ? PhosphorI18n::tr("Night light inhibited by another application")
                : !m_quick->nightLightEnabled()  ? PhosphorI18n::tr("Night light off")
                : night.active                   ? PhosphorI18n::tr("Night light on")
                                                 : PhosphorI18n::tr("Night light scheduled");
        }
        result.states.insert(QStringLiteral("nightlight"), night);
        StatusIconState focus;
        focus.available = m_notifications;
        focus.active = m_notifications && m_notifications->doNotDisturb();
        focus.off = !focus.active;
        focus.summary = focus.active ? PhosphorI18n::tr("Do not disturb on") : PhosphorI18n::tr("Do not disturb off");
        result.states.insert(QStringLiteral("focus"), focus);
        StatusIconState airplane;
        if (m_airplane) {
            airplane.available = m_airplane->available();
            airplane.active = m_airplane->enabled();
            airplane.off = !airplane.active;
            airplane.pending = m_airplane->pending();
            airplane.error = m_airplane->error();
            airplane.summary = !airplane.available ? PhosphorI18n::tr("Wireless controls unavailable")
                : airplane.active && m_airplane->bluetoothEnabled()
                ? PhosphorI18n::tr("Airplane mode on with Bluetooth")
                : airplane.active ? PhosphorI18n::tr("Airplane mode on")
                                  : PhosphorI18n::tr("Airplane mode off");
        }
        result.states.insert(QStringLiteral("airplane"), airplane);
        StatusIconState power;
        if (m_quick) {
            power.available = m_quick->powerAvailable();
            result.powerProfile = m_quick->powerProfile();
            result.powerProfiles = m_quick->powerProfiles();
            power.active =
                power.available && !result.powerProfile.isEmpty() && result.powerProfile != QLatin1String("balanced");
            power.pending = m_quick->powerPending();
            power.error = m_quick->powerError();
            power.attention = !m_quick->performanceDegraded().isEmpty();
            power.summary = !power.available                          ? PhosphorI18n::tr("Power profiles unavailable")
                : result.powerProfile == QLatin1String("power-saver") ? PhosphorI18n::tr("Power saver")
                : result.powerProfile == QLatin1String("performance") ? PhosphorI18n::tr("Performance")
                : result.powerProfile == QLatin1String("balanced")    ? PhosphorI18n::tr("Balanced")
                                                                      : PhosphorI18n::tr("Unknown power profile");
            power.off = !power.available;
        }
        result.states.insert(QStringLiteral("power"), power);
        return result;
    }

    bool toggle(const QString& id) override
    {
        if (id == QLatin1String("wifi") && m_airplane && !m_airplane->pending())
            m_airplane->setWifiEnabled(!m_airplane->wifiEnabled());
        else if (id == QLatin1String("bluetooth") && m_airplane && !m_airplane->pending())
            m_airplane->setBluetoothEnabled(!m_airplane->bluetoothEnabled());
        else if (id == QLatin1String("airplane") && m_airplane && !m_airplane->pending())
            m_airplane->setEnabled(!m_airplane->enabled());
        else if (id == QLatin1String("nightlight") && m_quick && !m_quick->nightLightPending()) {
            if (m_quick->nightLightPaused())
                m_quick->toggleNightLightPause();
            else
                m_quick->toggleNightLight();
        } else if (id == QLatin1String("focus") && m_notifications)
            m_notifications->setDoNotDisturb(!m_notifications->doNotDisturb());
        else if ((id == QLatin1String("audio") || id == QLatin1String("microphone")) && m_audio
                 && m_audio->isConnected()) {
            const bool input = id == QLatin1String("microphone");
            auto* node = input ? m_audio->defaultSource() : m_audio->defaultSink();
            if (!node)
                return false;
            if (input) {
                m_writeSource = node;
                m_requestedMicMute = !node->muted();
            } else {
                m_writeSink = node;
                m_requestedMute = !node->muted();
            }
            node->setMuted(!node->muted());
            startAudioRequest();
        } else
            return false;
        return true;
    }
    bool setVolume(int percent) override
    {
        auto* sink = m_audio && m_audio->isConnected() ? m_audio->defaultSink() : nullptr;
        if (!sink || sink->volumes().isEmpty() || percent < 0 || percent > 100)
            return false;
        m_writeSink = sink;
        m_requestedVolume = percent;
        sink->setVolume(percent / 100.0);
        startAudioRequest();
        return true;
    }
    bool selectOutput(const QString& id) override
    {
        if (!m_audio || !m_audio->isConnected())
            return false;
        for (auto* node : m_audio->connection()->nodes()) {
            if (node->mediaClass() == QLatin1String("Audio/Sink") && node->name() == id && !id.isEmpty()) {
                m_requestedOutput = id;
                m_audio->connection()->setDefaultSink(id);
                startAudioRequest();
                return true;
            }
        }
        return false;
    }
    bool setPowerProfile(const QString& id) override
    {
        if (!m_quick || !m_quick->powerAvailable() || m_quick->powerPending() || !m_quick->powerProfiles().contains(id))
            return false;
        m_quick->setPowerProfile(id);
        return true;
    }

private:
    void watchNode(PwNode* node)
    {
        connect(node, &PwNode::infoChanged, this, &LiveStatusIconsSource::update);
        connect(node, &PwNode::propsChanged, this, &LiveStatusIconsSource::update);
    }
    void addAudio(StatusIconsSnapshot& result) const
    {
        const bool connected = m_audio && m_audio->isConnected();
        auto* sink = connected ? m_audio->defaultSink() : nullptr;
        auto* source = connected ? m_audio->defaultSource() : nullptr;
        result.audioAvailable = sink != nullptr;
        result.volume = volumeOf(sink);
        result.muted = sink && sink->muted();
        StatusIconState audio;
        audio.available = result.audioAvailable;
        audio.off = result.muted || !sink;
        audio.active = audio.available && result.muted;
        audio.pending = m_requestedVolume.has_value() || m_requestedMute.has_value() || !m_requestedOutput.isEmpty();
        audio.error = m_audioError;
        audio.iconName = result.muted ? QStringLiteral("audio-volume-muted")
            : result.volume == 0      ? QStringLiteral("audio-volume-low")
                                      : QStringLiteral("audio-volume-high");
        audio.summary = !connected ? PhosphorI18n::tr("Sound service unavailable")
            : !sink                ? PhosphorI18n::tr("No output device")
            : result.muted         ? PhosphorI18n::tr("Muted")
                                   : PhosphorI18n::tr("%1%, %2").arg(result.volume).arg(nodeName(sink));
        result.states.insert(QStringLiteral("audio"), audio);
        StatusIconState microphone;
        microphone.available = source != nullptr;
        microphone.off = !source || source->muted();
        microphone.pending = m_requestedMicMute.has_value();
        microphone.error = m_audioError;
        microphone.iconName =
            microphone.off ? QStringLiteral("microphone-sensitivity-muted") : QStringLiteral("audio-input-microphone");
        QStringList recording;
        if (connected) {
            const auto nodes = m_audio->connection()->nodes();
            for (auto* node : nodes) {
                if (node->mediaClass() == QLatin1String("Audio/Sink") && !node->name().isEmpty())
                    result.outputs.append(QVariantMap{{QStringLiteral("id"), node->name()},
                                                      {QStringLiteral("name"), nodeName(node)},
                                                      {QStringLiteral("selected"), node == sink}});
                if (node->mediaClass() != QLatin1String("Stream/Input/Audio") || !node->running()
                    || node->properties().value(QStringLiteral("stream.capture.sink")) == QLatin1String("true"))
                    continue;
                // A capture stream explicitly routed to a sink is loopback audio,
                // not a microphone. Default-source capture remains visible.
                bool monitor = false;
                for (auto* endpoint : nodes) {
                    if (endpoint->mediaClass() == QLatin1String("Audio/Sink") && !node->targetName().isEmpty()
                        && (node->targetName() == endpoint->name() || node->targetName() == endpoint->serial()))
                        monitor = true;
                }
                if (!monitor) {
                    microphone.active = true;
                    const auto app = node->applicationName();
                    if (!app.isEmpty() && !recording.contains(app))
                        recording.append(app);
                }
            }
        }
        std::sort(result.outputs.begin(), result.outputs.end(), [](const QVariant& a, const QVariant& b) {
            const auto first = a.toMap(), second = b.toMap();
            const auto comparison = QString::localeAwareCompare(first.value(QStringLiteral("name")).toString(),
                                                                second.value(QStringLiteral("name")).toString());
            return comparison
                ? comparison < 0
                : first.value(QStringLiteral("id")).toString() < second.value(QStringLiteral("id")).toString();
        });
        recording.sort();
        microphone.attention = microphone.active;
        microphone.summary = !connected ? PhosphorI18n::tr("Sound service unavailable")
            : !source                   ? (microphone.active ? PhosphorI18n::tr("Recording with no default input")
                                                             : PhosphorI18n::tr("No input device"))
            : microphone.off ? (microphone.active ? PhosphorI18n::tr("Default microphone muted. Recording is active.")
                                                  : PhosphorI18n::tr("Microphone muted"))
            : microphone.active
            ? (recording.isEmpty() ? PhosphorI18n::tr("Microphone in use")
                                   : PhosphorI18n::tr("In use by %1").arg(recording.join(QStringLiteral(", "))))
            : PhosphorI18n::tr("Microphone idle");
        result.states.insert(QStringLiteral("microphone"), microphone);
    }
    void startAudioRequest()
    {
        m_audioError.clear();
        m_deadline.start();
        update();
    }
    void clearAudioRequests()
    {
        m_requestedVolume.reset();
        m_requestedMute.reset();
        m_requestedMicMute.reset();
        m_requestedOutput.clear();
        m_writeSink.clear();
        m_writeSource.clear();
        m_deadline.stop();
    }
    void update()
    {
        QStringList activePaths;
        for (auto* device : m_network->devices()) {
            const auto path = device->activeAccessPointPath();
            if (path.isEmpty() || path == QLatin1String("/"))
                continue;
            activePaths.append(path);
            if (!m_accessPoints.contains(path)) {
                auto* ap = new AccessPoint(path, this);
                m_accessPoints.insert(path, ap);
                connect(ap, &AccessPoint::ssidChanged, this, &LiveStatusIconsSource::update);
            }
        }
        for (auto it = m_accessPoints.begin(); it != m_accessPoints.end();) {
            if (!activePaths.contains(it.key())) {
                if (it.value())
                    it.value()->deleteLater();
                it = m_accessPoints.erase(it);
            } else
                ++it;
        }
        if (m_audio && !m_audio->isConnected()) {
            m_audioConnected = false;
            if (!m_reconnect.isActive())
                m_reconnect.start(m_retryDelay);
            clearAudioRequests();
        } else if (m_audio) {
            if (!m_audioConnected)
                m_audioError.clear();
            m_audioConnected = true;
            m_reconnect.stop();
            m_retryDelay = 2000;
            const auto nodes = m_audio->connection()->nodes();
            if (((m_requestedVolume || m_requestedMute) && (!m_writeSink || !nodes.contains(m_writeSink)))
                || (m_requestedMicMute && (!m_writeSource || !nodes.contains(m_writeSource)))) {
                clearAudioRequests();
                m_audioError = PhosphorI18n::tr("The audio device disconnected before the change completed.");
            }
            if (m_requestedVolume && m_writeSink && volumeOf(m_writeSink) == *m_requestedVolume)
                m_requestedVolume.reset();
            if (m_requestedMute && m_writeSink && m_writeSink->muted() == *m_requestedMute)
                m_requestedMute.reset();
            if (m_requestedMicMute && m_writeSource && m_writeSource->muted() == *m_requestedMicMute)
                m_requestedMicMute.reset();
            if (!m_requestedOutput.isEmpty() && m_audio->defaultSink()
                && m_audio->defaultSink()->name() == m_requestedOutput)
                m_requestedOutput.clear();
            if (!m_requestedVolume && !m_requestedMute && !m_requestedMicMute && m_requestedOutput.isEmpty())
                m_deadline.stop();
        } else {
            clearAudioRequests();
            m_reconnect.stop();
        }
        const auto current = snapshot();
        if (!m_lastSnapshot || current != *m_lastSnapshot) {
            m_lastSnapshot = current;
            Q_EMIT changed();
        }
    }

    QPointer<QuickSettingsController> m_quick;
    QPointer<AirplaneController> m_airplane;
    QPointer<NotificationController> m_notifications;
    NetworkHost* m_network;
    BluetoothHost* m_bluetooth;
    UPowerHost* m_battery;
    QPointer<UPowerDevice> m_display;
    QPointer<PipeWireHost> m_audio;
    QHash<QString, QPointer<AccessPoint>> m_accessPoints;
    QPointer<PwNode> m_writeSink, m_writeSource;
    std::optional<int> m_requestedVolume;
    std::optional<bool> m_requestedMute, m_requestedMicMute;
    QString m_requestedOutput, m_audioError;
    QTimer m_deadline, m_reconnect;
    int m_retryDelay = 2000;
    bool m_audioConnected = false;
    std::optional<StatusIconsSnapshot> m_lastSnapshot;
};
}

StatusIconsSource* createStatusIconsSource(QuickSettingsController* quickSettings, AirplaneController* airplane,
                                           NotificationController* notifications, QObject* parent)
{
    return new LiveStatusIconsSource(quickSettings, airplane, notifications, parent);
}

StatusIconsController::StatusIconsController(PhosphorTheme::AppearanceStore* store,
                                             QuickSettingsController* quickSettings, AirplaneController* airplane,
                                             NotificationController* notifications, QObject* parent)
    : StatusIconsController(store, static_cast<StatusIconsSource*>(nullptr), parent)
{
    attachSource(createStatusIconsSource(quickSettings, airplane, notifications, this));
}
}
