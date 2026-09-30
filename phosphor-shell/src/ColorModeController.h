// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class QDBusVariant;
namespace PhosphorTheme {
class AppearanceStore;
}
namespace PhosphorShellApp {

class ColorModeController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(bool systemAvailable READ systemAvailable NOTIFY changed)
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY changed)
    Q_PROPERTY(bool effectiveDark READ effectiveDark NOTIFY changed)
    Q_PROPERTY(bool previewActive READ previewActive NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit ColorModeController(PhosphorTheme::AppearanceStore* store, QObject* parent = nullptr);
    ColorModeController(PhosphorTheme::AppearanceStore* store, const QDBusConnection& bus, const QString& service,
                        QObject* parent = nullptr);
    QString mode() const;
    bool systemAvailable() const
    {
        return m_available;
    }
    bool systemDark() const
    {
        return m_systemDark;
    }
    bool effectiveDark() const;
    bool previewActive() const;
    QString error() const;
    Q_INVOKABLE bool setMode(const QString& mode);
    Q_INVOKABLE void refresh();
Q_SIGNALS:
    void changed();
private Q_SLOTS:
    void settingChanged(const QString& group, const QString& key, const QDBusVariant& value);

private:
    void notifyChanged();
    void acceptScheme(const QVariant& value);
    void syncAppearance();
    void setUnavailable(const QString& error);
    QPointer<PhosphorTheme::AppearanceStore> m_store;
    QDBusConnection m_bus;
    QString m_service;
    QString m_error;
    bool m_available = false;
    bool m_systemDark = false;
    bool m_syncing = false;
    quint64 m_generation = 0;
    QVariantList m_snapshot;
};
}
