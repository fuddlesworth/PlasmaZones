// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/PageController.h"

using PhosphorControl::ApplicationController;
using PhosphorControl::PageController;

// Named namespace (not anonymous) for external linkage — mirrors
// test_application_controller.cpp; each test is its own executable so
// the name cannot collide.
namespace ApplicationControllerHistoryTest {

// Minimal always-clean page: the history tests only exercise
// navigation, never the staging-domain surface.
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
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }
    void resetToDefaults() override
    {
    }
};

} // namespace ApplicationControllerHistoryTest

using ApplicationControllerHistoryTest::StubPage;

class TestApplicationControllerHistory : public QObject
{
    Q_OBJECT

private:
    // Registers pages "a", "b", "c" on the given controller.
    static void registerAbc(ApplicationController& app)
    {
        for (const auto* id : {"a", "b", "c"}) {
            const QString pageId = QLatin1String(id);
            app.registerPage(new StubPage(pageId), {}, pageId.toUpper(),
                             QUrl(QStringLiteral("qrc:/%1.qml").arg(pageId)));
        }
    }

private Q_SLOTS:
    void startsWithEmptyHistory()
    {
        ApplicationController app;
        registerAbc(app);

        QVERIFY(!app.canGoBack());
        QVERIFY(!app.canGoForward());
        QCOMPARE(app.goBack(), QString());
        QCOMPARE(app.goForward(), QString());

        // The startup transition (empty → first page) records nothing.
        app.setCurrentPageId(QStringLiteral("a"));
        QVERIFY(!app.canGoBack());
        QVERIFY(!app.canGoForward());
    }

    void recordsNavigationAndGoesBack()
    {
        ApplicationController app;
        registerAbc(app);

        app.setCurrentPageId(QStringLiteral("a"));
        app.setCurrentPageId(QStringLiteral("b"));
        app.setCurrentPageId(QStringLiteral("c"));
        QVERIFY(app.canGoBack());

        QCOMPARE(app.goBack(), QStringLiteral("b"));
        QCOMPARE(app.currentPageId(), QStringLiteral("b"));
        QCOMPARE(app.goBack(), QStringLiteral("a"));
        QCOMPARE(app.currentPageId(), QStringLiteral("a"));

        QVERIFY(!app.canGoBack());
        QCOMPARE(app.goBack(), QString());
        QCOMPARE(app.currentPageId(), QStringLiteral("a"));
    }

    void goForwardRetracesAfterBack()
    {
        ApplicationController app;
        registerAbc(app);

        app.setCurrentPageId(QStringLiteral("a"));
        app.setCurrentPageId(QStringLiteral("b"));
        QVERIFY(!app.canGoForward());

        QCOMPARE(app.goBack(), QStringLiteral("a"));
        QVERIFY(app.canGoForward());

        QCOMPARE(app.goForward(), QStringLiteral("b"));
        QCOMPARE(app.currentPageId(), QStringLiteral("b"));
        QVERIFY(!app.canGoForward());
        QCOMPARE(app.goForward(), QString());

        // The back/forward round trip must not have re-recorded: exactly
        // one back entry ("a") remains, as if the user never went back.
        QCOMPARE(app.goBack(), QStringLiteral("a"));
        QVERIFY(!app.canGoBack());
    }

    void ordinaryNavigationClearsForwardTrail()
    {
        ApplicationController app;
        registerAbc(app);

        app.setCurrentPageId(QStringLiteral("a"));
        app.setCurrentPageId(QStringLiteral("b"));
        QCOMPARE(app.goBack(), QStringLiteral("a"));
        QVERIFY(app.canGoForward());

        // Browser model: branching off the trail drops the forward stack.
        app.setCurrentPageId(QStringLiteral("c"));
        QVERIFY(!app.canGoForward());
        QCOMPARE(app.goForward(), QString());
        QCOMPARE(app.goBack(), QStringLiteral("a"));
    }

    void gotoNextPageRecordsHistory()
    {
        // The wrap-around page stepper routes through setCurrentPageId,
        // so its moves are ordinary recorded navigation.
        ApplicationController app;
        registerAbc(app);

        app.setCurrentPageId(QStringLiteral("a"));
        QCOMPARE(app.gotoNextPage(), QStringLiteral("b"));
        QCOMPARE(app.goBack(), QStringLiteral("a"));
    }

    void rejectedNavigationLeavesHistoryUntouched()
    {
        ApplicationController app;
        registerAbc(app);

        app.setCurrentPageId(QStringLiteral("a"));
        app.setCurrentPageId(QStringLiteral("ghost")); // refused: unknown page
        app.setCurrentPageId(QStringLiteral("a")); // refused: same id

        QVERIFY(!app.canGoBack());
        QCOMPARE(app.currentPageId(), QStringLiteral("a"));
    }

    void historyChangedEmitsOnlyOnFlips()
    {
        ApplicationController app;
        registerAbc(app);
        QSignalSpy spy(&app, &ApplicationController::historyChanged);

        app.setCurrentPageId(QStringLiteral("a")); // empty → a: no history
        QCOMPARE(spy.count(), 0);
        app.setCurrentPageId(QStringLiteral("b")); // canGoBack false → true
        QCOMPARE(spy.count(), 1);
        app.setCurrentPageId(QStringLiteral("c")); // both flags unchanged
        QCOMPARE(spy.count(), 1);
        app.goBack(); // canGoForward false → true
        QCOMPARE(spy.count(), 2);
        app.goBack(); // canGoBack true → false
        QCOMPARE(spy.count(), 3);
        app.goBack(); // no-op: nothing to pop
        QCOMPARE(spy.count(), 3);
        app.goForward(); // canGoBack false → true
        QCOMPARE(spy.count(), 4);
    }

    void depthIsCapped()
    {
        ApplicationController app;
        registerAbc(app);

        // 200 alternating navigations record 199 back entries uncapped;
        // the cap keeps only the most recent 64.
        app.setCurrentPageId(QStringLiteral("a"));
        for (int i = 0; i < 199; ++i) {
            app.setCurrentPageId((i % 2 == 0) ? QStringLiteral("b") : QStringLiteral("a"));
        }

        int hops = 0;
        while (app.canGoBack() && !app.goBack().isEmpty()) {
            ++hops;
        }
        QCOMPARE(hops, 64);
    }
};

QTEST_MAIN(TestApplicationControllerHistory)
#include "test_application_controller_history.moc"
