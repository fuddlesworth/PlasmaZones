// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <PhosphorConfig/IBackend.h>
#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>
#include <memory>

namespace PhosphorShellApp {
// Applies the shell's desktop style for its lifetime. The journal survives a
// crash; restoration only touches values that are still the ones we applied.
class DesktopStyleController final : public QObject
{
    Q_OBJECT
public:
    explicit DesktopStyleController(QObject* parent = nullptr);
    DesktopStyleController(QString kwinPath, QString journalPath, std::unique_ptr<PhosphorConfig::IBackend> backend,
                           bool available, QDBusConnection bus, QObject* parent = nullptr);
    ~DesktopStyleController() override;
    void apply(const QVariantMap& settings);
    bool restore();
    static QVariantMap gaps(const QVariantMap& settings);

private:
    bool saveJournal();
    void reconfigure();
    void reloadPlacement();
    QString m_kwinPath;
    QString m_journalPath;
    std::unique_ptr<PhosphorConfig::IBackend> m_backend;
    QDBusConnection m_bus;
    bool m_available;
    QVariantMap m_journal;
    QVariantMap m_settings;
};
}
