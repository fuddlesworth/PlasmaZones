// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <QTest>

using Phosphor::Screens::VirtualScreenConfig;
using Phosphor::Screens::VirtualScreenDef;

namespace {

constexpr QLatin1String kPhys{"Dell:U2722D:115107"};

VirtualScreenDef makeDef(int index, const QRectF& region)
{
    VirtualScreenDef def;
    def.index = index;
    def.id = PhosphorIdentity::VirtualScreenId::make(kPhys, index);
    def.physicalScreenId = kPhys;
    def.displayName = QStringLiteral("VS-%1").arg(index);
    def.region = region;
    return def;
}

VirtualScreenConfig makeHalvesConfig()
{
    VirtualScreenConfig cfg;
    cfg.physicalScreenId = kPhys;
    cfg.screens.append(makeDef(0, QRectF(0.0, 0.0, 0.5, 1.0)));
    cfg.screens.append(makeDef(1, QRectF(0.5, 0.0, 0.5, 1.0)));
    return cfg;
}

} // namespace

class TestVirtualScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── VirtualScreenDef::absoluteGeometry ───

    void testAbsoluteGeometryExclusiveRightEdges()
    {
        const QRect phys(0, 0, 1000, 1000);
        VirtualScreenDef left = makeDef(0, QRectF(0.0, 0.0, 0.5, 1.0));
        VirtualScreenDef right = makeDef(1, QRectF(0.5, 0.0, 0.5, 1.0));

        const QRect l = left.absoluteGeometry(phys);
        const QRect r = right.absoluteGeometry(phys);

        // No 1px gap or overlap between adjacent halves.
        QCOMPARE(l.x() + l.width(), r.x());
        QCOMPARE(l.width() + r.width(), phys.width());
    }

    void testAbsoluteGeometryClampsWhenToleranceOvershoots()
    {
        const QRect phys(100, 200, 800, 600);
        // Region intentionally slightly over 1.0 (JSON round-trip artifact).
        VirtualScreenDef def = makeDef(0, QRectF(0.0, 0.0, 1.0 + 5e-4, 1.0 + 5e-4));
        const QRect g = def.absoluteGeometry(phys);
        QVERIFY(g.x() >= phys.x());
        QVERIFY(g.y() >= phys.y());
        QVERIFY(g.right() <= phys.right());
        QVERIFY(g.bottom() <= phys.bottom());
    }

    void testAbsoluteGeometryNeverDegenerate()
    {
        const QRect phys(0, 0, 10, 10);
        // Zero-width input — absolute geometry must still be non-degenerate.
        VirtualScreenDef def = makeDef(0, QRectF(0.999, 0.0, 0.001, 1.0));
        const QRect g = def.absoluteGeometry(phys);
        QVERIFY(g.width() >= 1);
        QVERIFY(g.height() >= 1);
    }

    // ─── VirtualScreenDef::physicalEdges ───

    void testPhysicalEdgesAtBoundaries()
    {
        VirtualScreenDef full = makeDef(0, QRectF(0.0, 0.0, 1.0, 1.0));
        const auto e = full.physicalEdges();
        QVERIFY(e.left);
        QVERIFY(e.top);
        QVERIFY(e.right);
        QVERIFY(e.bottom);
    }

    void testPhysicalEdgesInteriorSplit()
    {
        VirtualScreenDef left = makeDef(0, QRectF(0.0, 0.0, 0.5, 1.0));
        const auto e = left.physicalEdges();
        QVERIFY(e.left); // at physical boundary
        QVERIFY(!e.right); // interior edge shared with sibling
        QVERIFY(e.top);
        QVERIFY(e.bottom);
    }

    // ─── VirtualScreenConfig::isValid ───

    void testIsValidAcceptsEmptyConfigAsRemoval()
    {
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = kPhys;
        QString err;
        QVERIFY(VirtualScreenConfig::isValid(cfg, kPhys, /*max=*/8, &err));
        QVERIFY(err.isEmpty());
    }

    void testIsValidRejectsSingleScreenSubdivision()
    {
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = kPhys;
        cfg.screens.append(makeDef(0, QRectF(0, 0, 1, 1)));
        QString err;
        QVERIFY(!VirtualScreenConfig::isValid(cfg, kPhys, 8, &err));
        QVERIFY(err.contains(QLatin1String("at least 2")));
    }

    void testIsValidRejectsExcessScreens()
    {
        VirtualScreenConfig cfg = makeHalvesConfig();
        QString err;
        QVERIFY(!VirtualScreenConfig::isValid(cfg, kPhys, /*max=*/1, &err));
        QVERIFY(err.contains(QLatin1String("too many")));
    }

    void testIsValidZeroCapMeansNoCap()
    {
        // Build a 10-entry stripe that evenly covers the unit square.
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = kPhys;
        for (int i = 0; i < 10; ++i) {
            cfg.screens.append(makeDef(i, QRectF(i * 0.1, 0.0, 0.1, 1.0)));
        }
        QString err;
        QVERIFY(VirtualScreenConfig::isValid(cfg, kPhys, /*max=*/0, &err));
    }

    void testIsValidRejectsOverlap()
    {
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = kPhys;
        cfg.screens.append(makeDef(0, QRectF(0.0, 0.0, 0.6, 1.0)));
        cfg.screens.append(makeDef(1, QRectF(0.4, 0.0, 0.6, 1.0)));
        QString err;
        QVERIFY(!VirtualScreenConfig::isValid(cfg, kPhys, 8, &err));
        QVERIFY(err.contains(QLatin1String("overlapping")));
    }

    void testIsValidRejectsGap()
    {
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = kPhys;
        cfg.screens.append(makeDef(0, QRectF(0.0, 0.0, 0.4, 1.0)));
        cfg.screens.append(makeDef(1, QRectF(0.5, 0.0, 0.4, 1.0)));
        QString err;
        QVERIFY(!VirtualScreenConfig::isValid(cfg, kPhys, 8, &err));
        QVERIFY(err.contains(QLatin1String("insufficient coverage")));
    }

    void testIsValidRejectsWrongPhysicalId()
    {
        VirtualScreenConfig cfg = makeHalvesConfig();
        QString err;
        QVERIFY(!VirtualScreenConfig::isValid(cfg, QStringLiteral("Other:Monitor"), 8, &err));
        QVERIFY(err.contains(QLatin1String("does not match")));
    }

    void testIsValidRejectsDuplicateIndex()
    {
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = kPhys;
        cfg.screens.append(makeDef(0, QRectF(0.0, 0.0, 0.5, 1.0)));
        auto dup = makeDef(1, QRectF(0.5, 0.0, 0.5, 1.0));
        dup.index = 0; // duplicate
        dup.id = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
        cfg.screens.append(dup);
        QString err;
        QVERIFY(!VirtualScreenConfig::isValid(cfg, kPhys, 8, &err));
    }

    // ─── swapRegions ───

    void testSwapRegionsExchangesGeometryPreservesIdentity()
    {
        VirtualScreenConfig cfg = makeHalvesConfig();
        const QString idA = cfg.screens[0].id;
        const QString idB = cfg.screens[1].id;
        const QRectF originalA = cfg.screens[0].region;
        const QRectF originalB = cfg.screens[1].region;

        QVERIFY(cfg.swapRegions(idA, idB));

        // IDs stay put in place (array order); only the regions move.
        QCOMPARE(cfg.screens[0].id, idA);
        QCOMPARE(cfg.screens[1].id, idB);
        QCOMPARE(cfg.screens[0].region, originalB);
        QCOMPARE(cfg.screens[1].region, originalA);
    }

    void testSwapRegionsRejectsSameId()
    {
        VirtualScreenConfig cfg = makeHalvesConfig();
        const QString id = cfg.screens[0].id;
        QVERIFY(!cfg.swapRegions(id, id));
    }

    void testSwapRegionsRejectsMissingId()
    {
        VirtualScreenConfig cfg = makeHalvesConfig();
        QVERIFY(!cfg.swapRegions(cfg.screens[0].id, QStringLiteral("ghost")));
    }

    // ─── rotateRegions ───

    void testRotateRegionsClockwiseCyclesGeometry()
    {
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = kPhys;
        cfg.screens.append(makeDef(0, QRectF(0.0, 0.0, 0.5, 0.5)));
        cfg.screens.append(makeDef(1, QRectF(0.5, 0.0, 0.5, 0.5)));
        cfg.screens.append(makeDef(2, QRectF(0.0, 0.5, 0.5, 0.5)));
        cfg.screens.append(makeDef(3, QRectF(0.5, 0.5, 0.5, 0.5)));

        // Ring order top-left → top-right → bottom-right → bottom-left
        // (clockwise visual order).
        QVector<QString> order{cfg.screens[0].id, cfg.screens[1].id, cfg.screens[3].id, cfg.screens[2].id};

        const QRectF r0 = cfg.screens[0].region;
        const QRectF r1 = cfg.screens[1].region;
        const QRectF r3 = cfg.screens[3].region;
        const QRectF r2 = cfg.screens[2].region;

        QVERIFY(cfg.rotateRegions(order, /*clockwise=*/true));

        // CW: each def at ring position i inherits successor's region.
        QCOMPARE(cfg.screens[0].region, r1); // tl ← tr
        QCOMPARE(cfg.screens[1].region, r3); // tr ← br
        QCOMPARE(cfg.screens[3].region, r2); // br ← bl
        QCOMPARE(cfg.screens[2].region, r0); // bl ← tl
    }

    void testRotateRegionsRejectsDuplicateId()
    {
        VirtualScreenConfig cfg = makeHalvesConfig();
        QVector<QString> order{cfg.screens[0].id, cfg.screens[0].id};
        QVERIFY(!cfg.rotateRegions(order, true));
    }

    void testRotateRegionsRejectsUnknownId()
    {
        VirtualScreenConfig cfg = makeHalvesConfig();
        QVector<QString> order{cfg.screens[0].id, QStringLiteral("ghost")};
        QVERIFY(!cfg.rotateRegions(order, true));
    }
};

QTEST_APPLESS_MAIN(TestVirtualScreen)
#include "test_virtualscreen.moc"
