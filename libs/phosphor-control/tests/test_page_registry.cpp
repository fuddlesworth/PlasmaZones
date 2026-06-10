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
};

QTEST_MAIN(TestPageRegistry)
#include "test_page_registry.moc"
