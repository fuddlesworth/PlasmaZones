// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShellPicker/AppearanceLibrary.h>
#include <PhosphorTheme/AppearanceStore.h>
#include <QFile>
#include <QQmlEngine>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
using PhosphorShellPicker::AppearanceLibrary;
using PhosphorTheme::AppearanceStore;
class TestAppearanceLibrary : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void trayPreferencesTravelOnlyWithTheBarPreset()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        AppearanceLibrary library(&store, dir.filePath(QStringLiteral("library")));
        const QStringList keys{QStringLiteral("trayIcons"), QStringLiteral("trayLimit"),
                               QStringLiteral("trayAttention"), QStringLiteral("trayOrder"),
                               QStringLiteral("trayVisibility")};
        const QVariantMap preferences{
            {QStringLiteral("trayIcons"), QStringLiteral("color")},
            {QStringLiteral("trayLimit"), 0},
            {QStringLiteral("trayAttention"), false},
            {QStringLiteral("trayOrder"), QVariantList{QStringLiteral("id:Nextcloud"), QStringLiteral("id:Steam")}},
            {QStringLiteral("trayVisibility"),
             QVariantMap{{QStringLiteral("id:Nextcloud"), QStringLiteral("pinned")},
                         {QStringLiteral("id:Steam"), QStringLiteral("hidden")}}}};
        for (auto it = preferences.cbegin(); it != preferences.cend(); ++it)
            QVERIFY(store.setValue(it.key(), it.value()));
        for (const auto& preset : {QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")}) {
            QVERIFY(library.previewPreset(preset, false, true));
            for (const auto& key : keys)
                QCOMPARE(store.values().value(key), preferences.value(key));
        }
        for (const bool withBar : {false, true}) {
            const auto name = withBar ? QStringLiteral("With tray") : QStringLiteral("Style only");
            QVERIFY(library.savePreset(name, false, withBar));
            const auto id = library.presets().last().toMap().value(QStringLiteral("id")).toString();
            const auto url = QUrl::fromLocalFile(dir.filePath(name + QStringLiteral(".json")));
            QVERIFY(library.exportPreset(id, url));
            QFile file(url.toLocalFile());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto exported =
                QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("settings")).toObject();
            for (const auto& key : keys) {
                QCOMPARE(exported.contains(key), withBar);
                if (withBar)
                    QCOMPARE(exported.value(key).toVariant(), preferences.value(key));
                QVERIFY(store.setValue(key, AppearanceStore::defaults().value(key)));
            }
            const auto before = store.values();
            QVERIFY(library.inspectImport(url));
            QCOMPARE(store.values(), before);
            QVERIFY(store.beginPreview());
            QVERIFY(library.previewPreset(QStringLiteral("imported"), false, false));
            for (const auto& key : keys)
                QCOMPARE(store.values().value(key), before.value(key));
            QVERIFY(library.previewPreset(QStringLiteral("imported"), false, true));
            for (const auto& key : keys)
                QCOMPARE(store.values().value(key), withBar ? preferences.value(key) : before.value(key));
            store.endPreview();
            QCOMPARE(store.values(), before);
            for (auto it = preferences.cbegin(); it != preferences.cend(); ++it)
                QVERIFY(store.setValue(it.key(), it.value()));
        }
        AppearanceLibrary reloaded(&store, dir.filePath(QStringLiteral("library")));
        const auto saved = reloaded.presets().last().toMap().value(QStringLiteral("settings")).toMap();
        for (const auto& key : keys)
            QCOMPARE(saved.value(key), preferences.value(key));
    }
    void malformedTrayImportDoesNotAlterAppearance()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        AppearanceLibrary library(&store, dir.filePath(QStringLiteral("library")));
        QVERIFY(store.setValue(QStringLiteral("trayLimit"), 1));
        const auto before = store.values();
        QFile file(dir.filePath(QStringLiteral("broken-tray.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"version\":1,\"settings\":{\"trayVisibility\":{\"id:App\":\"unknown\"}}}");
        file.close();
        QVERIFY(!library.inspectImport(QUrl::fromLocalFile(file.fileName())));
        QCOMPARE(store.values(), before);
        QVERIFY(!library.previewPreset(QStringLiteral("imported"), false, true));
        QCOMPARE(store.values(), before);
    }
    void statsPreferencesTravelOnlyWithTheBarPreset()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        AppearanceLibrary library(&store, dir.filePath(QStringLiteral("library")));
        const QVariantList metrics{QStringLiteral("network"), QStringLiteral("storage")};
        QVERIFY(store.setValue(QStringLiteral("statsMetrics"), metrics));
        QVERIFY(store.setValue(QStringLiteral("statsStyle"), QStringLiteral("numbers")));
        QVERIFY(store.setValue(QStringLiteral("statsInterval"), 5));
        QVERIFY(library.savePreset(QStringLiteral("Desktop stats"), false, true));
        const auto id = library.presets().last().toMap().value(QStringLiteral("id")).toString();
        const auto file = QUrl::fromLocalFile(dir.filePath(QStringLiteral("stats.json")));
        QVERIFY(library.exportPreset(id, file));
        QVERIFY(store.setValue(QStringLiteral("statsStyle"), QStringLiteral("meters")));
        QVERIFY(store.setValue(QStringLiteral("statsInterval"), 1));
        QVERIFY(library.inspectImport(file));
        QVERIFY(store.beginPreview());
        QVERIFY(library.previewPreset(QStringLiteral("imported"), false, false));
        QCOMPARE(store.values().value(QStringLiteral("statsStyle")).toString(), QStringLiteral("meters"));
        QCOMPARE(store.values().value(QStringLiteral("statsInterval")).toInt(), 1);
        QVERIFY(library.previewPreset(QStringLiteral("imported"), false, true));
        QCOMPARE(store.values().value(QStringLiteral("statsStyle")).toString(), QStringLiteral("numbers"));
        QCOMPARE(store.values().value(QStringLiteral("statsInterval")).toInt(), 5);
        QCOMPARE(store.values().value(QStringLiteral("statsMetrics")).toList(), metrics);
        store.endPreview();
    }
    void qmlAndNativeShareTheDraftAcrossEngineReloads()
    {
        auto* native = AppearanceStore::create(nullptr, nullptr);
        auto* catalog = AppearanceLibrary::create(nullptr, nullptr);
        for (int reload = 0; reload < 2; ++reload) {
            QQmlEngine engine;
            const auto store = engine.singletonInstance<AppearanceStore*>(QStringLiteral("Phosphor.Theme"),
                                                                          QStringLiteral("AppearanceStore"));
            const auto library = engine.singletonInstance<AppearanceLibrary*>(QStringLiteral("Phosphor.Picker"),
                                                                              QStringLiteral("AppearanceLibrary"));
            QCOMPARE(store, native);
            QCOMPARE(library, catalog);
            if (!reload) {
                store->beginPreview();
                QVERIFY(store->setValue(QStringLiteral("radius"), 11));
            } else {
                QVERIFY(store->dirty());
                QCOMPARE(store->values().value(QStringLiteral("radius")).toInt(), 11);
                store->endPreview();
            }
        }
    }

    void collectionAndImagePreview()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        AppearanceLibrary library(&store, dir.filePath(QStringLiteral("library")));
        library.rescan();
        QVERIFY(library.wallpapers().size() >= 8);
        const auto moss = library.wallpapers()[5].toMap();
        const auto image = moss.value(QStringLiteral("path")).toString();
        QVERIFY(QFileInfo::exists(image));
        QVERIFY(!AppearanceLibrary::inspectImage(image).isEmpty());
        const auto initial = store.values();
        QCOMPARE(initial.value(QStringLiteral("wallpaperColors")),
                 library.wallpapers().first().toMap().value(QStringLiteral("colors")));
        store.beginPreview();
        library.chooseWallpaper(image, QStringLiteral("DP-1"), QStringLiteral("fit"));
        QTRY_VERIFY(!library.busy());
        QVERIFY2(library.error().isEmpty(), qPrintable(library.error()));
        QCOMPARE(store.values().value(QStringLiteral("wallpaperColors")), moss.value(QStringLiteral("colors")));
        QCOMPARE(store.values()
                     .value(QStringLiteral("wallpapers"))
                     .toMap()
                     .value(QStringLiteral("DP-1"))
                     .toMap()
                     .value(QStringLiteral("path"))
                     .toString(),
                 image);
        QVERIFY(store.dirty());
        AppearanceStore persisted(dir.filePath(QStringLiteral("appearance.json")));
        QCOMPARE(persisted.values(), initial);
        library.chooseWallpaper(image, QStringLiteral("DP-2"), QStringLiteral("fill"));
        store.endPreview();
        QTest::qWait(100);
        QCOMPARE(store.values(), initial);
    }
    void importedImageIsDurableAndMalformedImagesAreRejected()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        AppearanceLibrary library(&store, dir.filePath(QStringLiteral("library")));
        const auto path = dir.filePath(QStringLiteral("My # green?.png"));
        QImage sample(48, 32, QImage::Format_RGB32);
        sample.fill(QColor(QStringLiteral("#285e31")));
        QVERIFY(sample.save(path));
        library.importImages({QUrl::fromLocalFile(path)});
        QTRY_VERIFY(!library.busy());
        QVERIFY2(library.error().isEmpty(), qPrintable(library.error()));
        QString copied;
        for (const auto& wallpaper : library.wallpapers()) {
            const auto candidate = wallpaper.toMap().value(QStringLiteral("path")).toString();
            if (candidate.startsWith(dir.filePath(QStringLiteral("library/images/")))) {
                copied = candidate;
                break;
            }
        }
        QVERIFY(!copied.isEmpty());
        QCOMPARE(library.wallpaper(copied).value(QStringLiteral("name")).toString(), QStringLiteral("My # green?"));
        QVERIFY(copied != path);
        QVERIFY(QFile::remove(path));
        QVERIFY(!AppearanceLibrary::inspectImage(copied).isEmpty());
        QFile bad(path);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not an image");
        bad.close();
        QVERIFY(AppearanceLibrary::inspectImage(path).isEmpty());
        store.beginPreview();
        const auto before = store.values();
        library.chooseWallpaper(path, QString(), QStringLiteral("fill"));
        QTRY_VERIFY(!library.busy());
        QVERIFY(!library.error().isEmpty());
        QCOMPARE(store.values(), before);
    }
    void imageColorsPreferDistinctChromaticAreasOverShadows()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("colors.png"));
        QImage sample(64, 64, QImage::Format_RGB32);
        sample.fill(QColor(QStringLiteral("#161218")));
        const QColor gold(QStringLiteral("#db953c")), violet(QStringLiteral("#864bca"));
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 64; ++x)
                sample.setPixelColor(x, y, x < 40 ? gold : violet);
        }
        QVERIFY(sample.save(path));
        const auto colors = AppearanceLibrary::inspectImage(path).value(QStringLiteral("colors")).toList();
        QCOMPARE(colors.size(), 4);
        QCOMPARE(QColor(colors[0].toString()), gold);
        QCOMPARE(QColor(colors[1].toString()), violet);

        sample.fill(QColor(QStringLiteral("#777777")));
        QVERIFY(sample.save(path));
        const auto neutral = AppearanceLibrary::inspectImage(path).value(QStringLiteral("colors")).toList();
        QCOMPARE(neutral.size(), 4);
        for (const auto& color : neutral)
            QCOMPARE(QColor(color.toString()).hslSaturationF(), 0.0);
    }
    void presetsPreservePrivacyAndCanBeInspectedBeforePreview()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        AppearanceLibrary library(&store, dir.filePath(QStringLiteral("library")));
        QVERIFY(store.setValue(QStringLiteral("lockMedia"), true));
        QVERIFY(store.setValue(QStringLiteral("motion"), false));
        QVERIFY(store.setValue(QStringLiteral("notificationPreviews"), false));
        QVERIFY(store.moveWidget(QStringLiteral("clock"), QStringLiteral("left"), 0));
        const auto bar = store.values().value(QStringLiteral("barLayout"));
        QVERIFY(library.savePreset(QStringLiteral("My look"), false, false));
        QVERIFY(!library.savePreset(QStringLiteral("my LOOK"), false, false));
        const auto id = library.presets().last().toMap().value(QStringLiteral("id")).toString();
        const auto file = QUrl::fromLocalFile(dir.filePath(QStringLiteral("look.json")));
        QVERIFY(library.exportPreset(id, file));
        store.beginPreview();
        QVERIFY(library.previewPreset(QStringLiteral("ember"), false, false));
        QCOMPARE(store.values().value(QStringLiteral("barLayout")), bar);
        QCOMPARE(store.values().value(QStringLiteral("edge")).toString(), QStringLiteral("top"));
        QVERIFY(store.values().value(QStringLiteral("lockMedia")).toBool());
        QVERIFY(!store.values().value(QStringLiteral("motion")).toBool());
        QVERIFY(!store.values().value(QStringLiteral("notificationPreviews")).toBool());
        const auto before = store.values();
        QVERIFY(library.inspectImport(file));
        QCOMPARE(store.values(), before);
        QVERIFY(library.previewPreset(QStringLiteral("imported"), false, false));
        QCOMPARE(store.values().value(QStringLiteral("palette")).toString(), QStringLiteral("spectrum"));
        QVERIFY(library.saveImported(QStringLiteral("Imported look")));
        QVERIFY(library.removePreset(id));
        AppearanceLibrary restored(&store, dir.filePath(QStringLiteral("library")));
        QCOMPARE(restored.presets().size(), 4);
    }
    void invalidImportsAndMissingWallpapersDoNotChangeLook()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        AppearanceLibrary library(&store, dir.filePath(QStringLiteral("library")));
        store.beginPreview();
        const auto before = store.values();
        QFile file(dir.filePath(QStringLiteral("broken.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"version\":1,\"settings\":{\"radius\":1000}}");
        file.close();
        QVERIFY(!library.inspectImport(QUrl::fromLocalFile(file.fileName())));
        QCOMPARE(store.values(), before);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(
            QJsonDocument(
                QJsonObject{
                    {QStringLiteral("version"), 1},
                    {QStringLiteral("settings"),
                     QJsonObject{{QStringLiteral("wallpapers"),
                                  QJsonObject{{QStringLiteral("DP-1"),
                                               QJsonObject{{QStringLiteral("path"),
                                                            dir.filePath(QStringLiteral("missing.png"))},
                                                           {QStringLiteral("fit"), QStringLiteral("fill")}}}}}}}})
                .toJson());
        file.close();
        QVERIFY(library.inspectImport(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(!library.previewPreset(QStringLiteral("imported"), true, false));
        QCOMPARE(store.values(), before);
        QFile corrupt(dir.filePath(QStringLiteral("missing.png")));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("not an image");
        corrupt.close();
        QVERIFY(!library.previewPreset(QStringLiteral("imported"), true, false));
        QCOMPARE(store.values(), before);
        QVERIFY(library.previewPreset(QStringLiteral("imported"), false, false));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{\"version\":1,\"kind\":42,\"settings\":{\"radius\":10}}");
        file.close();
        QVERIFY(!library.inspectImport(QUrl::fromLocalFile(file.fileName())));
    }
};
QTEST_MAIN(TestAppearanceLibrary)
#include "test_appearancelibrary.moc"
