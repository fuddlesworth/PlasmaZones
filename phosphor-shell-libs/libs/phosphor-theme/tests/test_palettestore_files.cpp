// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// loadFromFile slots for PaletteStore. Shares the QTemporaryDir + QFile
// fixture-write scaffolding with the primary + hot-reload TUs via
// test_palettestore_helpers.h.
//
// These slots specifically pin loadFromFile's documented asymmetry
// with loadFromJson: a malformed-JSON parse failure does NOT commit
// sourcePath, but a shape-failure (TokensKeyNotObject / NoUsableTokens)
// DOES commit so the user can fix the file in-editor and trigger an
// automatic reload via the watcher armed in the same call.

#include "test_palettestore_helpers.h"

#include <PhosphorTheme/PaletteStore.h>

#include <QColor>
#include <QFileSystemWatcher>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorTheme;
using namespace PhosphorThemeTestHelpers;

class TestPaletteStoreFiles : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void loadFromFile_persistsSourcePath();
    void loadFromFile_emitsErrorOnMissingFile();
    void loadFromFile_malformedJsonLeavesSourcePathUntouched();
    void loadFromFile_tokensKeyNotObjectKeepsWatcherArmed();
    void loadFromFile_noUsableTokensKeepsWatcherArmed();
};

void TestPaletteStoreFiles::loadFromFile_persistsSourcePath()
{
    QTemporaryDir tmp;
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), defaultWrappedPayload("#cafe00"), path));

    PaletteStore s;
    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.sourcePath(), path);
    QCOMPARE(pathSpy.count(), 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#cafe00"));
}

void TestPaletteStoreFiles::loadFromFile_emitsErrorOnMissingFile()
{
    PaletteStore s;
    QSignalSpy errSpy(&s, &PaletteStore::loadError);
    QVERIFY(!s.loadFromFile(QStringLiteral("/definitely/does/not/exist.json")));
    QCOMPARE(errSpy.count(), 1);
}

void TestPaletteStoreFiles::loadFromFile_malformedJsonLeavesSourcePathUntouched()
{
    // PaletteStore.h documents that outright parse failures DO NOT
    // commit sourcePath (asymmetric with the shape-failure paths,
    // which DO commit). This pins that half of the contract: load
    // a good file first to arm the watcher + populate sourcePath,
    // then attempt loadFromFile against a file with non-JSON bytes.
    // The malformed load must:
    //   1. Return false.
    //   2. Emit loadError exactly once.
    //   3. Leave sourcePath pointing at the FIRST (good) file —
    //      sourcePathChanged must not fire a second time.
    //   4. Leave the watcher still tracking the FIRST file +
    //      its parent directory.
    // A regression that committed sourcePath before the parse step
    // would be visible by either watcher inspection or pathSpy count.
    QTemporaryDir tmp;
    QString goodPath;
    QString badPath;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("good.json"), goodPath));
    QVERIFY(
        seedTempJsonFile(tmp, QStringLiteral("malformed.json"), QByteArray("this is not json at all { ]]} "), badPath));

    PaletteStore s;
    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);
    QSignalSpy errSpy(&s, &PaletteStore::loadError);

    QVERIFY(s.loadFromFile(goodPath));
    const QString sourceAfterGood = s.sourcePath();
    QVERIFY(!sourceAfterGood.isEmpty());
    QCOMPARE(pathSpy.count(), 1);

    // White-box: pin the watcher's tracked paths BEFORE the bad
    // load so we can compare them post-failure. See
    // resetToDefaults_releasesDirectoryWatch for the rationale
    // — a behavioural test would miss a watcher drop because
    // sourcePath alone could be restored on rollback while the
    // underlying watch stayed dropped.
    const auto watchers = s.findChildren<QFileSystemWatcher*>();
    QCOMPARE(watchers.size(), 1);
    QFileSystemWatcher* watcher = watchers.first();
    const QStringList filesBefore = watcher->files();
    const QStringList dirsBefore = watcher->directories();
    QVERIFY(!filesBefore.isEmpty());
    QVERIFY(!dirsBefore.isEmpty());

    // Attempt the malformed load.
    QVERIFY(!s.loadFromFile(badPath));

    // loadError fires (carries the malformed-JSON message), but
    // sourcePathChanged stays at its post-good-load count.
    QCOMPARE(errSpy.count(), 1);
    QCOMPARE(errSpy.first().at(1).toString(), QStringLiteral("invalid JSON"));
    QCOMPARE(s.sourcePath(), sourceAfterGood);
    QCOMPARE(pathSpy.count(), 1);

    // Watcher is unchanged: still pointed at the first file +
    // its parent directory.
    QCOMPARE(watcher->files(), filesBefore);
    QCOMPARE(watcher->directories(), dirsBefore);

    // The good file's palette tokens still apply.
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));
}

void TestPaletteStoreFiles::loadFromFile_tokensKeyNotObjectKeepsWatcherArmed()
{
    // Pins the documented asymmetry between loadFromJson (atomic on
    // shape failure) and loadFromFile (commits sourcePath + watcher
    // BEFORE shape check). When the wrapped layout's `tokens` key is
    // not an object the apply step returns TokensKeyNotObject; the
    // header contract says the new source + watcher are retained so
    // the user can fix the shape in-editor and trigger an automatic
    // reload. This test pins both halves: (1) loadError fires with
    // the precise message text, (2) sourcePath holds the new file's
    // path, (3) the watcher still tracks the file + parent directory.
    QTemporaryDir tmp;
    QString path;
    QVERIFY(
        seedTempJsonFile(tmp, QStringLiteral("bad-tokens.json"), QByteArray(R"({"tokens": "see other.json"})"), path));

    PaletteStore s;
    QSignalSpy errSpy(&s, &PaletteStore::loadError);
    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);

    QVERIFY(!s.loadFromFile(path));

    QCOMPARE(errSpy.count(), 1);
    QCOMPARE(errSpy.first().at(1).toString(), QStringLiteral("tokens key must be a JSON object"));

    // sourcePath was committed (asymmetric with loadFromJson).
    QCOMPARE(s.sourcePath(), path);
    QCOMPARE(pathSpy.count(), 1);

    // Watcher remains armed against the new file + parent directory.
    const auto watchers = s.findChildren<QFileSystemWatcher*>();
    QCOMPARE(watchers.size(), 1);
    QFileSystemWatcher* watcher = watchers.first();
    QVERIFY(watcher->files().contains(path));
    QVERIFY(!watcher->directories().isEmpty());

    // Active palette is unchanged (still defaults).
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
}

void TestPaletteStoreFiles::loadFromFile_noUsableTokensKeepsWatcherArmed()
{
    // Companion to the TokensKeyNotObject test: same asymmetry, but
    // the JSON is shaped fine and only the contents are unusable.
    // The wrapped tokens map contains entries that aren't valid
    // colors (numeric value here) so applyParsedJson short-circuits
    // with NoUsableTokens. Same retention contract: sourcePath +
    // watcher stay committed, loadError carries the precise message,
    // active palette unchanged.
    QTemporaryDir tmp;
    QString path;
    QVERIFY(
        seedTempJsonFile(tmp, QStringLiteral("no-tokens.json"), QByteArray(R"({"tokens": {"primary": 42}})"), path));

    PaletteStore s;
    QSignalSpy errSpy(&s, &PaletteStore::loadError);
    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);

    QVERIFY(!s.loadFromFile(path));

    QCOMPARE(errSpy.count(), 1);
    QCOMPARE(errSpy.first().at(1).toString(), QStringLiteral("no usable color tokens"));

    QCOMPARE(s.sourcePath(), path);
    QCOMPARE(pathSpy.count(), 1);

    const auto watchers = s.findChildren<QFileSystemWatcher*>();
    QCOMPARE(watchers.size(), 1);
    QFileSystemWatcher* watcher = watchers.first();
    QVERIFY(watcher->files().contains(path));
    QVERIFY(!watcher->directories().isEmpty());

    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
}

QTEST_GUILESS_MAIN(TestPaletteStoreFiles)
#include "test_palettestore_files.moc"
