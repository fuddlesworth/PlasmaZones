// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/WindowQuery.h>

#include <QTest>

using namespace PhosphorRules;
using PhosphorProtocol::WindowType;

class TestWindowQuery : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testContextOnlyQuery_hasNoWindow()
    {
        WindowQuery q;
        q.screenId = QStringLiteral("DP-2");
        q.virtualDesktop = 2;
        q.activity = QStringLiteral("{work}");
        QVERIFY(!q.hasWindow());
    }

    void testPerWindowQuery_hasWindow()
    {
        WindowQuery q;
        q.appId = QStringLiteral("org.kde.konsole");
        QVERIFY(q.hasWindow());
    }

    void testValueForField_contextAlwaysPresent()
    {
        WindowQuery q;
        q.screenId = QStringLiteral("DP-1");
        q.virtualDesktop = 3;
        q.activity = QStringLiteral("{act}");

        QCOMPARE(q.valueForField(Field::ScreenId)->toString(), QStringLiteral("DP-1"));
        QCOMPARE(q.valueForField(Field::VirtualDesktop)->toInt(), 3);
        QCOMPARE(q.valueForField(Field::Activity)->toString(), QStringLiteral("{act}"));
    }

    void testValueForField_absentWindowAttribute()
    {
        WindowQuery q; // no window attributes
        QVERIFY(!q.valueForField(Field::AppId).has_value());
        QVERIFY(!q.valueForField(Field::Title).has_value());
        QVERIFY(!q.valueForField(Field::Pid).has_value());
        QVERIFY(!q.valueForField(Field::WindowType).has_value());
        QVERIFY(!q.valueForField(Field::IsSticky).has_value());
        QVERIFY(!q.valueForField(Field::IsFocused).has_value());
    }

    void testValueForField_presentWindowAttribute()
    {
        WindowQuery q;
        q.appId = QStringLiteral("firefox");
        q.pid = 4242;
        q.windowType = WindowType::Dialog;
        q.isFullscreen = true;
        q.isFocused = true;

        QCOMPARE(q.valueForField(Field::AppId)->toString(), QStringLiteral("firefox"));
        QCOMPARE(q.valueForField(Field::Pid)->toInt(), 4242);
        // WindowType resolves to its underlying int.
        QCOMPARE(q.valueForField(Field::WindowType)->toInt(), static_cast<int>(WindowType::Dialog));
        QCOMPARE(q.valueForField(Field::IsFullscreen)->toBool(), true);
        QCOMPARE(q.valueForField(Field::IsFocused)->toBool(), true);
    }

    void testValueForField_falseBoolIsStillPresent()
    {
        WindowQuery q;
        q.isMinimized = false;
        const auto v = q.valueForField(Field::IsMinimized);
        QVERIFY(v.has_value()); // present even though false
        QCOMPARE(v->toBool(), false);
    }

    void testValueForField_newTransientNotificationSizeFields()
    {
        WindowQuery q;
        // Absent by default — inert during windowless context resolution.
        QVERIFY(!q.valueForField(Field::IsTransient).has_value());
        QVERIFY(!q.valueForField(Field::IsNotification).has_value());
        QVERIFY(!q.valueForField(Field::Width).has_value());
        QVERIFY(!q.valueForField(Field::Height).has_value());

        q.isTransient = true;
        q.isNotification = false;
        q.width = 320;
        q.height = 240;
        QCOMPARE(q.valueForField(Field::IsTransient)->toBool(), true);
        // Present even though false (mirrors the bool-field contract above).
        QVERIFY(q.valueForField(Field::IsNotification).has_value());
        QCOMPARE(q.valueForField(Field::IsNotification)->toBool(), false);
        QCOMPARE(q.valueForField(Field::Width)->toInt(), 320);
        QCOMPARE(q.valueForField(Field::Height)->toInt(), 240);
        QVERIFY(q.hasWindow());
    }

    void testValueForField_kwinPropertyFields()
    {
        WindowQuery q;
        // Absent by default — inert during windowless context resolution.
        QVERIFY(!q.valueForField(Field::KeepAbove).has_value());
        QVERIFY(!q.valueForField(Field::KeepBelow).has_value());
        QVERIFY(!q.valueForField(Field::SkipTaskbar).has_value());
        QVERIFY(!q.valueForField(Field::SkipPager).has_value());
        QVERIFY(!q.valueForField(Field::SkipSwitcher).has_value());
        QVERIFY(!q.valueForField(Field::IsModal).has_value());
        QVERIFY(!q.valueForField(Field::HasDecoration).has_value());
        QVERIFY(!q.valueForField(Field::IsResizable).has_value());
        QVERIFY(!q.valueForField(Field::PositionX).has_value());
        QVERIFY(!q.valueForField(Field::PositionY).has_value());
        QVERIFY(!q.valueForField(Field::CaptionNormal).has_value());
        QVERIFY(!q.hasWindow());

        q.keepAbove = true;
        q.keepBelow = false;
        q.skipTaskbar = true;
        q.skipPager = false;
        q.skipSwitcher = true;
        q.isModal = false;
        q.hasDecoration = true;
        q.isResizable = false;
        q.positionX = -50; // a negative X is valid (window straddling the left edge)
        q.positionY = 100;
        q.captionNormal = QStringLiteral("Firefox");

        QCOMPARE(q.valueForField(Field::KeepAbove)->toBool(), true);
        // Present even though false (the bool-field contract).
        QVERIFY(q.valueForField(Field::KeepBelow).has_value());
        QCOMPARE(q.valueForField(Field::KeepBelow)->toBool(), false);
        QCOMPARE(q.valueForField(Field::SkipTaskbar)->toBool(), true);
        QCOMPARE(q.valueForField(Field::SkipPager)->toBool(), false);
        QCOMPARE(q.valueForField(Field::SkipSwitcher)->toBool(), true);
        QCOMPARE(q.valueForField(Field::IsModal)->toBool(), false);
        QCOMPARE(q.valueForField(Field::HasDecoration)->toBool(), true);
        QCOMPARE(q.valueForField(Field::IsResizable)->toBool(), false);
        QCOMPARE(q.valueForField(Field::PositionX)->toInt(), -50);
        QCOMPARE(q.valueForField(Field::PositionY)->toInt(), 100);
        QCOMPARE(q.valueForField(Field::CaptionNormal)->toString(), QStringLiteral("Firefox"));
        QVERIFY(q.hasWindow());
    }

    void testValueForField_modeAlwaysPresent()
    {
        WindowQuery q; // no window attributes, mode unset
        // Mode is context-sourced: always engaged, even when unset, and
        // defaults to an empty string.
        const auto v = q.valueForField(Field::Mode);
        QVERIFY(v.has_value());
        QCOMPARE(v->toString(), QString());
        // Context-sourced, so it never makes the query look per-window.
        QVERIFY(!q.hasWindow());

        q.mode = QStringLiteral("tiling");
        QCOMPARE(q.valueForField(Field::Mode)->toString(), QStringLiteral("tiling"));
        // Setting the mode still does not flip hasWindow().
        QVERIFY(!q.hasWindow());
    }

    void testValueForField_pzStateFields()
    {
        WindowQuery q;
        // Absent by default — inert during windowless context resolution.
        QVERIFY(!q.valueForField(Field::IsFloating).has_value());
        QVERIFY(!q.valueForField(Field::IsSnapped).has_value());
        QVERIFY(!q.valueForField(Field::Zone).has_value());
        QVERIFY(!q.hasWindow());

        q.isFloating = false;
        q.isSnapped = true;
        q.zone = QStringLiteral("{a1b2c3d4-0000-0000-0000-000000000001}");

        // Present even though false (the bool-field contract).
        QVERIFY(q.valueForField(Field::IsFloating).has_value());
        QCOMPARE(q.valueForField(Field::IsFloating)->toBool(), false);
        QCOMPARE(q.valueForField(Field::IsSnapped)->toBool(), true);
        QCOMPARE(q.valueForField(Field::Zone)->toString(), QStringLiteral("{a1b2c3d4-0000-0000-0000-000000000001}"));
        QVERIFY(q.hasWindow());
    }
};

QTEST_GUILESS_MAIN(TestWindowQuery)
#include "test_windowquery.moc"
