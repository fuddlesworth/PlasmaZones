// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <QString>
#include <memory>

namespace PlasmaZones {

class DaemonController;
class UpdateChecker;

/**
 * @brief About sub-KCM — daemon toggle, version info, update checks, links, credits
 */
class KCMAbout : public KQuickConfigModule
{
    Q_OBJECT

    // Daemon status
    Q_PROPERTY(bool daemonRunning READ isDaemonRunning NOTIFY daemonRunningChanged)
    Q_PROPERTY(bool daemonEnabled READ isDaemonEnabled WRITE setDaemonEnabled NOTIFY daemonEnabledChanged)

    // Update checker
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool checkingForUpdates READ checkingForUpdates NOTIFY checkingForUpdatesChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString dismissedUpdateVersion READ dismissedUpdateVersion WRITE setDismissedUpdateVersion NOTIFY
                   dismissedUpdateVersionChanged)

public:
    KCMAbout(QObject* parent, const KPluginMetaData& data);
    ~KCMAbout() override;

    // Daemon
    bool isDaemonRunning() const;
    bool isDaemonEnabled() const;
    void setDaemonEnabled(bool enabled);

    // Update checker
    QString currentVersion() const;
    bool checkingForUpdates() const;
    bool updateAvailable() const;
    QString latestVersion() const;
    QString dismissedUpdateVersion() const;
    void setDismissedUpdateVersion(const QString& version);

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void openReleaseUrl();
    Q_INVOKABLE void openSettings();

Q_SIGNALS:
    void daemonRunningChanged();
    void daemonEnabledChanged();
    void checkingForUpdatesChanged();
    void updateAvailableChanged();
    void latestVersionChanged();
    void dismissedUpdateVersionChanged();
    void releaseUrlChanged();

private:
    std::unique_ptr<DaemonController> m_daemonController;
    UpdateChecker* m_updateChecker = nullptr;
    QString m_dismissedUpdateVersion;
};

} // namespace PlasmaZones
