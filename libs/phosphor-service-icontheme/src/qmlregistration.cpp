// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIconTheme/QmlRegistration.h>

#include <PhosphorServiceIconTheme/IconImageProvider.h>
#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QQmlEngine>

Q_LOGGING_CATEGORY(lcIconThemeQml, "phosphor.service.icontheme.qml")

namespace PhosphorServiceIconTheme {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.IconTheme";
constexpr const char* kImageProviderHost = "phosphor-service-icontheme";
} // namespace

void registerQmlTypes()
{
    // Singleton resolver: exposes themeName + iconForName() to QML
    // shells that want to resolve their own theme icons (e.g. for an
    // application-launcher widget that needs the same XDG lookup as
    // the tray). IconThemeResolver::instance() parents the QObject
    // to QCoreApplication, so the singleton outlives every
    // QQmlEngine the shell may construct during hot reload.
    if (!QCoreApplication::instance()) {
        qCWarning(lcIconThemeQml) << "registerQmlTypes called before QCoreApplication; refusing to register";
        return;
    }
    qmlRegisterSingletonInstance<IconThemeResolver>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                                    "IconThemeResolver", IconThemeResolver::instance());
}

void installImageProvider(QQmlEngine* engine)
{
    if (!engine) {
        qCWarning(lcIconThemeQml) << "installImageProvider called with null engine";
        return;
    }
    // QQmlEngine takes ownership of the provider; the documented
    // pattern is to pass a raw `new`. Re-installing on the same
    // engine with the same host name would make Qt warn and drop the
    // new one, but shells construct a fresh QQmlEngine on every hot
    // reload so the per-engine mount is the expected lifecycle.
    engine->addImageProvider(QString::fromLatin1(kImageProviderHost), new IconImageProvider());
    qCInfo(lcIconThemeQml).nospace() << "image provider mounted at image://" << kImageProviderHost << "/ on engine "
                                     << static_cast<void*>(engine);
}

const char* imageProviderUrlHost()
{
    return kImageProviderHost;
}

} // namespace PhosphorServiceIconTheme
