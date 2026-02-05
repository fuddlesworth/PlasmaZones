// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "updatechecker.h"
#include "version.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QDateTime>
#include <QRegularExpression>

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

QString UpdateChecker::stripVersionPrefix(const QString& version)
{
    return version.startsWith(QLatin1Char('v')) ? version.mid(1) : version;
}

void UpdateChecker::parseVersionComponent(const QString& part, int& number, QString& preRelease)
{
    // Handle versions like "4-beta", "0-rc1", "3-alpha2"
    static const QRegularExpression preReleasePattern(QStringLiteral("^(\\d+)(?:-(.+))?$"));
    QRegularExpressionMatch match = preReleasePattern.match(part);

    if (match.hasMatch()) {
        number = match.captured(1).toInt();
        preRelease = match.captured(2);  // Empty if no pre-release suffix
    } else {
        number = part.toInt();
        preRelease.clear();
    }
}

int UpdateChecker::compareVersions(const QString& v1, const QString& v2)
{
    QString ver1 = stripVersionPrefix(v1);
    QString ver2 = stripVersionPrefix(v2);

    QStringList parts1 = ver1.split(QLatin1Char('.'));
    QStringList parts2 = ver2.split(QLatin1Char('.'));

    // Compare each version component
    int maxParts = qMax(parts1.size(), parts2.size());
    for (int i = 0; i < maxParts; ++i) {
        int num1 = 0, num2 = 0;
        QString pre1, pre2;

        if (i < parts1.size()) {
            parseVersionComponent(parts1[i], num1, pre1);
        }
        if (i < parts2.size()) {
            parseVersionComponent(parts2[i], num2, pre2);
        }

        // Compare numeric parts first
        if (num1 < num2) return -1;
        if (num1 > num2) return 1;

        // If numbers equal, compare pre-release (no pre-release > pre-release)
        // e.g., "1.4.0" > "1.4.0-beta"
        if (pre1.isEmpty() && !pre2.isEmpty()) return 1;   // v1 is release, v2 is pre-release
        if (!pre1.isEmpty() && pre2.isEmpty()) return -1;  // v1 is pre-release, v2 is release
        if (!pre1.isEmpty() && !pre2.isEmpty()) {
            int cmp = pre1.compare(pre2);
            if (cmp != 0) return cmp < 0 ? -1 : 1;
        }
    }

    return 0; // Equal
}

void UpdateChecker::checkForUpdates()
{
    if (m_checking) {
        qCDebug(lcUpdateChecker) << "Update check already in progress";
        return;
    }

    // Rate limiting: don't check more than once per CHECK_INTERVAL_MS
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastCheckTime > 0 && (now - m_lastCheckTime) < CHECK_INTERVAL_MS) {
        qCDebug(lcUpdateChecker) << "Rate limited: last check was"
                                  << (now - m_lastCheckTime) / 1000 << "seconds ago";
        return;
    }

    m_checking = true;
    m_lastCheckTime = now;
    m_errorMessage.clear();
    Q_EMIT checkingChanged();
    Q_EMIT errorMessageChanged();

    qCDebug(lcUpdateChecker) << "Checking for updates at" << GITHUB_API_URL;

    QNetworkRequest request{QUrl(GITHUB_API_URL)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("PlasmaZones/%1").arg(VERSION_STRING));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);

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
    QString tagName = release[QLatin1String("tag_name")].toString();
    QString latestVersion = stripVersionPrefix(tagName);

    if (latestVersion.isEmpty()) {
        m_errorMessage = tr("No version found in release data");
        qCWarning(lcUpdateChecker) << m_errorMessage;
        Q_EMIT errorMessageChanged();
        Q_EMIT checkFinished(false);
        return;
    }

    m_latestVersion = latestVersion;
    m_releaseUrl = release[QLatin1String("html_url")].toString();
    m_releaseNotes = release[QLatin1String("body")].toString();

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
