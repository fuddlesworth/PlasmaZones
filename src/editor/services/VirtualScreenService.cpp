// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VirtualScreenService.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace PlasmaZones;

VirtualScreenService::VirtualScreenService(QObject* parent)
    : QObject(parent)
{
}

VirtualScreenService::~VirtualScreenService()
{
    m_interface = nullptr;
}

QDBusInterface* VirtualScreenService::getInterface() const
{
    if (m_interface && m_interface->isValid()) {
        return m_interface;
    }

    if (m_interface) {
        m_interface->deleteLater();
        m_interface = nullptr;
    }

    m_interface = new QDBusInterface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                                     QString::fromLatin1(DBus::Interface::Screen), QDBusConnection::sessionBus(),
                                     const_cast<VirtualScreenService*>(this));

    if (!m_interface->isValid()) {
        qCWarning(lcDbus) << "VirtualScreenService: D-Bus connection failed";
        m_interface->deleteLater();
        m_interface = nullptr;
        return nullptr;
    }

    return m_interface;
}

QStringList VirtualScreenService::physicalScreens() const
{
    QDBusInterface* iface = getInterface();
    if (!iface) {
        return {};
    }

    QDBusReply<QStringList> reply = iface->call(QStringLiteral("getPhysicalScreens"));
    if (!reply.isValid()) {
        qCWarning(lcDbus) << "VirtualScreenService::physicalScreens: D-Bus call failed:" << reply.error().message();
        return {};
    }

    return reply.value();
}

QVariantMap VirtualScreenService::screenInfo(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return {};
    }

    QDBusInterface* iface = getInterface();
    if (!iface) {
        return {};
    }

    QDBusReply<QString> reply = iface->call(QStringLiteral("getScreenInfo"), screenId);
    if (!reply.isValid()) {
        qCWarning(lcDbus) << "VirtualScreenService::screenInfo: D-Bus call failed:" << reply.error().message();
        return {};
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return {};
    }

    return doc.object().toVariantMap();
}

QVariantList VirtualScreenService::virtualScreensFor(const QString& physicalScreenId) const
{
    if (physicalScreenId.isEmpty()) {
        return {};
    }

    QDBusInterface* iface = getInterface();
    if (!iface) {
        return {};
    }

    QDBusReply<QString> reply = iface->call(QStringLiteral("getVirtualScreenConfig"), physicalScreenId);
    if (!reply.isValid()) {
        qCWarning(lcDbus) << "VirtualScreenService::virtualScreensFor: D-Bus call failed:" << reply.error().message();
        return {};
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return {};
    }

    QJsonObject root = doc.object();
    QJsonArray screensArr = root[QLatin1String("screens")].toArray();

    QVariantList result;
    for (const auto& entry : screensArr) {
        QJsonObject screenObj = entry.toObject();
        QJsonObject regionObj = screenObj[QLatin1String("region")].toObject();

        QVariantMap screen;
        screen[QStringLiteral("displayName")] = screenObj[QLatin1String("displayName")].toString();
        screen[QStringLiteral("x")] = regionObj[JsonKeys::X].toDouble();
        screen[QStringLiteral("y")] = regionObj[JsonKeys::Y].toDouble();
        screen[QStringLiteral("width")] = regionObj[JsonKeys::Width].toDouble();
        screen[QStringLiteral("height")] = regionObj[JsonKeys::Height].toDouble();
        screen[QStringLiteral("index")] = screenObj[QLatin1String("index")].toInt();
        result.append(screen);
    }

    return result;
}

void VirtualScreenService::applyConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    if (physicalScreenId.isEmpty()) {
        Q_EMIT errorOccurred(QCoreApplication::translate("VirtualScreenService", "Physical screen ID cannot be empty"));
        return;
    }

    QDBusInterface* iface = getInterface();
    if (!iface) {
        Q_EMIT errorOccurred(
            QCoreApplication::translate("VirtualScreenService", "Cannot connect to PlasmaZones daemon"));
        return;
    }

    // Build the JSON config expected by the daemon
    QJsonObject root;
    root[QLatin1String("physicalScreenId")] = physicalScreenId;

    QJsonArray screensArr;
    for (int i = 0; i < screens.size(); ++i) {
        QVariantMap screenData = screens[i].toMap();
        QJsonObject screenObj;
        screenObj[QLatin1String("index")] = i;
        screenObj[QLatin1String("displayName")] = screenData.value(QStringLiteral("displayName")).toString();
        screenObj[QLatin1String("region")] =
            QJsonObject{{JsonKeys::X, screenData.value(QStringLiteral("x")).toDouble()},
                        {JsonKeys::Y, screenData.value(QStringLiteral("y")).toDouble()},
                        {JsonKeys::Width, screenData.value(QStringLiteral("width")).toDouble()},
                        {JsonKeys::Height, screenData.value(QStringLiteral("height")).toDouble()}};
        screensArr.append(screenObj);
    }
    root[QLatin1String("screens")] = screensArr;

    QString configJson = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));

    QDBusReply<void> reply = iface->call(QStringLiteral("setVirtualScreenConfig"), physicalScreenId, configJson);
    if (!reply.isValid()) {
        QString error = QCoreApplication::translate("VirtualScreenService", "Failed to apply config: %1")
                            .arg(reply.error().message());
        qCWarning(lcDbus) << "VirtualScreenService::applyConfig:" << error;
        Q_EMIT errorOccurred(error);
        return;
    }

    Q_EMIT configChanged(physicalScreenId);
}

void VirtualScreenService::removeConfig(const QString& physicalScreenId)
{
    // Removing config = setting an empty screens array
    applyConfig(physicalScreenId, {});
}

void VirtualScreenService::applyPreset(const QString& physicalScreenId, const QString& preset)
{
    QVariantList screens;

    if (preset == QLatin1String("50-50")) {
        screens = {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Left")},
                               {QStringLiteral("x"), 0.0},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.5},
                               {QStringLiteral("height"), 1.0}},
                   QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Right")},
                               {QStringLiteral("x"), 0.5},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.5},
                               {QStringLiteral("height"), 1.0}}};
    } else if (preset == QLatin1String("60-40")) {
        screens = {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Left")},
                               {QStringLiteral("x"), 0.0},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.6},
                               {QStringLiteral("height"), 1.0}},
                   QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Right")},
                               {QStringLiteral("x"), 0.6},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.4},
                               {QStringLiteral("height"), 1.0}}};
    } else if (preset == QLatin1String("33-33-33")) {
        screens = {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Left")},
                               {QStringLiteral("x"), 0.0},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.333},
                               {QStringLiteral("height"), 1.0}},
                   QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Center")},
                               {QStringLiteral("x"), 0.333},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.334},
                               {QStringLiteral("height"), 1.0}},
                   QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Right")},
                               {QStringLiteral("x"), 0.667},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.333},
                               {QStringLiteral("height"), 1.0}}};
    } else if (preset == QLatin1String("40-20-40")) {
        screens = {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Left")},
                               {QStringLiteral("x"), 0.0},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.4},
                               {QStringLiteral("height"), 1.0}},
                   QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Center")},
                               {QStringLiteral("x"), 0.4},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.2},
                               {QStringLiteral("height"), 1.0}},
                   QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Right")},
                               {QStringLiteral("x"), 0.6},
                               {QStringLiteral("y"), 0.0},
                               {QStringLiteral("width"), 0.4},
                               {QStringLiteral("height"), 1.0}}};
    } else {
        Q_EMIT errorOccurred(QCoreApplication::translate("VirtualScreenService", "Unknown preset: %1").arg(preset));
        return;
    }

    applyConfig(physicalScreenId, screens);
}
