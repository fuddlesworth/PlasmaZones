// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtual_screen.cpp
 * @brief Unit tests for virtual screen data model and ID utilities
 *
 * Tests VirtualScreenId namespace (ID parsing/construction),
 * VirtualScreenDef::absoluteGeometry(), and VirtualScreenConfig
 * equality and subdivision detection.
 */

#include <QTest>
#include <QString>
#include <QRect>
#include <QRectF>

#include "core/virtualscreen.h"

using namespace PlasmaZones;

class TestVirtualScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // VirtualScreenId::isVirtual
    // ═══════════════════════════════════════════════════════════════════════════

    void testIsVirtual_physicalId_returnsFalse()
    {
        QVERIFY(!VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107")));
    }

    void testIsVirtual_virtualId_returnsTrue()
    {
        QVERIFY(VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107/vs:0")));
    }

    void testIsVirtual_virtualIdHighIndex_returnsTrue()
    {
        QVERIFY(VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107/vs:5")));
    }

    void testIsVirtual_emptyString_returnsFalse()
    {
        QVERIFY(!VirtualScreenId::isVirtual(QString()));
    }

    void testIsVirtual_bareVsSeparator_returnsTrue()
    {
        // Edge case: "/vs:0" with no physical ID prefix.
        // This is a malformed ID — a valid virtual screen ID must have a non-empty
        // physical screen ID before the "/vs:" separator. isVirtual() requires pos > 0
        // to reject false positives from malformed IDs.
        QVERIFY(!VirtualScreenId::isVirtual(QStringLiteral("/vs:0")));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // VirtualScreenId::extractPhysicalId
    // ═══════════════════════════════════════════════════════════════════════════

    void testExtractPhysicalId_virtualId()
    {
        QCOMPARE(VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107/vs:0")),
                 QStringLiteral("Dell:U2722D:115107"));
    }

    void testExtractPhysicalId_physicalId_returnsUnchanged()
    {
        QCOMPARE(VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107")),
                 QStringLiteral("Dell:U2722D:115107"));
    }

    void testExtractPhysicalId_higherIndex()
    {
        QCOMPARE(VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107/vs:2")),
                 QStringLiteral("Dell:U2722D:115107"));
    }

    void testExtractPhysicalId_emptyString()
    {
        QCOMPARE(VirtualScreenId::extractPhysicalId(QString()), QString());
    }

    void testExtractPhysicalId_bareVsSeparator_returnsOriginal()
    {
        // sep == 0, which is not > 0, so returns the original string
        QCOMPARE(VirtualScreenId::extractPhysicalId(QStringLiteral("/vs:0")), QStringLiteral("/vs:0"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // VirtualScreenId::extractIndex
    // ═══════════════════════════════════════════════════════════════════════════

    void testExtractIndex_index0()
    {
        QCOMPARE(VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:0")), 0);
    }

    void testExtractIndex_index3()
    {
        QCOMPARE(VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:3")), 3);
    }

    void testExtractIndex_physicalId_returnsNegOne()
    {
        QCOMPARE(VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107")), -1);
    }

    void testExtractIndex_invalidIndex_returnsNegOne()
    {
        QCOMPARE(VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:abc")), -1);
    }

    void testExtractIndex_emptyString_returnsNegOne()
    {
        QCOMPARE(VirtualScreenId::extractIndex(QString()), -1);
    }

    void testExtractIndex_emptyAfterSeparator_returnsNegOne()
    {
        QCOMPARE(VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:")), -1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // VirtualScreenId::make
    // ═══════════════════════════════════════════════════════════════════════════

    void testMake_index0()
    {
        QCOMPARE(VirtualScreenId::make(QStringLiteral("Dell:U2722D:115107"), 0),
                 QStringLiteral("Dell:U2722D:115107/vs:0"));
    }

    void testMake_index1()
    {
        QCOMPARE(VirtualScreenId::make(QStringLiteral("Dell:U2722D:115107"), 1),
                 QStringLiteral("Dell:U2722D:115107/vs:1"));
    }

    void testMake_roundTrip()
    {
        // make -> extractPhysicalId + extractIndex should round-trip
        const QString physId = QStringLiteral("LG:27GP850:ABC123");
        const int idx = 2;
        const QString vsId = VirtualScreenId::make(physId, idx);

        QCOMPARE(VirtualScreenId::extractPhysicalId(vsId), physId);
        QCOMPARE(VirtualScreenId::extractIndex(vsId), idx);
        QVERIFY(VirtualScreenId::isVirtual(vsId));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // VirtualScreenDef::absoluteGeometry
    // ═══════════════════════════════════════════════════════════════════════════

    void testAbsoluteGeometry_leftHalf()
    {
        VirtualScreenDef def;
        def.region = QRectF(0, 0, 0.5, 1.0);
        QRect phys(0, 0, 3440, 1440);

        QRect result = def.absoluteGeometry(phys);
        QCOMPARE(result, QRect(0, 0, 1720, 1440));
    }

    void testAbsoluteGeometry_rightHalf()
    {
        VirtualScreenDef def;
        def.region = QRectF(0.5, 0, 0.5, 1.0);
        QRect phys(0, 0, 3440, 1440);

        QRect result = def.absoluteGeometry(phys);
        QCOMPARE(result, QRect(1720, 0, 1720, 1440));
    }

    void testAbsoluteGeometry_leftThirdWithOffset()
    {
        // Physical screen at an offset (multi-monitor setup)
        VirtualScreenDef def;
        def.region = QRectF(0, 0, 0.333, 1.0);
        QRect phys(1920, 0, 3840, 1600);

        QRect result = def.absoluteGeometry(phys);
        // x = 1920 + round(0 * 3840) = 1920
        // y = 0 + round(0 * 1600) = 0
        // w = round(0.333 * 3840) = round(1278.72) = 1279
        // h = round(1.0 * 1600) = 1600
        QCOMPARE(result, QRect(1920, 0, 1279, 1600));
    }

    void testAbsoluteGeometry_middleThird()
    {
        VirtualScreenDef def;
        def.region = QRectF(0.333, 0, 0.334, 1.0);
        QRect phys(1920, 0, 3840, 1600);

        QRect result = def.absoluteGeometry(phys);
        // Edge-consistent rounding:
        // left  = 1920 + round(0.333 * 3840) = 1920 + 1279 = 3199
        // right = 1920 + round(0.667 * 3840) = 1920 + 2561 = 4481
        // width = 4481 - 3199 = 1282
        QCOMPARE(result.x(), 3199);
        QCOMPARE(result.width(), 1282);
        QCOMPARE(result.height(), 1600);
    }

    void testAbsoluteGeometry_rightThird()
    {
        VirtualScreenDef def;
        def.region = QRectF(0.667, 0, 0.333, 1.0);
        QRect phys(1920, 0, 3840, 1600);

        QRect result = def.absoluteGeometry(phys);
        // x = 1920 + round(0.667 * 3840) = 1920 + round(2561.28) = 1920 + 2561 = 4481
        // w = round(0.333 * 3840) = round(1278.72) = 1279
        QCOMPARE(result.x(), 4481);
        QCOMPARE(result.width(), 1279);
        QCOMPARE(result.height(), 1600);
    }

    void testAbsoluteGeometry_fullScreen()
    {
        VirtualScreenDef def;
        def.region = QRectF(0, 0, 1.0, 1.0);
        QRect phys(0, 0, 1920, 1080);

        QRect result = def.absoluteGeometry(phys);
        QCOMPARE(result, QRect(0, 0, 1920, 1080));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // VirtualScreenConfig::hasSubdivisions
    // ═══════════════════════════════════════════════════════════════════════════

    void testHasSubdivisions_empty_returnsFalse()
    {
        VirtualScreenConfig config;
        QVERIFY(!config.hasSubdivisions());
    }

    void testHasSubdivisions_singleScreen_returnsFalse()
    {
        VirtualScreenConfig config;
        config.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                               QStringLiteral("Full"), QRectF(0, 0, 1, 1), 0});
        QVERIFY(!config.hasSubdivisions());
    }

    void testHasSubdivisions_twoScreens_returnsTrue()
    {
        VirtualScreenConfig config;
        config.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                               QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});
        config.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                               QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1), 1});
        QVERIFY(config.hasSubdivisions());
    }

    void testIsEmpty_noScreens()
    {
        VirtualScreenConfig config;
        QVERIFY(config.isEmpty());
    }

    void testIsEmpty_withScreens()
    {
        VirtualScreenConfig config;
        config.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                               QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});
        QVERIFY(!config.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // VirtualScreenConfig equality
    // ═══════════════════════════════════════════════════════════════════════════

    void testConfigEquality_sameConfigs()
    {
        auto makeDef = [](const QString& id, const QString& name, const QRectF& region, int idx) {
            return VirtualScreenDef{id, QStringLiteral("phys"), name, region, idx};
        };

        VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys");
        a.screens.append(makeDef(QStringLiteral("phys/vs:0"), QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0));
        a.screens.append(makeDef(QStringLiteral("phys/vs:1"), QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1), 1));

        VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys");
        b.screens.append(makeDef(QStringLiteral("phys/vs:0"), QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0));
        b.screens.append(makeDef(QStringLiteral("phys/vs:1"), QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1), 1));

        QVERIFY(a == b);
        QVERIFY(!(a != b));
    }

    void testConfigEquality_differentRegions()
    {
        VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys");
        a.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Left"),
                                          QRectF(0, 0, 0.5, 1), 0});

        VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys");
        b.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Left"),
                                          QRectF(0, 0, 0.6, 1), 0});

        QVERIFY(a != b);
    }

    void testConfigEquality_differentNames()
    {
        VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys");
        a.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Left"),
                                          QRectF(0, 0, 0.5, 1), 0});

        VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys");
        b.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Right"),
                                          QRectF(0, 0, 0.5, 1), 0});

        QVERIFY(a != b);
    }

    void testConfigEquality_differentPhysicalScreenId()
    {
        VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys1");
        a.screens.append(VirtualScreenDef{QStringLiteral("phys1/vs:0"), QStringLiteral("phys1"), QStringLiteral("Left"),
                                          QRectF(0, 0, 0.5, 1), 0});

        VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys2");
        b.screens.append(VirtualScreenDef{QStringLiteral("phys2/vs:0"), QStringLiteral("phys2"), QStringLiteral("Left"),
                                          QRectF(0, 0, 0.5, 1), 0});

        QVERIFY(a != b);
    }

    void testConfigEquality_differentScreenCount()
    {
        VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys");
        a.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Left"),
                                          QRectF(0, 0, 0.5, 1), 0});

        VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys");
        b.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Left"),
                                          QRectF(0, 0, 0.5, 1), 0});
        b.screens.append(VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"), QStringLiteral("Right"),
                                          QRectF(0.5, 0, 0.5, 1), 1});

        QVERIFY(a != b);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // VirtualScreenDef equality (compares all fields)
    // ═══════════════════════════════════════════════════════════════════════════

    void testDefEquality_sameId()
    {
        VirtualScreenDef a{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Left"),
                           QRectF(0, 0, 0.5, 1), 0};
        VirtualScreenDef b{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Left"),
                           QRectF(0, 0, 0.5, 1), 0};

        // VirtualScreenDef::operator== compares all fields
        QVERIFY(a == b);
    }

    void testDefEquality_differentId()
    {
        VirtualScreenDef a{QStringLiteral("phys/vs:0"), QStringLiteral("phys"), QStringLiteral("Left"),
                           QRectF(0, 0, 0.5, 1), 0};
        VirtualScreenDef b{QStringLiteral("phys/vs:1"), QStringLiteral("phys"), QStringLiteral("Right"),
                           QRectF(0.5, 0, 0.5, 1), 1};

        QVERIFY(!(a == b));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Rounding edge case: no gaps or overlaps for common resolutions
    // ═══════════════════════════════════════════════════════════════════════════

    void testAbsoluteGeometry_noGapsOrOverlaps()
    {
        // Test that adjacent virtual screens have no gaps or overlaps
        // for common ultrawide resolutions with the 33-33-33 preset
        QVector<int> widths = {3440, 3840, 5120, 2560};

        for (int physWidth : widths) {
            QRect physGeom(0, 0, physWidth, 1440);

            VirtualScreenDef left;
            left.region = QRectF(0, 0, 0.333, 1.0);
            VirtualScreenDef center;
            center.region = QRectF(0.333, 0, 0.334, 1.0);
            VirtualScreenDef right;
            right.region = QRectF(0.667, 0, 0.333, 1.0);

            QRect leftGeom = left.absoluteGeometry(physGeom);
            QRect centerGeom = center.absoluteGeometry(physGeom);
            QRect rightGeom = right.absoluteGeometry(physGeom);

            // No gap between left and center
            QCOMPARE(leftGeom.x() + leftGeom.width(), centerGeom.x());
            // No gap between center and right
            QCOMPARE(centerGeom.x() + centerGeom.width(), rightGeom.x());
            // Right edge matches physical screen
            QCOMPARE(rightGeom.x() + rightGeom.width(), physWidth);
            // Left edge is at origin
            QCOMPARE(leftGeom.x(), 0);
        }
    }
};

QTEST_GUILESS_MAIN(TestVirtualScreen)
#include "test_virtual_screen.moc"
