// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QPalette>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorZones/ZoneDefaults.h>

#include "../helpers/IsolatedConfigGuard.h"
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

        QPalette pal = qGuiApp->palette();
        pal.setColor(QPalette::Active, QPalette::Highlight, QColor(0x12, 0xAB, 0x34));
        pal.setColor(QPalette::Active, QPalette::AlternateBase, QColor(0x22, 0x33, 0x44));
        qGuiApp->setPalette(pal);

        QTRY_VERIFY(highlightSpy.count() >= 1);
        QTRY_VERIFY(inactiveSpy.count() >= 1);

        QColor expectedHighlight(0x12, 0xAB, 0x34);
        expectedHighlight.setAlpha(::PhosphorZones::ZoneDefaults::HighlightAlpha);
        QCOMPARE(settings.highlightColor(), expectedHighlight);

        QColor expectedInactive(0x22, 0x33, 0x44);
        expectedInactive.setAlpha(::PhosphorZones::ZoneDefaults::InactiveAlpha);
        QCOMPARE(settings.inactiveColor(), expectedInactive);
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
