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
 * @brief Palette-following colours must TRACK palette changes at runtime.
 *
 * The four zone colours, plus the scrolling drop indicator's fill and border
 * which share the machinery, are theme-fallback keys: an EMPTY stored string
 * means "follow the system palette", resolved in the getters. A palette change
 * must
 * re-announce the following colours (or a running daemon/settings app keeps
 * serving the snapshot its bindings read last), must never dirty any config
 * key (nothing is written), and must stay silent for a colour the user pinned
 * to a concrete value.
 */
class TestSettingsSystemPaletteTracking : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void followingColorsTrackPaletteChange()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        // Fresh config: all four colours store the empty sentinel and follow.
        QCOMPARE(settings.highlightColorRaw(), QString());
        QCOMPARE(settings.labelFontColorRaw(), QString());

        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);
        QSignalSpy inactiveSpy(&settings, &Settings::inactiveColorChanged);
        QSignalSpy borderSpy(&settings, &Settings::borderColorChanged);
        QSignalSpy labelFontSpy(&settings, &Settings::labelFontColorChanged);
        // The scrolling drop indicator's two colours ride the same fan-out.
        QSignalSpy dropSpy(&settings, &Settings::scrollingDropIndicatorColorChanged);
        QSignalSpy dropBorderSpy(&settings, &Settings::scrollingDropIndicatorBorderColorChanged);
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

        // The full resolution contract: borderColor is QPalette::Mid at
        // ZoneDefaults::BorderAlpha; labelFontColor is QPalette::Text
        // verbatim (no alpha override).
        QColor expectedBorder(0x56, 0x78, 0x9A);
        expectedBorder.setAlpha(::PhosphorZones::ZoneDefaults::BorderAlpha);
        QCOMPARE(settings.borderColor(), expectedBorder);
        QCOMPARE(settings.labelFontColor(), QColor(0xDE, 0xAD, 0xBE));

        // The drop indicator takes QPalette::Highlight OPAQUE — no
        // ZoneDefaults alpha — because its fill alpha comes from the opacity
        // slider and its border has no slider at all.
        QCOMPARE(settings.scrollingDropIndicatorColor(), QColor(0x12, 0xAB, 0x34));
        QCOMPARE(settings.scrollingDropIndicatorBorderColor(), QColor(0x12, 0xAB, 0x34));

        // The stored sentinels are untouched: resolution happens in the
        // getters, the palette event writes nothing.
        QCOMPARE(settings.highlightColorRaw(), QString());
        QCOMPARE(settings.borderColorRaw(), QString());

        // Batched announcement: one palette event emits each following
        // colour's NOTIFY exactly once plus a single aggregate
        // settingsChanged.
        QTest::qWait(50); // let any stray duplicate event land before counting
        QCOMPARE(highlightSpy.count(), 1);
        QCOMPARE(inactiveSpy.count(), 1);
        QCOMPARE(borderSpy.count(), 1);
        QCOMPARE(labelFontSpy.count(), 1);
        QCOMPARE(dropSpy.count(), 1);
        QCOMPARE(dropBorderSpy.count(), 1);
        QCOMPARE(aggregateSpy.count(), 1);
    }

    void paletteChangeDoesNotDirtyColorKeys()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        settings.save();

        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);

        // There is no SettingsController unit-test target, so the controller
        // side of the phantom-dirty fix (onSettingsPropertyChanged early-
        // returns while the flag is up) is covered here by pinning the flag
        // contract it depends on: isAnnouncingPaletteChange() must be TRUE
        // inside every zone-color NOTIFY the palette event fans out, and
        // false again once the event is handled.
        bool flagUpDuringNotify = false;
        bool notifySeen = false;
        connect(&settings, &Settings::highlightColorChanged, &settings, [&]() {
            notifySeen = true;
            flagUpDuringNotify = settings.isAnnouncingPaletteChange();
        });
        QVERIFY(!settings.isAnnouncingPaletteChange());

        QPalette pal = qGuiApp->palette();
        pal.setColor(QPalette::Active, QPalette::Highlight, QColor(0x77, 0x11, 0x99));
        pal.setColor(QPalette::Active, QPalette::AlternateBase, QColor(0x10, 0x20, 0x30));
        pal.setColor(QPalette::Active, QPalette::Mid, QColor(0x40, 0x50, 0x60));
        pal.setColor(QPalette::Active, QPalette::Text, QColor(0xE0, 0xE0, 0xE0));
        qGuiApp->setPalette(pal);

        QTRY_VERIFY(highlightSpy.count() >= 1);
        QVERIFY(notifySeen);
        QVERIFY(flagUpDuringNotify);
        QVERIFY(!settings.isAnnouncingPaletteChange());

        // The palette event is a NOTIFY fan-out over UNCHANGED stored
        // sentinels — none of the four zone-color keys may count as modified,
        // or the settings app shows a phantom unsaved-changes footer.
        QVERIFY(!settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::highlightKey()));
        QVERIFY(!settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::inactiveKey()));
        QVERIFY(!settings.isKeyModified(ConfigDefaults::snappingZonesColorsGroup(), ConfigDefaults::borderKey()));
        QVERIFY(!settings.isKeyModified(ConfigDefaults::snappingZonesLabelsGroup(), ConfigDefaults::fontColorKey()));
        QVERIFY(!settings.isKeyModified(ConfigDefaults::scrollingDropIndicatorGroup(), ConfigDefaults::colorKey()));
        QVERIFY(
            !settings.isKeyModified(ConfigDefaults::scrollingDropIndicatorGroup(), ConfigDefaults::borderColorKey()));
    }

    void pinnedColorsIgnorePaletteChange()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        settings.setHighlightColor(QColor(0xAA, 0x00, 0xAA, 0x80));
        QCOMPARE(settings.highlightColorRaw(), QStringLiteral("#80aa00aa"));
        // The drop indicator's pair resolves from the same palette role, so
        // they have to be pinned too for "no follower moved" to hold.
        settings.setScrollingDropIndicatorColor(QColor(0x11, 0x22, 0x33));
        settings.setScrollingDropIndicatorBorderColor(QColor(0x44, 0x55, 0x66));

        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);
        QSignalSpy dropSpy(&settings, &Settings::scrollingDropIndicatorColorChanged);
        QSignalSpy aggregateSpy(&settings, &Settings::settingsChanged);
        // Only the Highlight ROLE moves: the pinned colours must stay
        // silent, and since no FOLLOWING colour's resolved value moved
        // either, the aggregate stays silent too (the fan-out is
        // change-gated, not merely follows-gated).
        QPalette pal = qGuiApp->palette();
        pal.setColor(QPalette::Active, QPalette::Highlight, QColor(0x55, 0x66, 0x77));
        qGuiApp->setPalette(pal);

        // Deliver any pending events, then confirm the pin held.
        QTest::qWait(50);
        QCOMPARE(highlightSpy.count(), 0);
        QCOMPARE(dropSpy.count(), 0);
        QCOMPARE(aggregateSpy.count(), 0);
        QCOMPARE(settings.highlightColor(), QColor(0xAA, 0x00, 0xAA, 0x80));
        QCOMPARE(settings.scrollingDropIndicatorColor(), QColor(0x11, 0x22, 0x33));
    }

    void unrelatedPaletteRoleChangeStaysSilent()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;

        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);
        QSignalSpy inactiveSpy(&settings, &Settings::inactiveColorChanged);
        QSignalSpy borderSpy(&settings, &Settings::borderColorChanged);
        QSignalSpy labelFontSpy(&settings, &Settings::labelFontColorChanged);
        QSignalSpy aggregateSpy(&settings, &Settings::settingsChanged);

        // An ApplicationPaletteChange that moves NO role the four colours
        // resolve from (a style change, a plasma-integration re-push) must
        // emit nothing: the aggregate re-runs the daemon config refresh and
        // the KWin effect reload, so an unconditional fan-out would pay that
        // full cost per palette event for no observable change.
        QPalette pal = qGuiApp->palette();
        pal.setColor(QPalette::Active, QPalette::Button, QColor(0x31, 0x41, 0x59));
        qGuiApp->setPalette(pal);

        QTest::qWait(50);
        QCOMPARE(highlightSpy.count(), 0);
        QCOMPARE(inactiveSpy.count(), 0);
        QCOMPARE(borderSpy.count(), 0);
        QCOMPARE(labelFontSpy.count(), 0);
        QCOMPARE(aggregateSpy.count(), 0);
    }

    void allFourRawSettersWriteTheirOwnKey()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;

        // Distinct hex per key, then per-key read-back with the other three
        // asserted untouched at each step: a copy-paste key swap inside one
        // of the P_STORE macro invocations would otherwise pass the whole
        // suite (the palette tests only ever assert these raws are EMPTY).
        settings.setHighlightColorRaw(QStringLiteral("#ff111111"));
        QCOMPARE(settings.highlightColorRaw(), QStringLiteral("#ff111111"));
        QCOMPARE(settings.inactiveColorRaw(), QString());
        QCOMPARE(settings.borderColorRaw(), QString());
        QCOMPARE(settings.labelFontColorRaw(), QString());

        settings.setInactiveColorRaw(QStringLiteral("#ff222222"));
        QCOMPARE(settings.inactiveColorRaw(), QStringLiteral("#ff222222"));
        QCOMPARE(settings.highlightColorRaw(), QStringLiteral("#ff111111"));

        settings.setBorderColorRaw(QStringLiteral("#ff333333"));
        QCOMPARE(settings.borderColorRaw(), QStringLiteral("#ff333333"));
        QCOMPARE(settings.inactiveColorRaw(), QStringLiteral("#ff222222"));

        settings.setLabelFontColorRaw(QStringLiteral("#ff444444"));
        QCOMPARE(settings.labelFontColorRaw(), QStringLiteral("#ff444444"));
        QCOMPARE(settings.borderColorRaw(), QStringLiteral("#ff333333"));
        QCOMPARE(settings.highlightColorRaw(), QStringLiteral("#ff111111"));
    }

    void resetRawToSentinelResumesFollowing()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        settings.setHighlightColorRaw(QStringLiteral("#FF123456"));
        QCOMPARE(settings.highlightColor(), QColor(QStringLiteral("#FF123456")));

        // Clearing back to the sentinel is the settings UI's per-row Reset:
        // the resolved colour immediately follows the live palette again.
        QSignalSpy highlightSpy(&settings, &Settings::highlightColorChanged);
        settings.setHighlightColorRaw(QString());
        QCOMPARE(highlightSpy.count(), 1);
        QColor expected = qGuiApp->palette().color(QPalette::Active, QPalette::Highlight);
        expected.setAlpha(::PhosphorZones::ZoneDefaults::HighlightAlpha);
        QCOMPARE(settings.highlightColor(), expected);
    }
};

QTEST_MAIN(TestSettingsSystemPaletteTracking)
#include "test_settings_system_palette_tracking.moc"
