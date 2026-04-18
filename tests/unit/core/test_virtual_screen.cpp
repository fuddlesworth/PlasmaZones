// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtual_screen.cpp
 * @brief Unit tests for virtual screen data model and ID utilities
 *
 * Tests VirtualScreenId namespace (ID parsing/construction),
 * Phosphor::Screens::VirtualScreenDef::absoluteGeometry(), and Phosphor::Screens::VirtualScreenConfig
 * equality and subdivision detection.
 */

#include <QTest>
#include <QString>
#include <QRect>
#include <QRectF>

#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorIdentity/VirtualScreenId.h>

using namespace Phosphor::Screens;
namespace VirtualScreenId = PhosphorIdentity::VirtualScreenId;

class TestVirtualScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // --- PhosphorIdentity::VirtualScreenId::isVirtual ---

    void testIsVirtual_physicalId_returnsFalse()
    {
        QVERIFY(!PhosphorIdentity::VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107")));
    }
    void testIsVirtual_virtualId_returnsTrue()
    {
        QVERIFY(PhosphorIdentity::VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107/vs:0")));
    }
    void testIsVirtual_virtualIdHighIndex_returnsTrue()
    {
        QVERIFY(PhosphorIdentity::VirtualScreenId::isVirtual(QStringLiteral("Dell:U2722D:115107/vs:5")));
    }
    void testIsVirtual_emptyString_returnsFalse()
    {
        QVERIFY(!PhosphorIdentity::VirtualScreenId::isVirtual(QString()));
    }
    void testIsVirtual_bareVsSeparator_returnsTrue()
    {
        // Malformed ID — isVirtual() requires pos > 0 to reject
        QVERIFY(!PhosphorIdentity::VirtualScreenId::isVirtual(QStringLiteral("/vs:0")));
    }

    // --- PhosphorIdentity::VirtualScreenId::extractPhysicalId ---
    void testExtractPhysicalId_virtualId()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107/vs:0")),
                 QStringLiteral("Dell:U2722D:115107"));
    }
    void testExtractPhysicalId_physicalId_returnsUnchanged()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107")),
                 QStringLiteral("Dell:U2722D:115107"));
    }
    void testExtractPhysicalId_higherIndex()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(QStringLiteral("Dell:U2722D:115107/vs:2")),
                 QStringLiteral("Dell:U2722D:115107"));
    }
    void testExtractPhysicalId_emptyString()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(QString()), QString());
    }
    void testExtractPhysicalId_bareVsSeparator_returnsOriginal()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(QStringLiteral("/vs:0")),
                 QStringLiteral("/vs:0"));
    }

    // --- PhosphorIdentity::VirtualScreenId::extractIndex ---
    void testExtractIndex_index0()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:0")), 0);
    }
    void testExtractIndex_index3()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:3")), 3);
    }
    void testExtractIndex_physicalId_returnsNegOne()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107")), -1);
    }
    void testExtractIndex_invalidIndex_returnsNegOne()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:abc")), -1);
    }
    void testExtractIndex_emptyString_returnsNegOne()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QString()), -1);
    }
    void testExtractIndex_emptyAfterSeparator_returnsNegOne()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("Dell:U2722D:115107/vs:")), -1);
    }

    // --- PhosphorIdentity::VirtualScreenId::make ---
    void testMake_index0()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::make(QStringLiteral("Dell:U2722D:115107"), 0),
                 QStringLiteral("Dell:U2722D:115107/vs:0"));
    }
    void testMake_index1()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::make(QStringLiteral("Dell:U2722D:115107"), 1),
                 QStringLiteral("Dell:U2722D:115107/vs:1"));
    }
    void testMake_roundTrip()
    {
        const QString physId = QStringLiteral("LG:27GP850:ABC123");
        const int idx = 2;
        const QString vsId = PhosphorIdentity::VirtualScreenId::make(physId, idx);
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(vsId), physId);
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(vsId), idx);
        QVERIFY(PhosphorIdentity::VirtualScreenId::isVirtual(vsId));
    }

    // --- Phosphor::Screens::VirtualScreenDef::absoluteGeometry ---

    void testAbsoluteGeometry_leftHalf()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0, 0, 0.5, 1.0);
        QRect phys(0, 0, 3440, 1440);

        QRect result = def.absoluteGeometry(phys);
        QCOMPARE(result, QRect(0, 0, 1720, 1440));
    }

    void testAbsoluteGeometry_rightHalf()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0.5, 0, 0.5, 1.0);
        QRect phys(0, 0, 3440, 1440);

        QRect result = def.absoluteGeometry(phys);
        QCOMPARE(result, QRect(1720, 0, 1720, 1440));
    }

    void testAbsoluteGeometry_leftThirdWithOffset()
    {
        // Physical screen at an offset (multi-monitor setup)
        Phosphor::Screens::VirtualScreenDef def;
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
        Phosphor::Screens::VirtualScreenDef def;
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
        Phosphor::Screens::VirtualScreenDef def;
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
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0, 0, 1.0, 1.0);
        QRect phys(0, 0, 1920, 1080);

        QRect result = def.absoluteGeometry(phys);
        QCOMPARE(result, QRect(0, 0, 1920, 1080));
    }

    // --- Phosphor::Screens::VirtualScreenConfig::hasSubdivisions ---

    void testHasSubdivisions_empty_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig config;
        QVERIFY(!config.hasSubdivisions());
    }

    void testHasSubdivisions_singleScreen_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig config;
        config.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                                  QStringLiteral("Full"), QRectF(0, 0, 1, 1), 0});
        QVERIFY(!config.hasSubdivisions());
    }

    void testHasSubdivisions_twoScreens_returnsTrue()
    {
        Phosphor::Screens::VirtualScreenConfig config;
        config.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                                  QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});
        config.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                                  QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1), 1});
        QVERIFY(config.hasSubdivisions());
    }

    void testIsEmpty_noScreens()
    {
        Phosphor::Screens::VirtualScreenConfig config;
        QVERIFY(config.isEmpty());
    }

    void testIsEmpty_withScreens()
    {
        Phosphor::Screens::VirtualScreenConfig config;
        config.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                                  QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});
        QVERIFY(!config.isEmpty());
    }

    // --- Phosphor::Screens::VirtualScreenConfig equality ---

    void testConfigEquality_sameConfigs()
    {
        auto makeDef = [](const QString& id, const QString& name, const QRectF& region, int idx) {
            return Phosphor::Screens::VirtualScreenDef{id, QStringLiteral("phys"), name, region, idx};
        };

        Phosphor::Screens::VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys");
        a.screens.append(makeDef(QStringLiteral("phys/vs:0"), QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0));
        a.screens.append(makeDef(QStringLiteral("phys/vs:1"), QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1), 1));

        Phosphor::Screens::VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys");
        b.screens.append(makeDef(QStringLiteral("phys/vs:0"), QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0));
        b.screens.append(makeDef(QStringLiteral("phys/vs:1"), QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1), 1));

        QVERIFY(a == b);
        QVERIFY(!(a != b));
    }

    void testConfigEquality_differentRegions()
    {
        Phosphor::Screens::VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys");
        a.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                             QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});

        Phosphor::Screens::VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys");
        b.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                             QStringLiteral("Left"), QRectF(0, 0, 0.6, 1), 0});

        QVERIFY(a != b);
    }

    void testConfigEquality_differentNames()
    {
        Phosphor::Screens::VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys");
        a.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                             QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});

        Phosphor::Screens::VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys");
        b.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                             QStringLiteral("Right"), QRectF(0, 0, 0.5, 1), 0});

        QVERIFY(a != b);
    }

    void testConfigEquality_differentPhysicalScreenId()
    {
        Phosphor::Screens::VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys1");
        a.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys1/vs:0"), QStringLiteral("phys1"),
                                                             QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});

        Phosphor::Screens::VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys2");
        b.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys2/vs:0"), QStringLiteral("phys2"),
                                                             QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});

        QVERIFY(a != b);
    }

    void testConfigEquality_differentScreenCount()
    {
        Phosphor::Screens::VirtualScreenConfig a;
        a.physicalScreenId = QStringLiteral("phys");
        a.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                             QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});

        Phosphor::Screens::VirtualScreenConfig b;
        b.physicalScreenId = QStringLiteral("phys");
        b.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                             QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0});
        b.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                             QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1), 1});

        QVERIFY(a != b);
    }

    // --- Phosphor::Screens::VirtualScreenDef equality ---

    void testDefEquality_sameId()
    {
        Phosphor::Screens::VirtualScreenDef a{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                              QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0};
        Phosphor::Screens::VirtualScreenDef b{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                              QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0};

        // Phosphor::Screens::VirtualScreenDef::operator== compares all fields
        QVERIFY(a == b);
    }

    void testDefEquality_differentId()
    {
        Phosphor::Screens::VirtualScreenDef a{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                              QStringLiteral("Left"), QRectF(0, 0, 0.5, 1), 0};
        Phosphor::Screens::VirtualScreenDef b{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                              QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1), 1};

        QVERIFY(!(a == b));
    }

    // --- Tiny region: absoluteGeometry guarantees minimum 1px dimensions ---

    void testAbsoluteGeometry_tinyRegion()
    {
        // A very tiny region (width=0.001) on a 1920px screen:
        // raw width = round(0.001 * 1920) - round(0 * 1920) could be ~2px,
        // but absoluteGeometry enforces qMax(1, ...) so even a sub-pixel
        // region must produce at least 1px in each dimension.
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0, 0, 0.001, 0.001);
        QRect phys(0, 0, 1920, 1080);

        QRect result = def.absoluteGeometry(phys);
        QVERIFY2(result.width() >= 1, "Tiny region must produce at least 1px width");
        QVERIFY2(result.height() >= 1, "Tiny region must produce at least 1px height");
        QCOMPARE(result.x(), 0);
        QCOMPARE(result.y(), 0);
    }

    // --- Rounding edge case: no gaps or overlaps for common resolutions ---

    void testAbsoluteGeometry_noGapsOrOverlaps()
    {
        // Test that adjacent virtual screens have no gaps or overlaps
        // for common ultrawide resolutions with the 33-33-33 preset
        QVector<int> widths = {3440, 3840, 5120, 2560};

        for (int physWidth : widths) {
            QRect physGeom(0, 0, physWidth, 1440);

            Phosphor::Screens::VirtualScreenDef left;
            left.region = QRectF(0, 0, 0.333, 1.0);
            Phosphor::Screens::VirtualScreenDef center;
            center.region = QRectF(0.333, 0, 0.334, 1.0);
            Phosphor::Screens::VirtualScreenDef right;
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

    // --- Phosphor::Screens::VirtualScreenConfig::swapRegions (direct struct-level tests) ---
    //
    // The swapper test suite (test_virtualscreen_swapper.cpp) exercises
    // this through Settings::setVirtualScreenConfig. These tests pin the
    // struct-level contract directly — no Settings, no IsolatedConfigGuard,
    // no D-Bus — so a failure localizes to the helper rather than the
    // whole pipeline.

    void testSwapRegions_exchangesRegionsAndPreservesFields()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("Left"), QRectF(0.0, 0.0, 0.5, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("Right"), QRectF(0.5, 0.0, 0.5, 1.0), 1});

        QVERIFY(cfg.swapRegions(QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:1")));

        // Regions swapped.
        QCOMPARE(cfg.screens[0].region, QRectF(0.5, 0.0, 0.5, 1.0));
        QCOMPARE(cfg.screens[1].region, QRectF(0.0, 0.0, 0.5, 1.0));
        // Every other field preserved on both defs.
        QCOMPARE(cfg.screens[0].id, QStringLiteral("phys/vs:0"));
        QCOMPARE(cfg.screens[1].id, QStringLiteral("phys/vs:1"));
        QCOMPARE(cfg.screens[0].displayName, QStringLiteral("Left"));
        QCOMPARE(cfg.screens[1].displayName, QStringLiteral("Right"));
        QCOMPARE(cfg.screens[0].index, 0);
        QCOMPARE(cfg.screens[1].index, 1);
        QCOMPARE(cfg.screens[0].physicalScreenId, QStringLiteral("phys"));
        QCOMPARE(cfg.screens[1].physicalScreenId, QStringLiteral("phys"));
    }

    void testSwapRegions_selfSwap_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("Left"), QRectF(0.0, 0.0, 0.5, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("Right"), QRectF(0.5, 0.0, 0.5, 1.0), 1});

        QVERIFY(!cfg.swapRegions(QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:0")));
        // Regions unchanged.
        QCOMPARE(cfg.screens[0].region, QRectF(0.0, 0.0, 0.5, 1.0));
        QCOMPARE(cfg.screens[1].region, QRectF(0.5, 0.0, 0.5, 1.0));
    }

    void testSwapRegions_unknownId_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("Left"), QRectF(0.0, 0.0, 0.5, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("Right"), QRectF(0.5, 0.0, 0.5, 1.0), 1});

        // idA unknown
        QVERIFY(!cfg.swapRegions(QStringLiteral("phys/vs:99"), QStringLiteral("phys/vs:1")));
        // idB unknown
        QVERIFY(!cfg.swapRegions(QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:99")));
        // Both unknown
        QVERIFY(!cfg.swapRegions(QStringLiteral("phys/vs:7"), QStringLiteral("phys/vs:8")));
        // Regions unchanged.
        QCOMPARE(cfg.screens[0].region, QRectF(0.0, 0.0, 0.5, 1.0));
        QCOMPARE(cfg.screens[1].region, QRectF(0.5, 0.0, 0.5, 1.0));
    }

    void testSwapRegions_isInvolutive()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("Left"), QRectF(0.0, 0.0, 0.5, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("Right"), QRectF(0.5, 0.0, 0.5, 1.0), 1});

        const Phosphor::Screens::VirtualScreenConfig before = cfg;
        QVERIFY(cfg.swapRegions(QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:1")));
        QVERIFY(cfg.swapRegions(QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:1")));
        QCOMPARE(cfg, before);
    }

    // --- Phosphor::Screens::VirtualScreenConfig::rotateRegions (direct struct-level tests) ---

    void testRotateRegions_threeElement_clockwise()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("A"), QRectF(0.0, 0.0, 0.333, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("B"), QRectF(0.333, 0.0, 0.334, 1.0), 1});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:2"), QStringLiteral("phys"),
                                                               QStringLiteral("C"), QRectF(0.667, 0.0, 0.333, 1.0), 2});

        const QRectF a = cfg.screens[0].region;
        const QRectF b = cfg.screens[1].region;
        const QRectF c = cfg.screens[2].region;

        const QVector<QString> order{QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:1"),
                                     QStringLiteral("phys/vs:2")};
        QVERIFY(cfg.rotateRegions(order, /*clockwise=*/true));

        // Per docs: CW rotate means def[i] takes def[(i+1) mod n]'s old region.
        QCOMPARE(cfg.screens[0].region, b);
        QCOMPARE(cfg.screens[1].region, c);
        QCOMPARE(cfg.screens[2].region, a);
    }

    void testRotateRegions_threeElement_counterclockwise()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("A"), QRectF(0.0, 0.0, 0.333, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("B"), QRectF(0.333, 0.0, 0.334, 1.0), 1});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:2"), QStringLiteral("phys"),
                                                               QStringLiteral("C"), QRectF(0.667, 0.0, 0.333, 1.0), 2});

        const QRectF a = cfg.screens[0].region;
        const QRectF b = cfg.screens[1].region;
        const QRectF c = cfg.screens[2].region;

        const QVector<QString> order{QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:1"),
                                     QStringLiteral("phys/vs:2")};
        QVERIFY(cfg.rotateRegions(order, /*clockwise=*/false));

        // CCW: def[i] takes def[(i-1+n) mod n]'s old region.
        QCOMPARE(cfg.screens[0].region, c);
        QCOMPARE(cfg.screens[1].region, a);
        QCOMPARE(cfg.screens[2].region, b);
    }

    void testRotateRegions_acceptsSubset()
    {
        // rotateRegions must accept an orderedIds list that names only a
        // subset of the config's defs — the docstring explicitly says so.
        // Here we rotate only VSs 0 and 2, leaving VS 1 completely alone.
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("A"), QRectF(0.0, 0.0, 0.25, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("B"), QRectF(0.25, 0.0, 0.25, 1.0), 1});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:2"), QStringLiteral("phys"),
                                                               QStringLiteral("C"), QRectF(0.5, 0.0, 0.5, 1.0), 2});

        const QRectF r0 = cfg.screens[0].region;
        const QRectF r1 = cfg.screens[1].region;
        const QRectF r2 = cfg.screens[2].region;

        const QVector<QString> partial{QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:2")};
        QVERIFY(cfg.rotateRegions(partial, /*clockwise=*/true));

        // 2-element rotation = swap, so def[0] takes def[1]'s region and vice versa.
        QCOMPARE(cfg.screens[0].region, r2);
        QCOMPARE(cfg.screens[2].region, r0);
        // Untouched VS keeps its original region.
        QCOMPARE(cfg.screens[1].region, r1);
    }

    void testRotateRegions_fewerThanTwoIds_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("A"), QRectF(0.0, 0.0, 0.5, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("B"), QRectF(0.5, 0.0, 0.5, 1.0), 1});
        const Phosphor::Screens::VirtualScreenConfig before = cfg;

        QVERIFY(!cfg.rotateRegions({}, /*clockwise=*/true));
        QVERIFY(!cfg.rotateRegions({QStringLiteral("phys/vs:0")}, /*clockwise=*/true));
        // Config unchanged.
        QCOMPARE(cfg, before);
    }

    void testRotateRegions_unknownId_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("A"), QRectF(0.0, 0.0, 0.5, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("B"), QRectF(0.5, 0.0, 0.5, 1.0), 1});
        const Phosphor::Screens::VirtualScreenConfig before = cfg;

        const QVector<QString> order{QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:999")};
        QVERIFY(!cfg.rotateRegions(order, /*clockwise=*/true));
        // Config unchanged even though one id resolved — failure is atomic.
        QCOMPARE(cfg, before);
    }

    void testRotateRegions_fullCycle_returnsToStart()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("A"), QRectF(0.0, 0.0, 0.25, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("B"), QRectF(0.25, 0.0, 0.25, 1.0), 1});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:2"), QStringLiteral("phys"),
                                                               QStringLiteral("C"), QRectF(0.5, 0.0, 0.25, 1.0), 2});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:3"), QStringLiteral("phys"),
                                                               QStringLiteral("D"), QRectF(0.75, 0.0, 0.25, 1.0), 3});
        const Phosphor::Screens::VirtualScreenConfig before = cfg;

        const QVector<QString> order{QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:1"),
                                     QStringLiteral("phys/vs:2"), QStringLiteral("phys/vs:3")};
        for (int i = 0; i < 4; ++i) {
            QVERIFY(cfg.rotateRegions(order, /*clockwise=*/true));
        }
        QCOMPARE(cfg, before);
    }

    void testRotateRegions_cwThenCcw_isNoOp()
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = QStringLiteral("phys");
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:0"), QStringLiteral("phys"),
                                                               QStringLiteral("A"), QRectF(0.0, 0.0, 0.333, 1.0), 0});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:1"), QStringLiteral("phys"),
                                                               QStringLiteral("B"), QRectF(0.333, 0.0, 0.334, 1.0), 1});
        cfg.screens.append(Phosphor::Screens::VirtualScreenDef{QStringLiteral("phys/vs:2"), QStringLiteral("phys"),
                                                               QStringLiteral("C"), QRectF(0.667, 0.0, 0.333, 1.0), 2});
        const Phosphor::Screens::VirtualScreenConfig before = cfg;

        const QVector<QString> order{QStringLiteral("phys/vs:0"), QStringLiteral("phys/vs:1"),
                                     QStringLiteral("phys/vs:2")};
        QVERIFY(cfg.rotateRegions(order, /*clockwise=*/true));
        QVERIFY(cfg.rotateRegions(order, /*clockwise=*/false));
        QCOMPARE(cfg, before);
    }

    // --- Rounding edge case: vertical split — no gaps or overlaps ---

    void testAbsoluteGeometry_verticalSplit_noGapsOrOverlaps()
    {
        // Test that adjacent top/bottom virtual screens have no gaps or overlaps
        QVector<int> heights = {1080, 1440, 1600, 2160};

        for (int physHeight : heights) {
            QRect physGeom(0, 0, 1920, physHeight);

            Phosphor::Screens::VirtualScreenDef top;
            top.region = QRectF(0, 0, 1.0, 0.5);
            Phosphor::Screens::VirtualScreenDef bottom;
            bottom.region = QRectF(0, 0.5, 1.0, 0.5);

            QRect topGeom = top.absoluteGeometry(physGeom);
            QRect bottomGeom = bottom.absoluteGeometry(physGeom);

            // No gap between top and bottom
            QCOMPARE(topGeom.y() + topGeom.height(), bottomGeom.y());
            // Bottom edge matches physical screen
            QCOMPARE(bottomGeom.y() + bottomGeom.height(), physHeight);
            // Top edge is at origin
            QCOMPARE(topGeom.y(), 0);
            // Both span full width
            QCOMPARE(topGeom.x(), 0);
            QCOMPARE(topGeom.width(), 1920);
            QCOMPARE(bottomGeom.x(), 0);
            QCOMPARE(bottomGeom.width(), 1920);
        }
    }

    // --- Rounding edge case: 2x2 grid split — no gaps or overlaps ---

    void testAbsoluteGeometry_2x2Grid_noGapsOrOverlaps()
    {
        // Test that a 2x2 grid of virtual screens has no gaps or overlaps
        // Common ultrawide + standard resolutions
        struct Resolution
        {
            int w;
            int h;
        };
        QVector<Resolution> resolutions = {{3840, 2160}, {3440, 1440}, {2560, 1440}, {1920, 1080}};

        for (const auto& res : resolutions) {
            QRect physGeom(0, 0, res.w, res.h);

            Phosphor::Screens::VirtualScreenDef topLeft;
            topLeft.region = QRectF(0, 0, 0.5, 0.5);
            Phosphor::Screens::VirtualScreenDef topRight;
            topRight.region = QRectF(0.5, 0, 0.5, 0.5);
            Phosphor::Screens::VirtualScreenDef bottomLeft;
            bottomLeft.region = QRectF(0, 0.5, 0.5, 0.5);
            Phosphor::Screens::VirtualScreenDef bottomRight;
            bottomRight.region = QRectF(0.5, 0.5, 0.5, 0.5);

            QRect tlGeom = topLeft.absoluteGeometry(physGeom);
            QRect trGeom = topRight.absoluteGeometry(physGeom);
            QRect blGeom = bottomLeft.absoluteGeometry(physGeom);
            QRect brGeom = bottomRight.absoluteGeometry(physGeom);

            // Horizontal adjacency: no gap between left and right columns
            QCOMPARE(tlGeom.x() + tlGeom.width(), trGeom.x());
            QCOMPARE(blGeom.x() + blGeom.width(), brGeom.x());

            // Vertical adjacency: no gap between top and bottom rows
            QCOMPARE(tlGeom.y() + tlGeom.height(), blGeom.y());
            QCOMPARE(trGeom.y() + trGeom.height(), brGeom.y());

            // Outer edges match physical screen bounds
            QCOMPARE(tlGeom.x(), 0);
            QCOMPARE(tlGeom.y(), 0);
            QCOMPARE(trGeom.x() + trGeom.width(), res.w);
            QCOMPARE(trGeom.y(), 0);
            QCOMPARE(blGeom.x(), 0);
            QCOMPARE(blGeom.y() + blGeom.height(), res.h);
            QCOMPARE(brGeom.x() + brGeom.width(), res.w);
            QCOMPARE(brGeom.y() + brGeom.height(), res.h);
        }
    }

    // --- Phosphor::Screens::VirtualScreenDef::physicalEdges ---

    void testPhysicalEdges_leftEdgeAtOrigin()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0.0, 0.0, 0.5, 1.0);
        auto edges = def.physicalEdges();
        QVERIFY(edges.left);
        QVERIFY(edges.top);
        QVERIFY(!edges.right);
        QVERIFY(edges.bottom);
    }

    void testPhysicalEdges_rightEdgeAtOne()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0.5, 0.0, 0.5, 1.0);
        auto edges = def.physicalEdges();
        QVERIFY(!edges.left);
        QVERIFY(edges.top);
        QVERIFY(edges.right);
        QVERIFY(edges.bottom);
    }

    void testPhysicalEdges_topEdgeAtOrigin()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0.0, 0.0, 1.0, 0.5);
        auto edges = def.physicalEdges();
        QVERIFY(edges.left);
        QVERIFY(edges.top);
        QVERIFY(edges.right);
        QVERIFY(!edges.bottom);
    }

    void testPhysicalEdges_bottomEdgeAtOne()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0.0, 0.5, 1.0, 0.5);
        auto edges = def.physicalEdges();
        QVERIFY(edges.left);
        QVERIFY(!edges.top);
        QVERIFY(edges.right);
        QVERIFY(edges.bottom);
    }

    void testPhysicalEdges_interiorRegion_allFalse()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0.3, 0.3, 0.4, 0.4);
        auto edges = def.physicalEdges();
        QVERIFY(!edges.left);
        QVERIFY(!edges.top);
        QVERIFY(!edges.right);
        QVERIFY(!edges.bottom);
    }

    void testPhysicalEdges_withinTolerance_stillTrue()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0.0005, 0.0005, 0.999, 0.999);
        auto edges = def.physicalEdges();
        QVERIFY(edges.left);
        QVERIFY(edges.top);
        QVERIFY(edges.right);
        QVERIFY(edges.bottom);
    }

    void testPhysicalEdges_fullScreen_allTrue()
    {
        Phosphor::Screens::VirtualScreenDef def;
        def.region = QRectF(0.0, 0.0, 1.0, 1.0);
        auto edges = def.physicalEdges();
        QVERIFY(edges.left);
        QVERIFY(edges.top);
        QVERIFY(edges.right);
        QVERIFY(edges.bottom);
    }

    // --- VirtualScreenId edge cases: negative, double-nested, multi-digit ---

    void testMake_negativeIndex_returnsEmpty()
    {
        QVERIFY(PhosphorIdentity::VirtualScreenId::make(QStringLiteral("physId"), -1).isEmpty());
    }

    void testMake_doubleNestedVirtualId()
    {
        // Calling make() with an already-virtual ID produces a double-suffixed ID.
        // extractPhysicalId uses indexOf (first match), so it returns the true physical ID.
        // extractIndex fails because "0/vs:1" is not a valid integer.
        QString result = PhosphorIdentity::VirtualScreenId::make(QStringLiteral("physId/vs:0"), 1);
        QCOMPARE(result, QStringLiteral("physId/vs:0/vs:1"));
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractPhysicalId(result), QStringLiteral("physId"));
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(result), -1);
    }

    void testExtractIndex_multiDigit()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("physId/vs:10")), 10);
    }

    void testExtractIndex_largeIndex()
    {
        QCOMPARE(PhosphorIdentity::VirtualScreenId::extractIndex(QStringLiteral("physId/vs:999")), 999);
    }

    // --- Cross-validation: daemon vs effect extractPhysicalId ---
    // Validates daemon PhosphorIdentity::VirtualScreenId::extractPhysicalId() matches effect-side logic.
    // If the effect's implementation changes, the local copy below must be updated.
    void testExtractPhysicalId_crossValidation()
    {
        // Local copy of effect's extractPhysicalScreenId (kwin-effect/plasmazoneseffect.h ~540)
        auto effectExtractPhysicalScreenId = [](const QString& screenId) -> QString {
            static const QLatin1String vsSep("/vs:");
            int pos = screenId.indexOf(vsSep);
            return (pos > 0) ? screenId.left(pos) : screenId;
        };

        struct TestCase
        {
            QString input;
            QString expected;
        };
        const QVector<TestCase> cases = {
            {QStringLiteral("DP-1/vs:0"), QStringLiteral("DP-1")}, // virtual
            {QStringLiteral("DP-1/vs:2"), QStringLiteral("DP-1")}, // higher index
            {QStringLiteral("DP-1:BenQ:12345/vs:2"), QStringLiteral("DP-1:BenQ:12345")}, // EDID
            {QStringLiteral("DP-1"), QStringLiteral("DP-1")}, // plain connector
            {QStringLiteral("Dell:U2722D:115107"), QStringLiteral("Dell:U2722D:115107")}, // EDID
            {QString(), QString()}, // empty
            {QStringLiteral("/vs:0"), QStringLiteral("/vs:0")}, // malformed
            {QStringLiteral("DP-1/vs:"), QStringLiteral("DP-1")}, // no index
        };

        for (const auto& tc : cases) {
            QString daemonResult = PhosphorIdentity::VirtualScreenId::extractPhysicalId(tc.input);
            QString effectResult = effectExtractPhysicalScreenId(tc.input);

            QCOMPARE(daemonResult, tc.expected);
            QCOMPARE(effectResult, tc.expected);
            QCOMPARE(daemonResult, effectResult);
        }
    }

    // ─── Phosphor::Screens::VirtualScreenConfig::swapRegions ─────────────────────────────────

    // Helper: build a two-VS horizontal-split config on a fake monitor.
    static Phosphor::Screens::VirtualScreenConfig makeTwoSplit(const QString& physicalId = QStringLiteral("DP-1"))
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = physicalId;
        Phosphor::Screens::VirtualScreenDef left;
        left.id = PhosphorIdentity::VirtualScreenId::make(physicalId, 0);
        left.physicalScreenId = physicalId;
        left.displayName = QStringLiteral("Left");
        left.region = QRectF(0.0, 0.0, 0.5, 1.0);
        left.index = 0;
        Phosphor::Screens::VirtualScreenDef right;
        right.id = PhosphorIdentity::VirtualScreenId::make(physicalId, 1);
        right.physicalScreenId = physicalId;
        right.displayName = QStringLiteral("Right");
        right.region = QRectF(0.5, 0.0, 0.5, 1.0);
        right.index = 1;
        cfg.screens = {left, right};
        return cfg;
    }

    // Helper: build a three-VS horizontal-split config.
    static Phosphor::Screens::VirtualScreenConfig makeThreeSplit(const QString& physicalId = QStringLiteral("DP-1"))
    {
        Phosphor::Screens::VirtualScreenConfig cfg;
        cfg.physicalScreenId = physicalId;
        for (int i = 0; i < 3; ++i) {
            Phosphor::Screens::VirtualScreenDef def;
            def.id = PhosphorIdentity::VirtualScreenId::make(physicalId, i);
            def.physicalScreenId = physicalId;
            def.displayName = QStringLiteral("VS %1").arg(i);
            def.region = QRectF(i / 3.0, 0.0, 1.0 / 3.0, 1.0);
            def.index = i;
            cfg.screens.append(def);
        }
        return cfg;
    }

    void swapRegions_twoSplit_exchangesGeometry()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeTwoSplit();
        const QString idLeft = cfg.screens[0].id;
        const QString idRight = cfg.screens[1].id;
        const QRectF leftRegion = cfg.screens[0].region;
        const QRectF rightRegion = cfg.screens[1].region;

        QVERIFY(cfg.swapRegions(idLeft, idRight));
        // Regions exchanged.
        QCOMPARE(cfg.screens[0].region, rightRegion);
        QCOMPARE(cfg.screens[1].region, leftRegion);
        // Everything else preserved — IDs, display names, indices, physical id.
        QCOMPARE(cfg.screens[0].id, idLeft);
        QCOMPARE(cfg.screens[1].id, idRight);
        QCOMPARE(cfg.screens[0].displayName, QStringLiteral("Left"));
        QCOMPARE(cfg.screens[1].displayName, QStringLiteral("Right"));
        QCOMPARE(cfg.screens[0].index, 0);
        QCOMPARE(cfg.screens[1].index, 1);
    }

    void swapRegions_resultPassesValidation()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeTwoSplit();
        QVERIFY(cfg.swapRegions(cfg.screens[0].id, cfg.screens[1].id));
        QString err;
        QVERIFY2(Phosphor::Screens::VirtualScreenConfig::isValid(cfg, cfg.physicalScreenId, 8, &err), qPrintable(err));
    }

    void swapRegions_sameId_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeTwoSplit();
        const QString idLeft = cfg.screens[0].id;
        const Phosphor::Screens::VirtualScreenConfig before = cfg;
        QVERIFY(!cfg.swapRegions(idLeft, idLeft));
        QCOMPARE(cfg, before); // unchanged
    }

    void swapRegions_unknownId_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeTwoSplit();
        const Phosphor::Screens::VirtualScreenConfig before = cfg;
        QVERIFY(!cfg.swapRegions(cfg.screens[0].id, QStringLiteral("DP-1/vs:9")));
        QCOMPARE(cfg, before);
    }

    // ─── Phosphor::Screens::VirtualScreenConfig::rotateRegions ──────────────────────────────

    void rotateRegions_threeSplit_clockwise()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        QVector<QString> order{cfg.screens[0].id, cfg.screens[1].id, cfg.screens[2].id};
        const QRectF r0 = cfg.screens[0].region;
        const QRectF r1 = cfg.screens[1].region;
        const QRectF r2 = cfg.screens[2].region;

        QVERIFY(cfg.rotateRegions(order, /*clockwise=*/true));
        // Clockwise: def[i] takes def[(i+1) mod n]'s old region.
        // def[0] ← def[1] = r1; def[1] ← def[2] = r2; def[2] ← def[0] = r0.
        QCOMPARE(cfg.screens[0].region, r1);
        QCOMPARE(cfg.screens[1].region, r2);
        QCOMPARE(cfg.screens[2].region, r0);
    }

    void rotateRegions_threeSplit_counterClockwise()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        QVector<QString> order{cfg.screens[0].id, cfg.screens[1].id, cfg.screens[2].id};
        const QRectF r0 = cfg.screens[0].region;
        const QRectF r1 = cfg.screens[1].region;
        const QRectF r2 = cfg.screens[2].region;

        QVERIFY(cfg.rotateRegions(order, /*clockwise=*/false));
        // Counter-clockwise: def[i] takes def[(i-1) mod n]'s old region.
        QCOMPARE(cfg.screens[0].region, r2);
        QCOMPARE(cfg.screens[1].region, r0);
        QCOMPARE(cfg.screens[2].region, r1);
    }

    void rotateRegions_fullCycleReturnsToStart()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        const Phosphor::Screens::VirtualScreenConfig original = cfg;
        QVector<QString> order{cfg.screens[0].id, cfg.screens[1].id, cfg.screens[2].id};

        for (int i = 0; i < 3; ++i) {
            QVERIFY(cfg.rotateRegions(order, true));
        }
        QCOMPARE(cfg, original);
    }

    void rotateRegions_clockwiseThenCCW_isNoOp()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        const Phosphor::Screens::VirtualScreenConfig original = cfg;
        QVector<QString> order{cfg.screens[0].id, cfg.screens[1].id, cfg.screens[2].id};

        QVERIFY(cfg.rotateRegions(order, true));
        QVERIFY(cfg.rotateRegions(order, false));
        QCOMPARE(cfg, original);
    }

    void rotateRegions_twoSplit_equivalentToSwap()
    {
        Phosphor::Screens::VirtualScreenConfig a = makeTwoSplit();
        Phosphor::Screens::VirtualScreenConfig b = makeTwoSplit();
        QVERIFY(a.rotateRegions({a.screens[0].id, a.screens[1].id}, true));
        QVERIFY(b.swapRegions(b.screens[0].id, b.screens[1].id));
        QCOMPARE(a, b);
    }

    void rotateRegions_resultPassesValidation()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        QVector<QString> order{cfg.screens[0].id, cfg.screens[1].id, cfg.screens[2].id};
        QVERIFY(cfg.rotateRegions(order, true));
        QString err;
        QVERIFY2(Phosphor::Screens::VirtualScreenConfig::isValid(cfg, cfg.physicalScreenId, 8, &err), qPrintable(err));
    }

    void rotateRegions_subsetOfDefs()
    {
        // Rotate only two of three defs — the third should be untouched.
        // For a two-element list, CW and swap are equivalent.
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        const QRectF untouched = cfg.screens[2].region;
        const QRectF r0 = cfg.screens[0].region;
        const QRectF r1 = cfg.screens[1].region;

        QVERIFY(cfg.rotateRegions({cfg.screens[0].id, cfg.screens[1].id}, true));
        QCOMPARE(cfg.screens[0].region, r1);
        QCOMPARE(cfg.screens[1].region, r0);
        QCOMPARE(cfg.screens[2].region, untouched);
    }

    void rotateRegions_tooFewIds_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        const Phosphor::Screens::VirtualScreenConfig before = cfg;
        QVERIFY(!cfg.rotateRegions({}, true));
        QVERIFY(!cfg.rotateRegions({cfg.screens[0].id}, true));
        QCOMPARE(cfg, before);
    }

    void rotateRegions_unknownId_returnsFalse()
    {
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        const Phosphor::Screens::VirtualScreenConfig before = cfg;
        QVERIFY(!cfg.rotateRegions({cfg.screens[0].id, QStringLiteral("DP-1/vs:9")}, true));
        QCOMPARE(cfg, before);
    }

    void rotateRegions_duplicateId_returnsFalse()
    {
        // Passing the same id twice would cause two ring slots to share a
        // target def index, and the rotation loop would then overwrite that
        // def twice with different sources — silently corrupting geometry.
        // The guard must reject this before any mutation happens.
        Phosphor::Screens::VirtualScreenConfig cfg = makeThreeSplit();
        const Phosphor::Screens::VirtualScreenConfig before = cfg;
        QVERIFY(!cfg.rotateRegions({cfg.screens[0].id, cfg.screens[0].id, cfg.screens[1].id}, true));
        QCOMPARE(cfg, before);
        QVERIFY(!cfg.rotateRegions({cfg.screens[0].id, cfg.screens[1].id, cfg.screens[0].id}, false));
        QCOMPARE(cfg, before);
    }
};

QTEST_GUILESS_MAIN(TestVirtualScreen)
#include "test_virtual_screen.moc"
