// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorControl/ApplicationController.h>
#include <PhosphorControl/FacetHost.h>
#include <PhosphorControl/PageController.h>
#include <PhosphorControl/PageRegistry.h>
#include <PhosphorControl/SearchController.h>
#include <PhosphorControl/SearchEntry.h>
#include <PhosphorControl/StagingDomain.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifndef PHOSPHOR_CONTROL_FIXTURE_DIR
#error "PHOSPHOR_CONTROL_FIXTURE_DIR must be defined by tests/CMakeLists.txt"
#endif

using namespace PhosphorControl;

namespace {

class SimplePage : public PageController
{
public:
    using PageController::PageController;
    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
        Q_EMIT applyResult(true, QString());
    }
    void discard() override
    {
        Q_EMIT discardResult(true, QString());
    }
};

// A temporary module directory, a controller and a host over it.
struct Harness
{
    QTemporaryDir root;
    QString modules;
    QString sentinel;
    ApplicationController controller;
    std::unique_ptr<FacetHost> host;

    explicit Harness(const QStringList& variants)
    {
        modules = root.filePath(QStringLiteral("facet"));
        sentinel = root.filePath(QStringLiteral("facet-loading"));
        QDir().mkpath(modules);
        const QFileDevice::Permissions mode = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
            | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther;
        QFile::setPermissions(modules, mode);
        for (const QString& variant : variants) {
            const QString source =
                QStringLiteral(PHOSPHOR_CONTROL_FIXTURE_DIR) + QLatin1Char('/') + variant + QStringLiteral(".so");
            const QString target = modules + QLatin1Char('/') + variant + QStringLiteral(".so");
            if (!QFile::copy(source, target)) {
                qWarning() << "Harness: cannot stage" << source;
            }
            QFile::setPermissions(target, mode);
        }
        host = std::make_unique<FacetHost>(&controller);
        host->setSearchPaths({{modules, PhosphorRegistry::PluginOrigin::Development}});
        host->setSentinelPath(sentinel);
        host->setLiveReload(false);
        host->setLocale(QLocale::c());
    }

    PageRegistry* registry() const
    {
        return controller.registry();
    }
};

} // namespace

class TestFacetHost : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void bindPageGivesAPlaceholderItsRealController()
    {
        ApplicationController controller;
        auto* placeholder = new SimplePage(QStringLiteral("p"), &controller);
        controller.registerPage(placeholder, QString(), QStringLiteral("Title"), QUrl(), QStringLiteral("icon"));
        QSignalSpy bound(controller.registry(), &PageRegistry::pageBound);

        auto* real = new SimplePage(QStringLiteral("p"), &controller);
        QVERIFY(controller.bindPage(QStringLiteral("p"), real, QUrl(QStringLiteral("qrc:/p.qml"))));
        QCOMPARE(bound.count(), 1);
        QCOMPARE(controller.registry()->controller(QStringLiteral("p")), real);
        const PageRegistry::Entry entry = controller.registry()->entry(QStringLiteral("p"));
        QCOMPARE(entry.qmlSource, QUrl(QStringLiteral("qrc:/p.qml")));
        QCOMPARE(entry.title, QStringLiteral("Title"));
        QCOMPARE(entry.iconSource, QStringLiteral("icon"));

        // Refusals: an unknown page, and a controller another page already has.
        auto* other = new SimplePage(QStringLiteral("q"), &controller);
        controller.registerPage(other, QString(), QStringLiteral("Other"), QUrl());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("bindPage: unknown page")));
        QVERIFY(
            !controller.bindPage(QStringLiteral("missing"), new SimplePage(QStringLiteral("m"), &controller), QUrl()));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("already registered for another page")));
        QVERIFY(!controller.bindPage(QStringLiteral("q"), real, QUrl()));
    }

    void theNavigationHookRunsBeforeThePageChanges()
    {
        ApplicationController controller;
        controller.registerPage(new SimplePage(QStringLiteral("a"), &controller), QString(), QStringLiteral("A"),
                                QUrl());
        controller.registerPage(new SimplePage(QStringLiteral("b"), &controller), QString(), QStringLiteral("B"),
                                QUrl());
        controller.setCurrentPageId(QStringLiteral("a"));
        QString target;
        QString currentDuringHook;
        controller.setNavigationHook([&](const QString& pageId) {
            target = pageId;
            currentDuringHook = controller.currentPageId();
        });
        controller.setCurrentPageId(QStringLiteral("b"));
        QCOMPARE(target, QStringLiteral("b"));
        QCOMPARE(currentDuringHook, QStringLiteral("a"));
        QCOMPARE(controller.currentPageId(), QStringLiteral("b"));
    }

    void startShowsEveryModuleWithoutLoadingAny()
    {
        Harness h({QStringLiteral("good"), QStringLiteral("child"), QStringLiteral("orphan")});
        h.host->start();
        PageRegistry* const registry = h.registry();

        const QString input = FacetHost::categoryNodeId(QStringLiteral("input"));
        const QString apps = FacetHost::categoryNodeId(QStringLiteral("apps"));
        QVERIFY(registry->hasPage(input));
        QCOMPARE(registry->entry(input).title, QStringLiteral("Input"));
        QCOMPARE(registry->parentIdOf(input), QString());

        QCOMPARE(registry->parentIdOf(QStringLiteral("good")), input);
        QCOMPARE(registry->entry(QStringLiteral("good")).title, QStringLiteral("Good"));
        QCOMPARE(registry->entry(QStringLiteral("good")).iconSource, QStringLiteral("good-icon"));
        QCOMPARE(registry->parentIdOf(QStringLiteral("good:general")), QStringLiteral("good"));
        QCOMPARE(registry->entry(QStringLiteral("good:general")).title, QStringLiteral("General"));
        QVERIFY(registry->entry(QStringLiteral("good:general")).isCollapsible);
        QCOMPARE(registry->parentIdOf(QStringLiteral("good:general/advanced")), QStringLiteral("good:general"));

        // Nested under another module, and a page under another module's page.
        QCOMPARE(registry->parentIdOf(QStringLiteral("child")), QStringLiteral("good"));
        QCOMPARE(registry->parentIdOf(QStringLiteral("child:extra")), QStringLiteral("good:general"));

        // Missing targets fall back: the unknown category to apps, the absent
        // parent module to the category, the absent parent page to the module.
        QCOMPARE(registry->parentIdOf(QStringLiteral("orphan")), apps);
        QCOMPARE(registry->parentIdOf(QStringLiteral("orphan:page")), QStringLiteral("orphan"));

        for (const QString& id : h.host->moduleIds()) {
            QCOMPARE(h.host->state(id), FacetHost::ModuleState::Available);
        }
        // A module page stands in with the unavailable page, so it is a
        // navigable leaf the sidebar shows and search can land on before its
        // module loads. Category and module nodes stay headers.
        QCOMPARE(registry->entry(QStringLiteral("good:general")).qmlSource,
                 QUrl(QStringLiteral("qrc:/qt/qml/org/phosphor/control/qml/FacetUnavailablePage.qml")));
        QVERIFY(registry->hasNavigablePage(QStringLiteral("good:general/advanced")));
        QCOMPARE(registry->firstVisibleLeafId(input), QStringLiteral("good:general"));
        QVERIFY(!registry->hasNavigablePage(QStringLiteral("good")));
        QVERIFY(!registry->hasNavigablePage(input));
    }

    void openingAPageLoadsItsModule()
    {
        Harness h({QStringLiteral("good")});
        h.host->start();
        PageController* const placeholder = h.registry()->controller(QStringLiteral("good:general"));
        QSignalSpy loaded(h.host.get(), &FacetHost::moduleLoaded);

        h.controller.setCurrentPageId(QStringLiteral("good:general"));

        QCOMPARE(loaded.count(), 1);
        QCOMPARE(h.host->state(QStringLiteral("good")), FacetHost::ModuleState::Loaded);
        QCOMPARE(h.controller.currentPageId(), QStringLiteral("good:general"));
        PageController* const real = h.registry()->controller(QStringLiteral("good:general"));
        QVERIFY(real);
        QVERIFY(real != placeholder);
        QCOMPARE(real->id(), QStringLiteral("good:general"));
        QCOMPARE(h.registry()->entry(QStringLiteral("good:general")).qmlSource,
                 QUrl(QStringLiteral("qrc:/fake/general.qml")));
        QCOMPARE(h.host->boundPages(QStringLiteral("good")).size(), 2);
        QCOMPARE(h.host->moduleDomains(QStringLiteral("good")).size(), 1);
        QVERIFY(!QFile::exists(h.sentinel));

        // A second navigation does not load again.
        h.controller.setCurrentPageId(QStringLiteral("good:general/advanced"));
        QCOMPARE(loaded.count(), 1);
    }

    void aModuleThatBreaksItsContractFails_data()
    {
        QTest::addColumn<QString>("variant");
        QTest::addColumn<QString>("reason");
        QTest::newRow("skips a page") << QStringLiteral("missingpage")
                                      << QStringLiteral("did not register page general/advanced");
        QTest::newRow("registers an undeclared page")
            << QStringLiteral("extrapage") << QStringLiteral("registered page undeclared");
        QTest::newRow("wrong controller id") << QStringLiteral("wrongid") << QStringLiteral("wrong:id");
        QTest::newRow("skips an extension")
            << QStringLiteral("missingextension") << QStringLiteral("did not register extension second");
        QTest::newRow("registers an undeclared extension")
            << QStringLiteral("extraextension") << QStringLiteral("registered extension undeclared");
        QTest::newRow("registers a search provider it did not declare")
            << QStringLiteral("undeclaredprovider") << QStringLiteral("without \"SearchProviders\": true");
        QTest::newRow("opens a config file by a relative path")
            << QStringLiteral("relativeconfig") << QStringLiteral("relative.json, which is not an absolute path");
        QTest::newRow("registers an extension twice")
            << QStringLiteral("extraextension") << QStringLiteral("registered extension first twice");
    }

    void aModuleThatBreaksItsContractFails()
    {
        QFETCH(QString, variant);
        QFETCH(QString, reason);
        Harness h({variant});
        h.host->start();
        QSignalSpy failed(h.host.get(), &FacetHost::moduleFailed);

        QString error;
        QVERIFY(!h.host->loadModule(variant, &error));
        QVERIFY2(error.contains(reason), qPrintable(error));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(h.host->state(variant), FacetHost::ModuleState::Failed);
        QVERIFY(h.host->stateReason(variant).contains(reason));
    }

    void aModuleBuiltForAnotherApiVersionIsRefused()
    {
        Harness h({QStringLiteral("wrongapi")});
        h.host->start();
        QVERIFY(h.host->moduleIds().isEmpty());
        const auto refused = h.host->refusedModules();
        QCOMPARE(refused.size(), 1);
        QVERIFY(refused.first().reason.contains(QStringLiteral("ApiVersion 2")));
    }

    void invalidMetadataFailsWithoutAPlaceholder()
    {
        Harness h({QStringLiteral("badmetadata")});
        h.host->start();
        QCOMPARE(h.host->state(QStringLiteral("badmetadata")), FacetHost::ModuleState::Failed);
        QVERIFY(h.host->stateReason(QStringLiteral("badmetadata")).contains(QStringLiteral("invalid metadata")));
        QVERIFY(!h.registry()->hasPage(QStringLiteral("badmetadata")));
        QVERIFY(!h.host->retryModule(QStringLiteral("badmetadata")));
    }

    void aModuleThatCrashedWhileLoadingStaysDisabledUntilRetried()
    {
        Harness h({QStringLiteral("good")});
        {
            QFile sentinel(h.sentinel);
            QVERIFY(sentinel.open(QIODevice::WriteOnly));
            sentinel.write("good");
        }
        h.host->start();
        QCOMPARE(h.host->state(QStringLiteral("good")), FacetHost::ModuleState::DisabledAfterCrash);
        QVERIFY(!QFile::exists(h.sentinel));
        // Its pages are still shown, and opening one does not load it.
        QVERIFY(h.registry()->hasPage(QStringLiteral("good:general")));
        h.controller.setCurrentPageId(QStringLiteral("good:general"));
        QCOMPARE(h.host->state(QStringLiteral("good")), FacetHost::ModuleState::DisabledAfterCrash);

        // Its page says why, and offers the retry, which loads it.
        PageController* const stand = h.registry()->controller(QStringLiteral("good:general"));
        QVERIFY(stand->property("unavailableReason").toString().contains(QStringLiteral("stopped unexpectedly")));
        QVERIFY(stand->property("retryable").toBool());
        QSignalSpy bound(h.registry(), &PageRegistry::pageBound);
        QVERIFY(QMetaObject::invokeMethod(stand, "retry"));
        QCOMPARE(h.host->state(QStringLiteral("good")), FacetHost::ModuleState::Loaded);
        QCOMPARE(bound.count(), 2);
        QVERIFY(h.registry()->controller(QStringLiteral("good:general")) != stand);
    }

    void aPageWhoseModuleFailedSaysWhy()
    {
        Harness h({QStringLiteral("missingpage")});
        h.host->start();
        h.controller.setCurrentPageId(QStringLiteral("missingpage:general"));
        QCOMPARE(h.host->state(QStringLiteral("missingpage")), FacetHost::ModuleState::Failed);
        // The page it did bind keeps its real controller; the one it skipped
        // keeps the placeholder, which explains. It bound a page, so loading it
        // again is not offered.
        PageController* const stand = h.registry()->controller(QStringLiteral("missingpage:general/advanced"));
        QVERIFY(stand->property("unavailableReason")
                    .toString()
                    .contains(QStringLiteral("did not register page general/advanced")));
        QVERIFY(!stand->property("retryable").toBool());
    }

    void extensionsResolveAgainstTheirTargetPage()
    {
        Harness h({QStringLiteral("good"), QStringLiteral("extender"), QStringLiteral("badextender")});
        h.host->start();

        // Order, then module, then id. The one aimed at a module that is not
        // installed is neither shown nor refused.
        QCOMPARE(h.host->extensionsOn(QStringLiteral("good:general")),
                 (QStringList{QStringLiteral("extender:after-speed"), QStringLiteral("extender:panel"),
                              QStringLiteral("extender:top-early"), QStringLiteral("extender:top-late")}));
        QVERIFY(h.host->extensionsOn(QStringLiteral("absent:page")).isEmpty());

        const QList<FacetHost::RefusedExtension> refused = h.host->refusedExtensions();
        QStringList ids;
        for (const FacetHost::RefusedExtension& r : refused) {
            ids.append(r.id);
        }
        QCOMPARE(ids,
                 (QStringList{QStringLiteral("badextender:new-anchors"), QStringLiteral("badextender:no-anchor"),
                              QStringLiteral("badextender:no-page"), QStringLiteral("badextender:no-point"),
                              QStringLiteral("badextender:old-panel")}));
        const auto reasonOf = [&refused](const QString& id) {
            for (const FacetHost::RefusedExtension& r : refused) {
                if (r.id == id) {
                    return r.reason;
                }
            }
            return QString();
        };
        QCOMPARE(reasonOf(QStringLiteral("badextender:new-anchors")),
                 QStringLiteral("it is built for version 2 of slot top on good:general, which is at version 1"));
        QCOMPARE(reasonOf(QStringLiteral("badextender:old-panel")),
                 QStringLiteral("it is built for version 1 of slot panel on good:general, which is at version 2"));
        QCOMPARE(reasonOf(QStringLiteral("badextender:no-anchor")),
                 QStringLiteral("page good:general has no slot before:nope"));
        QCOMPARE(reasonOf(QStringLiteral("badextender:no-point")),
                 QStringLiteral("page good:general/advanced has no slot panel"));
        QCOMPARE(reasonOf(QStringLiteral("badextender:no-page")),
                 QStringLiteral("its target page good:nowhere does not exist"));

        // Nothing is listed for a slot before its module loads.
        QVERIFY(h.controller.extensionsFor(QStringLiteral("good:general"), QStringLiteral("top")).isEmpty());
    }

    void openingATargetPageLoadsTheModulesThatExtendIt()
    {
        Harness h({QStringLiteral("good"), QStringLiteral("extender"), QStringLiteral("badextender")});
        h.host->start();
        QSignalSpy changed(&h.controller, &ApplicationController::extensionsChanged);

        h.controller.setCurrentPageId(QStringLiteral("good:general"));

        QCOMPARE(h.host->state(QStringLiteral("good")), FacetHost::ModuleState::Loaded);
        QCOMPARE(h.host->state(QStringLiteral("extender")), FacetHost::ModuleState::Loaded);
        // Every one of its extensions was refused, so nothing needs it here.
        QCOMPARE(h.host->state(QStringLiteral("badextender")), FacetHost::ModuleState::Available);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toString(), QStringLiteral("good:general"));

        const QVariantList top = h.controller.extensionsFor(QStringLiteral("good:general"), QStringLiteral("top"));
        QCOMPARE(top.size(), 2);
        const QVariantMap first = top.first().toMap();
        QCOMPARE(first.value(QStringLiteral("id")).toString(), QStringLiteral("extender:top-early"));
        QCOMPARE(first.value(QStringLiteral("moduleId")).toString(), QStringLiteral("extender"));
        QCOMPARE(first.value(QStringLiteral("moduleName")).toString(), QStringLiteral("Extender"));
        QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("Top early"));
        QCOMPARE(first.value(QStringLiteral("qmlSource")).toUrl(),
                 QUrl(QStringLiteral("qrc:/fake/extension/top-early.qml")));
        auto* domain = first.value(QStringLiteral("domain")).value<QObject*>();
        QVERIFY(domain);
        QCOMPARE(domain->objectName(), QStringLiteral("top-early"));
        QVERIFY(!first.contains(QStringLiteral("error")));
        QCOMPARE(top.last().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("extender:top-late"));

        QCOMPARE(h.controller.extensionsFor(QStringLiteral("good:general"), QStringLiteral("after:speed")).size(), 1);
        QCOMPARE(h.controller.extensionsFor(QStringLiteral("good:general"), QStringLiteral("panel")).size(), 1);
        QVERIFY(h.controller.extensionsFor(QStringLiteral("good:general"), QStringLiteral("bottom")).isEmpty());
        // Its own headless domain plus one per declared extension, the
        // inactive one included.
        QCOMPARE(h.host->moduleDomains(QStringLiteral("extender")).size(), 6);
    }

    void anExtensionDirtiesAndResetsWithItsPage()
    {
        Harness h({QStringLiteral("good"), QStringLiteral("extender")});
        h.host->start();
        h.controller.setCurrentPageId(QStringLiteral("good:general"));
        const QVariantList panel = h.controller.extensionsFor(QStringLiteral("good:general"), QStringLiteral("panel"));
        QCOMPARE(panel.size(), 1);
        QObject* const domain = panel.first().toMap().value(QStringLiteral("domain")).value<QObject*>();
        QVERIFY(domain);
        QVERIFY(!h.controller.isDirty());

        domain->setProperty("fakeDirty", true);
        QVERIFY(h.controller.isDirty());
        QCOMPARE(h.controller.dirtyPageIds(), QStringList{QStringLiteral("good:general")});

        h.controller.resetCurrentPage();
        QVERIFY(!domain->property("fakeDirty").toBool());
        QVERIFY(!h.controller.isDirty());
        QVERIFY(h.controller.dirtyPageIds().isEmpty());
    }

    void aFailedExtenderShowsAnErrorInItsSlot()
    {
        Harness h({QStringLiteral("good"), QStringLiteral("missingextension")});
        h.host->start();
        h.controller.setCurrentPageId(QStringLiteral("good:general"));

        QCOMPARE(h.host->state(QStringLiteral("good")), FacetHost::ModuleState::Loaded);
        QCOMPARE(h.host->state(QStringLiteral("missingextension")), FacetHost::ModuleState::Failed);
        const QVariantList bottom =
            h.controller.extensionsFor(QStringLiteral("good:general"), QStringLiteral("bottom"));
        QCOMPARE(bottom.size(), 2);
        for (const QVariant& entry : bottom) {
            const QVariantMap map = entry.toMap();
            QVERIFY(
                map.value(QStringLiteral("error")).toString().contains(QStringLiteral("did not register extension")));
            QVERIFY(!map.contains(QStringLiteral("domain")));
        }
    }

    void anExtenderLoadsWithoutItsTarget()
    {
        Harness h({QStringLiteral("extender")});
        h.host->start();
        QString error;
        QVERIFY2(h.host->loadModule(QStringLiteral("extender"), &error), qPrintable(error));
        QVERIFY(h.host->refusedExtensions().isEmpty());
        QVERIFY(h.host->extensionsOn(QStringLiteral("good:general")).isEmpty());
    }

    void modulesWritingOneFileShareItsBackend()
    {
        Harness h({QStringLiteral("sharera"), QStringLiteral("sharerb")});
        const QString path = h.root.filePath(QStringLiteral("shared/config.json"));
        QDir().mkpath(QFileInfo(path).absolutePath());
        qputenv("FAKE_CONFIG_PATH", path.toUtf8());
        h.host->start();
        // Both load before either applies, so separate backends would each
        // hold a document without the other's key and the last write would win.
        QString error;
        QVERIFY2(h.host->loadModule(QStringLiteral("sharera"), &error), qPrintable(error));
        QVERIFY2(h.host->loadModule(QStringLiteral("sharerb"), &error), qPrintable(error));
        QVERIFY(h.host->configBackend(path));
        // The same file by another spelling is the same backend.
        QCOMPARE(h.host->configBackend(h.root.filePath(QStringLiteral("shared/../shared/config.json"))),
                 h.host->configBackend(path));

        for (const QString& module : {QStringLiteral("sharera"), QStringLiteral("sharerb")}) {
            for (StagingDomain* domain : h.host->moduleDomains(module)) {
                if (domain->objectName() == QLatin1String("config")) {
                    domain->setProperty("fakeDirty", true);
                }
            }
        }
        QSignalSpy done(&h.controller, &ApplicationController::applyAllComplete);
        h.controller.applyAllAsync();
        QVERIFY(done.count() == 1 || done.wait());
        QVERIFY(done.first().first().toBool());

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject fake =
            QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("Fake")).toObject();
        QCOMPARE(fake.value(QStringLiteral("sharera")).toString(), QStringLiteral("applied"));
        QCOMPARE(fake.value(QStringLiteral("sharerb")).toString(), QStringLiteral("applied"));
    }

    void searchFindsModulesBeforeTheyLoad()
    {
        Harness h({QStringLiteral("good"), QStringLiteral("extender"), QStringLiteral("translated")});
        h.host->setLocale(QLocale(QStringLiteral("de_DE")));
        SearchController search(&h.controller);
        search.setLimit(-1);
        h.host->setSearchController(&search);
        h.host->start();

        // Translated at build time from the module's catalog.
        QCOMPARE(h.host->metadata(QStringLiteral("translated"))->name, QStringLiteral("Übersetzt"));

        const auto find = [&search](const QString& query, const QString& pageId, const QString& anchor) {
            search.setQuery(query);
            for (const QVariant& result : search.results()) {
                const QVariantMap map = result.toMap();
                if (map.value(QStringLiteral("pageId")).toString() == pageId
                    && map.value(QStringLiteral("anchor")).toString() == anchor) {
                    return map;
                }
            }
            return QVariantMap();
        };
        // A page by its translated keyword, and by the module's keyword.
        QVERIFY(!find(QStringLiteral("schnell"), QStringLiteral("translated:speed"), QString()).isEmpty());
        QVERIFY(!find(QStringLiteral("module-word"), QStringLiteral("translated:speed"), QString()).isEmpty());
        // A static entry at its anchor; its title has no finished translation.
        const QVariantMap setting =
            find(QStringLiteral("Pointer"), QStringLiteral("translated:speed"), QStringLiteral("pointer"));
        QCOMPARE(setting.value(QStringLiteral("title")).toString(), QStringLiteral("Pointer speed"));
        QCOMPARE(setting.value(QStringLiteral("kind")).toInt(), static_cast<int>(SearchEntry::Kind::Setting));
        // An extension, as a section of the page it is on.
        const QVariantMap section =
            find(QStringLiteral("Top early"), QStringLiteral("good:general"), QStringLiteral("extender:top-early"));
        QCOMPARE(section.value(QStringLiteral("kind")).toInt(), static_cast<int>(SearchEntry::Kind::Section));
        // The extension aimed at a module that is not installed is not found.
        QVERIFY(
            find(QStringLiteral("Ghost"), QStringLiteral("absent:page"), QStringLiteral("extender:ghost")).isEmpty());

        for (const QString& id : h.host->moduleIds()) {
            QCOMPARE(h.host->state(id), FacetHost::ModuleState::Available);
        }
    }

    void searchLoadsModulesWithProvidersOnTheFirstQuery()
    {
        Harness h({QStringLiteral("providers"), QStringLiteral("good")});
        SearchController search(&h.controller);
        search.setLimit(-1);
        h.host->setSearchController(&search);
        h.host->start();
        QCOMPARE(h.host->state(QStringLiteral("providers")), FacetHost::ModuleState::Available);

        search.setQuery(QStringLiteral("Dynamic entity"));
        QCOMPARE(h.host->state(QStringLiteral("providers")), FacetHost::ModuleState::Loaded);
        // A module without providers stays unloaded.
        QCOMPARE(h.host->state(QStringLiteral("good")), FacetHost::ModuleState::Available);
        QStringList titles;
        for (const QVariant& result : search.results()) {
            titles.append(result.toMap().value(QStringLiteral("title")).toString());
        }
        QVERIFY2(titles.contains(QStringLiteral("Dynamic entity")), qPrintable(titles.join(QLatin1String(", "))));
    }

    void attachingToAnUnknownPageIsRefused()
    {
        ApplicationController controller;
        auto* domain = new SimplePage(QStringLiteral("d"), &controller);
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("attachDomainToPage: null domain or unknown page")));
        controller.attachDomainToPage(domain, QStringLiteral("missing"));
        QVERIFY(controller.dirtyPageIds().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFacetHost)
#include "test_facet_host.moc"
