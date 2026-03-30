// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <QString>

namespace PlasmaZones {

/**
 * @brief About sub-KCM — version info, links, settings launcher
 */
class KCMAbout : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)

public:
    KCMAbout(QObject* parent, const KPluginMetaData& data);
    ~KCMAbout() override;

    QString currentVersion() const;

    Q_INVOKABLE void openSettings(const QString& page = QString());
};

} // namespace PlasmaZones
