// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-toast-demo entry point.
//
// QGuiApplication + QQmlApplicationEngine + a NotificationServer
// (phosphor-service-notifications, Phase 2.5). When this process owns the
// org.freedesktop.Notifications name, `notify-send` raises a toast; when
// another daemon owns it, the in-window buttons still drive the toast
// stack. The server is exposed to QML so Main.qml binds its
// notificationAdded signal into the ToastHost.

#include "NotificationImageProvider.h"

#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationServer.h>
#include <PhosphorServiceNotifications/QmlRegistration.h>

#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-toast-demo"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Register the notification types (Notification etc.) so the QML
    // signal handler can read the delivered notification's properties.
    PhosphorServiceNotifications::registerQmlTypes();

    // Acquire the session-bus notification name (inert if another daemon
    // owns it; nameAcquired reports which case we're in).
    PhosphorServiceNotifications::NotificationServer notifier;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("notifier"), &notifier);

    // Serve decoded notification images (the freedesktop image-data hint) to
    // QML under image://notification/<id>. The engine takes ownership of the
    // provider. Each cache/drop connection names a context object (the engine
    // for add/close, the provider for the per-notification changed() re-cache);
    // destroying that context object on engine teardown severs the connection,
    // so no callback fires into the destroyed provider even though notifier
    // (and its Notification objects) outlives the engine.
    auto* imageProvider = new PhosphorToastDemo::NotificationImageProvider;
    engine.addImageProvider(QStringLiteral("notification"), imageProvider);
    QObject::connect(&notifier, &PhosphorServiceNotifications::NotificationServer::notificationAdded, &engine,
                     [imageProvider](PhosphorServiceNotifications::Notification* notification) {
                         if (notification == nullptr) {
                             return;
                         }
                         const auto cache = [imageProvider, notification] {
                             // Mirror the notification's image state: cache it
                             // when present, drop it when a changed() update
                             // cleared a previously set image.
                             if (notification->hasImage()) {
                                 imageProvider->cacheImage(notification->id(), notification->image());
                             } else {
                                 imageProvider->dropImage(notification->id());
                             }
                         };
                         cache();
                         // A replaces-id update reports through changed(), not
                         // notificationAdded, so re-cache on it to keep a
                         // swapped image current.
                         QObject::connect(notification, &PhosphorServiceNotifications::Notification::changed,
                                          imageProvider, cache);
                     });
    QObject::connect(&notifier, &PhosphorServiceNotifications::NotificationServer::NotificationClosed, &engine,
                     [imageProvider](uint id, uint) {
                         imageProvider->dropImage(id);
                     });

    engine.loadFromModule(QStringLiteral("Phosphor.ToastDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
