// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include "plasmazones_export.h"

namespace PlasmaZones {

/**
 * @brief Checks GitHub releases for available updates
 *
 * Queries the GitHub API to check if a newer version is available.
 * Respects rate limiting and caches results to avoid excessive requests.
 */
class PLASMAZONES_EXPORT UpdateChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY releaseUrlChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseNotesChanged)
    Q_PROPERTY(bool checking READ isChecking NOTIFY checkingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override = default;

    bool updateAvailable() const { return m_updateAvailable; }
    QString latestVersion() const { return m_latestVersion; }
    QString releaseUrl() const { return m_releaseUrl; }
    QString releaseNotes() const { return m_releaseNotes; }
    bool isChecking() const { return m_checking; }
    QString errorMessage() const { return m_errorMessage; }

    /// Current installed version (from compile-time constant)
    static QString currentVersion();

    /// Compare two version strings (returns -1, 0, or 1)
    static int compareVersions(const QString& v1, const QString& v2);

public Q_SLOTS:
    /// Start checking for updates
    void checkForUpdates();

Q_SIGNALS:
    void updateAvailableChanged();
    void latestVersionChanged();
    void releaseUrlChanged();
    void releaseNotesChanged();
    void checkingChanged();
    void errorMessageChanged();

    /// Emitted when check completes (success or failure)
    void checkFinished(bool success);

private Q_SLOTS:
    void onRequestFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_networkManager = nullptr;
    bool m_updateAvailable = false;
    bool m_checking = false;
    QString m_latestVersion;
    QString m_releaseUrl;
    QString m_releaseNotes;
    QString m_errorMessage;
};

} // namespace PlasmaZones
