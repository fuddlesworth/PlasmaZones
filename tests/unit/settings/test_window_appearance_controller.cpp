// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_appearance_controller.cpp
 * @brief Tests for the config-backed @c WindowAppearanceController.
 *
 * The controller is a thin Q_PROPERTY surface over ISettings: border / title-bar
 * and the shared inner/outer gap values forward to the matching ISettings
 * getter/setter. On top of that it exposes the scope-aware gapValue()/writeGap()
 * invokables the Gaps card's monitor scope chip drives. These tests pin:
 *   1. Border / title-bar / gap properties round-trip through ISettings.
 *   2. gapValue("", key) / writeGap("", key, v) read and write the GLOBAL config.
 *   3. gapValue(monitor, key) / writeGap(monitor, key, v) read and write the
 *      per-monitor override, falling back to the global value when absent.
 */

#include <QSignalSpy>
#include <QTest>

#include "settings/windowappearancecontroller.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;

class TestWindowAppearanceController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void borderAndTitleBarPropertiesForwardToSettings()
    {
        StubSettings settings;
        WindowAppearanceController controller(settings);

        controller.setShowWindowBorder(true);
        QCOMPARE(settings.showWindowBorder(), true);
        QCOMPARE(controller.showWindowBorder(), true);

        controller.setWindowBorderWidth(5);
        QCOMPARE(settings.windowBorderWidth(), 5);
        QCOMPARE(controller.windowBorderWidth(), 5);

        controller.setWindowBorderScope(QStringLiteral("normal"));
        QCOMPARE(settings.windowBorderScope(), QStringLiteral("normal"));
        QCOMPARE(controller.windowBorderScope(), QStringLiteral("normal"));

        controller.setHideWindowTitleBars(true);
        QCOMPARE(settings.hideWindowTitleBars(), true);
        QCOMPARE(controller.hideWindowTitleBars(), true);

        controller.setFocusFadeDuration(320);
        QCOMPARE(settings.focusFadeDuration(), 320);
        QCOMPARE(controller.focusFadeDuration(), 320);
    }

    void gapPropertiesForwardToSettings()
    {
        StubSettings settings;
        WindowAppearanceController controller(settings);

        controller.setInnerGap(14);
        QCOMPARE(settings.innerGap(), 14);
        QCOMPARE(controller.innerGap(), 14);

        controller.setUsePerSideOuterGap(true);
        QCOMPARE(settings.usePerSideOuterGap(), true);
        controller.setOuterGapTop(6);
        QCOMPARE(settings.outerGapTop(), 6);
        QCOMPARE(controller.outerGapTop(), 6);
    }

    // "" scope reads/writes the global gap config through the ISettings getters.
    void gapValueAndWriteGap_globalScope()
    {
        StubSettings settings;
        WindowAppearanceController controller(settings);

        settings.setInnerGap(9);
        QCOMPARE(controller.gapValue(QString(), QStringLiteral("InnerGap")).toInt(), 9);

        controller.writeGap(QString(), QStringLiteral("InnerGap"), 17);
        QCOMPARE(settings.innerGap(), 17);
        QCOMPARE(controller.gapValue(QString(), QStringLiteral("InnerGap")).toInt(), 17);

        // UsePerSideOuterGap round-trips as a bool, not coerced to int.
        controller.writeGap(QString(), QStringLiteral("UsePerSideOuterGap"), true);
        QCOMPARE(settings.usePerSideOuterGap(), true);
        const QVariant perSide = controller.gapValue(QString(), QStringLiteral("UsePerSideOuterGap"));
        QCOMPARE(perSide.typeId(), static_cast<int>(QMetaType::Bool));
        QCOMPARE(perSide.toBool(), true);
    }

    // A non-empty scope reads/writes the per-monitor override; a monitor with no
    // override for the key falls back to the global value.
    void gapValueAndWriteGap_perMonitorScope()
    {
        StubSettings settings;
        WindowAppearanceController controller(settings);

        settings.setInnerGap(8); // global baseline
        const QString monitor = QStringLiteral("DP-1");

        // No override yet: the scoped read falls back to the global value.
        QCOMPARE(controller.gapValue(monitor, QStringLiteral("InnerGap")).toInt(), 8);

        // Writing a scoped value stores the per-monitor override, not the global.
        controller.writeGap(monitor, QStringLiteral("InnerGap"), 22);
        QCOMPARE(controller.gapValue(monitor, QStringLiteral("InnerGap")).toInt(), 22);
        QCOMPARE(settings.innerGap(), 8); // global untouched
        QCOMPARE(settings.getPerScreenAutotileSettings(monitor).value(QStringLiteral("InnerGap")).toInt(), 22);

        // Another monitor with no override still falls back to the global value.
        QCOMPARE(controller.gapValue(QStringLiteral("DP-2"), QStringLiteral("InnerGap")).toInt(), 8);
    }
};

QTEST_MAIN(TestWindowAppearanceController)
#include "test_window_appearance_controller.moc"
