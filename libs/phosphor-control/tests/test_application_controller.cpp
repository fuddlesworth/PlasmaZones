// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QUrl>

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/PageController.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/StagingDomain.h"

using PhosphorControl::ApplicationController;
using PhosphorControl::PageController;
using PhosphorControl::StagingDomain;

// A named namespace (not anonymous) so these helpers have external
// linkage. Lambdas in the tests below capture StubPage* and are handed
// to connect() / invokeMethod() as template arguments, which gives the
// closure types external linkage; an anonymous-namespace (internal
// linkage) capture member then trips -Wsubobject-linkage. The name is
// file-specific because each test is its own executable (see
// tests/CMakeLists.txt), so it cannot collide with another test's
// helpers.
namespace ApplicationControllerTest {

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
        // Sync stub emits applyResult immediately so
        // applyAllAsync's wait-counter ticks down. Tests pinning the
        // batch-timeout recovery path flip m_emitApplyResult to false
        // to simulate a wedged domain.
        if (m_emitApplyResult)
            Q_EMIT applyResult(true, QString());
    }

    void discard() override
    {
        ++discardCount;
        setDirty(false);
        Q_EMIT discardResult(true, QString());
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

    void setEmitApplyResult(bool shouldEmit)
    {
        m_emitApplyResult = shouldEmit;
    }

    // Public counters for tests to observe — deliberately not m_-prefixed
    // so test sites read as `stub.applyCount` (assertion style) rather
    // than `stub.m_applyCount`, which would suggest direct private-state
    // poking. Private state below follows the project's m_ convention.
    int applyCount = 0;
    int discardCount = 0;
    int resetCount = 0;

private:
    bool m_dirty = false;
    bool m_emitApplyResult = true;
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
        Q_EMIT applyResult(true, QString());
    }
    void discard() override
    {
        ++discardCount;
        setDirty(false);
        Q_EMIT discardResult(true, QString());
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

/// Subclass for the apply-leaves-dirty contract test below — the lib's
/// documented best-effort apply semantics say domains that fail to
/// persist must keep isDirty() true so the global flag survives.
class FailingHeadlessDomain : public StubHeadlessDomain
{
    Q_OBJECT
public:
    using StubHeadlessDomain::StubHeadlessDomain;

    void apply() override
    {
        ++applyCount;
        // Deliberately do not clear m_dirty — caller is on the hook to
        // surface the failure to the user, but the framework must keep
        // dirty set so a subsequent Apply retry hits this domain again.
        Q_EMIT applyResult(false, QStringLiteral("simulated apply failure"));
    }
};

/// Stub headless domain whose discard() deliberately omits the
/// discardResult emit, so the controller's discard batch stays pending
/// — exercises the forceResetAsyncState() recovery path on the discard
/// half of the state machine.
class SilentDiscardDomain : public StubHeadlessDomain
{
    Q_OBJECT
public:
    using StubHeadlessDomain::StubHeadlessDomain;

    void discard() override
    {
        ++discardCount;
        // No discardResult emit — leaves m_discardPending > 0.
    }
};

} // namespace ApplicationControllerTest

using namespace ApplicationControllerTest;

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

    // ── Deep-link reveal latch (pending anchor) ──────────────────────────
    void pendingAnchorConsumeOnceAndKeyed()
    {
        ApplicationController app;
        app.registerPage(new StubPage(QStringLiteral("a")), {}, QStringLiteral("A"), QUrl());
        app.registerPage(new StubPage(QStringLiteral("b")), {}, QStringLiteral("B"), QUrl());

        QSignalSpy spy(&app, &ApplicationController::pendingAnchorChanged);
        app.setCurrentPageId(QStringLiteral("a"));
        app.setPendingAnchor(QStringLiteral("a"), QStringLiteral("foo"));
        QCOMPARE(spy.count(), 1);

        // Wrong page → nothing returned, pending preserved.
        QVERIFY(app.takePendingAnchor(QStringLiteral("b")).isEmpty());
        // Right page → returns the anchor and consumes it.
        QCOMPARE(app.takePendingAnchor(QStringLiteral("a")), QStringLiteral("foo"));
        // Consume-once: a second take is empty.
        QVERIFY(app.takePendingAnchor(QStringLiteral("a")).isEmpty());
    }

    void pendingAnchorDiscardedOnNavigateAway()
    {
        ApplicationController app;
        app.registerPage(new StubPage(QStringLiteral("a")), {}, QStringLiteral("A"), QUrl());
        app.registerPage(new StubPage(QStringLiteral("b")), {}, QStringLiteral("B"), QUrl());

        app.setCurrentPageId(QStringLiteral("a"));
        app.setPendingAnchor(QStringLiteral("a"), QStringLiteral("foo"));
        // Navigating to a different page must drop the stale pending anchor so
        // a never-reached deep link never fires on the wrong page later.
        app.setCurrentPageId(QStringLiteral("b"));
        QVERIFY(app.takePendingAnchor(QStringLiteral("a")).isEmpty());
    }

    void pendingAnchorRejectsEmptyParts()
    {
        ApplicationController app;
        app.registerPage(new StubPage(QStringLiteral("a")), {}, QStringLiteral("A"), QUrl());

        app.setPendingAnchor(QStringLiteral("a"), QString()); // empty anchor → no-op
        QVERIFY(app.takePendingAnchor(QStringLiteral("a")).isEmpty());
        app.setPendingAnchor(QString(), QStringLiteral("x")); // empty page → no-op
        QVERIFY(app.takePendingAnchor(QString()).isEmpty());
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
        app.registerPage(collapsible, {}, QStringLiteral("Cat"), QUrl(), QString(), /*isCollapsible=*/true);
        app.registerPage(drillIn, {}, QStringLiteral("Drill"), QUrl(), QString(), /*isCollapsible=*/false);

        const auto cat = app.registry()->entry(QStringLiteral("cat"));
        const auto drill = app.registry()->entry(QStringLiteral("drill"));
        QVERIFY(cat.isCollapsible);
        QVERIFY(!drill.isCollapsible);

        // Q_INVOKABLE pageData should surface the flag for QML consumers.
        const QVariantMap catData = app.registry()->pageData(QStringLiteral("cat"));
        QCOMPARE(catData.value(QStringLiteral("isCollapsible")).toBool(), true);
    }

    void hasDividerAfterFlowsThroughRegistry()
    {
        ApplicationController app;
        auto* divPage = new StubPage(QStringLiteral("div"));
        app.registerPage(divPage, {}, QStringLiteral("Div"), QUrl(), QString(), /*isCollapsible=*/false,
                         /*hasDividerAfter=*/true);

        const auto entry = app.registry()->entry(QStringLiteral("div"));
        QVERIFY(entry.hasDividerAfter);

        const QVariantMap data = app.registry()->pageData(QStringLiteral("div"));
        QCOMPARE(data.value(QStringLiteral("hasDividerAfter")).toBool(), true);
    }

    void headlessDomainParticipatesInDirty()
    {
        // Regression test: the headless-domain path (registerDomain
        // vs registerPage) must contribute to the global dirty flag
        // exactly the same way as page-backed StagingDomains. Earlier
        // tests only exercised the page path for dirty aggregation.
        ApplicationController app;
        auto* page = new StubPage(QStringLiteral("p"));
        auto* headless = new StubHeadlessDomain();
        app.registerPage(page, {}, QStringLiteral("P"), QUrl());
        app.registerDomain(headless);

        QSignalSpy spy(&app, &ApplicationController::dirtyChanged);
        QVERIFY(!app.isDirty());

        // Toggle dirty ONLY via the headless domain — the page stays
        // clean. Global dirty must still flip.
        headless->setDirty(true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(app.isDirty());

        headless->setDirty(false);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!app.isDirty());
    }

    void rejectsDuplicateDomainRegistration()
    {
        // Registry-level dedup: a domain registered first via
        // registerPage(), then again via registerDomain() must be
        // tracked exactly once. trackDomain's "contains check + warn"
        // is the visible contract; this test pins the no-op return
        // (the dirtyChanged spy fires once per logical toggle, not
        // twice as it would if the connection was duplicated).
        ApplicationController app;
        auto* page = new StubPage(QStringLiteral("p"));
        app.registerPage(page, {}, QStringLiteral("P"), QUrl());
        // Same domain through registerDomain after registerPage —
        // the controller already tracks it via the page registration.
        // This call should warn + no-op rather than connect
        // dirtyChanged a second time.
        app.registerDomain(page);

        QSignalSpy spy(&app, &ApplicationController::dirtyChanged);
        page->setDirty(true);
        // If the dedupe failed, dirtyChanged would fire twice per
        // toggle (one per duplicate dirtyChanged → recomputeDirty
        // edge). We require exactly one.
        QCOMPARE(spy.count(), 1);
    }

    void dedupesViaUniqueConnectionOnDoubleRegisterDomain()
    {
        // Defence-in-depth complement to the registry-level dedup:
        // trackDomain() also passes Qt::UniqueConnection on the
        // dirtyChanged → onDomainDirtyChanged hookup, so even if a
        // future refactor accidentally skipped the m_domains contains
        // check, the signal-side dedup would still catch it.
        //
        // We can't easily *bypass* the contains check without friend
        // access, but registerDomain on a domain that was added via
        // registerPage is the closest reachable scenario — the page's
        // PageController is already in m_domains via registerPage's
        // own trackDomain call, so the second registerDomain triggers
        // BOTH layers of dedup. A regression that broke one layer
        // would still trip the other unless both were broken.
        ApplicationController app;
        auto* page = new StubPage(QStringLiteral("p"));
        app.registerPage(page, {}, QStringLiteral("P"), QUrl());
        app.registerDomain(page);
        app.registerDomain(page); // triple-registration for good measure

        QSignalSpy spy(&app, &ApplicationController::dirtyChanged);
        page->setDirty(true);
        page->setDirty(false);
        // 2 logical edges → exactly 2 dirtyChanged emissions (one per
        // recomputeDirty's value-changed gate). A leaked duplicate
        // connection would multiply the count.
        QCOMPARE(spy.count(), 2);
    }

    void rejectsNullPageRegistration()
    {
        // Pin the nullptr-rejection paths in registerPage/registerDomain.
        // A regression that allowed nullptr through would crash on the
        // first recomputeDirty/applyAll iteration.
        ApplicationController app;
        QSignalSpy spy(&app, &ApplicationController::dirtyChanged);
        app.registerPage(nullptr, {}, QStringLiteral("P"), QUrl());
        app.registerDomain(nullptr);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(app.isDirty(), false);
    }

    void resetCurrentPageOnEmptyIdIsNoOp()
    {
        // Pins the empty-id early-return branch in resetCurrentPage —
        // a regression removing the guard would deref a null controller
        // lookup.
        ApplicationController app;
        auto* page = new StubPage(QStringLiteral("p"));
        app.registerPage(page, {}, QStringLiteral("P"), QUrl());
        // currentPageId is empty by default; resetCurrentPage should
        // not invoke resetToDefaults on any registered page.
        app.resetCurrentPage();
        QCOMPARE(page->resetCount, 0);
    }

    void applyAllSurvivesIteratorInvalidation()
    {
        // Pin the snapshot in applyAll/discardAll: domain->apply() fires
        // dirtyChanged synchronously, which routes through
        // onDomainDirtyChanged → recomputeDirty(), which may erase null
        // QPointer entries from m_domains while the outer range-for is
        // still iterating. Without the snapshot the outer iterators get
        // invalidated mid-iteration. This test pins the no-crash + all-
        // domains-applied contract by destroying a domain inline before
        // applyAll runs — recomputeDirty's null-prune happens during
        // the loop, and every still-live domain must still apply.
        ApplicationController app;
        auto* keepA = new StubPage(QStringLiteral("a"));
        auto* doomed = new StubHeadlessDomain();
        auto* keepB = new StubPage(QStringLiteral("b"));
        app.registerPage(keepA, {}, QStringLiteral("A"), QUrl());
        app.registerDomain(doomed);
        app.registerPage(keepB, {}, QStringLiteral("B"), QUrl());

        keepA->setDirty(true);
        doomed->setDirty(true);
        keepB->setDirty(true);

        // Destroy doomed BEFORE applyAll so m_domains carries a null
        // QPointer entry. recomputeDirty will compact it during apply
        // dispatch — the snapshot guarantees the outer loop keeps
        // walking the original list.
        delete doomed;

        app.applyAll();

        QCOMPARE(keepA->applyCount, 1);
        QCOMPARE(keepB->applyCount, 1);
        QVERIFY(!app.isDirty());

        // Post-condition: a subsequent registerPage must still work —
        // the recomputeDirty compaction during apply should have
        // removed the stale null QPointer, leaving m_domains in a
        // healthy state for further mutations.
        auto* fresh = new StubPage(QStringLiteral("c"));
        app.registerPage(fresh, {}, QStringLiteral("C"), QUrl());
        fresh->setDirty(true);
        QVERIFY(app.isDirty());
        app.applyAll();
        QCOMPARE(fresh->applyCount, 1);
        QVERIFY(!app.isDirty());
    }

    void applyAllAsyncEmitsCompleteOnAllSync()
    {
        // Pin the F#2 chrome contract: applyAllAsync calls each dirty
        // domain's apply(), waits for every applyResult to land, then
        // emits applyAllComplete(ok, errors) exactly once. With three
        // sync domains the whole thing completes in the same event-
        // loop turn.
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* b = new StubPage(QStringLiteral("b"));
        auto* headless = new StubHeadlessDomain();
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());
        app.registerPage(b, {}, QStringLiteral("B"), QUrl());
        app.registerDomain(headless);

        a->setDirty(true);
        b->setDirty(true);
        headless->setDirty(true);

        QSignalSpy completeSpy(&app, &ApplicationController::applyAllComplete);
        QSignalSpy applyingSpy(&app, &ApplicationController::applyingChanged);
        app.applyAllAsync();

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(completeSpy.first().at(0).toBool(), true);
        QCOMPARE(completeSpy.first().at(1).toStringList().size(), 0);
        QCOMPARE(a->applyCount, 1);
        QCOMPARE(b->applyCount, 1);
        QCOMPARE(headless->applyCount, 1);
        QVERIFY(!app.isDirty());
        // applying transitions exactly false→true→false: two emits.
        // Tightened from `>= 2` so a regression that adds spurious
        // applyingChanged emissions (e.g. an extra toggle inside the
        // completion helper) trips the assertion.
        QCOMPARE(applyingSpy.count(), 2);
        QVERIFY(!app.isApplying());
    }

    void applyAllAsyncCollectsFailureErrors()
    {
        // FailingHeadlessDomain emits applyResult(false, error) so the
        // batch should complete with ok=false and the error string in
        // the errors list. The clean domain's apply still counts.
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* failing = new FailingHeadlessDomain();
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());
        app.registerDomain(failing);

        a->setDirty(true);
        failing->setDirty(true);

        QSignalSpy completeSpy(&app, &ApplicationController::applyAllComplete);
        app.applyAllAsync();

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(completeSpy.first().at(0).toBool(), false);
        const QStringList errors = completeSpy.first().at(1).toStringList();
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.first(), QStringLiteral("simulated apply failure"));
        // The failing domain kept itself dirty, so global stays dirty.
        QVERIFY(app.isDirty());
        QVERIFY(!app.isApplying());
    }

    void applyAllAsyncOnCleanEmitsImmediately()
    {
        // No dirty domains → applyAllComplete fires synchronously with
        // ok=true. Chrome can rely on this so a "save when not dirty"
        // path doesn't hang in a saving-forever state.
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());

        QSignalSpy completeSpy(&app, &ApplicationController::applyAllComplete);
        QSignalSpy applyingSpy(&app, &ApplicationController::applyingChanged);
        app.applyAllAsync();

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(completeSpy.first().at(0).toBool(), true);
        QCOMPARE(a->applyCount, 0); // not dirty → not called
        // Even on a no-op batch we drive applyingChanged through the
        // full false→true→false transition so consumers binding "show
        // toast on applyingChanged false→true" see the tick.
        // Documented in ApplicationController.h::applyingChanged.
        QCOMPARE(applyingSpy.count(), 2);
        QVERIFY(!app.isApplying());
    }

    void discardAllAsyncEmitsComplete()
    {
        // Symmetric to applyAllAsyncEmitsCompleteOnAllSync — sync
        // domains report discardResult immediately, complete fires
        // on the same event-loop turn.
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* headless = new StubHeadlessDomain();
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());
        app.registerDomain(headless);

        a->setDirty(true);
        headless->setDirty(true);

        QSignalSpy completeSpy(&app, &ApplicationController::discardAllComplete);
        QSignalSpy discardingSpy(&app, &ApplicationController::discardingChanged);
        app.discardAllAsync();

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(completeSpy.first().at(0).toBool(), true);
        QCOMPARE(a->discardCount, 1);
        QCOMPARE(headless->discardCount, 1);
        QVERIFY(!app.isDirty());
        // discarding flag drives the chrome's "Discarding…" UX;
        // matches the apply path's pinning of applyingChanged.
        QCOMPARE(discardingSpy.count(), 2);
        QVERIFY(!app.isDiscarding());
    }

    void asyncBatchTimeoutEmitsCompleteWithErrors()
    {
        // Pin the kAsyncBatchTimeoutMs recovery path. A domain that
        // never reports applyResult AND whose lifetime spans the
        // timeout must land in the completion handler with ok=false
        // and a synthesised error per pending domain, so the chrome's
        // "Saving…" indicator can't pin forever.
        ApplicationController app;
        // Override the default 60 s with 80 ms — short enough to keep
        // the test under a second but long enough to outlast the
        // first event-loop turn the spy reads on.
        app.setAsyncBatchTimeoutMs(80);

        auto* silent = new StubPage(QStringLiteral("silent"));
        silent->setEmitApplyResult(false);
        // Stamping objectName exercises the "Domain %1 did not report
        // apply completion within timeout" branch in
        // applicationcontroller.cpp's timeout handler — see the
        // unnamed-vs-named arms around line 282 — instead of the
        // generic message. Asserting the name appears in the error
        // pins the named-domain branch.
        silent->setObjectName(QStringLiteral("SilentDomain"));
        app.registerPage(silent, {}, QStringLiteral("Silent"), QUrl());
        silent->setDirty(true);

        QSignalSpy completeSpy(&app, &ApplicationController::applyAllComplete);
        app.applyAllAsync();
        QVERIFY(completeSpy.wait(2000));
        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(completeSpy.first().at(0).toBool(), false);
        const QStringList errors = completeSpy.first().at(1).toStringList();
        QCOMPARE(errors.size(), 1);
        // The named-domain timeout message must mention both the
        // object name AND the word "timeout" (case-insensitive on the
        // word "timeout" would tighten the contract further, but the
        // current message uses lowercase).
        QVERIFY2(errors.first().contains(QStringLiteral("timeout")),
                 qPrintable(QStringLiteral("expected timeout in error, got: ") + errors.first()));
        QVERIFY2(errors.first().contains(QStringLiteral("SilentDomain")),
                 qPrintable(QStringLiteral("expected SilentDomain in error, got: ") + errors.first()));
    }

    void registerPageMidBatchIsSafe()
    {
        // Registering a new dirty domain DURING an applyAllAsync (from a
        // slot connected to applyingChanged on the way to true) must
        // not destabilise the in-flight batch. The new domain isn't
        // captured in the batch's snapshot — it's a fresh entrant that
        // contributes its dirty bit to the global flag and gets
        // committed on the NEXT applyAllAsync. This test pins that
        // contract so a regression where mid-batch registration tries
        // to mutate the iteration target doesn't ship.
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());
        a->setDirty(true);

        // Late entrant — a slot dispatched on applyingChanged=true
        // registers a new dirty page mid-batch.
        StubPage* late = nullptr;
        connect(&app, &ApplicationController::applyingChanged, &app, [&app, &late]() {
            if (app.isApplying() && !late) {
                late = new StubPage(QStringLiteral("late"));
                app.registerPage(late, {}, QStringLiteral("Late"), QUrl());
                late->setDirty(true);
            }
        });

        QSignalSpy completeSpy(&app, &ApplicationController::applyAllComplete);
        app.applyAllAsync();

        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(a->applyCount, 1);
        // The late entrant did NOT participate in the just-completed
        // batch — it joined after the dirty-snapshot was taken.
        QVERIFY(late);
        QCOMPARE(late->applyCount, 0);
        // Its dirty state contributes to the global flag.
        QVERIFY(app.isDirty());
    }

    void applyAndDiscardAreMutuallyExclusive()
    {
        // Pin the cross-batch mutex: applyAllAsync must refuse when
        // discardAllAsync is already in flight (and vice versa).
        // Without the guard both batches would share m_inTransaction
        // and per-domain outstanding sets, corrupting either book.
        // Test via a domain that holds discardResult indefinitely so
        // m_discarding stays true while applyAllAsync attempts to
        // start a parallel batch.
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());

        // Suppress applyResult AND override discard() to skip the
        // discardResult emit too — keeps both batches "in flight"
        // from the controller's view, but with a tight timeout so
        // the test doesn't hang.
        a->setEmitApplyResult(false);
        app.setAsyncBatchTimeoutMs(100);
        a->setDirty(true);

        QSignalSpy applyCompleteSpy(&app, &ApplicationController::applyAllComplete);
        // First, start applyAllAsync (which won't complete because
        // m_emitApplyResult=false and timeout is 100ms).
        app.applyAllAsync();
        QVERIFY(app.isApplying());

        // While apply is in flight, discardAllAsync must no-op.
        QSignalSpy discardCompleteSpy(&app, &ApplicationController::discardAllComplete);
        app.discardAllAsync();
        QCOMPARE(discardCompleteSpy.count(), 0);
        QVERIFY(!app.isDiscarding());
        QVERIFY(app.isApplying());

        // Same in reverse: sync applyAll while apply is in flight
        // should refuse without crashing.
        app.applyAll(); // refused, no-op
        QVERIFY(app.isApplying());

        // Let the timeout fire so the test cleans up.
        QVERIFY(applyCompleteSpy.wait(2000));
        QVERIFY(!app.isApplying());
    }

    void forceResetAsyncStateClearsApplyingMidBatch()
    {
        // Start an apply batch that won't complete (silent stub +
        // generous timeout), call forceResetAsyncState during, verify
        // applying flips back to false and applyAllComplete fires with
        // ok=false + a synthesised error.
        ApplicationController app;
        // Keep timeout long so the test exercises the explicit
        // force-reset path rather than racing the timeout fire.
        app.setAsyncBatchTimeoutMs(60'000);

        auto* silent = new StubPage(QStringLiteral("silent"));
        silent->setEmitApplyResult(false);
        app.registerPage(silent, {}, QStringLiteral("S"), QUrl());
        silent->setDirty(true);

        QSignalSpy completeSpy(&app, &ApplicationController::applyAllComplete);
        QSignalSpy applyingSpy(&app, &ApplicationController::applyingChanged);
        app.applyAllAsync();
        QVERIFY(app.isApplying());
        QCOMPARE(applyingSpy.count(), 1); // false→true

        app.forceResetAsyncState();
        QVERIFY(!app.isApplying());
        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(completeSpy.first().at(0).toBool(), false);
        const QStringList errors = completeSpy.first().at(1).toStringList();
        QVERIFY(!errors.isEmpty());
        // applying transitions true→false on the reset.
        QCOMPARE(applyingSpy.count(), 2);
    }

    void forceResetAsyncStateClearsDiscardingMidBatch()
    {
        // Symmetric forceResetAsyncState test for the discardAllAsync
        // half of the state machine — pins the `else if (m_discarding)`
        // branch (around line 552 in applicationcontroller.cpp).
        ApplicationController app;
        app.setAsyncBatchTimeoutMs(60'000);

        // SilentDiscardDomain (defined in the ApplicationControllerTest
        // namespace at top of TU) never emits discardResult, so the
        // discard batch stays pending until force-reset.
        auto* silent = new SilentDiscardDomain();
        app.registerDomain(silent);
        silent->setDirty(true);

        QSignalSpy completeSpy(&app, &ApplicationController::discardAllComplete);
        QSignalSpy discardingSpy(&app, &ApplicationController::discardingChanged);
        app.discardAllAsync();
        QVERIFY(app.isDiscarding());

        app.forceResetAsyncState();
        QVERIFY(!app.isDiscarding());
        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(completeSpy.first().at(0).toBool(), false);
        QVERIFY(!completeSpy.first().at(1).toStringList().isEmpty());
        QCOMPARE(discardingSpy.count(), 2);
    }

    void setAsyncBatchTimeoutMsRefusesNonPositive()
    {
        // Pin the qWarning + no-op return for <= 0 inputs. Without
        // the guard a zero/negative timeout would route into QTimer
        // and either fire-instantly or block-forever, both of which
        // would corrupt the async batch's accounting.
        ApplicationController app;
        const int originalTimeout = app.asyncBatchTimeoutMs();

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing non-positive value")));
        app.setAsyncBatchTimeoutMs(-1);
        QCOMPARE(app.asyncBatchTimeoutMs(), originalTimeout);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing non-positive value")));
        app.setAsyncBatchTimeoutMs(0);
        QCOMPARE(app.asyncBatchTimeoutMs(), originalTimeout);

        // Positive values are accepted — sanity check that the guard
        // isn't over-eager.
        app.setAsyncBatchTimeoutMs(123);
        QCOMPARE(app.asyncBatchTimeoutMs(), 123);
    }

    void gotoNavigationOnEmptyRegistryReturnsEmpty()
    {
        // collectNavigable's empty-list early-return must surface as
        // an empty QString from both navigation directions. Without
        // the guard a regression would index into an empty list and
        // UB.
        ApplicationController app;
        QCOMPARE(app.gotoPreviousPage(), QString());
        QCOMPARE(app.gotoNextPage(), QString());
        QVERIFY(app.currentPageId().isEmpty());
    }

    void setCurrentPageIdSameIdIsNoOp()
    {
        // The setter's "no change" early-return must NOT emit
        // currentPageIdChanged. Without the guard QML bindings
        // rebound to currentPageId would spuriously re-evaluate on
        // every assignment, including identity assignments from
        // Sidebar.qml's currentIndex change handlers.
        ApplicationController app;
        auto* page = new StubPage(QStringLiteral("p"));
        app.registerPage(page, {}, QStringLiteral("P"), QUrl());

        app.setCurrentPageId(QStringLiteral("p"));
        QCOMPARE(app.currentPageId(), QStringLiteral("p"));

        QSignalSpy spy(&app, &ApplicationController::currentPageIdChanged);
        app.setCurrentPageId(QStringLiteral("p"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(app.currentPageId(), QStringLiteral("p"));
    }

    void crossThreadDomainRegistrationIsRefused()
    {
        // registerPage refuses pages living on a non-controller thread
        // (cross-thread dirtyChanged → recomputeDirty would race
        // m_domains mutation). Verify the rejection side-effect: the
        // page is NOT in the registry, and pageRegistered did not fire.
        ApplicationController app;
        QThread worker;
        worker.start();
        auto* foreignPage = new StubPage(QStringLiteral("foreign"));
        foreignPage->moveToThread(&worker);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("cross-thread page refused")));
        QSignalSpy registeredSpy(app.registry(), &PhosphorControl::PageRegistry::pageRegistered);
        app.registerPage(foreignPage, {}, QStringLiteral("F"), QUrl());

        // Contract: the page-level cross-thread guard returns before
        // any registry entry is created, so the page must not be
        // queryable through the registry.
        QVERIFY(!app.registry()->hasPage(QStringLiteral("foreign")));
        QCOMPARE(registeredSpy.count(), 0);

        // Tear down the foreign page on its own thread to avoid
        // cross-thread destruction UB.
        QMetaObject::invokeMethod(
            foreignPage,
            [foreignPage]() {
                foreignPage->deleteLater();
            },
            Qt::QueuedConnection);
        worker.quit();
        worker.wait(2000);
    }

    void applyAllOnPartialFailureKeepsDirty()
    {
        // Pin the documented best-effort semantics: a domain whose
        // apply() leaves itself dirty must keep the global dirty flag
        // set so the user can retry. This is the contract
        // PlasmaZones::SettingsStagingDomain relies on for the
        // save-failed-keep-dirty UX path.
        ApplicationController app;
        auto* a = new StubPage(QStringLiteral("a"));
        auto* failing = new FailingHeadlessDomain();
        app.registerPage(a, {}, QStringLiteral("A"), QUrl());
        app.registerDomain(failing);

        a->setDirty(true);
        failing->setDirty(true);
        QVERIFY(app.isDirty());

        app.applyAll();
        QCOMPARE(a->applyCount, 1);
        QCOMPARE(failing->applyCount, 1);
        // a applied → clean. failing kept itself dirty → global must
        // still be dirty.
        QVERIFY(app.isDirty());
    }
};

QTEST_MAIN(TestApplicationController)
#include "test_application_controller.moc"
