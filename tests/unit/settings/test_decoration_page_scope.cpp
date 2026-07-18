// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decoration_page_scope.cpp
 * @brief Decoration page→root scoping helpers behind per-page Reset/Discard/dirty.
 *
 * Pins the pure logic SettingsController's decoration branches dispatch
 * through (decorationpagescope.{h,cpp}): the page→root mapping, the
 * root-prefix membership test, and the root-scoped tree diff. A wrong root,
 * a prefix off-by-one ("osdX" leaking into "osd"), or a diff that sees
 * another root's edits would silently re-introduce the cross-surface
 * clobbering these helpers exist to prevent. The end-to-end flow (clear →
 * seed-strip → reveal) is pinned in test_settings_decoration_tree.cpp.
 */

#include <QTest>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include "settings/decorationpagescope.h"

using namespace PlasmaZones;
using PhosphorSurfaceShaders::DecorationProfile;
using PhosphorSurfaceShaders::DecorationProfileTree;

namespace {

DecorationProfile chainProfile(const QStringList& chain)
{
    DecorationProfile p;
    p.chain = chain;
    return p;
}

} // namespace

class TestDecorationPageScope : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// The three surface pages map to their roots; the non-surface leaves
    /// (sets library, shaders browser) and unknown ids map to the empty
    /// string, which the callers dispatch to whole-tree handling.
    void testDecorationSurfaceRoot_mapsSurfacePagesOnly()
    {
        QCOMPARE(decorationSurfaceRoot(QStringLiteral("decorations-windows")), QStringLiteral("window"));
        QCOMPARE(decorationSurfaceRoot(QStringLiteral("decorations-osds")), QStringLiteral("osd"));
        QCOMPARE(decorationSurfaceRoot(QStringLiteral("decorations-popups")), QStringLiteral("popup"));
        QVERIFY(decorationSurfaceRoot(QStringLiteral("decorations-sets")).isEmpty());
        QVERIFY(decorationSurfaceRoot(QStringLiteral("decorations-shaders")).isEmpty());
        QVERIFY(decorationSurfaceRoot(QStringLiteral("window-appearance")).isEmpty());
        QVERIFY(decorationSurfaceRoot(QString()).isEmpty());
    }

    void testDecorationPathInRoot_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("root");
        QTest::addColumn<bool>("expected");

        QTest::newRow("root itself") << QStringLiteral("osd") << QStringLiteral("osd") << true;
        QTest::newRow("direct child") << QStringLiteral("popup.layoutPicker") << QStringLiteral("popup") << true;
        QTest::newRow("deep descendant") << QStringLiteral("window.tiled") << QStringLiteral("window") << true;
        // The dot guard: a sibling root sharing a prefix must not leak in.
        QTest::newRow("shared prefix is not membership") << QStringLiteral("osdX") << QStringLiteral("osd") << false;
        QTest::newRow("other root") << QStringLiteral("window.tiled") << QStringLiteral("popup") << false;
        QTest::newRow("parent is not in child root")
            << QStringLiteral("popup") << QStringLiteral("popup.layoutPicker") << false;
        // The empty root matches only the empty path (callers dispatch the
        // whole-tree case before ever asking).
        QTest::newRow("empty root vs path") << QStringLiteral("osd") << QString() << false;
        QTest::newRow("empty root vs empty path") << QString() << QString() << true;
    }

    void testDecorationPathInRoot()
    {
        QFETCH(QString, path);
        QFETCH(QString, root);
        QFETCH(bool, expected);
        QCOMPARE(decorationPathInRoot(path, root), expected);
    }

    /// The root-scoped diff sees adds, removes, and value changes under its
    /// root — and ONLY under its root: edits in a sibling root and baseline
    /// changes (path "", never under a surface root) are invisible.
    void testDecorationRootDiffers_scopesToRoot()
    {
        DecorationProfileTree a;
        a.setOverride(QStringLiteral("osd"), chainProfile({QStringLiteral("border")}));
        a.setOverride(QStringLiteral("popup.layoutPicker"), chainProfile({QStringLiteral("glow")}));

        // Identical trees: no root differs.
        DecorationProfileTree b = a;
        QVERIFY(!decorationRootDiffers(a, b, QStringLiteral("osd")));
        QVERIFY(!decorationRootDiffers(a, b, QStringLiteral("popup")));
        QVERIFY(!decorationRootDiffers(a, b, QStringLiteral("window")));

        // Value change under osd: only the osd root differs.
        b.setOverride(QStringLiteral("osd"), chainProfile({QStringLiteral("shadow")}));
        QVERIFY(decorationRootDiffers(a, b, QStringLiteral("osd")));
        QVERIFY(!decorationRootDiffers(a, b, QStringLiteral("popup")));
        QVERIFY(!decorationRootDiffers(a, b, QStringLiteral("window")));

        // Removal counts (present in one, absent in the other) — both ways.
        DecorationProfileTree c = a;
        QVERIFY(c.clearOverride(QStringLiteral("popup.layoutPicker")));
        QVERIFY(decorationRootDiffers(a, c, QStringLiteral("popup")));
        QVERIFY(decorationRootDiffers(c, a, QStringLiteral("popup")));
        QVERIFY(!decorationRootDiffers(a, c, QStringLiteral("osd")));

        // Addition under a previously untouched root.
        DecorationProfileTree d = a;
        d.setOverride(QStringLiteral("window.tiled"), chainProfile({QStringLiteral("glow")}));
        QVERIFY(decorationRootDiffers(a, d, QStringLiteral("window")));
        QVERIFY(!decorationRootDiffers(a, d, QStringLiteral("osd")));

        // Baseline-only difference: invisible to every surface root.
        DecorationProfileTree e = a;
        e.setBaseline(chainProfile({QStringLiteral("glow")}));
        QVERIFY(!decorationRootDiffers(a, e, QStringLiteral("osd")));
        QVERIFY(!decorationRootDiffers(a, e, QStringLiteral("popup")));
        QVERIFY(!decorationRootDiffers(a, e, QStringLiteral("window")));
    }
};

QTEST_MAIN(TestDecorationPageScope)
#include "test_decoration_page_scope.moc"
