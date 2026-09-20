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
        const auto copied = library.wallpapers().last().toMap().value(QStringLiteral("path")).toString();
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
