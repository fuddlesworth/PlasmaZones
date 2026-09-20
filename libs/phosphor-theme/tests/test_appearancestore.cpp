// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/AppearanceStore.h>
#include <PhosphorTheme/ShellPalette.h>
#include <QFile>
#include <QImage>
#include <cmath>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using PhosphorTheme::AppearanceStore;
class TestAppearanceStore : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void decorationPreviewIsOwnedAndNeverReplacesSavedSettings()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.applyPreset(QStringLiteral("phosphor")));
        const auto saved = store.values();
        QVERIFY(store.beginPreview());
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        bool active = false;
        QCOMPARE(AppearanceStore::effectiveValues(path, &active), store.values());
        QVERIFY(active);
        QCOMPARE(AppearanceStore(path).values(), saved);
        AppearanceStore competing(path);
        QVERIFY(!competing.beginPreview());
        store.revertPreview();
        QCOMPARE(AppearanceStore::effectiveValues(path), saved);
        QVERIFY(store.applyPreset(QStringLiteral("paper")));
        QVERIFY(store.applyPreview());
        const auto applied = store.values();
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        store.endPreview();
        QCOMPARE(AppearanceStore::effectiveValues(path, &active), applied);
        QVERIFY(!active);
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".preview")));
        // An orphan draft, left by a crashed producer, must never win on startup.
        QVERIFY(QFile::copy(path, path + QStringLiteral(".preview")));
        QVERIFY(store.applyPreset(QStringLiteral("phosphor")));
        QCOMPARE(AppearanceStore::effectiveValues(path, &active), store.values());
        QVERIFY(!active);
    }
    void previewIsOneAtomicLook()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.applyPreset(QStringLiteral("phosphor")));
        const auto original = store.values();
        const auto image = dir.filePath(QStringLiteral("wallpaper.png"));
        QImage sample(16, 16, QImage::Format_RGB32);
        sample.fill(Qt::green);
        QVERIFY(sample.save(image));
        store.beginPreview();
        QVERIFY(store.applyPreset(QStringLiteral("paper")));
        QVERIFY(store.setWallpaper(image, QStringLiteral("DP-1"), QStringLiteral("fit")));
        QVERIFY(store.dirty());
        AppearanceStore during(path);
        QCOMPARE(during.values(), original);
        store.beginPreview(); // reload/open does not replace the original snapshot
        store.revertPreview();
        QCOMPARE(store.values(), original);
        QVERIFY(!store.dirty());
        QVERIFY(store.setWallpaper(image, QString(), QStringLiteral("fill")));
        QVERIFY(store.setValue(QStringLiteral("textScale"), 110));
        QVERIFY(store.applyPreview());
        const auto applied = store.values();
        QVERIFY(!store.dirty());
        QVERIFY(store.setValue(QStringLiteral("barInset"), 24));
        store.endPreview();
        QCOMPARE(store.values(), applied);
        QVERIFY(!store.editing());
        AppearanceStore restored(path);
        QCOMPARE(restored.values(), applied);
    }
    void failedApplyRetainsEditableDraft()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.path());
        const auto original = store.values();
        store.beginPreview();
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        QVERIFY(!store.applyPreview());
        QVERIFY(store.dirty());
        QVERIFY(!store.error().isEmpty());
        store.endPreview();
        QCOMPARE(store.values(), original);
    }
    void wallpaperPaletteChangesEveryRoleAndKeepsContrast()
    {
        const auto luminance = [](const QColor& color) {
            const auto c = [](double v) {
                return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
            };
            return .2126 * c(color.redF()) + .7152 * c(color.greenF()) + .0722 * c(color.blueF());
        };
        for (const auto& material : {QStringLiteral("glass"), QStringLiteral("solid"), QStringLiteral("light")}) {
            auto values = AppearanceStore::defaults();
            values[QStringLiteral("palette")] = QStringLiteral("wallpaper");
            values[QStringLiteral("material")] = material;
            const auto purple = PhosphorTheme::ShellPalette::fromSettings(values);
            for (const auto& seed : {QStringLiteral("#9bb897"), QStringLiteral("#d3906c"), QStringLiteral("#000000"),
                                     QStringLiteral("#ffffff"), QStringLiteral("#00ff00")}) {
                values[QStringLiteral("wallpaperColors")] = QVariantList{seed, seed, seed, seed};
                const auto palette = PhosphorTheme::ShellPalette::fromSettings(values);
                QVERIFY(palette.surface != purple.surface);
                QVERIFY(palette.card != purple.card);
                QVERIFY(palette.recess != purple.recess);
                for (const auto& foreground : QList<QColor>{palette.text, palette.muted} + palette.stops) {
                    for (const auto& background : {palette.surface, palette.card, palette.recess}) {
                        const auto a = luminance(foreground), b = luminance(background);
                        QVERIFY((std::max(a, b) + .05) / (std::min(a, b) + .05) >= 4.49);
                    }
                }
            }
        }
    }
    void newSettingsRejectMalformedImports()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QVERIFY(!store.setValue(QStringLiteral("accentIndex"), 4));
        QVERIFY(!store.setValue(QStringLiteral("textScale"), 400));
        QVERIFY(!store.setValue(QStringLiteral("wallpaperColors"), QVariantList{QStringLiteral("invalid")}));
        QVERIFY(!store.setValue(QStringLiteral("wallpapers"),
                                QVariantMap{{QStringLiteral("DP-1"),
                                             QVariantMap{{QStringLiteral("path"), QStringLiteral("relative.png")},
                                                         {QStringLiteral("fit"), QStringLiteral("fill")}}}}));
        QVariantMap oversized;
        for (int i = 0; i < 32; ++i)
            oversized[QString::number(i)] = QVariantMap{{QStringLiteral("path"), QString(u'/' + QString(4000, u'a'))},
                                                        {QStringLiteral("fit"), QStringLiteral("fill")}};
        QVERIFY(!store.setValue(QStringLiteral("wallpapers"), oversized));
    }

    void notificationPreferencesSurvivePresetAndReload()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.setValue(QStringLiteral("notificationGrouping"), QStringLiteral("time")));
        QVERIFY(store.setValue(QStringLiteral("notificationPreviews"), false));
        QVERIFY(!store.setValue(QStringLiteral("notificationGrouping"), QStringLiteral("invalid")));
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        AppearanceStore loaded(path);
        QCOMPARE(loaded.values().value(QStringLiteral("notificationGrouping")).toString(), QStringLiteral("time"));
        QCOMPARE(loaded.values().value(QStringLiteral("notificationPreviews")).toBool(), false);
    }
    void sharedPalettePreservesApprovedColors()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        const QList<QColor> backgrounds{QColor(QStringLiteral("#101d32")), QColor(QStringLiteral("#eef0f7")),
                                        QColor(QStringLiteral("#272620"))};
        const QList<QString> presets{QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")};
        for (int i = 0; i < presets.size(); ++i) {
            QVERIFY(store.applyPreset(presets[i]));
            const auto palette = PhosphorTheme::ShellPalette::fromSettings(store.values());
            QCOMPARE(palette.surface.name(), backgrounds[i].name());
            QCOMPARE(store.palette(), palette.toVariant());
            QCOMPARE(palette.windowColor(0), palette.stops[0]);
            QCOMPARE(palette.windowColor(1), palette.stops[2]);
            QCOMPARE(palette.windowColor(2), palette.stops[3]);
            QCOMPARE(palette.windowColor(3), palette.stops[1]);
            QCOMPARE(palette.windowColor(7), palette.windowColor(3));
            QVERIFY(palette.text != palette.surface);
        }
    }
    void presetNamesReflectStyleEdits()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        for (const auto& preset : {QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")}) {
            QVERIFY(store.applyPreset(preset));
            QCOMPARE(store.currentPreset(), preset);
            QVERIFY(store.setValue(QStringLiteral("presentation"), QStringLiteral("stage")));
            QCOMPARE(store.currentPreset(), preset);
            QVERIFY(store.setValue(QStringLiteral("radius"), 29));
            QCOMPARE(store.currentPreset(), QStringLiteral("custom"));
        }
    }

    void persistsAndRoundTrips()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        QVERIFY(store.setValue(QStringLiteral("visualizer"), QStringLiteral("halo")));
        const auto exported = QUrl::fromLocalFile(dir.filePath(QStringLiteral("export.json")));
        QVERIFY(store.exportPreset(exported));
        AppearanceStore reloaded(dir.filePath(QStringLiteral("appearance.json")));
        QCOMPARE(reloaded.values(), store.values());
        QVERIFY(store.applyPreset(QStringLiteral("paper")));
        QVERIFY(store.importPreset(exported));
        QCOMPARE(store.values(), reloaded.values());
    }
    void lockPrivacyChoicesPersistAcrossPresets()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QCOMPARE(store.values().value(QStringLiteral("lockMedia")).toBool(), false);
        QCOMPARE(store.values().value(QStringLiteral("lockNotifications")).toBool(), true);
        QVERIFY(store.setValue(QStringLiteral("lockLayout"), QStringLiteral("centered")));
        QVERIFY(store.setValue(QStringLiteral("lockMedia"), true));
        QVERIFY(store.setValue(QStringLiteral("lockNotifications"), false));
        QVERIFY(store.applyPreset(QStringLiteral("paper")));
        QCOMPARE(store.values().value(QStringLiteral("lockLayout")).toString(), QStringLiteral("centered"));
        QCOMPARE(store.values().value(QStringLiteral("lockMedia")).toBool(), true);
        QCOMPARE(store.values().value(QStringLiteral("lockNotifications")).toBool(), false);
        AppearanceStore restored(path);
        QCOMPARE(restored.values(), store.values());
        QVERIFY(!store.setValue(QStringLiteral("lockLayout"), QStringLiteral("unknown")));
        QVERIFY(!store.setValue(QStringLiteral("lockMedia"), QStringLiteral("true")));
    }
    void rejectsInvalidWithoutChangingMemoryOrDisk()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.applyPreset(QStringLiteral("phosphor")));
        const auto before = store.values();
        QVERIFY(!store.setValue(QStringLiteral("radius"), 10000));
        QVERIFY(!store.setValue(QStringLiteral("radius"), 9.5));
        QVERIFY(!store.setValue(QStringLiteral("motion"), QStringLiteral("false")));
        QVERIFY(!store.setValue(QStringLiteral("edge"), QStringLiteral("sideways")));
        QVERIFY(!store.setValue(QStringLiteral("unknown"), true));
        QCOMPARE(store.values(), before);
        AppearanceStore reloaded(path);
        QCOMPARE(reloaded.values(), before);
        QFile invalid(dir.filePath(QStringLiteral("invalid.json")));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("{\"version\":2,\"settings\":{}}");
        invalid.close();
        QVERIFY(!store.importPreset(QUrl::fromLocalFile(invalid.fileName())));
        QCOMPARE(store.values(), before);
    }
    void geometryNotificationOnlyForLayoutChanges()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QSignalSpy layout(&store, &AppearanceStore::geometryChanged);
        QSignalSpy changed(&store, &AppearanceStore::changed);
        QVERIFY(store.setValue(QStringLiteral("radius"), 12));
        QCOMPARE(layout.count(), 0);
        QVERIFY(store.setValue(QStringLiteral("radius"), 12));
        QCOMPARE(changed.count(), 1);
        QVERIFY(store.setValue(QStringLiteral("density"), QStringLiteral("compact")));
        QCOMPARE(layout.count(), 1);
    }
    void layoutAndFontsSurvivePresetsAndRoundTrip()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QVERIFY(store.moveWidget(QStringLiteral("clock"), QStringLiteral("left"), 0));
        QVERIFY(store.moveWidget(QStringLiteral("power"), QString()));
        QVERIFY(store.setValue(QStringLiteral("uiFont"), QStringLiteral("Noto Sans")));
        QVERIFY(store.setValue(QStringLiteral("monoFont"), QStringLiteral("monospace")));
        QVERIFY(store.setValue(QStringLiteral("presentation"), QStringLiteral("stage")));
        const auto layout = store.values().value(QStringLiteral("barLayout"));
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        QCOMPARE(store.values().value(QStringLiteral("barLayout")), layout);
        QCOMPARE(store.values().value(QStringLiteral("uiFont")).toString(), QStringLiteral("Noto Sans"));
        QCOMPARE(store.values().value(QStringLiteral("presentation")).toString(), QStringLiteral("stage"));
        const auto url = QUrl::fromLocalFile(dir.filePath(QStringLiteral("preset.json")));
        QVERIFY(store.exportPreset(url));
        AppearanceStore restored(dir.filePath(QStringLiteral("second.json")));
        QVERIFY(restored.importPreset(url));
        QCOMPARE(restored.values(), store.values());
        QVERIFY(store.resetBarLayout());
        QCOMPARE(store.values().value(QStringLiteral("barLayout")),
                 AppearanceStore::defaults().value(QStringLiteral("barLayout")));
    }
    void layoutBoundaryRejectsDuplicatesAndMalformedIds()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        const auto before = store.values();
        auto layout = before.value(QStringLiteral("barLayout")).toMap();
        layout[QStringLiteral("center")] = QVariantList{QVariantList{QStringLiteral("clock"), QStringLiteral("clock")}};
        QVERIFY(!store.setValue(QStringLiteral("barLayout"), layout));
        QVERIFY(!store.moveWidget(QStringLiteral("bad id"), QStringLiteral("left")));
        QVERIFY(!store.moveWidget(QStringLiteral("clock"), QStringLiteral("above")));
        QVERIFY(!store.setValue(QStringLiteral("uiFont"), QStringLiteral("bad\nfont")));
        QVERIFY(!store.setValue(QStringLiteral("monoFont"), 42));
        QCOMPARE(store.values(), before);
    }
    void reorderCountsWidgetsAcrossGroups()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        auto layout = AppearanceStore::defaults().value(QStringLiteral("barLayout")).toMap();
        layout[QStringLiteral("left")] =
            QVariantList{QVariantList{QStringLiteral("focusedapp")}, QVariantList{QStringLiteral("media")}};
        QVERIFY(store.setValue(QStringLiteral("barLayout"), layout));
        QVERIFY(store.moveWidget(QStringLiteral("placementmap"), QStringLiteral("left"), 1));
        QVariantList flattened;
        for (const auto& group :
             store.values().value(QStringLiteral("barLayout")).toMap().value(QStringLiteral("left")).toList())
            flattened += group.toList();
        QCOMPARE(flattened,
                 (QVariantList{QStringLiteral("focusedapp"), QStringLiteral("placementmap"), QStringLiteral("media")}));
    }
    void failedWriteKeepsCurrentAppearance()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.path()); // a directory cannot be replaced by a preset
        const auto before = store.values();
        QVERIFY(!store.applyPreset(QStringLiteral("ember")));
        QCOMPARE(store.values(), before);
        QVERIFY(!store.error().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAppearanceStore)
#include "test_appearancestore.moc"
