// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorControl/ApplicationController.h>
#include <PhosphorControl/FacetHost.h>
#include <PhosphorControl/PageController.h>
#include <PhosphorControl/PageRegistry.h>

#include <QDir>
#include <QFile>
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
        QVERIFY(registry->entry(QStringLiteral("good:general")).qmlSource.isEmpty());
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

        QString error;
        QVERIFY2(h.host->retryModule(QStringLiteral("good"), &error), qPrintable(error));
        QCOMPARE(h.host->state(QStringLiteral("good")), FacetHost::ModuleState::Loaded);
    }
};

QTEST_GUILESS_MAIN(TestFacetHost)
#include "test_facet_host.moc"
