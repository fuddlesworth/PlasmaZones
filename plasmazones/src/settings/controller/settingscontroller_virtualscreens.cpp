// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Virtual-screen lifecycle methods for SettingsController. All methods here
// are members of PlasmaZones::SettingsController. Same class, separate
// translation unit, no API change.
//
// Group covers:
//   * getVirtualScreenConfig — the one direct daemon read.
//   * Staged variants (stage*, hasStaged*, getStaged*) that live on
//     m_staging rather than going through the daemon directly. Every WRITE
//     goes through these; the flush to the daemon is StagingService's job,
//     driven by save().

#include "settingscontroller.h"

#include "settings/utils/dbusutils.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

QVariantList SettingsController::getVirtualScreenConfig(const QString& physicalScreenId) const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                QStringLiteral("getVirtualScreenConfig"), {physicalScreenId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString json = reply.arguments().first().toString();
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonArray screensArr = root.value(QLatin1String("screens")).toArray();
            QVariantList result;
            for (const auto& entry : screensArr) {
                QJsonObject screenObj = entry.toObject();
                QJsonObject regionObj = screenObj.value(QLatin1String("region")).toObject();
                QVariantMap screen;
                screen[QStringLiteral("displayName")] = screenObj.value(QLatin1String("displayName")).toString();
                screen[QStringLiteral("x")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::X).toDouble();
                screen[QStringLiteral("y")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Y).toDouble();
                screen[QStringLiteral("width")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Width).toDouble();
                screen[QStringLiteral("height")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Height).toDouble();
                screen[QStringLiteral("index")] = screenObj.value(QLatin1String("index")).toInt();
                result.append(screen);
            }
            return result;
        }
    }
    return {};
}

void SettingsController::stageVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    m_staging.stageVirtualScreenConfig(physicalScreenId, screens);
    setNeedsSave(true);
}

void SettingsController::stageVirtualScreenRemoval(const QString& physicalScreenId)
{
    m_staging.stageVirtualScreenRemoval(physicalScreenId);
    setNeedsSave(true);
}

bool SettingsController::hasUnsavedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_staging.hasUnsavedVirtualScreenConfig(physicalScreenId);
}

QVariantList SettingsController::getStagedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_staging.stagedVirtualScreenConfig(physicalScreenId);
}

} // namespace PlasmaZones
