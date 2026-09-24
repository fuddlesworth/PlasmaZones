// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/AppearanceStore.h>
#include <PhosphorTheme/AppearanceWatcher.h>

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorTheme;

class TestAppearanceWatcher : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void findsNewDirectoriesAndReattachesAfterAtomicReplacement()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("new/nested/appearance.json"));
        AppearanceWatcher watcher(path, dir.filePath(QStringLiteral("kwinrc")));
        QSignalSpy changed(&watcher, &AppearanceWatcher::changed);
        AppearanceStore store(path);
        QVERIFY(store.setValue(QStringLiteral("radius"), 23));
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 23);
        QVERIFY(store.setValue(QStringLiteral("radius"), 27));
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 27);
        QVERIFY(store.setValue(QStringLiteral("radius"), 30));
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 30);
        QCOMPARE(changed.count(), 3);
    }

    void previewsRevertApplyAndTrackActualDecorationOwnership()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        const auto kwinPath = dir.filePath(QStringLiteral("kwinrc"));
        AppearanceStore store(path);
        QVERIFY(store.setValue(QStringLiteral("radius"), 22));
        AppearanceWatcher watcher(path, kwinPath);
        QVERIFY(!watcher.desktopStyleActive());
        const auto selectDecoration = [&kwinPath](const QByteArray& plugin) {
            QSaveFile file(kwinPath);
            if (!file.open(QIODevice::WriteOnly))
                return false;
            const QByteArray contents = QByteArrayLiteral("[org.kde.kdecoration2]\nlibrary=") + plugin + '\n';
            return file.write(contents) == contents.size() && file.commit();
        };
        QVERIFY(selectDecoration(QByteArrayLiteral("org.phosphor.decoration")));
        QTRY_VERIFY(watcher.desktopStyleActive());
        QVERIFY(store.beginPreview());
        QVERIFY(store.setValue(QStringLiteral("radius"), 26));
        QVERIFY(store.setValue(QStringLiteral("desktopStyle"), false));
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 26);
        QTRY_VERIFY(!watcher.desktopStyleActive());
        QCOMPARE(AppearanceStore(path).values().value(QStringLiteral("radius")).toInt(), 22);
        store.revertPreview();
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 22);
        QTRY_VERIFY(watcher.desktopStyleActive());
        QVERIFY(store.setValue(QStringLiteral("radius"), 29));
        QVERIFY(store.applyPreview());
        QVERIFY(store.setValue(QStringLiteral("radius"), 28));
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 28);
        store.endPreview();
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 29);
        QVERIFY(selectDecoration(QByteArrayLiteral("org.kde.breeze")));
        QTRY_VERIFY(!watcher.desktopStyleActive());
    }

    void orphanPreviewFallsBackToSavedValues()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore saved(path);
        QVERIFY(saved.setValue(QStringLiteral("radius"), 21));
        AppearanceWatcher watcher(path, dir.filePath(QStringLiteral("kwinrc")));
        // Simulate a producer losing ownership without deleting its draft.
        QLockFile owner(path + QStringLiteral(".preview.lock"));
        owner.setStaleLockTime(0);
        QVERIFY(owner.tryLock());
        AppearanceStore draft(path + QStringLiteral(".preview"));
        QVERIFY(draft.setValue(QStringLiteral("radius"), 25));
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 25);
        owner.unlock();
        QTRY_COMPARE(watcher.values().value(QStringLiteral("radius")).toInt(), 21);
        QVERIFY(QFile::exists(path + QStringLiteral(".preview")));
        // A newly created reader must also reject that abandoned draft.
        AppearanceWatcher restarted(path, dir.filePath(QStringLiteral("kwinrc")));
        QCOMPARE(restarted.values().value(QStringLiteral("radius")).toInt(), 21);
    }
};

QTEST_GUILESS_MAIN(TestAppearanceWatcher)
#include "test_appearancewatcher.moc"
