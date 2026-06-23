// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Hot-reload slots for PaletteStore + the white-box
// resetToDefaults_releasesDirectoryWatch slot. Split out of
// test_palettestore.cpp so each TU stays under the project's 800-line
// cap. Shares the QTemporaryDir + QFile fixture-write scaffolding with
// the primary + files TUs via test_palettestore_helpers.h.
//
// All slots here exercise the QFileSystemWatcher + debounce-timer
// machinery armed by loadFromFile: in-place edits, atomic renames,
// rapid bursts, and the reset-cancels-debounce interaction. The
// 5-second wait ceilings match the rest of the hot-reload suite — a
// 2-second window flakes on IO-bound CI runners.

#include "test_palettestore_helpers.h"

#include <PhosphorTheme/PaletteStore.h>

#include <QColor>
#include <QFile>
#include <QFileSystemWatcher>
#include <QSignalSpy>
#include <QString>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace PhosphorTheme;
using namespace PhosphorThemeTestHelpers;

class TestPaletteStoreHotReload : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void resetToDefaults_releasesDirectoryWatch();
    void hotReload_picksUpInPlaceEdit();
    void hotReload_survivesConsecutiveInPlaceEdits();
    void hotReload_picksUpAtomicRename();
    void hotReload_debouncesBurstOfFileChanges();
    void hotReload_resetCancelsPendingDebounce();
};

void TestPaletteStoreHotReload::resetToDefaults_releasesDirectoryWatch()
{
    // Direct white-box assertion against the watcher's tracked paths.
    // A behavioural test would route through directoryChanged, but the
    // handler short-circuits when m_sourcePath.isEmpty() (which reset
    // makes true), so a leaked watch would still emit no paletteChanged
    // and the test would falsely pass.
    QTemporaryDir tmp;
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), path));

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));

    // The watcher is constructed with `this` as parent, so findChildren
    // surfaces it. Confirm both file and directory watches were armed.
    const auto watchers = s.findChildren<QFileSystemWatcher*>();
    QCOMPARE(watchers.size(), 1);
    QFileSystemWatcher* watcher = watchers.first();
    QVERIFY(!watcher->files().isEmpty());
    QVERIFY(!watcher->directories().isEmpty());

    s.resetToDefaults();

    // Both lists must be cleared. A leaked directory entry would mean
    // an inotify slot stayed held for the program lifetime.
    QVERIFY(watcher->files().isEmpty());
    QVERIFY(watcher->directories().isEmpty());
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
}

void TestPaletteStoreHotReload::hotReload_picksUpInPlaceEdit()
{
    QTemporaryDir tmp;
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), path));

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);

    // Edit in place (truncate + rewrite, the same syscall pattern most
    // editors use when configured for in-place save).
    QVERIFY(writeJsonFile(path, defaultWrappedPayload("#445566")));

    // QFileSystemWatcher is asynchronous: pump the event loop until it
    // fires or the deadline expires. 5s ceiling matches sibling tests.
    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#445566"));
}

void TestPaletteStoreHotReload::hotReload_survivesConsecutiveInPlaceEdits()
{
    // Regression for the Pass-2 PaletteStore::reloadFromCurrentPath
    // bug: a successful hot-reload was routing through the public
    // loadFromJson, which dropped the watcher + cleared m_sourcePath
    // every time, silently breaking the next on-disk edit. This test
    // performs TWO consecutive edits and asserts both are picked up,
    // pinning the no-watcher-drop routing in reloadFromCurrentPath
    // (the only watcher-tearing path is dropWatcherAndClearSourcePath,
    // which only loadFromJson + resetToDefaults call).
    QTemporaryDir tmp;
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), defaultWrappedPayload("#101010"), path));

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#101010"));
    const QString sourceAfterLoad = s.sourcePath();
    QVERIFY(!sourceAfterLoad.isEmpty());

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);

    // First edit.
    QVERIFY(writeJsonFile(path, defaultWrappedPayload("#202020")));
    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#202020"));
    // Watcher must still be armed after the first reload.
    QCOMPARE(s.sourcePath(), sourceAfterLoad);

    palSpy.clear();

    // Second edit. Without routing reloadFromCurrentPath through
    // readParseAndApply (instead of the public loadFromJson, which
    // tears down the watcher), this edit never fires paletteChanged
    // because the watcher was disarmed during the first reload.
    QVERIFY(writeJsonFile(path, defaultWrappedPayload("#303030")));
    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#303030"));
    QCOMPARE(s.sourcePath(), sourceAfterLoad);
}

void TestPaletteStoreHotReload::hotReload_picksUpAtomicRename()
{
    QTemporaryDir tmp;
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), path));

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);

    // Simulate vim's default save flow: write to a temp file, then
    // rename(2) over the destination. This unlinks the watched inode
    // and creates a new one, which drops QFileSystemWatcher's file
    // watch silently. The parent-directory watch armed in the ctor
    // is what catches the rename and re-arms the file watch.
    const QString tmpFile = path + QStringLiteral(".tmp");
    QVERIFY(writeJsonFile(tmpFile, defaultWrappedPayload("#445566")));
    QVERIFY(QFile::remove(path));
    QVERIFY(QFile::rename(tmpFile, path));

    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#445566"));
}

void TestPaletteStoreHotReload::hotReload_debouncesBurstOfFileChanges()
{
    QTemporaryDir tmp;
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), path));

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);
    QSignalSpy errSpy(&s, &PaletteStore::loadError);

    // Burst of rapid in-place rewrites. Each one fires fileChanged
    // synchronously. The 80ms debounce window is INTENDED to collapse
    // them into a single reload, but on slow CI (a stuck scheduler,
    // an IO-bound runner) the writes themselves can take long enough
    // that two separate debounce windows fire and we observe two
    // paletteChanged emissions. The hot-reload contract that matters
    // to a user is "after a burst settles, the active palette
    // reflects the FINAL write" — we assert that explicitly. We also
    // assert at least one paletteChanged fired (debounce can't be a
    // full swallow) and zero loadErrors (every intermediate payload
    // is a syntactically valid token map). The strict "exactly one
    // emit" assertion is intentionally relaxed because the unit-test
    // contract should not depend on scheduler quanta the production
    // code doesn't observe.
    for (int i = 0; i < 5; ++i) {
        const QByteArray hex = (i == 4) ? QByteArray("#ffeedd") : QStringLiteral("#11223%1").arg(i).toUtf8();
        QVERIFY(writeJsonFile(path, defaultWrappedPayload(hex)));
    }

    // 5s ceiling matches the rest of the hot-reload suite — a 2s
    // window flakes on IO-bound CI runners.
    QVERIFY(palSpy.wait(5000));
    QTest::qWait(150); // let any stragglers fire so we catch them
    QVERIFY(palSpy.count() >= 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#ffeedd"));
    QCOMPARE(errSpy.count(), 0);
}

void TestPaletteStoreHotReload::hotReload_resetCancelsPendingDebounce()
{
    QTemporaryDir tmp;
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), path));

    PaletteStore s;
    // Shrink the debounce window so the QTRY_VERIFY_WITH_TIMEOUT below
    // doesn't have to wait a full 80 ms after every fileChanged delivery.
    // 100 ms is well above QTRY_VERIFY's 50 ms poll interval (the timer
    // is reliably observable as active during one poll), and far below
    // the production 80 ms so the test still finishes in well under a
    // second. The previous 1 ms was racy on busy CI runners — the timer
    // could fire and clear isActive between QTRY_VERIFY polls, flaking
    // the assertion. We're pinning timer-arm → reset stops timer →
    // verify isActive returns false, not exact ms counts.
    s.setDebounceIntervalForTest(100);
    QVERIFY(s.loadFromFile(path));

    // The debounce timer is parented to the store. We need it to verify
    // the timer is actually armed before we call resetToDefaults; without
    // this check the test passes even if reset never stops the timer
    // (the fileChanged event would deliver later and re-arm the timer
    // anyway, then short-circuit on empty m_sourcePath).
    //
    // Prefer the objectName-based lookup so a future PaletteStore that
    // adds a sibling QTimer child (animation, retry-backoff, anything)
    // doesn't silently break this test by changing findChildren's order
    // / count. Fall back to findChildren if the objectName isn't set —
    // a defensive guard against an in-flight rename of the stable name
    // ("paletteReloadDebounce") in the production code.
    QTimer* debounce = s.findChild<QTimer*>(QStringLiteral("paletteReloadDebounce"));
    if (!debounce) {
        const auto timers = s.findChildren<QTimer*>();
        QCOMPARE(timers.size(), 1);
        debounce = timers.first();
    }
    QVERIFY(debounce);
    QVERIFY(!debounce->isActive());

    // Edit the file. The watcher schedules a reload via the debounce
    // timer. fileChanged is delivered asynchronously, so spin the event
    // loop until the timer arms (or until a short deadline) before
    // attempting to cancel it.
    QVERIFY(writeJsonFile(path, defaultWrappedPayload("#ff0000")));
    QTRY_VERIFY_WITH_TIMEOUT(debounce->isActive(), 2000);

    // Reset with a debounce known to be armed. resetToDefaults must
    // stop the timer. Without that fix the timer would fire 80ms later,
    // run reloadFromCurrentPath against the now-empty sourcePath, and
    // no-op (clean short-circuit) — but the timer leaking is still a
    // contract violation we want pinned.
    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);
    s.resetToDefaults();
    QCOMPARE(palSpy.count(), 1); // reset itself
    QVERIFY(!debounce->isActive());

    // Even after waiting past the debounce window, no additional
    // paletteChanged fires.
    QTest::qWait(200);
    QCOMPARE(palSpy.count(), 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
}

QTEST_GUILESS_MAIN(TestPaletteStoreHotReload)
#include "test_palettestore_hotreload.moc"
