// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace PhosphorServicePipeWire {
class PipeWireHost;
}

namespace PhosphorShellApp {
class QuickSettingsController;
class AirplaneController;
class NotificationController;

struct StatusIconState
{
    QString summary;
    QString iconName;
    QString error;
    bool available = false;
    bool active = false;
    bool off = false;
    bool attention = false;
    bool pending = false;
    int percent = -1;
    bool operator==(const StatusIconState&) const = default;
};

struct StatusIconsSnapshot
{
    QHash<QString, StatusIconState> states;
    QVariantList outputs;
    QString powerProfile;
    QStringList powerProfiles;
    int volume = 0;
    bool muted = false;
    bool audioAvailable = false;
    bool operator==(const StatusIconsSnapshot&) const = default;
};

// Read snapshots and explicit actions form the boundary between indicator
// preferences and system services. Tests inject a source without connecting
// to the host's D-Bus or PipeWire services.
class StatusIconsSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual StatusIconsSnapshot snapshot() const = 0;
    virtual bool toggle(const QString& id) = 0;
    virtual bool setVolume(int percent) = 0;
    virtual bool selectOutput(const QString& id) = 0;
    virtual bool setPowerProfile(const QString& id) = 0;
    virtual void setAudioHost(PhosphorServicePipeWire::PipeWireHost* host) = 0;
Q_SIGNALS:
    void changed();
};

StatusIconsSource* createStatusIconsSource(QuickSettingsController* quickSettings, AirplaneController* airplane,
                                           NotificationController* notifications, QObject* parent);
}
