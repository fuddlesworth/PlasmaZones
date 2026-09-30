// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

namespace PhosphorTheme {
class AppearanceStore;
}
namespace PhosphorServicePipeWire {
class PipeWireHost;
}
namespace PhosphorShellApp {
class QuickSettingsController;
class AirplaneController;
class NotificationController;
class StatusIconsSource;

class StatusIconsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList icons READ icons NOTIFY changed)
    Q_PROPERTY(QVariantList shownIcons READ shownIcons NOTIFY changed)
    Q_PROPERTY(QVariantList overflowIcons READ overflowIcons NOTIFY changed)
    Q_PROPERTY(int maxVisible READ maxVisible NOTIFY changed)
    Q_PROPERTY(bool showBatteryPercentage READ showBatteryPercentage NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY changed)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(QString powerProfile READ powerProfile NOTIFY changed)
    Q_PROPERTY(QStringList powerProfiles READ powerProfiles NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    StatusIconsController(PhosphorTheme::AppearanceStore* store, QuickSettingsController* quickSettings,
                          AirplaneController* airplane, NotificationController* notifications,
                          QObject* parent = nullptr);
    StatusIconsController(PhosphorTheme::AppearanceStore* store, StatusIconsSource* source, QObject* parent = nullptr);
    ~StatusIconsController() override;
    QVariantList icons() const;
    QVariantList shownIcons() const;
    QVariantList overflowIcons() const;
    int maxVisible() const;
    bool showBatteryPercentage() const;
    int volume() const;
    bool muted() const;
    bool audioAvailable() const;
    QVariantList outputs() const;
    QString powerProfile() const;
    QStringList powerProfiles() const;
    QString error() const;
    Q_INVOKABLE bool setVisibility(const QString& id, const QString& policy);
    Q_INVOKABLE bool moveIcon(const QString& id, int delta);
    Q_INVOKABLE bool moveBefore(const QString& id, const QString& beforeId);
    Q_INVOKABLE bool resetIcons();
    Q_INVOKABLE bool setMaxVisible(int value);
    Q_INVOKABLE bool setShowBatteryPercentage(bool value);
    Q_INVOKABLE bool toggle(const QString& id);
    Q_INVOKABLE bool setVolume(int percent);
    Q_INVOKABLE bool selectOutput(const QString& id);
    Q_INVOKABLE bool setPowerProfile(const QString& id);
    void setAudioHost(PhosphorServicePipeWire::PipeWireHost* host);
Q_SIGNALS:
    void changed();

private:
    void attachSource(StatusIconsSource* source);
    void rebuild();
    bool save(const QVariantMap& patch);
    QStringList order() const;
    QPointer<PhosphorTheme::AppearanceStore> m_store;
    QPointer<StatusIconsSource> m_source;
    QVariantMap m_data;
    QString m_error;
};
}
