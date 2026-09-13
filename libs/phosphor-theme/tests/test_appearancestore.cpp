// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/AppearanceStore.h>
#include <PhosphorTheme/ShellPalette.h>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using PhosphorTheme::AppearanceStore;
class TestAppearanceStore : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void sharedPalettePreservesApprovedColors()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        const QList<QColor> backgrounds{QColor(QStringLiteral("#101d32")), QColor(QStringLiteral("#eef2fa")),
                                        QColor(QStringLiteral("#272620"))};
        const QList<QString> presets{QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")};
        for (int i = 0; i < presets.size(); ++i) {
            QVERIFY(store.applyPreset(presets[i]));
            const auto palette = PhosphorTheme::ShellPalette::fromSettings(store.values());
            QCOMPARE(palette.surface, backgrounds[i]);
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
