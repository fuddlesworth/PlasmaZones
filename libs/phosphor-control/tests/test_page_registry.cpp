// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "PhosphorControl/PageController.h"
#include "PhosphorControl/PageRegistry.h"

using PhosphorControl::PageController;
using PhosphorControl::PageRegistry;

namespace {

/** Minimal PageController concretion that lets tests pretend to be dirty
 *  on demand. The lib's PageController is abstract, so tests need a stub. */
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
        setDirty(false);
    }
    void discard() override
    {
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

private:
    bool m_dirty = false;
};

// ── Simple/advanced visibility surface ─────────────────────────────
// Registers one Always leaf, one AdvancedOnly leaf, one SimpleOnly leaf
// (counterpart pair), and a virtual category whose only leaf is
// AdvancedOnly — the fixture for the tier filter, the empty-category
// auto-hide, pageAllowedInCurrentMode, and firstVisibleLeafId.
// Returns false at the first rejected registration so callers can
// QVERIFY it: QTEST_FAIL_ACTION is a bare `return`, which would only exit
// this helper and let the caller keep asserting against a half-built
// registry.
bool buildTieredRegistry(PageRegistry& reg)
{
    using PV = PageRegistry::PageVisibility;
    auto* always = new StubPage(QStringLiteral("home"), &reg);
    if (!reg.registerPage(
            {QStringLiteral("home"), {}, QStringLiteral("Home"), {}, QUrl(QStringLiteral("qrc:/Home.qml")), always}))
        return false;

    auto* simple = new StubPage(QStringLiteral("easy"), &reg);
    PageRegistry::Entry simpleEntry{
        QStringLiteral("easy"), {}, QStringLiteral("Easy"), {}, QUrl(QStringLiteral("qrc:/Easy.qml")), simple};
    simpleEntry.visibility = PV::SimpleOnly;
    simpleEntry.counterpartId = QStringLiteral("full");
    if (!reg.registerPage(std::move(simpleEntry)))
        return false;

    auto* full = new StubPage(QStringLiteral("full"), &reg);
    PageRegistry::Entry fullEntry{
        QStringLiteral("full"), {}, QStringLiteral("Full"), {}, QUrl(QStringLiteral("qrc:/Full.qml")), full};
    fullEntry.visibility = PV::AdvancedOnly;
    fullEntry.counterpartId = QStringLiteral("easy");
    if (!reg.registerPage(std::move(fullEntry)))
        return false;

    auto* cat = new StubPage(QStringLiteral("cat"), &reg);
    if (!reg.registerPage({QStringLiteral("cat"), {}, QStringLiteral("Category"), {}, QUrl(), cat}))
        return false;
    auto* deep = new StubPage(QStringLiteral("cat.deep"), &reg);
    PageRegistry::Entry deepEntry{QStringLiteral("cat.deep"),
                                  QStringLiteral("cat"),
                                  QStringLiteral("Deep"),
                                  {},
                                  QUrl(QStringLiteral("qrc:/Deep.qml")),
                                  deep};
    deepEntry.visibility = PV::AdvancedOnly;
    if (!reg.registerPage(std::move(deepEntry)))
        return false;
    return true;
}
} // namespace

class TestPageRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registersTopLevelPage()
    {
        PageRegistry reg;
        QSignalSpy spy(&reg, &PageRegistry::pageRegistered);

        auto* page = new StubPage(QStringLiteral("general"), &reg);
        reg.registerPage({QStringLiteral("general"),
                          QString(),
                          QStringLiteral("General"),
                          {},
                          QUrl(QStringLiteral("qrc:/General.qml")),
                          page});

        QCOMPARE(spy.count(), 1);
        QVERIFY(reg.hasPage(QStringLiteral("general")));
        QCOMPARE(reg.controller(QStringLiteral("general")), page);
        QCOMPARE(reg.topLevelPages().size(), 1);
        QCOMPARE(reg.topLevelPages().first().title, QStringLiteral("General"));
    }

    void rejectsDuplicateId()
    {
        PageRegistry reg;
        auto* a = new StubPage(QStringLiteral("dup"), &reg);
        auto* b = new StubPage(QStringLiteral("dup"), &reg);

        reg.registerPage({QStringLiteral("dup"), {}, QStringLiteral("A"), {}, QUrl(), a});
        reg.registerPage({QStringLiteral("dup"), {}, QStringLiteral("B"), {}, QUrl(), b});

        // First registration wins; second is dropped.
        QCOMPARE(reg.controller(QStringLiteral("dup")), a);
        QCOMPARE(reg.allPages().size(), 1);
    }

    void rejectsUnknownParent()
    {
        // Stack-order matters: originalParent declared BEFORE reg so it
        // outlives reg's destruction (RAII destroys in reverse decl
        // order). The child is parented to originalParent — if the
        // registry's rejection path is buggy and re-parents on failure,
        // child would end up owned by reg and originalParent's stack
        // destruction wouldn't reach it. With the bug, double-delete
        // would also happen on reg destruction. We assert parent
        // didn't change before either gets destroyed.
        QObject originalParent;
        PageRegistry reg;
        auto* child = new StubPage(QStringLiteral("child"), &originalParent);

        QSignalSpy spy(&reg, &PageRegistry::pageRegistered);
        reg.registerPage(
            {QStringLiteral("child"), QStringLiteral("ghost-parent"), QStringLiteral("Child"), {}, QUrl(), child});

        QVERIFY(!reg.hasPage(QStringLiteral("child")));
        // pageRegistered must NOT fire on a rejection path — a
        // regression that emitted the signal pre-validation would
        // surface here.
        QCOMPARE(spy.count(), 0);
        // The controller's QObject parent must be untouched on the
        // rejection path. A regression where the registry reparents
        // before validating the parentId would surface here as
        // child->parent() == &reg.
        QCOMPARE(child->parent(), &originalParent);
    }

    void exposesChildPages()
    {
        PageRegistry reg;
        auto* parent = new StubPage(QStringLiteral("snap"), &reg);
        auto* childA = new StubPage(QStringLiteral("snap.behavior"), &reg);
        auto* childB = new StubPage(QStringLiteral("snap.appearance"), &reg);
        auto* other = new StubPage(QStringLiteral("anim"), &reg);

        reg.registerPage({QStringLiteral("snap"), {}, QStringLiteral("Snapping"), {}, QUrl(), parent});
        reg.registerPage(
            {QStringLiteral("snap.behavior"), QStringLiteral("snap"), QStringLiteral("Behavior"), {}, QUrl(), childA});
        reg.registerPage({QStringLiteral("snap.appearance"),
                          QStringLiteral("snap"),
                          QStringLiteral("Appearance"),
                          {},
                          QUrl(),
                          childB});
        reg.registerPage({QStringLiteral("anim"), {}, QStringLiteral("Animations"), {}, QUrl(), other});

        const auto children = reg.childPages(QStringLiteral("snap"));
        QCOMPARE(children.size(), 2);
        QCOMPARE(reg.topLevelPages().size(), 2);
    }

    void rejectsEmptyId()
    {
        PageRegistry reg;
        auto* page = new StubPage(QStringLiteral("ignored"), &reg);
        reg.registerPage({QString(), {}, QStringLiteral("Empty"), {}, QUrl(), page});

        QVERIFY(reg.allPages().isEmpty());
    }

    void rejectsParentNotYetRegistered()
    {
        // This pins the "parent must already be registered" guard
        // rather than a dedicated self-parent cycle guard — the
        // registry rejects parentId="self" + id="self" because
        // "self" hasn't been registered yet at the time the parent
        // existence check runs. Renamed from `rejectsSelfParent` so
        // a future maintainer doesn't read it as "registry has a
        // dedicated self-cycle guard."
        PageRegistry reg;
        auto* page = new StubPage(QStringLiteral("self"), &reg);
        reg.registerPage({QStringLiteral("self"), QStringLiteral("self"), QStringLiteral("Self"), {}, QUrl(), page});

        QVERIFY(!reg.hasPage(QStringLiteral("self")));
    }

    void rejectsNullController()
    {
        PageRegistry reg;
        QSignalSpy spy(&reg, &PageRegistry::pageRegistered);
        reg.registerPage({QStringLiteral("nullctrl"), {}, QStringLiteral("Null"), {}, QUrl(), nullptr});

        QCOMPARE(spy.count(), 0);
        QVERIFY(!reg.hasPage(QStringLiteral("nullctrl")));
    }

    void rejectsDuplicateControllerUnderDifferentIds()
    {
        // Registering the same PageController* under two ids creates two
        // sidebar rows that share one dirty bit (only the first
        // registration ends up in ApplicationController::m_domains), a
        // shape the UI cannot render coherently. The registry must
        // refuse the second registration.
        PageRegistry reg;
        QSignalSpy spy(&reg, &PageRegistry::pageRegistered);
        auto* page = new StubPage(QStringLiteral("first"), &reg);

        QVERIFY(reg.registerPage({QStringLiteral("first"), {}, QStringLiteral("First"), {}, QUrl(), page}));
        // Same controller, different id — must be rejected.
        QVERIFY(!reg.registerPage({QStringLiteral("second"), {}, QStringLiteral("Second"), {}, QUrl(), page}));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(reg.allPages().size(), 1);
        QVERIFY(reg.hasPage(QStringLiteral("first")));
        QVERIFY(!reg.hasPage(QStringLiteral("second")));
    }

    void hasDividerAfterRoundTrips()
    {
        PageRegistry reg;
        auto* page = new StubPage(QStringLiteral("p1"), &reg);
        PageRegistry::Entry entry{QStringLiteral("p1"), {}, QStringLiteral("P1"), {}, QUrl(), page};
        entry.hasDividerAfter = true;
        reg.registerPage(std::move(entry));

        // Round-trip through both the C++ Entry accessor and the
        // QML-facing QVariantMap. Both must surface the flag —
        // The QML serialization contract, exercised here because no in-tree
        // consumer calls these two any more (see the header).
        QVERIFY(reg.entry(QStringLiteral("p1")).hasDividerAfter);
        QCOMPARE(reg.pageData(QStringLiteral("p1")).value(QStringLiteral("hasDividerAfter")).toBool(), true);
    }

    void allPagesDataReturnsFlatList()
    {
        // allPagesData feeds the lib's apply-on-close failure toast
        // (SettingsAppWindow.collectDirtyPageIds) — must enumerate
        // every registered page in insertion order so consumers can
        // iterate without first walking topLevel + child layers.
        PageRegistry reg;
        auto* a = new StubPage(QStringLiteral("a"), &reg);
        auto* b = new StubPage(QStringLiteral("a.b"), &reg);
        auto* c = new StubPage(QStringLiteral("c"), &reg);
        reg.registerPage({QStringLiteral("a"), {}, QStringLiteral("A"), {}, QUrl(), a});
        reg.registerPage({QStringLiteral("a.b"), QStringLiteral("a"), QStringLiteral("B"), {}, QUrl(), b});
        reg.registerPage({QStringLiteral("c"), {}, QStringLiteral("C"), {}, QUrl(), c});

        const auto all = reg.allPagesData();
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("a"));
        QCOMPARE(all.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("a.b"));
        QCOMPARE(all.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("c"));
    }

    void qmlAccessorsCoverEverything()
    {
        // The QML serialization contract for all three Q_INVOKABLE
        // accessors. pageData is live (PageHost.qml); topLevelPagesData
        // and childPagesData are retained API with no in-tree consumer
        // (see the header), so this test is what keeps their key set
        // from rotting. They must surface every entry field.
        PageRegistry reg;
        auto* p = new StubPage(QStringLiteral("p"), &reg);
        auto* c = new StubPage(QStringLiteral("p.c"), &reg);
        reg.registerPage({QStringLiteral("p"), {}, QStringLiteral("P"), QStringLiteral("monitor"), QUrl(), p});
        PageRegistry::Entry childEntry{
            QStringLiteral("p.c"), QStringLiteral("p"), QStringLiteral("C"), {}, QUrl(QStringLiteral("qrc:/c.qml")), c};
        childEntry.isCollapsible = true;
        childEntry.hasDividerAfter = true;
        reg.registerPage(std::move(childEntry));

        const auto top = reg.topLevelPagesData();
        QCOMPARE(top.size(), 1);
        const auto child = reg.childPagesData(QStringLiteral("p"));
        QCOMPARE(child.size(), 1);
        const auto leaf = reg.pageData(QStringLiteral("p.c"));
        QCOMPARE(leaf.value(QStringLiteral("id")).toString(), QStringLiteral("p.c"));
        QCOMPARE(leaf.value(QStringLiteral("parentId")).toString(), QStringLiteral("p"));
        QCOMPARE(leaf.value(QStringLiteral("isCollapsible")).toBool(), true);
        QCOMPARE(leaf.value(QStringLiteral("hasDividerAfter")).toBool(), true);
        QCOMPARE(leaf.value(QStringLiteral("hasQmlSource")).toBool(), true);
    }

    void tierFilterFollowsShowAdvanced()
    {
        PageRegistry reg;
        QVERIFY(buildTieredRegistry(reg));

        // Registry default is show-everything-advanced: SimpleOnly hidden.
        QVERIFY(reg.showAdvanced());
        auto ids = [](const QVariantList& rows) {
            QStringList out;
            for (const auto& row : rows)
                out << row.toMap().value(QStringLiteral("id")).toString();
            return out;
        };
        QCOMPARE(ids(reg.topLevelPagesData()),
                 (QStringList{QStringLiteral("home"), QStringLiteral("full"), QStringLiteral("cat")}));

        QSignalSpy spy(&reg, &PageRegistry::showAdvancedChanged);
        reg.setShowAdvanced(false);
        QCOMPARE(spy.count(), 1);
        // Simple mode: AdvancedOnly leaves drop out, the SimpleOnly leaf
        // appears, and the category auto-hides because its only leaf is
        // filtered (empty-virtual-category rule).
        QCOMPARE(ids(reg.topLevelPagesData()), (QStringList{QStringLiteral("home"), QStringLiteral("easy")}));
        QVERIFY(reg.childPagesData(QStringLiteral("cat")).isEmpty());
        // Unchanged value must not re-emit.
        reg.setShowAdvanced(false);
        QCOMPARE(spy.count(), 1);
        // The unfiltered accessors keep resolving hidden pages (a shown or
        // dirty page must always resolve).
        QVERIFY(!reg.pageData(QStringLiteral("full")).isEmpty());
        QVERIFY(reg.controller(QStringLiteral("full")) != nullptr);
    }

    void pageAllowedInCurrentModeIsATierFilterOnly()
    {
        PageRegistry reg;
        QVERIFY(buildTieredRegistry(reg));

        reg.setShowAdvanced(false);
        QVERIFY(reg.pageAllowedInCurrentMode(QStringLiteral("home")));
        QVERIFY(reg.pageAllowedInCurrentMode(QStringLiteral("easy")));
        QVERIFY(!reg.pageAllowedInCurrentMode(QStringLiteral("full")));
        // Unknown ids express no opinion — existence is hasPage's job.
        QVERIFY(reg.pageAllowedInCurrentMode(QStringLiteral("no-such-page")));

        reg.setShowAdvanced(true);
        QVERIFY(!reg.pageAllowedInCurrentMode(QStringLiteral("easy")));
        QVERIFY(reg.pageAllowedInCurrentMode(QStringLiteral("full")));
    }

    void firstVisibleLeafFollowsMode()
    {
        PageRegistry reg;
        QVERIFY(buildTieredRegistry(reg));

        // Root search skips whatever the mode hides, in registration order.
        reg.setShowAdvanced(true);
        QCOMPARE(reg.firstVisibleLeafId(QString()), QStringLiteral("home"));
        QCOMPARE(reg.firstVisibleLeafId(QStringLiteral("cat")), QStringLiteral("cat.deep"));

        reg.setShowAdvanced(false);
        // The category's only leaf is advanced-only: nothing to land on.
        QCOMPARE(reg.firstVisibleLeafId(QStringLiteral("cat")), QString());
        QCOMPARE(reg.firstVisibleLeafId(QString()), QStringLiteral("home"));
    }

    void counterpartIdIsStoredVerbatim()
    {
        PageRegistry reg;
        QVERIFY(buildTieredRegistry(reg));
        QCOMPARE(reg.entry(QStringLiteral("easy")).counterpartId, QStringLiteral("full"));
        QCOMPARE(reg.entry(QStringLiteral("full")).counterpartId, QStringLiteral("easy"));
        // Assert the page EXISTS before asserting its counterpart is empty:
        // entry() returns a default-constructed Entry for an unknown id, so
        // the bare QCOMPARE would also pass on a broken fixture.
        QVERIFY(reg.hasPage(QStringLiteral("home")));
        QCOMPARE(reg.entry(QStringLiteral("home")).counterpartId, QString());
    }

    void validateCounterpartsAcceptsAReciprocalPair()
    {
        // The fixture's easy ↔ full pair is reciprocal and opposite-tier,
        // which is the shape every real counterpart declaration must have.
        PageRegistry reg;
        QVERIFY(buildTieredRegistry(reg));
        QVERIFY(reg.validateCounterparts());
    }

    // Four of the six broken shapes; the other two have their own tests below
    // (they need multi-entry fixtures that do not fit this block form).
    //
    // Each block trips ONE branch of validateCounterparts. The function
    // returns a single bool for all six, so a QVERIFY(!ok) alone cannot tell
    // which branch fired — a fixture usually trips several as collateral, and
    // pass-6 mutation testing found two branches whose deletion left this
    // green. QTest::ignoreMessage binds the assertion to the specific warning:
    // it FAILS the test if the named message is not emitted, which is exactly
    // the per-branch isolation the bool cannot give.
    void validateCounterpartsRejectsFourBrokenShapes()
    {
        using PV = PageRegistry::PageVisibility;

        // Counterpart names a page that was never registered. Nothing else in
        // the stack checks this: the mode gate just falls back, so the typo is
        // indistinguishable from correct operation without this warning.
        {
            PageRegistry reg;
            auto* p = new StubPage(QStringLiteral("a"), &reg);
            PageRegistry::Entry e{
                QStringLiteral("a"), {}, QStringLiteral("A"), {}, QUrl(QStringLiteral("qrc:/A.qml")), p};
            e.visibility = PV::SimpleOnly;
            e.counterpartId = QStringLiteral("typo");
            QVERIFY(reg.registerPage(std::move(e)));
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("which is not registered")));
            QVERIFY(!reg.validateCounterparts());
        }

        // Self-reference: flipping mode would redirect to the page just hidden.
        {
            PageRegistry reg;
            auto* p = new StubPage(QStringLiteral("a"), &reg);
            PageRegistry::Entry e{
                QStringLiteral("a"), {}, QStringLiteral("A"), {}, QUrl(QStringLiteral("qrc:/A.qml")), p};
            e.visibility = PV::SimpleOnly;
            e.counterpartId = QStringLiteral("a");
            QVERIFY(reg.registerPage(std::move(e)));
            QTest::ignoreMessage(QtWarningMsg,
                                 QRegularExpression(QStringLiteral("declares itself as its own counterpart")));
            QVERIFY(!reg.validateCounterparts());
        }

        // Same tier on both sides: the counterpart is hidden in exactly the
        // mode the redirect is trying to escape to.
        {
            PageRegistry reg;
            auto* p1 = new StubPage(QStringLiteral("a"), &reg);
            PageRegistry::Entry e1{
                QStringLiteral("a"), {}, QStringLiteral("A"), {}, QUrl(QStringLiteral("qrc:/A.qml")), p1};
            e1.visibility = PV::SimpleOnly;
            e1.counterpartId = QStringLiteral("b");
            QVERIFY(reg.registerPage(std::move(e1)));

            auto* p2 = new StubPage(QStringLiteral("b"), &reg);
            PageRegistry::Entry e2{
                QStringLiteral("b"), {}, QStringLiteral("B"), {}, QUrl(QStringLiteral("qrc:/B.qml")), p2};
            e2.visibility = PV::SimpleOnly;
            QVERIFY(reg.registerPage(std::move(e2)));

            // `b` names nobody, so reciprocity fires too and is consumed
            // first (it is checked before the tier block). Both must be named
            // or ignoreMessage fails on the unconsumed one.
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("must point back at each other")));
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("share a visibility tier")));
            QVERIFY(!reg.validateCounterparts());
        }

        // One-way declaration: `a` points at `b`, but `b` names nobody. The
        // OUTBOUND flip off `a` redirects correctly, so this shape looks
        // healthy from the side anyone would test by hand — it is the RETURN
        // flip off `b` that silently lands on the app fallback instead of `a`.
        {
            PageRegistry reg;
            auto* p1 = new StubPage(QStringLiteral("a"), &reg);
            PageRegistry::Entry e1{
                QStringLiteral("a"), {}, QStringLiteral("A"), {}, QUrl(QStringLiteral("qrc:/A.qml")), p1};
            e1.visibility = PV::SimpleOnly;
            e1.counterpartId = QStringLiteral("b");
            QVERIFY(reg.registerPage(std::move(e1)));

            auto* p2 = new StubPage(QStringLiteral("b"), &reg);
            PageRegistry::Entry e2{
                QStringLiteral("b"), {}, QStringLiteral("B"), {}, QUrl(QStringLiteral("qrc:/B.qml")), p2};
            e2.visibility = PV::AdvancedOnly; // opposite tier, so only reciprocity is at fault
            QVERIFY(reg.registerPage(std::move(e2)));

            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("must point back at each other")));
            QVERIFY(!reg.validateCounterparts());
        }
    }

    void validateCounterpartsRejectsANonNavigableCounterpart()
    {
        // A counterpart with no QML of its own is a CATEGORY. The mode gate
        // would redirect onto a page with no body — the same silent
        // degradation the validator exists to catch, one level down.
        using PV = PageRegistry::PageVisibility;
        PageRegistry reg;
        auto* p1 = new StubPage(QStringLiteral("a"), &reg);
        PageRegistry::Entry e1{
            QStringLiteral("a"), {}, QStringLiteral("A"), {}, QUrl(QStringLiteral("qrc:/A.qml")), p1};
        e1.visibility = PV::SimpleOnly;
        e1.counterpartId = QStringLiteral("cat");
        QVERIFY(reg.registerPage(std::move(e1)));

        auto* p2 = new StubPage(QStringLiteral("cat"), &reg);
        // No qmlSource — a virtual category.
        PageRegistry::Entry e2{QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, QUrl(), p2};
        e2.visibility = PV::AdvancedOnly;
        e2.counterpartId = QStringLiteral("a");
        QVERIFY(reg.registerPage(std::move(e2)));

        QVERIFY(!reg.validateCounterparts());
    }

    void validateCounterpartsRejectsACounterpartHiddenByItsAncestor()
    {
        // Opposite tiers are necessary but NOT sufficient: a counterpart whose
        // own tier is right but whose ANCESTOR the mode filters out is still
        // unreachable, so the redirect falls back anyway. This is the branch
        // that distinguishes the reachability walk from a plain tier compare.
        using PV = PageRegistry::PageVisibility;
        PageRegistry reg;
        auto* p1 = new StubPage(QStringLiteral("a"), &reg);
        PageRegistry::Entry e1{
            QStringLiteral("a"), {}, QStringLiteral("A"), {}, QUrl(QStringLiteral("qrc:/A.qml")), p1};
        e1.visibility = PV::SimpleOnly;
        e1.counterpartId = QStringLiteral("cat.leaf");
        QVERIFY(reg.registerPage(std::move(e1)));

        // Category hidden in the mode that hides `a` (advanced).
        auto* pc = new StubPage(QStringLiteral("cat"), &reg);
        PageRegistry::Entry cat{QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, QUrl(), pc};
        cat.visibility = PV::SimpleOnly;
        QVERIFY(reg.registerPage(std::move(cat)));

        auto* p2 = new StubPage(QStringLiteral("cat.leaf"), &reg);
        PageRegistry::Entry e2{QStringLiteral("cat.leaf"),
                               QStringLiteral("cat"),
                               QStringLiteral("Leaf"),
                               {},
                               QUrl(QStringLiteral("qrc:/L.qml")),
                               p2};
        e2.visibility = PV::AdvancedOnly; // own tier is right...
        e2.counterpartId = QStringLiteral("a");
        QVERIFY(reg.registerPage(std::move(e2)));

        // ...but its SimpleOnly parent hides it in advanced mode, which is the
        // mode `a` disappears in.
        QVERIFY(!reg.validateCounterparts());
    }

    void reachabilityFailsClosedOnAnUnresolvableAncestorChain()
    {
        // pageAllowedInCurrentMode answers for search, keyboard next/prev and
        // the mode gate. If it cannot verify a page's ancestry it must say
        // "hidden": claiming visible would offer a row the rail cannot draw.
        // The registry rejects unknown parents, so the only way to reach the
        // guard is a chain deeper than the hop cap.
        PageRegistry reg;
        QString parent;
        for (int i = 0; i < 40; ++i) {
            const QString id = QStringLiteral("n%1").arg(i);
            auto* p = new StubPage(id, &reg);
            PageRegistry::Entry e{id, parent, id, {}, QUrl(QStringLiteral("qrc:/N.qml")), p};
            QVERIFY(reg.registerPage(std::move(e)));
            parent = id;
        }
        // Shallow entries still resolve normally.
        QVERIFY(reg.pageAllowedInCurrentMode(QStringLiteral("n0")));
        // Pin the BOUNDARY, not just a value either side of it: n31 is 31 hops
        // from the root and still resolves, n32 is the first to exceed the
        // 32-hop cap. Asserting only a far-past-the-cap id would stay green if
        // the constant moved.
        QVERIFY(reg.pageAllowedInCurrentMode(QStringLiteral("n31")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not resolve the ancestor chain")));
        QVERIFY(!reg.pageAllowedInCurrentMode(QStringLiteral("n32")));
    }

    void reachabilityFollowsTheAncestorChain()
    {
        // A page whose OWN tier passes but whose parent category is filtered
        // out has no sidebar row and no drill path to it. Answering "allowed"
        // for it would let search, keyboard next/prev and the mode gate land
        // the user somewhere they cannot navigate back from.
        using PV = PageRegistry::PageVisibility;
        PageRegistry reg;

        auto* cat = new StubPage(QStringLiteral("adv-cat"), &reg);
        PageRegistry::Entry catEntry{QStringLiteral("adv-cat"), {}, QStringLiteral("Advanced"), {}, QUrl(), cat};
        catEntry.visibility = PV::AdvancedOnly;
        QVERIFY(reg.registerPage(std::move(catEntry)));

        // Child is Always — its own tier passes in BOTH modes.
        auto* leaf = new StubPage(QStringLiteral("adv-cat.leaf"), &reg);
        QVERIFY(reg.registerPage({QStringLiteral("adv-cat.leaf"),
                                  QStringLiteral("adv-cat"),
                                  QStringLiteral("Leaf"),
                                  {},
                                  QUrl(QStringLiteral("qrc:/Leaf.qml")),
                                  leaf}));

        reg.setShowAdvanced(true);
        QVERIFY(reg.pageAllowedInCurrentMode(QStringLiteral("adv-cat.leaf")));

        reg.setShowAdvanced(false);
        // Own tier still says Always; the hidden ancestor is what makes it
        // unreachable. Before the ancestor walk this returned true.
        QVERIFY(!reg.pageAllowedInCurrentMode(QStringLiteral("adv-cat.leaf")));
    }

    void setPageVisibilityReclassifiesLate()
    {
        // Registration-time declaration is the canonical path; this pins the
        // late-reclassification escape hatch the API keeps for dynamic apps.
        PageRegistry reg;
        QVERIFY(buildTieredRegistry(reg));
        reg.setShowAdvanced(false);
        QVERIFY(!reg.pageAllowedInCurrentMode(QStringLiteral("full")));
        reg.setPageVisibility(QStringLiteral("full"), PageRegistry::PageVisibility::Always);
        QVERIFY(reg.pageAllowedInCurrentMode(QStringLiteral("full")));
        // Unknown id: warn-and-ignore, no crash.
        reg.setPageVisibility(QStringLiteral("no-such-page"), PageRegistry::PageVisibility::SimpleOnly);
    }

    void restampAnnouncesTheVisibleSetWithoutFakingAModeFlip()
    {
        // showAdvancedChanged is the showAdvanced Q_PROPERTY's NOTIFY, so it
        // must fire ONLY for a real mode flip; a per-entry restamp announces
        // itself on visibleSetChanged instead. Without the split, every
        // binding on the property re-evaluates and any consumer reading it as
        // "the user switched modes" acts on a change that never happened.
        PageRegistry reg;
        QVERIFY(buildTieredRegistry(reg));

        QSignalSpy modeSpy(&reg, &PageRegistry::showAdvancedChanged);
        QSignalSpy visibleSpy(&reg, &PageRegistry::visibleSetChanged);

        // A genuine mode flip announces both: the property really did change,
        // and so did the visible set.
        reg.setShowAdvanced(false);
        QCOMPARE(modeSpy.count(), 1);
        QCOMPARE(visibleSpy.count(), 1);

        // A restamp changes the visible set but NOT the mode.
        reg.setPageVisibility(QStringLiteral("full"), PageRegistry::PageVisibility::Always);
        QCOMPARE(modeSpy.count(), 1);
        QCOMPARE(visibleSpy.count(), 2);

        // Restamping to the tier it already has is a no-op — no spurious
        // rebuild for consumers caching a filtered view.
        reg.setPageVisibility(QStringLiteral("full"), PageRegistry::PageVisibility::Always);
        QCOMPARE(visibleSpy.count(), 2);

        // An unknown id is warn-and-ignore, not an announcement.
        reg.setPageVisibility(QStringLiteral("no-such-page"), PageRegistry::PageVisibility::SimpleOnly);
        QCOMPARE(modeSpy.count(), 1);
        QCOMPARE(visibleSpy.count(), 2);
    }
};

QTEST_MAIN(TestPageRegistry)
#include "test_page_registry.moc"
