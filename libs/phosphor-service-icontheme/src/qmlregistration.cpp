// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIconTheme/QmlRegistration.h>

#include <PhosphorServiceIconTheme/IconImageProvider.h>
#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include <QQmlEngine>

namespace PhosphorServiceIconTheme {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.IconTheme";
constexpr const char* kImageProviderHost = "phosphor-service-icontheme";
} // namespace

void registerQmlTypes()
{
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
        qWarning("PhosphorServiceIconTheme::installImageProvider called with null engine");
        return;
    }
    // QQmlEngine takes ownership of the provider — passing a raw new
    // is the documented pattern. Repeated installs on the same engine
    // would clash (Qt warns + drops the new one), but we only call
    // this once per engine construction.
    engine->addImageProvider(QString::fromLatin1(kImageProviderHost), new IconImageProvider());
    qInfo("PhosphorServiceIconTheme: image provider mounted at image://%s/ on engine %p", kImageProviderHost,
          static_cast<void*>(engine));
}

const char* imageProviderUrlHost()
{
    return kImageProviderHost;
}

} // namespace PhosphorServiceIconTheme
