// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Virtual-screen lifecycle methods for SettingsController. Extracted
// from settingscontroller_session.cpp to keep that TU under the
// 1000-line guideline (CLAUDE.md). All methods here are members of
// PlasmaZones::SettingsController — same class, separate translation
// unit, no API change.
//
// Group covers:
//   * getVirtualScreenConfig / applyVirtualScreenConfig
//   * removeVirtualScreenConfig
//   * Staged variants (stage*, hasStaged*, getStaged*) that live on
//     m_staging rather than going through the daemon directly.

#include "settingscontroller.h"

#include "../core/logging.h"
#include "virtualscreenutils.h"

#include "dbusutils.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusMessage>
#include <QLoggingCategory>

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

void SettingsController::applyVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    QJsonObject root;
    root[QLatin1String("physicalScreenId")] = physicalScreenId;

    QJsonArray screensArr;
    for (int i = 0; i < screens.size(); ++i) {
        PhosphorScreens::VirtualScreenDef def =
            VirtualScreenUtils::variantMapToVirtualScreenDef(screens[i].toMap(), physicalScreenId, i);
        if (!def.isValid()) {
            qCWarning(lcConfig) << "Skipping invalid virtual screen def for" << physicalScreenId << "index" << i
                                << "region:" << def.region;
            continue;
        }
        QJsonObject screenObj;
        screenObj[QLatin1String("index")] = def.index;
        screenObj[QLatin1String("displayName")] = def.displayName;
        screenObj[QLatin1String("region")] = QJsonObject{{::PhosphorZones::ZoneJsonKeys::X, def.region.x()},
                                                         {::PhosphorZones::ZoneJsonKeys::Y, def.region.y()},
                                                         {::PhosphorZones::ZoneJsonKeys::Width, def.region.width()},
                                                         {::PhosphorZones::ZoneJsonKeys::Height, def.region.height()}};
        screensArr.append(screenObj);
    }
    root[QLatin1String("screens")] = screensArr;

    QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    const QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                               QStringLiteral("setVirtualScreenConfig"), {physicalScreenId, json});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        // Surface the daemon rejection — Pass-4 hardened the layout
        // setters against silent-error swallowing but applyVirtualScreen
        // Config and its removeVirtualScreenConfig forwarder are still
        // Q_INVOKABLE reachable from QML / D-Bus and would otherwise
        // leave the user with no feedback on a failed save.
        qCWarning(lcConfig) << "applyVirtualScreenConfig failed for" << physicalScreenId << ":" << reply.errorMessage();
        Q_EMIT virtualScreenConfigFailed(physicalScreenId, reply.errorMessage());
    }
}

void SettingsController::removeVirtualScreenConfig(const QString& physicalScreenId)
{
    applyVirtualScreenConfig(physicalScreenId, {});
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
