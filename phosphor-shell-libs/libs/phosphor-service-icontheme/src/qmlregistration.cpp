// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIconTheme/QmlRegistration.h>

#include <PhosphorServiceIconTheme/IconImageProvider.h>
#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QQmlEngine>

#include <mutex>

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
    // qmlRegisterSingletonInstance is process-global; a repeat call
    // with a different instance pointer is undefined per Qt's docs.
    // The IconThemeResolver singleton is parented to QCoreApplication
    // and has process lifetime so the pointer is stable across calls,
    // but a hot-reloading shell that calls this from every engine
    // setup would still trigger Qt's duplicate-registration warning.
    // Guard with call_once so the function is safe to invoke per
    // engine, matching the sibling phosphor-service-sni pattern.
    if (!QCoreApplication::instance()) {
        qCWarning(lcIconThemeQml) << "registerQmlTypes called before QCoreApplication; refusing to register";
        return;
    }
    static std::once_flag once;
    std::call_once(once, [] {
        // Singleton resolver: exposes themeName + iconForName() to QML
        // shells that want to resolve their own theme icons (e.g. for an
        // application-launcher widget that needs the same XDG lookup as
        // the tray). IconThemeResolver::instance() parents the QObject
        // to QCoreApplication, so the singleton outlives every
        // QQmlEngine the shell may construct during hot reload.
        qmlRegisterSingletonInstance<IconThemeResolver>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                                        "IconThemeResolver", IconThemeResolver::instance());
    });
}

void installImageProvider(QQmlEngine* engine)
{
    if (!engine) {
        qCWarning(lcIconThemeQml) << "installImageProvider called with null engine";
        return;
    }
    // QQmlEngine takes ownership of the provider; the documented
    // pattern is to pass a raw `new`. Per Qt's contract,
    // addImageProvider replaces the existing provider for the same
    // host (delete-old + install-new), so re-installing on the same
    // engine is well-defined; shells still construct a fresh
    // QQmlEngine per hot reload, so re-install is the steady-state
    // pattern.
    engine->addImageProvider(QString::fromLatin1(kImageProviderHost), new IconImageProvider());
    // Pointer addresses leak ASLR layout into logs that may be
    // forwarded; log the URL host (the actionable identity) and gate
    // engine identity behind qCDebug for in-process correlation.
    qCInfo(lcIconThemeQml).nospace() << "image provider mounted at image://" << kImageProviderHost << "/";
    qCDebug(lcIconThemeQml).nospace() << "image provider engine=" << static_cast<void*>(engine);
}

const char* imageProviderUrlHost()
{
    return kImageProviderHost;
}

} // namespace PhosphorServiceIconTheme
