// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QPalette>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorZones/ZoneDefaults.h>

#include "helpers/IsolatedConfigGuard.h"
#include "config/configdefaults.h"
#include "config/settings.h"

using namespace PlasmaZones;

/**
 * @brief System-colors mode must TRACK palette changes at runtime.
 *
 * load() derives zone colors from the application palette once; before the
 * eventFilter fix, a running daemon/settings app kept that snapshot forever,
 * so overlays and previews showed the PREVIOUS color scheme until the
 * process restarted (wallpaper-driven schemes change often). This pins:
 * palette change -> colors re-derived (useSystemColors on), and NOT
 * re-derived when the user runs custom colors.
 */
class TestSettingsSystemPaletteTracking : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void systemColorsFollowPaletteChange()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        settings.setUseSystemColors(true);

        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);
        QSignalSpy inactiveSpy(&settings, &Settings::inactiveColorChanged);
        QSignalSpy borderSpy(&settings, &Settings::borderColorChanged);
        QSignalSpy labelFontSpy(&settings, &Settings::labelFontColorChanged);
        QSignalSpy aggregateSpy(&settings, &Settings::settingsChanged);

        QPalette pal = qGuiApp->palette();
        pal.setColor(QPalette::Active, QPalette::Highlight, QColor(0x12, 0xAB, 0x34));
        pal.setColor(QPalette::Active, QPalette::AlternateBase, QColor(0x22, 0x33, 0x44));
        pal.setColor(QPalette::Active, QPalette::Mid, QColor(0x56, 0x78, 0x9A));
        pal.setColor(QPalette::Active, QPalette::Text, QColor(0xDE, 0xAD, 0xBE));
        qGuiApp->setPalette(pal);

        QTRY_VERIFY(highlightSpy.count() >= 1);
        QTRY_VERIFY(inactiveSpy.count() >= 1);
        QTRY_VERIFY(borderSpy.count() >= 1);
        QTRY_VERIFY(labelFontSpy.count() >= 1);

        QColor expectedHighlight(0x12, 0xAB, 0x34);
        expectedHighlight.setAlpha(::PhosphorZones::ZoneDefaults::HighlightAlpha);
        QCOMPARE(settings.highlightColor(), expectedHighlight);

        QColor expectedInactive(0x22, 0x33, 0x44);
        expectedInactive.setAlpha(::PhosphorZones::ZoneDefaults::InactiveAlpha);
        QCOMPARE(settings.inactiveColor(), expectedInactive);

        // The full derive contract: borderColor is QPalette::Mid at
        // ZoneDefaults::BorderAlpha; labelFontColor is QPalette::Text
        // verbatim (no alpha override).
        QColor expectedBorder(0x56, 0x78, 0x9A);
        expectedBorder.setAlpha(::PhosphorZones::ZoneDefaults::BorderAlpha);
        QCOMPARE(settings.borderColor(), expectedBorder);
        QCOMPARE(settings.labelFontColor(), QColor(0xDE, 0xAD, 0xBE));

        // Batched announcement (mirrors load()): one palette event emits each
        // changed NOTIFY exactly once plus a single aggregate settingsChanged
        // — never one settingsChanged per color setter.
        QTest::qWait(50); // let any stray duplicate event land before counting
        QCOMPARE(highlightSpy.count(), 1);
        QCOMPARE(inactiveSpy.count(), 1);
        QCOMPARE(borderSpy.count(), 1);
        QCOMPARE(labelFontSpy.count(), 1);
        QCOMPARE(aggregateSpy.count(), 1);
    }

    void paletteChangeDoesNotDirtyColorKeys()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        settings.setUseSystemColors(true);
        // Commit the toggle: save() re-captures the baseline, putting the
        // settings app in its steady state (system colors on, nothing dirty).
        settings.save();

        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);

        // There is no SettingsController unit-test target, so the controller
        // side of the phantom-dirty fix (onSettingsPropertyChanged early-
        // returns while the flag is up) is covered here by pinning the flag
        // contract it depends on: isApplyingSystemPalette() must be TRUE
        // inside every zone-color NOTIFY the palette re-derive emits, and
        // false again once the event is handled.
        bool flagUpDuringNotify = false;
        bool notifySeen = false;
        connect(&settings, &Settings::highlightColorChanged, &settings, [&]() {
            notifySeen = true;
            flagUpDuringNotify = settings.isApplyingSystemPalette();
        });
        QVERIFY(!settings.isApplyingSystemPalette());

        QPalette pal = qGuiApp->palette();
        pal.setColor(QPalette::Active, QPalette::Highlight, QColor(0x77, 0x11, 0x99));
        pal.setColor(QPalette::Active, QPalette::AlternateBase, QColor(0x10, 0x20, 0x30));
        pal.setColor(QPalette::Active, QPalette::Mid, QColor(0x40, 0x50, 0x60));
        pal.setColor(QPalette::Active, QPalette::Text, QColor(0xE0, 0xE0, 0xE0));
        qGuiApp->setPalette(pal);

        QTRY_VERIFY(highlightSpy.count() >= 1);
        QVERIFY(notifySeen);
        QVERIFY(flagUpDuringNotify);
        QVERIFY(!settings.isApplyingSystemPalette());

        // The re-derive is a palette-driven refresh of DERIVED values, not a
        // user edit — none of the four zone-color keys may count as modified,
        // or the settings app shows a phantom unsaved-changes footer and
        // Discard reverts to the stale pre-switch colors.
        QVERIFY(!settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::highlightKey()));
        QVERIFY(!settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::inactiveKey()));
        QVERIFY(!settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::borderKey()));
        QVERIFY(!settings.isKeyModified(ConfigDefaults::snappingZonesLabelsGroup(), ConfigDefaults::fontColorKey()));
    }

    void pendingToggleKeepsColorsDiscardable()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        // Commit a baseline with system colors OFF, then flip the toggle ON
        // without saving — the toggle is now a PENDING user edit.
        settings.setUseSystemColors(false);
        settings.save();
        settings.setUseSystemColors(true);
        QVERIFY(settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::useSystemKey()));

        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);

        QPalette pal = qGuiApp->palette();
        pal.setColor(QPalette::Active, QPalette::Highlight, QColor(0x31, 0x41, 0x59));
        qGuiApp->setPalette(pal);

        QTRY_VERIFY(highlightSpy.count() >= 1);

        // While the useSystemColors toggle itself is unsaved, eventFilter()
        // must NOT rebaseline: the toggle and the colors it derived are ONE
        // user edit and must stay discardable together. Both the toggle key
        // and the palette-derived color keys therefore stay modified.
        QVERIFY(settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::useSystemKey()));
        QVERIFY(settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::highlightKey()));
    }

    void customColorsIgnorePaletteChange()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        settings.setUseSystemColors(false);
        settings.setHighlightColor(QColor(0xAA, 0x00, 0xAA, 0x80));

        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);

        QPalette pal = qGuiApp->palette();
        pal.setColor(QPalette::Active, QPalette::Highlight, QColor(0x55, 0x66, 0x77));
        qGuiApp->setPalette(pal);

        // Deliver any pending events, then confirm no re-derivation.
        QTest::qWait(50);
        QCOMPARE(highlightSpy.count(), 0);
        QCOMPARE(settings.highlightColor(), QColor(0xAA, 0x00, 0xAA, 0x80));
    }
};

QTEST_MAIN(TestSettingsSystemPaletteTracking)
#include "test_settings_system_palette_tracking.moc"
