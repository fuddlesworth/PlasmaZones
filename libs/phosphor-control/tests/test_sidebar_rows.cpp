// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_sidebar_rows.cpp
 * @brief The three sidebar rail walks (flat / tree / search).
 *
 * This logic used to live in Sidebar.qml, where it had no automated coverage
 * at all despite driving the rail's entire contents. The cases below are the
 * ones its comments called out as load-bearing, each of which fails silently
 * in the UI (a row that quietly vanishes, a divider that strands itself at the
 * end of a list, a drill step into a page with nothing under it).
 */

#include <QTest>

#include <QSignalSpy>

#include <PhosphorControl/PageController.h>
#include <PhosphorControl/PageRegistry.h>
#include <PhosphorControl/SidebarRows.h>

using PhosphorControl::PageController;
using PhosphorControl::PageRegistry;
using PhosphorControl::SidebarRows;

namespace {

class StubPage : public PageController
{
    Q_OBJECT

public:
    explicit StubPage(const QString& id, QObject* parent = nullptr)
        : PageController(id, parent)
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
};

using PV = PageRegistry::PageVisibility;

/// Register one page. `qml` empty makes it a virtual (non-navigable) node.
bool reg(PageRegistry& r, const QString& id, const QString& parentId, const QString& title, const QString& qml,
         bool collapsible = false, bool dividerAfter = false, PV vis = PV::Always)
{
    auto* ctrl = new StubPage(id, &r);
    PageRegistry::Entry e{id, parentId, title, QStringLiteral("icon-") + id, qml.isEmpty() ? QUrl() : QUrl(qml), ctrl};
    e.isCollapsible = collapsible;
    e.hasDividerAfter = dividerAfter;
    e.visibility = vis;
    return r.registerPage(std::move(e));
}

QStringList idsOf(const QVariantList& rows)
{
    QStringList out;
    for (const QVariant& v : rows) {
        out << v.toMap().value(QStringLiteral("pageId")).toString();
    }
    return out;
}

QVariantMap rowAt(const QVariantList& rows, int i)
{
    return rows.at(i).toMap();
}

} // namespace

class TestSidebarRows : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void returnsNothingWithoutARegistry()
    {
        // The QML instantiates this before its registry binding resolves, so
        // the null case is a real startup state, not a hypothetical.
        SidebarRows rows;
        QVERIFY(rows.build(false, QString(), QString(), {}, {}).isEmpty());
        QVERIFY(rows.build(true, QString(), QString(), {}, {}).isEmpty());
        QVERIFY(rows.build(false, QStringLiteral("x"), QString(), {}, {}).isEmpty());
    }

    void flatEmitsEveryNavigablePageAtDepthZero()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("home"), {}, QStringLiteral("Home"), QStringLiteral("qrc:/H.qml")));
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}));
        QVERIFY(
            reg(r, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("A"), QStringLiteral("qrc:/A.qml")));
        QVERIFY(
            reg(r, QStringLiteral("cat.b"), QStringLiteral("cat"), QStringLiteral("B"), QStringLiteral("qrc:/B.qml")));

        const QVariantList out = rows.build(true, QString(), QString(), {}, {});
        // The category itself carries no QML, so it contributes no row; its
        // leaves are hoisted to depth 0 alongside the top-level page.
        QCOMPARE(idsOf(out), QStringList({QStringLiteral("home"), QStringLiteral("cat.a"), QStringLiteral("cat.b")}));
        for (const QVariant& v : out) {
            QCOMPARE(v.toMap().value(QStringLiteral("_depth")).toInt(), 0);
            QVERIFY(!v.toMap().value(QStringLiteral("_isDrillParent")).toBool());
            QVERIFY(!v.toMap().value(QStringLiteral("_isCollapsibleHeader")).toBool());
        }
    }

    void flatAppliesTitleOverrides()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(
            reg(r, QStringLiteral("window-appearance"), {}, QStringLiteral("General"), QStringLiteral("qrc:/W.qml")));

        QVariantMap overrides;
        overrides.insert(QStringLiteral("window-appearance"), QStringLiteral("Appearance"));
        const QVariantList out = rows.build(true, QString(), QString(), {}, overrides);
        QCOMPARE(out.size(), 1);
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("title")).toString(), QStringLiteral("Appearance"));

        // Without the override the registered (tree-context) title stands.
        const QVariantList bare = rows.build(true, QString(), QString(), {}, {});
        QCOMPARE(rowAt(bare, 0).value(QStringLiteral("title")).toString(), QStringLiteral("General"));
    }

    void flatHonoursOnlyTopLevelDividersAndTrimsTheTrailingOne()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        // A top-level entry with a divider, whose seam must fire after the LAST
        // row its subtree emitted (not immediately after itself).
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, false, /*dividerAfter=*/true));
        QVERIFY(reg(r, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("A"),
                    QStringLiteral("qrc:/A.qml"), false, /*dividerAfter=*/true));
        QVERIFY(
            reg(r, QStringLiteral("cat.b"), QStringLiteral("cat"), QStringLiteral("B"), QStringLiteral("qrc:/B.qml")));
        QVERIFY(reg(r, QStringLiteral("last"), {}, QStringLiteral("Last"), QStringLiteral("qrc:/L.qml"), false,
                    /*dividerAfter=*/true));

        const QVariantList out = rows.build(true, QString(), QString(), {}, {});
        // cat.a's LEAF-level divider is ignored (leaf flags are tuned for the
        // tree rail's rhythm); cat's top-level one fires after cat.b; and
        // "last"'s trailing divider is trimmed rather than dangling.
        QCOMPARE(idsOf(out),
                 QStringList({QStringLiteral("cat.a"), QStringLiteral("cat.b"),
                              SidebarRows::dividerPrefix() + QStringLiteral("flat/cat"), QStringLiteral("last")}));
        QVERIFY(rowAt(out, 2).value(QStringLiteral("_isDivider")).toBool());
    }

    void flatSuppressesADividerForACategoryThatEmittedNothing()
    {
        // Every leaf filtered out by tier, so the seam has nothing to separate.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, false, /*dividerAfter=*/true));
        QVERIFY(reg(r, QStringLiteral("cat.adv"), QStringLiteral("cat"), QStringLiteral("Adv"),
                    QStringLiteral("qrc:/A.qml"), false, false, PV::AdvancedOnly));
        QVERIFY(reg(r, QStringLiteral("home"), {}, QStringLiteral("Home"), QStringLiteral("qrc:/H.qml")));

        r.setShowAdvanced(false);
        QCOMPARE(idsOf(rows.build(true, QString(), QString(), {}, {})), QStringList({QStringLiteral("home")}));

        r.setShowAdvanced(true);
        QCOMPARE(idsOf(rows.build(true, QString(), QString(), {}, {})),
                 QStringList({QStringLiteral("cat.adv"), SidebarRows::dividerPrefix() + QStringLiteral("flat/cat"),
                              QStringLiteral("home")}));
    }

    void treeMarksCollapsibleHeadersAndRecursesOnlyWhenExpanded()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, /*collapsible=*/true));
        QVERIFY(
            reg(r, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("A"), QStringLiteral("qrc:/A.qml")));

        // Absent from the map = expanded, matching the rail's open-by-default.
        const QVariantList open = rows.build(false, QString(), QString(), {}, {});
        QCOMPARE(idsOf(open), QStringList({QStringLiteral("cat"), QStringLiteral("cat.a")}));
        QVERIFY(rowAt(open, 0).value(QStringLiteral("_isCollapsibleHeader")).toBool());
        QVERIFY(rowAt(open, 0).value(QStringLiteral("_isExpanded")).toBool());
        QCOMPARE(rowAt(open, 1).value(QStringLiteral("_depth")).toInt(), 1);

        QVariantMap collapsed;
        collapsed.insert(QStringLiteral("cat"), false);
        const QVariantList shut = rows.build(false, QString(), QString(), collapsed, {});
        QCOMPARE(idsOf(shut), QStringList({QStringLiteral("cat")}));
        QVERIFY(!rowAt(shut, 0).value(QStringLiteral("_isExpanded")).toBool());
    }

    void treeFlattensASingleLeafDrillButKeepsATwoLeafOne()
    {
        // One visible leaf: the drill step is pure friction, so the category row
        // navigates straight to the leaf while keeping its own title/icon.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.only"), QStringLiteral("cat"), QStringLiteral("Only"),
                    QStringLiteral("qrc:/O.qml")));

        const QVariantList one = rows.build(false, QString(), QString(), {}, {});
        QCOMPARE(one.size(), 1);
        QCOMPARE(rowAt(one, 0).value(QStringLiteral("pageId")).toString(), QStringLiteral("cat.only"));
        QCOMPARE(rowAt(one, 0).value(QStringLiteral("title")).toString(), QStringLiteral("Cat"));
        QVERIFY(rowAt(one, 0).value(QStringLiteral("hasQmlSource")).toBool());
        QVERIFY(!rowAt(one, 0).value(QStringLiteral("_isDrillParent")).toBool());

        // A second visible leaf restores the drill step.
        PageRegistry r2;
        SidebarRows rows2;
        rows2.setRegistry(&r2);
        QVERIFY(reg(r2, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}));
        QVERIFY(
            reg(r2, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("A"), QStringLiteral("qrc:/A.qml")));
        QVERIFY(
            reg(r2, QStringLiteral("cat.b"), QStringLiteral("cat"), QStringLiteral("B"), QStringLiteral("qrc:/B.qml")));
        const QVariantList two = rows2.build(false, QString(), QString(), {}, {});
        QCOMPARE(two.size(), 1);
        QCOMPARE(rowAt(two, 0).value(QStringLiteral("pageId")).toString(), QStringLiteral("cat"));
        QVERIFY(rowAt(two, 0).value(QStringLiteral("_isDrillParent")).toBool());
    }

    void treeDoesNotFlattenWhenAGrandchildIsAlsoNavigable()
    {
        // Exactly the misfire worth guarding: one visible leaf PLUS a navigable
        // grandchild is two reachable pages, so the drill step must stay.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}));
        QVERIFY(
            reg(r, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("A"), QStringLiteral("qrc:/A.qml")));
        QVERIFY(reg(r, QStringLiteral("cat.a.deep"), QStringLiteral("cat.a"), QStringLiteral("Deep"),
                    QStringLiteral("qrc:/D.qml")));

        const QVariantList out = rows.build(false, QString(), QString(), {}, {});
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("pageId")).toString(), QStringLiteral("cat"));
        QVERIFY(rowAt(out, 0).value(QStringLiteral("_isDrillParent")).toBool());
    }

    void treeSkipsADrillCategoryThatLeadsNowhere()
    {
        // Its only leaf is filtered out, so drilling in would show a Back
        // button and nothing else. The row must not be offered at all.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.sub"), QStringLiteral("cat"), QStringLiteral("Sub"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.sub.adv"), QStringLiteral("cat.sub"), QStringLiteral("Adv"),
                    QStringLiteral("qrc:/A.qml"), false, false, PV::AdvancedOnly));
        QVERIFY(reg(r, QStringLiteral("home"), {}, QStringLiteral("Home"), QStringLiteral("qrc:/H.qml")));

        r.setShowAdvanced(false);
        QCOMPARE(idsOf(rows.build(false, QString(), QString(), {}, {})), QStringList({QStringLiteral("home")}));
    }

    void treeScopesToTheDrillParent()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}));
        QVERIFY(
            reg(r, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("A"), QStringLiteral("qrc:/A.qml")));
        QVERIFY(
            reg(r, QStringLiteral("cat.b"), QStringLiteral("cat"), QStringLiteral("B"), QStringLiteral("qrc:/B.qml")));

        const QVariantList out = rows.build(false, QString(), QStringLiteral("cat"), {}, {});
        QCOMPARE(idsOf(out), QStringList({QStringLiteral("cat.a"), QStringLiteral("cat.b")}));
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("_depth")).toInt(), 0);
    }

    void searchSpansEveryScopeWhileDrilledIn()
    {
        // Search is global no matter how deep the user has drilled. Rooting
        // the walk at currentParentId instead would narrow results to the
        // current category while the rail still looks top-level (the Back
        // button is hidden whenever a search is active), so the user gets an
        // empty list and no reason why.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("snapping"), {}, QStringLiteral("Snapping"), {}));
        QVERIFY(reg(r, QStringLiteral("snapping.behavior"), QStringLiteral("snapping"), QStringLiteral("Behavior"),
                    QStringLiteral("qrc:/B.qml")));
        QVERIFY(reg(r, QStringLiteral("rules"), {}, QStringLiteral("Rules"), QStringLiteral("qrc:/R.qml")));

        // Drilled into "snapping", searching for a page that lives outside it.
        const QVariantList out = rows.build(false, QStringLiteral("rules"), QStringLiteral("snapping"), {}, {});
        QCOMPARE(idsOf(out), QStringList{QStringLiteral("rules")});
    }

    void flatPageDataAppliesTheSameOverrideTheRailUses()
    {
        // Breadcrumbs resolves flat titles through this, so it must agree with
        // the rail row for the same id — the two used to be separate
        // implementations, and only the rail's was covered.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Decorations"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.general"), QStringLiteral("cat"), QStringLiteral("General"),
                    QStringLiteral("qrc:/G.qml")));
        const QVariantMap overrides{{QStringLiteral("cat.general"), QStringLiteral("Window Appearance")}};

        const QVariantMap data = rows.flatPageData(QStringLiteral("cat.general"), overrides);
        QCOMPARE(data.value(QStringLiteral("title")).toString(), QStringLiteral("Window Appearance"));
        QCOMPARE(data.value(QStringLiteral("pageId")).toString().isEmpty(), true); // registry dict uses "id"
        QCOMPARE(data.value(QStringLiteral("id")).toString(), QStringLiteral("cat.general"));

        // Same id, same override, via the rail: titles must match.
        const QVariantList railRows = rows.build(true, QString(), QString(), {}, overrides);
        QVERIFY(!railRows.isEmpty());
        bool matched = false;
        for (const QVariant& v : railRows) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("pageId")).toString() == QStringLiteral("cat.general")) {
                QCOMPARE(m.value(QStringLiteral("title")).toString(), QStringLiteral("Window Appearance"));
                matched = true;
            }
        }
        QVERIFY(matched);

        // A page with no override keeps its registered title.
        QCOMPARE(rows.flatPageData(QStringLiteral("cat.general"), {}).value(QStringLiteral("title")).toString(),
                 QStringLiteral("General"));
        // Unknown id yields an empty map, not a title-only dict.
        QVERIFY(rows.flatPageData(QStringLiteral("no-such-page"), overrides).isEmpty());
    }

    void searchReturnsNothingWhenEveryPageIsHiddenByTheTier()
    {
        // A registry that EXISTS but whose every entry is filtered out by the
        // active tier is a different path from the null-registry early return.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("adv"), {}, QStringLiteral("Advanced Thing"), QStringLiteral("qrc:/A.qml"), false,
                    false, PV::AdvancedOnly));
        r.setShowAdvanced(false);

        QVERIFY(rows.build(false, QStringLiteral("advanced"), QString(), {}, {}).isEmpty());
        QVERIFY(rows.build(false, QString(), QString(), {}, {}).isEmpty());
        QVERIFY(rows.build(true, QString(), QString(), {}, {}).isEmpty());
    }

    void buildsNothingForARegisteredButEmptyTree()
    {
        // Registry present, zero pages — falls through the whole walk rather
        // than hitting the null-registry guard.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(rows.build(false, QString(), QString(), {}, {}).isEmpty());
        QVERIFY(rows.build(true, QString(), QString(), {}, {}).isEmpty());
        QVERIFY(rows.build(false, QStringLiteral("anything"), QString(), {}, {}).isEmpty());
    }

    void searchMatchesOnTheBreadcrumbAndFlattensResults()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Snapping"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("Behavior"),
                    QStringLiteral("qrc:/A.qml")));

        const QVariantList out = rows.build(false, QStringLiteral("behav"), QString(), {}, {});
        QCOMPARE(out.size(), 1);
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("pageId")).toString(), QStringLiteral("cat.a"));
        // Ancestor context is the only thing telling same-named leaves apart.
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("title")).toString(), QStringLiteral("Snapping / Behavior"));
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("_depth")).toInt(), 0);

        // A needle matching the ANCESTOR's title matches the leaf too, through
        // the breadcrumb prefix. Both used to emit, giving two rows for ONE
        // destination under different titles; the duplicate is now dropped.
        // The leaf's own row is the one kept because it names the destination
        // exactly, where the category's row names only the ancestor and leaves
        // the reader to guess where it lands.
        const QVariantList ancestor = rows.build(false, QStringLiteral("snapping"), QString(), {}, {});
        QCOMPARE(idsOf(ancestor), QStringList{QStringLiteral("cat.a")});
        QCOMPARE(rowAt(ancestor, 0).value(QStringLiteral("title")).toString(), QStringLiteral("Snapping / Behavior"));
    }

    void searchRoutesACategoryOnlyMatchToItsFirstNavigableDescendant()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Widgets"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.sub"), QStringLiteral("cat"), QStringLiteral("Sub"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.sub.leaf"), QStringLiteral("cat.sub"), QStringLiteral("Leaf"),
                    QStringLiteral("qrc:/L.qml")));

        // FLAT mode with an override on the leaf is the shape that actually
        // exercises the category-landing branch. In tree mode the leaf's
        // breadcrumb is "Widgets / Sub / Leaf", so "widgets" matches the LEAF
        // too and the leaf's own row is what appears — the assertion would pass
        // without the landing branch existing at all. The override replaces the
        // breadcrumb with "Zzz", so only the category matches and the landing
        // row is the sole thing offering the destination.
        const QVariantMap overrides{{QStringLiteral("cat.sub.leaf"), QStringLiteral("Zzz")}};
        const QVariantList out = rows.build(true, QStringLiteral("widgets"), QString(), {}, overrides);
        QVERIFY(!out.isEmpty());
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("pageId")).toString(), QStringLiteral("cat.sub.leaf"));
        QVERIFY(rowAt(out, 0).value(QStringLiteral("hasQmlSource")).toBool());
        // Exactly one row, and titled by the INNERMOST matching ancestor
        // ("Widgets / Sub", not "Widgets"): the recursion reaches cat.sub
        // first, so it claims the destination and the outer cat then dedups
        // against it. Nearest context wins, which is the more useful label.
        QCOMPARE(out.size(), 1);
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("title")).toString(), QStringLiteral("Widgets / Sub"));
    }

    void searchEmitsNoDividers()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("a"), {}, QStringLiteral("Alpha"), QStringLiteral("qrc:/A.qml"), false,
                    /*dividerAfter=*/true));
        QVERIFY(reg(r, QStringLiteral("b"), {}, QStringLiteral("Alps"), QStringLiteral("qrc:/B.qml")));

        const QVariantList out = rows.build(false, QStringLiteral("al"), QString(), {}, {});
        QCOMPARE(out.size(), 2);
        for (const QVariant& v : out) {
            QVERIFY(!v.toMap().value(QStringLiteral("_isDivider")).toBool());
        }
    }

    void searchUsesTheFlatOverrideAndDropsTheBreadcrumb()
    {
        // An overridden id must read the same as its flat rail row: the
        // override exists precisely because the registered title needs its
        // ancestors, which is what a breadcrumb would put back.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("deco"), {}, QStringLiteral("Decorations"), {}));
        QVERIFY(reg(r, QStringLiteral("window-appearance"), QStringLiteral("deco"), QStringLiteral("General"),
                    QStringLiteral("qrc:/W.qml")));

        QVariantMap overrides;
        overrides.insert(QStringLiteral("window-appearance"), QStringLiteral("Appearance"));

        const QVariantList flat = rows.build(true, QStringLiteral("appear"), QString(), {}, overrides);
        QCOMPARE(flat.size(), 1);
        QCOMPARE(rowAt(flat, 0).value(QStringLiteral("title")).toString(), QStringLiteral("Appearance"));

        // In TREE mode the override is not consulted, so the breadcrumb stands.
        const QVariantList tree = rows.build(false, QStringLiteral("general"), QString(), {}, overrides);
        QCOMPARE(tree.size(), 1);
        QCOMPARE(rowAt(tree, 0).value(QStringLiteral("title")).toString(), QStringLiteral("Decorations / General"));
    }

    void everyRowCarriesTheNineContractRoles()
    {
        // The delegate binds these by name; a missing role reads as undefined
        // and silently breaks a row rather than failing loudly.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, true, /*dividerAfter=*/true));
        QVERIFY(
            reg(r, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("A"), QStringLiteral("qrc:/A.qml")));
        QVERIFY(reg(r, QStringLiteral("z"), {}, QStringLiteral("Z"), QStringLiteral("qrc:/Z.qml")));

        const QStringList expected{
            QStringLiteral("pageId"),         QStringLiteral("title"),       QStringLiteral("iconSource"),
            QStringLiteral("hasQmlSource"),   QStringLiteral("_depth"),      QStringLiteral("_isCollapsibleHeader"),
            QStringLiteral("_isDrillParent"), QStringLiteral("_isExpanded"), QStringLiteral("_isDivider")};

        for (bool flat : {false, true}) {
            const QVariantList out = rows.build(flat, QString(), QString(), {}, {});
            QVERIFY(!out.isEmpty());
            for (const QVariant& v : out) {
                const QVariantMap row = v.toMap();
                QCOMPARE(row.size(), expected.size());
                for (const QString& key : expected) {
                    QVERIFY2(row.contains(key), qPrintable(key));
                }
            }
        }
    }

    void registryPropertyEmitsOnlyOnChange()
    {
        SidebarRows rows;
        QSignalSpy spy(&rows, &SidebarRows::registryChanged);
        PageRegistry r;
        rows.setRegistry(&r);
        QCOMPARE(spy.count(), 1);
        rows.setRegistry(&r);
        QCOMPARE(spy.count(), 1);
        rows.setRegistry(nullptr);
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_MAIN(TestSidebarRows)
#include "test_sidebar_rows.moc"
