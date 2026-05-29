// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/QmlRegistration.h>

#include <PhosphorServices/DBusMenuModel.h>
#include <PhosphorServices/StatusNotifierHost.h>
#include <PhosphorServices/StatusNotifierItem.h>
#include <PhosphorServices/StatusNotifierItemModel.h>

#include <QQmlEngine>

namespace PhosphorServices {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Services";
} // namespace

void registerQmlTypes()
{
    // Instantiable types. StatusNotifierHost is the main entry point —
    // QML constructs one, hands its pointer to a model, and binds the
    // model to a Repeater.
    qmlRegisterType<StatusNotifierHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "StatusNotifierHost");
    qmlRegisterType<StatusNotifierItemModel>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                             "StatusNotifierItemModel");
    qmlRegisterType<DBusMenuModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "DBusMenuModel");

    // Pointer-receivable types — exposed as Q_PROPERTY values from the
    // host / model, never directly constructed in QML, but their
    // metatype needs to be known so QML can read iconImage etc. off
    // them.
    qmlRegisterUncreatableType<StatusNotifierItem>(
        kModule, kModuleVersionMajor, kModuleVersionMinor, "StatusNotifierItem",
        QStringLiteral("StatusNotifierItem is owned by StatusNotifierHost — bind via the model"));

    // MPRIS now lives in phosphor-service-mpris; consumers import
    // `Phosphor.Service.Mpris 1.0` separately. UPower lives in
    // phosphor-service-upower (`Phosphor.Service.UPower 1.0`).
    // The icon-theme resolver + image provider now live in
    // phosphor-service-icontheme (`Phosphor.Service.IconTheme 1.0`);
    // the host application calls
    // `PhosphorServiceIconTheme::registerQmlTypes()` +
    // `installImageProvider()` directly. Don't re-register any of
    // these here.
}

} // namespace PhosphorServices
