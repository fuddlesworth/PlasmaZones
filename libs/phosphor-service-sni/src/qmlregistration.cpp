// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/QmlRegistration.h>

#include <PhosphorServiceSni/DBusMenuModel.h>
#include <PhosphorServiceSni/StatusNotifierHost.h>
#include <PhosphorServiceSni/StatusNotifierItem.h>
#include <PhosphorServiceSni/StatusNotifierItemModel.h>

#include <QQmlEngine>

namespace PhosphorServiceSni {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Sni";
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

    // Pointer-receivable type — exposed as a Q_PROPERTY value from
    // the host / model, never directly constructed in QML, but the
    // metatype needs to be known so QML can read iconUrl, tooltip,
    // etc. off it.
    qmlRegisterUncreatableType<StatusNotifierItem>(
        kModule, kModuleVersionMajor, kModuleVersionMinor, "StatusNotifierItem",
        QStringLiteral("StatusNotifierItem is owned by StatusNotifierHost — bind via the model"));
}

} // namespace PhosphorServiceSni
