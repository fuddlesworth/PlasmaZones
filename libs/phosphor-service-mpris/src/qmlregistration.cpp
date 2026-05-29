// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceMpris/QmlRegistration.h>

#include <PhosphorServiceMpris/MprisHost.h>
#include <PhosphorServiceMpris/MprisPlayer.h>
#include <PhosphorServiceMpris/MprisPlayerModel.h>

#include <QQmlEngine>

namespace PhosphorServiceMpris {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Mpris";
} // namespace

void registerQmlTypes()
{
    qmlRegisterType<MprisHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "MprisHost");
    qmlRegisterType<MprisPlayerModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "MprisPlayerModel");

    // MprisPlayer is vended by MprisHost / MprisPlayerModel; QML
    // shouldn't construct one directly. The uncreatable registration
    // still lets QML read its Q_PROPERTY values when accessed through
    // a model role or a host.players() entry.
    qmlRegisterUncreatableType<MprisPlayer>(
        kModule, kModuleVersionMajor, kModuleVersionMinor, "MprisPlayer",
        QStringLiteral("MprisPlayer is owned by MprisHost — bind via the host or model"));
}

} // namespace PhosphorServiceMpris
