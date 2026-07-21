// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

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
        // Sidebar.qml reads via pageData() / childPagesData().
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
        // topLevelPagesData / childPagesData / pageData are the three
        // Q_INVOKABLE accessors Sidebar.qml + Breadcrumbs.qml drive
        // their Repeaters from. They must surface every entry field.
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
};

QTEST_MAIN(TestPageRegistry)
#include "test_page_registry.moc"
