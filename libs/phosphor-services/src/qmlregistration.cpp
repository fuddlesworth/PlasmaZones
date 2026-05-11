// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/QmlRegistration.h>

#include <PhosphorServices/DBusMenuModel.h>
#include <PhosphorServices/IconThemeResolver.h>
#include <PhosphorServices/StatusNotifierHost.h>
#include <PhosphorServices/StatusNotifierItem.h>
#include <PhosphorServices/StatusNotifierItemModel.h>

#include "iconimageprovider.h"

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

    // Singleton resolver — exposes themeName + iconForName() to QML
    // shells that want to resolve their own theme icons (e.g. for an
    // application-launcher widget that needs the same XDG lookup as
    // the tray).
    qmlRegisterSingletonInstance<IconThemeResolver>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                                    "IconThemeResolver", IconThemeResolver::instance());
}

void installImageProvider(QQmlEngine* engine)
{
    if (!engine) {
        return;
    }
    // QQmlEngine takes ownership of the provider — passing a raw new
    // is the documented pattern. Repeated installs on the same engine
    // would clash (Qt warns + drops the new one), but we only call
    // this once per engine construction.
    engine->addImageProvider(QStringLiteral("phosphor-services"), new IconImageProvider());
}

} // namespace PhosphorServices
