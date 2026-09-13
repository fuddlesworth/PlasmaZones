// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/AppearanceStore.h>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using PhosphorTheme::AppearanceStore;
class TestAppearanceStore : public QObject
{
    Q_OBJECT
private Q_SLOTS:
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
