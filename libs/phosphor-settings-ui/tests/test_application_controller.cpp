// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "PhosphorSettingsUi/ApplicationController.h"
#include "PhosphorSettingsUi/PageController.h"
#include "PhosphorSettingsUi/PageRegistry.h"
#include "PhosphorSettingsUi/StagingDomain.h"

using PhosphorSettingsUi::ApplicationController;
using PhosphorSettingsUi::PageController;
using PhosphorSettingsUi::StagingDomain;

namespace {

class StubPage : public PageController
{
    Q_OBJECT
public:
    explicit StubPage(QString id, QObject* parent = nullptr)
        : PageController(std::move(id), parent)
    {
    }

    bool isDirty() const override
    {
        return m_dirty;
    }

    void apply() override
    {
        ++applyCount;
        setDirty(false);
    }

    void discard() override
    {
        ++discardCount;
        setDirty(false);
    }

    void resetToDefaults() override
    {
        ++resetCount;
        setDirty(true);
    }

    void setDirty(bool d)
    {
        if (m_dirty == d) {
            return;
        }
        m_dirty = d;
        Q_EMIT dirtyChanged();
    }

    int applyCount = 0;
    int discardCount = 0;
    int resetCount = 0;

private:
    bool m_dirty = false;
};

class StubHeadlessDomain : public StagingDomain
{
    Q_OBJECT
public:
    explicit StubHeadlessDomain(QObject* parent = nullptr)
        : StagingDomain(parent)
    {
    }

    bool isDirty() const override
    {
        return m_dirty;
    }
    void apply() override
    {
        ++applyCount;
        setDirty(false);
    }
    void discard() override
    {
        ++discardCount;
        setDirty(false);
    }

    void setDirty(bool d)
    {
        if (m_dirty == d) {
            return;
        }
        m_dirty = d;
        Q_EMIT dirtyChanged();
    }

    int applyCount = 0;
    int discardCount = 0;

private:
    bool m_dirty = false;
};

} // namespace

class TestApplicationController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsClean()
    {
        ApplicationController app;
        QVERIFY(!app.isDirty());
        QVERIFY(app.currentPageId().isEmpty());
    }

    void propagatesPageDirtyFlag()
    {
        ApplicationController app;
        auto* page = new StubPage(QStringLiteral("p"));
        app.registerPage(page, {}, QStringLiteral("P"), QUrl(QStringLiteral("qrc:/P.qml")));

        QSignalSpy spy(&app, &ApplicationController::dirtyChanged);
        QVERIFY(!app.isDirty());

        page->setDirty(true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(app.isDirty());

        page->setDirty(false);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!app.isDirty());
    }

    void aggregatesAcrossDomains()
    {
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* b = new StubPage(QStringLiteral("b"));
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());
        app.registerPage(b, {}, QStringLiteral("B"), QUrl());

        a->setDirty(true);
        QVERIFY(app.isDirty());
        b->setDirty(true);
        QVERIFY(app.isDirty());
        a->setDirty(false);
        QVERIFY(app.isDirty()); // b still dirty
        b->setDirty(false);
        QVERIFY(!app.isDirty());
    }

    void applyAllOnlyHitsDirtyDomains()
    {
        ApplicationController app;
        auto* dirtyPage = new StubPage(QStringLiteral("dirty"));
        auto* cleanPage = new StubPage(QStringLiteral("clean"));
        app.registerPage(dirtyPage, {}, QStringLiteral("D"), QUrl());
        app.registerPage(cleanPage, {}, QStringLiteral("C"), QUrl());

        dirtyPage->setDirty(true);
        app.applyAll();

        QCOMPARE(dirtyPage->applyCount, 1);
        QCOMPARE(cleanPage->applyCount, 0);
        QVERIFY(!app.isDirty());
    }

    void discardAllRevertsAllDomains()
    {
        ApplicationController app;
        auto* page = new StubPage(QStringLiteral("p"));
        auto* headless = new StubHeadlessDomain();
        app.registerPage(page, {}, QStringLiteral("P"), QUrl());
        app.registerDomain(headless);

        page->setDirty(true);
        headless->setDirty(true);
        QVERIFY(app.isDirty());

        app.discardAll();
        QCOMPARE(page->discardCount, 1);
        QCOMPARE(headless->discardCount, 1);
        QVERIFY(!app.isDirty());
    }

    void resetCurrentPageOnlyHitsCurrent()
    {
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* b = new StubPage(QStringLiteral("b"));
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());
        app.registerPage(b, {}, QStringLiteral("B"), QUrl());

        app.setCurrentPageId(QStringLiteral("b"));
        app.resetCurrentPage();

        QCOMPARE(a->resetCount, 0);
        QCOMPARE(b->resetCount, 1);
    }

    void rejectsUnknownCurrentPage()
    {
        ApplicationController app;
        auto* page = new StubPage(QStringLiteral("known"));
        app.registerPage(page, {}, QStringLiteral("K"), QUrl());

        app.setCurrentPageId(QStringLiteral("ghost"));
        QVERIFY(app.currentPageId().isEmpty());

        app.setCurrentPageId(QStringLiteral("known"));
        QCOMPARE(app.currentPageId(), QStringLiteral("known"));
    }

    void registryIsAccessibleViaProperty()
    {
        ApplicationController app;
        QVERIFY(app.registry() != nullptr);
        auto* page = new StubPage(QStringLiteral("x"));
        app.registerPage(page, {}, QStringLiteral("X"), QUrl());
        QVERIFY(app.registry()->hasPage(QStringLiteral("x")));
    }

    void parentChainWalksUpward()
    {
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* aB = new StubPage(QStringLiteral("a.b"));
        auto* aBC = new StubPage(QStringLiteral("a.b.c"));
        app.registerPage(a, {}, QStringLiteral("A"), QUrl(QStringLiteral("qrc:/A.qml")));
        app.registerPage(aB, QStringLiteral("a"), QStringLiteral("B"), QUrl(QStringLiteral("qrc:/B.qml")));
        app.registerPage(aBC, QStringLiteral("a.b"), QStringLiteral("C"), QUrl(QStringLiteral("qrc:/C.qml")));

        QCOMPARE(app.parentChainFor(QStringLiteral("a.b.c")),
                 QStringList({QStringLiteral("a"), QStringLiteral("a.b")}));
        QCOMPARE(app.parentChainFor(QStringLiteral("a.b")), QStringList({QStringLiteral("a")}));
        QCOMPARE(app.parentChainFor(QStringLiteral("a")), QStringList());
        QCOMPARE(app.parentChainFor(QStringLiteral("ghost")), QStringList());
    }

    void gotoNextSkipsNonNavigablePages()
    {
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* category = new StubPage(QStringLiteral("cat"));
        auto* b = new StubPage(QStringLiteral("b"));
        auto* c = new StubPage(QStringLiteral("c"));
        app.registerPage(a, {}, QStringLiteral("A"), QUrl(QStringLiteral("qrc:/A.qml")));
        app.registerPage(category, {}, QStringLiteral("Category"), QUrl());
        app.registerPage(b, QStringLiteral("cat"), QStringLiteral("B"), QUrl(QStringLiteral("qrc:/B.qml")));
        app.registerPage(c, QStringLiteral("cat"), QStringLiteral("C"), QUrl(QStringLiteral("qrc:/C.qml")));

        app.setCurrentPageId(QStringLiteral("a"));
        QCOMPARE(app.gotoNextPage(), QStringLiteral("b")); // skips "cat"
        QCOMPARE(app.currentPageId(), QStringLiteral("b"));
        QCOMPARE(app.gotoNextPage(), QStringLiteral("c"));
        QCOMPARE(app.gotoNextPage(), QStringLiteral("a")); // wraps
    }

    void gotoPreviousWrapsBackward()
    {
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* b = new StubPage(QStringLiteral("b"));
        app.registerPage(a, {}, QStringLiteral("A"), QUrl(QStringLiteral("qrc:/A.qml")));
        app.registerPage(b, {}, QStringLiteral("B"), QUrl(QStringLiteral("qrc:/B.qml")));

        app.setCurrentPageId(QStringLiteral("a"));
        QCOMPARE(app.gotoPreviousPage(), QStringLiteral("b")); // wraps
        QCOMPARE(app.gotoPreviousPage(), QStringLiteral("a"));
    }

    void isCollapsibleFlowsThroughRegistry()
    {
        ApplicationController app;
        auto* collapsible = new StubPage(QStringLiteral("cat"));
        auto* drillIn = new StubPage(QStringLiteral("drill"));
        app.registerPage(collapsible, {}, QStringLiteral("Cat"), QUrl(), QStringLiteral(""), /*isCollapsible=*/true);
        app.registerPage(drillIn, {}, QStringLiteral("Drill"), QUrl(), QStringLiteral(""), /*isCollapsible=*/false);

        const auto cat = app.registry()->entry(QStringLiteral("cat"));
        const auto drill = app.registry()->entry(QStringLiteral("drill"));
        QVERIFY(cat.isCollapsible);
        QVERIFY(!drill.isCollapsible);

        // Q_INVOKABLE pageData should surface the flag for QML consumers.
        const QVariantMap catData = app.registry()->pageData(QStringLiteral("cat"));
        QCOMPARE(catData.value(QStringLiteral("isCollapsible")).toBool(), true);
    }
};

QTEST_MAIN(TestApplicationController)
#include "test_application_controller.moc"
