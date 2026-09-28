// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <optional>

namespace PhosphorShellApp {
// Applies the shell's desktop style for its lifetime: the window gaps through
// the PlasmaZones daemon's Settings interface, and the window decoration
// through kwinrc. The journal survives a crash; restoration only touches
// values that are still the ones we applied.
//
// The gaps go over D-Bus rather than into config.json because the daemon owns
// that file and the tier rules keep this binary from linking its config code.
// A request made while the daemon is away is replayed when it registers.
class DesktopStyleController final : public QObject
{
    Q_OBJECT
public:
    explicit DesktopStyleController(QObject* parent = nullptr);
    DesktopStyleController(QString kwinPath, QString journalPath, QString service, bool available, QDBusConnection bus,
                           QObject* parent = nullptr);
    ~DesktopStyleController() override;
    void apply(const QVariantMap& settings);
    bool restore();
    /// Gap values keyed by the daemon's Settings property names.
    static QVariantMap gaps(const QVariantMap& settings);

private:
    bool saveJournal();
    void reconfigure();
    std::optional<QVariantMap> readGaps(const QStringList& keys) const;
    bool writeGaps(const QVariantMap& values) const;
    QString m_kwinPath;
    QString m_journalPath;
    QString m_service;
    QDBusConnection m_bus;
    bool m_available;
    QVariantMap m_journal;
    QVariantMap m_settings;
    QVariantMap m_requested;
};
}
