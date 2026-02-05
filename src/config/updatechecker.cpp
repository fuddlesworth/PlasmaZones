// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "updatechecker.h"
#include "version.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUpdateChecker, "plasmazones.updatechecker", QtInfoMsg)

namespace PlasmaZones {

namespace {
const QString GITHUB_API_URL = QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(GITHUB_REPO);
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &UpdateChecker::onRequestFinished);
}

QString UpdateChecker::currentVersion()
{
    return VERSION_STRING;
}

int UpdateChecker::compareVersions(const QString& v1, const QString& v2)
{
    // Remove 'v' prefix if present
    QString ver1 = v1.startsWith(QLatin1Char('v')) ? v1.mid(1) : v1;
    QString ver2 = v2.startsWith(QLatin1Char('v')) ? v2.mid(1) : v2;

    QStringList parts1 = ver1.split(QLatin1Char('.'));
    QStringList parts2 = ver2.split(QLatin1Char('.'));

    // Compare each numeric component
    int maxParts = qMax(parts1.size(), parts2.size());
    for (int i = 0; i < maxParts; ++i) {
        int num1 = (i < parts1.size()) ? parts1[i].toInt() : 0;
        int num2 = (i < parts2.size()) ? parts2[i].toInt() : 0;

        if (num1 < num2) return -1;
        if (num1 > num2) return 1;
    }

    return 0; // Equal
}

void UpdateChecker::checkForUpdates()
{
    if (m_checking) {
        qCDebug(lcUpdateChecker) << "Update check already in progress";
        return;
    }

    m_checking = true;
    m_errorMessage.clear();
    Q_EMIT checkingChanged();
    Q_EMIT errorMessageChanged();

    qCDebug(lcUpdateChecker) << "Checking for updates at" << GITHUB_API_URL;

    QNetworkRequest request{QUrl(GITHUB_API_URL)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("PlasmaZones/%1").arg(VERSION_STRING));
    request.setRawHeader("Accept", "application/vnd.github+json");

    m_networkManager->get(request);
}

void UpdateChecker::onRequestFinished(QNetworkReply* reply)
{
    m_checking = false;
    Q_EMIT checkingChanged();

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_errorMessage = reply->errorString();
        qCWarning(lcUpdateChecker) << "Update check failed:" << m_errorMessage;
        Q_EMIT errorMessageChanged();
        Q_EMIT checkFinished(false);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        m_errorMessage = tr("Failed to parse response: %1").arg(parseError.errorString());
        qCWarning(lcUpdateChecker) << m_errorMessage;
        Q_EMIT errorMessageChanged();
        Q_EMIT checkFinished(false);
        return;
    }

    QJsonObject release = doc.object();

    // Extract version from tag_name (e.g., "v1.4.0" -> "1.4.0")
    QString tagName = release[QStringLiteral("tag_name")].toString();
    QString latestVersion = tagName.startsWith(QLatin1Char('v')) ? tagName.mid(1) : tagName;

    if (latestVersion.isEmpty()) {
        m_errorMessage = tr("No version found in release data");
        qCWarning(lcUpdateChecker) << m_errorMessage;
        Q_EMIT errorMessageChanged();
        Q_EMIT checkFinished(false);
        return;
    }

    m_latestVersion = latestVersion;
    m_releaseUrl = release[QStringLiteral("html_url")].toString();
    m_releaseNotes = release[QStringLiteral("body")].toString();

    Q_EMIT latestVersionChanged();
    Q_EMIT releaseUrlChanged();
    Q_EMIT releaseNotesChanged();

    // Check if update is available
    bool wasUpdateAvailable = m_updateAvailable;
    m_updateAvailable = (compareVersions(VERSION_STRING, m_latestVersion) < 0);

    qCInfo(lcUpdateChecker) << "Current version:" << VERSION_STRING
                            << "Latest version:" << m_latestVersion
                            << "Update available:" << m_updateAvailable;

    if (m_updateAvailable != wasUpdateAvailable) {
        Q_EMIT updateAvailableChanged();
    }

    Q_EMIT checkFinished(true);
}

} // namespace PlasmaZones
