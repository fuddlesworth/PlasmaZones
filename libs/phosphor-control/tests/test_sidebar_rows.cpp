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
        // Collapsible: a CATEGORY header dissolves in the flat rail (a
        // non-collapsible drill parent with two leaves keeps a header row —
        // see flatKeepsAMultiLeafDrillParentAsAnExpandableHeader).
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, /*collapsible=*/true));
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

    void flatKeepsAMultiLeafDrillParentAsAnExpandableHeader()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        // A no-QML DRILL parent (non-collapsible) with two navigable leaves —
        // the flat walk applies the tree walk's 0/1/2+ distinction, so this
        // keeps a collapsible header row instead of dissolving into two
        // orphaned sibling rows. A second drill parent with ONE leaf still
        // dissolves and promotes the lone leaf.
        QVERIFY(reg(r, QStringLiteral("mode"), {}, QStringLiteral("Mode"), {}));
        QVERIFY(reg(r, QStringLiteral("mode.general"), QStringLiteral("mode"), QStringLiteral("General"),
                    QStringLiteral("qrc:/G.qml")));
        QVERIFY(reg(r, QStringLiteral("mode.library"), QStringLiteral("mode"), QStringLiteral("Library"),
                    QStringLiteral("qrc:/L.qml")));
        QVERIFY(reg(r, QStringLiteral("solo"), {}, QStringLiteral("Solo"), {}));
        QVERIFY(reg(r, QStringLiteral("solo.only"), QStringLiteral("solo"), QStringLiteral("Only"),
                    QStringLiteral("qrc:/O.qml")));

        const QVariantList out = rows.build(true, QString(), QString(), {}, {});
        QCOMPARE(idsOf(out),
                 QStringList({QStringLiteral("mode"), QStringLiteral("mode.general"), QStringLiteral("mode.library"),
                              QStringLiteral("solo.only")}));

        const QVariantMap header = rowAt(out, 0);
        QVERIFY(header.value(QStringLiteral("_isCollapsibleHeader")).toBool());
        QVERIFY(header.value(QStringLiteral("_isExpanded")).toBool());
        QVERIFY(!header.value(QStringLiteral("hasQmlSource")).toBool());
        // Not a drill parent: SidebarRow routes clicks on _isCollapsibleHeader
        // first, but the chevron branch and any future consumer read this flag
        // independently, so a header wrongly carrying it would mis-render.
        QVERIFY(!header.value(QStringLiteral("_isDrillParent")).toBool());
        QCOMPARE(header.value(QStringLiteral("_depth")).toInt(), 0);
        // Children indent one step under the kept header; the promoted lone
        // leaf stays at the surrounding depth.
        QCOMPARE(rowAt(out, 1).value(QStringLiteral("_depth")).toInt(), 1);
        QCOMPARE(rowAt(out, 2).value(QStringLiteral("_depth")).toInt(), 1);
        QCOMPARE(rowAt(out, 3).value(QStringLiteral("_depth")).toInt(), 0);

        // Collapsing the header swallows its subtree, exactly like a tree
        // category.
        QVariantMap collapsed;
        collapsed.insert(QStringLiteral("mode"), false);
        const QVariantList closed = rows.build(true, QString(), QString(), collapsed, {});
        QCOMPARE(idsOf(closed), QStringList({QStringLiteral("mode"), QStringLiteral("solo.only")}));
        QVERIFY(!rowAt(closed, 0).value(QStringLiteral("_isExpanded")).toBool());
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

    void flatAppliesATitleOverrideToAKeptHeaderRow()
    {
        // An override keyed on a multi-leaf drill parent retitles its HEADER
        // row. Since the per-mode split retired the leaf overrides, a parent
        // override is the only remaining way to retitle a mode header, and
        // the header row goes through the same flatTitleOf lookup as a leaf.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("mode"), {}, QStringLiteral("Mode"), {}));
        QVERIFY(reg(r, QStringLiteral("mode.a"), QStringLiteral("mode"), QStringLiteral("A"),
                    QStringLiteral("qrc:/A.qml")));
        QVERIFY(reg(r, QStringLiteral("mode.b"), QStringLiteral("mode"), QStringLiteral("B"),
                    QStringLiteral("qrc:/B.qml")));

        const QVariantMap overrides{{QStringLiteral("mode"), QStringLiteral("Renamed")}};
        const QVariantList out = rows.build(true, QString(), QString(), {}, overrides);
        QVERIFY(!out.isEmpty());
        const QVariantMap header = rowAt(out, 0);
        QVERIFY(header.value(QStringLiteral("_isCollapsibleHeader")).toBool());
        QCOMPARE(header.value(QStringLiteral("title")).toString(), QStringLiteral("Renamed"));
    }

    void flatNestsAKeptHeaderInsideAnotherKeptHeader()
    {
        // Two stacked no-QML drill parents, the inner with two leaves. The
        // recursion carries TWO counters — `depth` bounds the registry walk
        // and gates the top-level seam, `rowDepth` is the emitted indent —
        // and this is the shape that drives them apart by more than one step:
        // outer header at rowDepth 0, inner header at 1, leaves at 2.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("outer"), {}, QStringLiteral("Outer"), {}));
        QVERIFY(reg(r, QStringLiteral("outer.inner"), QStringLiteral("outer"), QStringLiteral("Inner"), {}));
        QVERIFY(reg(r, QStringLiteral("outer.inner.a"), QStringLiteral("outer.inner"), QStringLiteral("A"),
                    QStringLiteral("qrc:/A.qml")));
        QVERIFY(reg(r, QStringLiteral("outer.inner.b"), QStringLiteral("outer.inner"), QStringLiteral("B"),
                    QStringLiteral("qrc:/B.qml")));

        const QVariantList out = rows.build(true, QString(), QString(), {}, {});
        QCOMPARE(idsOf(out),
                 QStringList({QStringLiteral("outer"), QStringLiteral("outer.inner"), QStringLiteral("outer.inner.a"),
                              QStringLiteral("outer.inner.b")}));
        QVERIFY(rowAt(out, 0).value(QStringLiteral("_isCollapsibleHeader")).toBool());
        QCOMPARE(rowAt(out, 0).value(QStringLiteral("_depth")).toInt(), 0);
        QVERIFY(rowAt(out, 1).value(QStringLiteral("_isCollapsibleHeader")).toBool());
        QCOMPARE(rowAt(out, 1).value(QStringLiteral("_depth")).toInt(), 1);
        QCOMPARE(rowAt(out, 2).value(QStringLiteral("_depth")).toInt(), 2);
        QCOMPARE(rowAt(out, 3).value(QStringLiteral("_depth")).toInt(), 2);

        // Collapsing the OUTER header removes the inner header and its leaves.
        QVariantMap collapsed;
        collapsed.insert(QStringLiteral("outer"), false);
        const QVariantList closed = rows.build(true, QString(), QString(), collapsed, {});
        QCOMPARE(idsOf(closed), QStringList({QStringLiteral("outer")}));
    }

    void flatPutsAKeptHeadersSeamAfterItsSubtreeAndAfterTheHeaderWhenCollapsed()
    {
        // A top-level kept header carrying hasDividerAfter: expanded, the seam
        // fires after the last row its subtree emitted; collapsed, the header
        // row itself satisfies the `out.size() > before` seam condition, so
        // the seam lands directly after the header rather than vanishing and
        // gluing sections together.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("mode"), {}, QStringLiteral("Mode"), {}, /*collapsible=*/false,
                    /*dividerAfter=*/true));
        QVERIFY(reg(r, QStringLiteral("mode.a"), QStringLiteral("mode"), QStringLiteral("A"),
                    QStringLiteral("qrc:/A.qml")));
        QVERIFY(reg(r, QStringLiteral("mode.b"), QStringLiteral("mode"), QStringLiteral("B"),
                    QStringLiteral("qrc:/B.qml")));
        QVERIFY(reg(r, QStringLiteral("last"), {}, QStringLiteral("Last"), QStringLiteral("qrc:/L.qml")));

        const QString seam = SidebarRows::dividerPrefix() + QStringLiteral("flat/mode");
        const QVariantList open = rows.build(true, QString(), QString(), {}, {});
        QCOMPARE(idsOf(open),
                 QStringList({QStringLiteral("mode"), QStringLiteral("mode.a"), QStringLiteral("mode.b"), seam,
                              QStringLiteral("last")}));

        QVariantMap collapsed;
        collapsed.insert(QStringLiteral("mode"), false);
        const QVariantList closed = rows.build(true, QString(), QString(), collapsed, {});
        QCOMPARE(idsOf(closed), QStringList({QStringLiteral("mode"), seam, QStringLiteral("last")}));
    }

    void flatHonoursOnlyTopLevelDividersAndTrimsTheTrailingOne()
    {
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        // A top-level entry with a divider, whose seam must fire after the LAST
        // row its subtree emitted (not immediately after itself). Collapsible,
        // so it dissolves rather than keeping a drill-parent header row.
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, /*collapsible=*/true,
                    /*dividerAfter=*/true));
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
        // Its only leaf is filtered out, so the row must not appear. What
        // actually drops it is the REGISTRY's visibility rule: isEntryVisible
        // hides a virtual node with no visible navigable descendant, so `cat`
        // never reaches build()'s descendant count at all (that count's zero
        // case is reachable only through depth truncation — see
        // firstTwoNavigableUnder's doc). The assertion pins the user-visible
        // behaviour either way.
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

    void resolveDrillScopeRejectsAScopeTheModeAbolished()
    {
        // The rail is drilled into a category the tier filter then hides. The
        // parent itself is gone, so the rail must fall back to the top level
        // rather than keep rendering a scope the mode no longer has.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}, false, false, PV::AdvancedOnly));
        // Two leaves so the scope is a genuine drill target before the flip:
        // one would be flattened into a direct row instead (see
        // resolveDrillScopeRejectsAScopeDownToASingleDescendant), which would
        // make this test pass for the wrong reason.
        QVERIFY(reg(r, QStringLiteral("cat.leaf"), QStringLiteral("cat"), QStringLiteral("Leaf"),
                    QStringLiteral("qrc:/L.qml")));
        QVERIFY(reg(r, QStringLiteral("cat.leaf2"), QStringLiteral("cat"), QStringLiteral("Leaf2"),
                    QStringLiteral("qrc:/L2.qml")));

        QCOMPARE(rows.resolveDrillScope(QStringLiteral("cat")), QStringLiteral("cat"));
        r.setShowAdvanced(false);
        QCOMPARE(rows.resolveDrillScope(QStringLiteral("cat")), QString());
    }

    void resolveDrillScopeRejectsAScopeWithNothingLeftUnderIt()
    {
        // The DISTINCT second failure: the parent survives the filter but
        // every navigable page beneath it is hidden. Two advanced leaves, so
        // the pre-state is a genuine drill target (build() flattens a
        // one-descendant category rather than offering it), and simple mode
        // then removes both. Mechanically the rejection happens at the
        // kids.isEmpty() guard — the registry's visibility rule hides the
        // intermediate `cat.sub` once both its leaves filter out — not at the
        // descendant count; the assertion pins the behaviour either way.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.sub"), QStringLiteral("cat"), QStringLiteral("Sub"), {}));
        QVERIFY(reg(r, QStringLiteral("cat.sub.adv"), QStringLiteral("cat.sub"), QStringLiteral("Adv"),
                    QStringLiteral("qrc:/A.qml"), false, false, PV::AdvancedOnly));
        QVERIFY(reg(r, QStringLiteral("cat.sub.adv2"), QStringLiteral("cat.sub"), QStringLiteral("Adv2"),
                    QStringLiteral("qrc:/B.qml"), false, false, PV::AdvancedOnly));

        // Reachable while both leaves show, including through the intermediate
        // non-navigable category, which is what makes the walk recursive.
        QCOMPARE(rows.resolveDrillScope(QStringLiteral("cat")), QStringLiteral("cat"));
        r.setShowAdvanced(false);
        QCOMPARE(rows.resolveDrillScope(QStringLiteral("cat")), QString());
    }

    void resolveDrillScopeRejectsAScopeDownToASingleDescendant()
    {
        // build() FLATTENS a category with exactly one navigable descendant
        // into a direct row one level up (sidebarrows.cpp: `found.size() == 1`
        // sets isDrill = false). A rail left drilled into such a category shows
        // a Back button over one row that the level above renders directly, so
        // the rail would disagree with its own rows. Counting only to one
        // instead of two is exactly that bug, and this is the case that
        // distinguishes them.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("cat"), {}, QStringLiteral("Cat"), {}));
        QVERIFY(
            reg(r, QStringLiteral("cat.a"), QStringLiteral("cat"), QStringLiteral("A"), QStringLiteral("qrc:/A.qml")));
        QVERIFY(reg(r, QStringLiteral("cat.b"), QStringLiteral("cat"), QStringLiteral("B"),
                    QStringLiteral("qrc:/B.qml"), false, false, PV::AdvancedOnly));

        // Two navigable children: a real drill target.
        QCOMPARE(rows.resolveDrillScope(QStringLiteral("cat")), QStringLiteral("cat"));

        // Simple mode leaves exactly one, so build() would render "A" directly
        // and never offer "cat" as a drill.
        r.setShowAdvanced(false);
        QCOMPARE(rows.resolveDrillScope(QStringLiteral("cat")), QString());
    }

    void resolveDrillScopePassesThroughTheTopLevel()
    {
        // Empty in, empty out: the top level is not a drill scope and must
        // never be "corrected" to something else.
        PageRegistry r;
        SidebarRows rows;
        rows.setRegistry(&r);
        QVERIFY(reg(r, QStringLiteral("home"), {}, QStringLiteral("Home"), QStringLiteral("qrc:/H.qml")));
        QCOMPARE(rows.resolveDrillScope(QString()), QString());
        // An unknown id has no visible descendant either, so it also resets.
        QCOMPARE(rows.resolveDrillScope(QStringLiteral("ghost")), QString());
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
        // Absence, not empty-string read-back: the registry dict uses "id",
        // and a `pageId` key holding an EMPTY value would have passed the old
        // form of this assertion.
        QVERIFY(!data.contains(QStringLiteral("pageId")));
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
        // A non-collapsible two-leaf drill parent so the FLAT pass emits its
        // kept-header row shape too — with only the collapsible `cat`, that
        // shape dissolved and the flat loop only ever checked leaf rows.
        QVERIFY(reg(r, QStringLiteral("drill"), {}, QStringLiteral("Drill"), {}));
        QVERIFY(reg(r, QStringLiteral("drill.a"), QStringLiteral("drill"), QStringLiteral("DA"),
                    QStringLiteral("qrc:/DA.qml")));
        QVERIFY(reg(r, QStringLiteral("drill.b"), QStringLiteral("drill"), QStringLiteral("DB"),
                    QStringLiteral("qrc:/DB.qml")));
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

    void survivesARegistryDestroyedWhileStillAssigned()
    {
        // The ONLY thing that justifies QPointer over a raw pointer: the
        // registry is owned by the ApplicationController, not by SidebarRows,
        // so it can be torn down first. A raw pointer would leave the existing
        // `m_registry == nullptr` guards reading as non-null and every accessor
        // dereferencing freed memory. Explicit setRegistry(nullptr) does NOT
        // cover this — that path nulls the member itself.
        SidebarRows rows;
        auto* reg = new PageRegistry;
        rows.setRegistry(reg);
        auto* page = new StubPage(QStringLiteral("a"), reg);
        QVERIFY(reg->registerPage(
            {QStringLiteral("a"), {}, QStringLiteral("A"), {}, QUrl(QStringLiteral("qrc:/A.qml")), page}));
        QVERIFY(!rows.build(false, QString(), QString(), {}, {}).isEmpty());

        // The destroyed()→registryChanged relay must fire on the delete: the
        // QPointer self-nulls SILENTLY, so without the relay a QML binding on
        // `registry` would keep the stale non-null value while build() had
        // already begun returning an empty list. This spy is the only thing
        // in the suite that would fail if the relay connection were removed.
        QSignalSpy relaySpy(&rows, &SidebarRows::registryChanged);
        delete reg;
        QCOMPARE(relaySpy.count(), 1);

        QCOMPARE(rows.registry(), nullptr);
        QVERIFY(rows.build(false, QString(), QString(), {}, {}).isEmpty());
        QVERIFY(rows.build(true, QString(), QString(), {}, {}).isEmpty());
        QVERIFY(rows.flatPageData(QStringLiteral("a"), {}).isEmpty());
        QCOMPARE(rows.resolveDrillScope(QStringLiteral("a")), QString());
    }
};

QTEST_MAIN(TestSidebarRows)
#include "test_sidebar_rows.moc"
