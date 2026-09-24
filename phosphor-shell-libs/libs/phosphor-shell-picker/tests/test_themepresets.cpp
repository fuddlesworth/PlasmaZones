// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// ThemePresets over fixtures/palettes: Dark and Light lead, one tile per
// readable palette file follows, current.json and junk are skipped.

#include <PhosphorShellPicker/ThemePresets.h>

#include <PhosphorTheme/IThemeService.h>

#include <QColor>
#include <QDir>
#include <QSignalSpy>
#include <QTest>

using PhosphorShellPicker::ThemePresets;
using PhosphorTheme::TokenNames;

namespace {

QString fixtures()
{
    return QDir(QStringLiteral(PHOSPHOR_PICKER_FIXTURES)).filePath(QStringLiteral("palettes"));
}

QStringList names(const ThemePresets& presets)
{
    QStringList list;
    for (const QVariant& entry : presets.presets()) {
        list.append(entry.toMap().value(QStringLiteral("name")).toString());
    }
    return list;
}

} // namespace

class TestThemePresets : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void builtInsLeadThenFilesByName()
    {
        ThemePresets presets;
        presets.setDirectory(fixtures());
        QSignalSpy changed(&presets, &ThemePresets::presetsChanged);
        presets.rescan();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(names(presets),
                 (QStringList{QStringLiteral("Dark"), QStringLiteral("Light"), QStringLiteral("Ember"),
                              QStringLiteral("Flat")}));
        presets.rescan();
        QCOMPARE(changed.count(), 1);
    }

    void tokensAndSwatches()
    {
        ThemePresets presets;
        presets.setDirectory(fixtures());
        presets.rescan();
        // Wrapped shape.
        QCOMPARE(
            presets.tokensFor(QStringLiteral("Ember")).value(QString::fromLatin1(TokenNames::Primary)).value<QColor>(),
            QColor(QStringLiteral("#ff5522")));
        // Flat shape.
        QCOMPARE(
            presets.tokensFor(QStringLiteral("Flat")).value(QString::fromLatin1(TokenNames::Primary)).value<QColor>(),
            QColor(QStringLiteral("#22ff55")));
        QVERIFY(presets.tokensFor(QStringLiteral("Nope")).isEmpty());

        const QVariantList swatches = ThemePresets::swatchesFor(ThemePresets::lightPalette());
        QCOMPARE(swatches.size(), 5);
        QCOMPARE(swatches[2].value<QColor>(), QColor(QStringLiteral("#3B82F6")));
    }

    void lightPaletteCarriesNoBrandStops()
    {
        const QVariantMap light = ThemePresets::lightPalette();
        QVERIFY(!light.contains(QString::fromLatin1(TokenNames::BrandStop0)));
        QVERIFY(light.contains(QString::fromLatin1(TokenNames::Background)));
        // The dark palette is the store's own.
        QVERIFY(ThemePresets::darkPalette().contains(QString::fromLatin1(TokenNames::BrandStop0)));
    }

    void missingDirectoryGivesTheBuiltInsOnly()
    {
        ThemePresets presets;
        presets.setDirectory(QStringLiteral("/nonexistent/phosphor/palettes"));
        presets.rescan();
        QCOMPARE(presets.count(), 2);
    }
};

QTEST_GUILESS_MAIN(TestThemePresets)
#include "test_themepresets.moc"
