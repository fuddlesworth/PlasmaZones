// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../core/animationbootstrap.h"
#include "../core/logging.h"
#include "../core/single_instance_service.h"
#include "../core/translationloader.h"
#include "../config/configmigration.h"
#include "settingscontroller.h"
#include "settingslaunchcontroller.h"
#include "version.h"
#include "phosphor_i18n.h"
#include "phosphor_qml_i18n.h"
#include "searchcatalog.h"
#include "searchproviders.h"
#include "profilepagecontroller.h"
#include "profilestore.h"
#include "rulecontroller.h"
#include "rulemodel.h"

#include <PhosphorControl/SearchController.h>

#include "../core/constants.h"
#include "../daemon/rendering/zoneshaderitem.h"
#include <PhosphorProtocol/ServiceConstants.h>

#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/QtQuickClockManager.h>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QScopeGuard>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>
#include <vector>

// Import the static org.plasmazones.common QML module (same pattern as the
// daemon, src/daemon/main.cpp). The generated plugin carries the
// auto-generated qmldir, so shared types can never silently go missing again.
Q_IMPORT_QML_PLUGIN(org_plasmazones_commonPlugin)

namespace {

constexpr PlasmaZones::SingleInstanceIds kSettingsIds{PhosphorProtocol::Service::Apps::Settings::ServiceName,
                                                      PhosphorProtocol::Service::Apps::Settings::ObjectPath,
                                                      PhosphorProtocol::Service::Apps::Settings::Interface};

/// Try to forward a --page request to an already-running instance.
/// Returns true if a running instance exists (caller should exit).
///
/// Forwards only the page switch — does not try to raise the running window.
/// No Wayland workaround reliably convinces KWin to bring an already-mapped
/// xdg_toplevel to the front from a programmatic caller, so the user has to
/// focus the existing window themselves.
bool activateRunningInstance(const QString& address)
{
    if (!PlasmaZones::SingleInstanceService::isRunning(kSettingsIds))
        return false;

    if (!address.isEmpty()) {
        // The D-Bus method is still "setActivePage" (signature unchanged); the
        // running instance routes it through navigateTo(), so an address with a
        // trailing "#anchor" fragment deep-links to a specific setting.
        PlasmaZones::SingleInstanceService::forward(kSettingsIds, QStringLiteral("setActivePage"), {address});
    }
    return true;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Opt out of MangoHud's implicit Vulkan layer injection. MangoHud's
    // implicit_layer manifest attaches whenever MANGOHUD=1 is in the
    // environment (e.g. set globally for games), and its NVIDIA stat-polling
    // thread costs ~30% CPU continuously inside this process — we are a
    // settings UI, not a game client. Both env vars are cleared:
    // MANGOHUD=0 alone is not enough on all manifest versions; the explicit
    // DISABLE_MANGOHUD opt-out is honored regardless of MANGOHUD's value.
    // Must run before QApplication construction (which initializes the
    // QtQuick render path and may load the Vulkan ICD chain).
    qunsetenv("MANGOHUD");
    qputenv("DISABLE_MANGOHUD", "1");

    // QApplication (not QGuiApplication): the org.kde.desktop QtQuick Controls
    // style (qqc2-desktop-style) renders every control through a QtWidgets
    // QStyle via KQuickStyleItem. That path calls qApp->style(), which requires
    // a QApplication — under a plain QGuiApplication it operates in a degenerate
    // context that fragile third-party QStyle plugins (e.g. Darkly) dereference
    // into a crash on the first paint frame. See discussion #262.
    QApplication app(argc, argv);
    PlasmaZones::loadTranslations(&app);

    app.setApplicationName(QStringLiteral("plasmazones-settings"));
    app.setApplicationVersion(PlasmaZones::VERSION_STRING);
    app.setOrganizationName(QStringLiteral("plasmazones"));
    app.setOrganizationDomain(QStringLiteral("org.plasmazones"));
    app.setDesktopFileName(QStringLiteral("org.plasmazones.settings"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("plasmazones-settings")));

    QCommandLineParser parser;
    parser.setApplicationDescription(PhosphorI18n::tr("PlasmaZones Settings"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption pageOption(QStringList{QStringLiteral("p"), QStringLiteral("page")},
                                  PhosphorI18n::tr("Open a specific settings page"), QStringLiteral("name"));
    parser.addOption(pageOption);
    QCommandLineOption settingOption(QStringList{QStringLiteral("s"), QStringLiteral("setting")},
                                     PhosphorI18n::tr("Reveal a specific setting on the page (deep link)"),
                                     QStringLiteral("anchor"));
    parser.addOption(settingOption);
    QCommandLineOption sectionOption(QStringLiteral("section"),
                                     PhosphorI18n::tr("Reveal a specific section on the page (deep link)"),
                                     QStringLiteral("anchor"));
    parser.addOption(sectionOption);
    parser.process(app);

    const QString requestedPage = parser.isSet(pageOption) ? parser.value(pageOption) : QString();
    // --setting takes precedence over --section; either composes a
    // "pageId#anchor" address consumed by navigateTo(). An anchor is
    // meaningless without a page, so it's only appended when both are present.
    const QString requestedAnchor = parser.isSet(settingOption)
        ? parser.value(settingOption)
        : (parser.isSet(sectionOption) ? parser.value(sectionOption) : QString());
    const QString requestedAddress = (!requestedPage.isEmpty() && !requestedAnchor.isEmpty())
        ? (requestedPage + QLatin1Char('#') + requestedAnchor)
        : requestedPage;

    // Single-instance: if another instance is running, forward the request and exit
    if (activateRunningInstance(requestedAddress)) {
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
        if (desktop.contains(QLatin1String("kde")) || desktop.contains(QLatin1String("plasma"))) {
            QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        } else {
            QQuickStyle::setStyle(QStringLiteral("Fusion"));
        }
    }

    // Ensure INI→JSON migration has run (the daemon does this too, but the
    // settings app may start before the daemon on first upgrade).
    PlasmaZones::ConfigMigration::ensureJsonConfig();

    // Bootstrap the per-process PhosphorProfileRegistry so QML
    // `PhosphorMotionAnimation { profile: "..." }` lookups resolve. The
    // shipped tree carries no bundled profile JSONs (timings are driven
    // entirely by the Settings UI's per-node overrides); the bootstrap
    // loader stays wired so user-authored JSONs at
    // `~/.local/share/plasmazones/profiles/<path>.json` are still
    // picked up. Must outlive the QML engine (Behavior bindings keep
    // registry handles).
    PlasmaZones::AnimationBootstrap animationBootstrap;

    // Publish the bootstrap-owned registries + a fresh clock manager as
    // the QML-side defaults. Phase A3 of the architecture refactor
    // retired the prior `PhosphorProfileRegistry::instance()` /
    // `QtQuickClockManager::instance()` Meyers singletons — composition
    // roots own and publish their own.
    PhosphorAnimation::QtQuickClockManager clockManager;
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(animationBootstrap.curveRegistry());
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(animationBootstrap.profileRegistry());
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(&clockManager);
    auto unpublishAnimationDefaults = qScopeGuard([] {
        PhosphorAnimation::PhosphorCurve::setDefaultRegistry(nullptr);
        PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
        PhosphorAnimation::QtQuickClockManager::setDefaultManager(nullptr);
    });

    PlasmaZones::SettingsController controller;

    // The launch controller owns the D-Bus single-instance lifecycle. Holds a
    // non-owning pointer to `controller`, which must outlive it (guaranteed by
    // reverse destruction order: `controller` is declared first and destroyed
    // last).
    PlasmaZones::SettingsLaunchController launcher(&controller);

    // Register D-Bus service so future launches can forward to us.
    // If registration fails, another instance registered between our
    // activateRunningInstance() check and now — retry forwarding and exit.
    if (!launcher.registerDBusService()) {
        qCWarning(PlasmaZones::lcCore) << "D-Bus service already owned; forwarding to running instance";
        if (activateRunningInstance(requestedAddress)) {
            return 0;
        }
        // D-Bus name is taken but we can't reach the owner — bail out
        // rather than running a second instance without single-instance support.
        qCCritical(PlasmaZones::lcCore) << "Cannot register D-Bus service and cannot reach existing instance; exiting";
        return 1;
    }

    // Register ZoneShaderItem for QML (live zone-shader preview in the settings
    // shader browser — mirrors daemon/main.cpp + editor/main.cpp).
    qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");

    // Global settings search, set up BEFORE the engine so the provider locals
    // below outlive ~QQmlApplicationEngine: a model change signal firing during
    // engine teardown must not reach a destroyed provider via invalidate() →
    // buildIndex(). Page entries derive from the page registry; seedSearchCatalog
    // adds per-page synonyms + addressable anchors. searchController is parented
    // to `controller` (destroyed last).
    auto* searchController = new PhosphorControl::SearchController(controller.app(), &controller);
    PlasmaZones::seedSearchCatalog(searchController);

    // Dynamic content providers (layouts, rules, profiles). unique_ptr locals
    // declared before the engine so they outlive it; SearchController holds them
    // non-owning (ISearchProvider is not a QObject, so no parent applies).
    auto layoutsProvider = std::make_unique<PlasmaZones::LayoutsSearchProvider>(&controller);
    auto rulesProvider = std::make_unique<PlasmaZones::RulesSearchProvider>(&controller);
    auto profilesProvider = std::make_unique<PlasmaZones::ProfilesSearchProvider>(&controller);
    searchController->registerProvider(layoutsProvider.get());
    searchController->registerProvider(rulesProvider.get());
    searchController->registerProvider(profilesProvider.get());
    QObject::connect(&controller, &PlasmaZones::SettingsController::layoutsChanged, searchController,
                     &PhosphorControl::SearchController::invalidate);
    if (controller.rulesPage() != nullptr && controller.rulesPage()->model() != nullptr) {
        // Both add/remove (countChanged) and any in-place edit (rename,
        // match-summary, … via dataChanged) must refresh the index. invalidate()
        // is lazy, so over-firing on unrelated role changes is cheap.
        QObject::connect(controller.rulesPage()->model(), &PlasmaZones::RuleModel::countChanged, searchController,
                         &PhosphorControl::SearchController::invalidate);
        QObject::connect(controller.rulesPage()->model(), &PlasmaZones::RuleModel::dataChanged, searchController,
                         &PhosphorControl::SearchController::invalidate);
    }

    if (controller.profilesPage() != nullptr && controller.profilesPage()->bridge() != nullptr) {
        // One signal covers the lot: profilesChanged fires on create, rename,
        // duplicate, delete, import, reparent, and activation.
        QObject::connect(controller.profilesPage()->bridge(), &PlasmaZones::ProfileStore::profilesChanged,
                         searchController, &PhosphorControl::SearchController::invalidate);
    }

    QQmlApplicationEngine engine;

    auto* localizedContext = new PhosphorLocalizedContext(&engine);
    engine.rootContext()->setContextObject(localizedContext);

    engine.rootContext()->setContextProperty(QStringLiteral("settingsController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), controller.settings());
    engine.rootContext()->setContextProperty(QStringLiteral("searchController"), searchController);

    if (!requestedAddress.isEmpty()) {
        controller.navigateTo(requestedAddress);
    }

    engine.loadFromModule("org.plasmazones.settings", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCCritical(PlasmaZones::lcCore) << "Failed to load settings QML";
        return 1;
    }

    // Background-compile every settings QML unit — the pages AND the shared
    // child component types they use (SettingsCard, AnimationEventCard,
    // AnimationProfileEditor, CurveThumbnail, …). A page's child component
    // types compile LAZILY on first instantiation, not when the page's own
    // unit compiles, so the first navigation to a page otherwise pays that
    // first-time child compilation — which measurement showed dominates
    // first-visit cost (e.g. Rules' first build was ~1560 ms, ~1300 ms
    // of it child compilation; warmed it drops to ~260 ms). Compiling here,
    // asynchronously (on the type-loader thread, never blocking the UI) and
    // holding the components so the engine keeps the compiled units cached,
    // means PageHost's Loader pays construction only on first visit.
    //
    // `warmComponents` is declared after `engine` so it is destroyed BEFORE
    // the engine (the components reference it). Compilation only — no
    // instantiation — so no page `onCompleted` side effects run here.
    std::vector<std::unique_ptr<QQmlComponent>> warmComponents;
    {
        QDirIterator qmlIt(QStringLiteral(":/qt/qml/org/plasmazones/settings/qml"),
                           QStringList{QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
        while (qmlIt.hasNext()) {
            qmlIt.next();
            const QUrl url(QStringLiteral("qrc") + qmlIt.filePath());
            warmComponents.push_back(std::make_unique<QQmlComponent>(&engine, url, QQmlComponent::Asynchronous));
        }
    }

    return app.exec();
}
