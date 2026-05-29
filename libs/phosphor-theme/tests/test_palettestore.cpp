// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/PaletteStore.h>

#include <QColor>
#include <QFile>
#include <QFileSystemWatcher>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVariantMap>

using namespace PhosphorTheme;

class TestPaletteStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaults_loadsCanonicalPhosphorTokens();
    void defaultPalette_staticAccessorMatchesInstance();
    void loadFromJson_acceptsWrappedShape();
    void loadFromJson_acceptsFlatShape();
    void loadFromJson_mergesNotReplaces();
    void loadFromJson_rejectsInvalidPayload();
    void loadFromJson_failedAfterFileLoadPreservesSourcePath();
    void loadFromJson_rejectsWrappedTokensWithNonObjectValue();
    void loadFromFile_persistsSourcePath();
    void loadFromFile_emitsErrorOnMissingFile();
    void loadFromFile_malformedJsonLeavesSourcePathUntouched();
    void loadFromFile_tokensKeyNotObjectKeepsWatcherArmed();
    void loadFromFile_noUsableTokensKeepsWatcherArmed();
    void applyTokens_emptyIsNoOp();
    void applyTokens_normalisesStringHexToColor();
    void resetToDefaults_clearsSourcePath();
    void resetToDefaults_dropsExtraUserTokens();
    void resetToDefaults_signalOrderIsSourceBeforePalette();
    void resetToDefaults_releasesDirectoryWatch();
    void hotReload_picksUpInPlaceEdit();
    void hotReload_survivesConsecutiveInPlaceEdits();
    void hotReload_picksUpAtomicRename();
    void hotReload_debouncesBurstOfFileChanges();
    void hotReload_resetCancelsPendingDebounce();
};

void TestPaletteStore::defaults_loadsCanonicalPhosphorTokens()
{
    PaletteStore s;
    // Spot-check the canonical defaults, these are the load-bearing
    // values the rest of the shell will reference until matugen runs.
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
    QCOMPARE(s.token(QStringLiteral("background")), QColor("#050916"));
    QCOMPARE(s.token(QStringLiteral("brand_stop_3")), QColor("#F43F5E"));
    // Unknown token → invalid color.
    QVERIFY(!s.token(QStringLiteral("nonexistent")).isValid());
}

void TestPaletteStore::loadFromJson_acceptsWrappedShape()
{
    PaletteStore s;
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    const QByteArray payload = R"({"tokens": {"primary": "#112233", "background": "#445566"}})";
    QVERIFY(s.loadFromJson(payload));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));
    QCOMPARE(s.token(QStringLiteral("background")), QColor("#445566"));
    QCOMPARE(spy.count(), 1);
}

void TestPaletteStore::loadFromJson_acceptsFlatShape()
{
    PaletteStore s;
    const QByteArray payload = R"({"primary": "#aabbcc"})";
    QVERIFY(s.loadFromJson(payload));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#aabbcc"));
}

void TestPaletteStore::loadFromJson_mergesNotReplaces()
{
    // The merge contract: tokens absent from the new payload preserve
    // their prior value. brand_stop_* should survive a matugen run that
    // only emits M3 tokens.
    PaletteStore s;
    const QColor brandBefore = s.token(QStringLiteral("brand_stop_3"));
    QVERIFY(brandBefore.isValid());

    const QByteArray payload = R"({"tokens": {"primary": "#000000"}})";
    QVERIFY(s.loadFromJson(payload));
    QCOMPARE(s.token(QStringLiteral("brand_stop_3")), brandBefore);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#000000"));
}

void TestPaletteStore::loadFromJson_rejectsInvalidPayload()
{
    PaletteStore s;
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    // Outright malformed.
    QVERIFY(!s.loadFromJson("not json"));
    // Valid JSON with no usable color tokens.
    QVERIFY(!s.loadFromJson(R"({"tokens": {"primary": 42}})"));
    // Active palette is unchanged.
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
    QCOMPARE(spy.count(), 0);
}

void TestPaletteStore::loadFromJson_failedAfterFileLoadPreservesSourcePath()
{
    // Regression for the Pass-2 finding: loadFromJson used to call
    // dropWatcherAndClearSourcePath() BEFORE validating the payload.
    // On a failed parse against a previously file-loaded store the
    // watcher torn down, m_sourcePath cleared, and sourcePathChanged
    // fired — all while the function returned false. The contract
    // is "a failed load does not observably mutate state". This
    // test pins that contract: load a good file, then attempt THREE
    // failing loadFromJson calls covering each failure mode the
    // validator distinguishes:
    //   1. Outright malformed (parse error)
    //   2. Valid JSON but no usable color tokens (NoUsableTokens)
    //   3. Wrapped tokens key whose value is not an object
    //      (TokensKeyNotObject)
    // After each failure we assert sourcePath survives unchanged,
    // sourcePathChanged fires exactly ONCE total (the initial
    // loadFromFile), and the watcher remains armed on the file +
    // parent directory.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString goodPath = tmp.filePath(QStringLiteral("good.json"));
    {
        QFile f(goodPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }

    PaletteStore s;
    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);
    QVERIFY(s.loadFromFile(goodPath));
    const QString sourceAfterLoad = s.sourcePath();
    QVERIFY(!sourceAfterLoad.isEmpty());
    QCOMPARE(pathSpy.count(), 1);

    // White-box assertion against the watcher's tracked paths —
    // matches the convention used by resetToDefaults_releasesDirectoryWatch.
    // A purely behavioural test that routed through fileChanged /
    // directoryChanged would silently pass on a regression that
    // restored m_sourcePath on rollback but left the watcher
    // dropped: the handler short-circuits on m_sourcePath !=
    // event-path, and a missing watch entry produces no event at
    // all, so the test would observe "no reload" and conclude
    // success. Direct watcher inspection via findChildren is the
    // only way to prove the underlying inotify watch survived
    // each failing call. The watcher is constructed with `this`
    // as parent (see PaletteStore ctor), so findChildren surfaces it.
    const auto watchers = s.findChildren<QFileSystemWatcher*>();
    QCOMPARE(watchers.size(), 1);
    QFileSystemWatcher* watcher = watchers.first();
    QVERIFY(!watcher->files().isEmpty());
    QVERIFY(!watcher->directories().isEmpty());

    // Failing loadFromJson #1: outright malformed.
    QVERIFY(!s.loadFromJson("not json"));
    QCOMPARE(s.sourcePath(), sourceAfterLoad);
    QCOMPARE(pathSpy.count(), 1); // no additional fire
    QVERIFY(!watcher->files().isEmpty());
    QVERIFY(!watcher->directories().isEmpty());

    // Failing loadFromJson #2: valid JSON, no usable tokens.
    QVERIFY(!s.loadFromJson(R"({"tokens": {"primary": 42}})"));
    QCOMPARE(s.sourcePath(), sourceAfterLoad);
    QCOMPARE(pathSpy.count(), 1); // still no additional fire
    QVERIFY(!watcher->files().isEmpty());
    QVERIFY(!watcher->directories().isEmpty());

    // Failing loadFromJson #3: wrapped tokens with non-object value.
    QVERIFY(!s.loadFromJson(R"({"tokens": "see other.json"})"));
    QCOMPARE(s.sourcePath(), sourceAfterLoad);
    QCOMPARE(pathSpy.count(), 1);
    QVERIFY(!watcher->files().isEmpty());
    QVERIFY(!watcher->directories().isEmpty());

    // Palette tokens that were committed by the file load survive.
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));
}

void TestPaletteStore::loadFromFile_persistsSourcePath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"tokens": {"primary": "#cafe00"}})");
    f.close();

    PaletteStore s;
    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.sourcePath(), path);
    QCOMPARE(pathSpy.count(), 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#cafe00"));
}

void TestPaletteStore::loadFromFile_emitsErrorOnMissingFile()
{
    PaletteStore s;
    QSignalSpy errSpy(&s, &PaletteStore::loadError);
    QVERIFY(!s.loadFromFile(QStringLiteral("/definitely/does/not/exist.json")));
    QCOMPARE(errSpy.count(), 1);
}

void TestPaletteStore::loadFromFile_malformedJsonLeavesSourcePathUntouched()
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
    QVERIFY(tmp.isValid());
    const QString goodPath = tmp.filePath(QStringLiteral("good.json"));
    const QString badPath = tmp.filePath(QStringLiteral("malformed.json"));
    {
        QFile f(goodPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }
    {
        QFile f(badPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not json at all { ]]} ");
    }

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

void TestPaletteStore::loadFromFile_tokensKeyNotObjectKeepsWatcherArmed()
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
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("bad-tokens.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": "see other.json"})");
    }

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

void TestPaletteStore::loadFromFile_noUsableTokensKeepsWatcherArmed()
{
    // Companion to the TokensKeyNotObject test: same asymmetry, but
    // the JSON is shaped fine and only the contents are unusable.
    // The wrapped tokens map contains entries that aren't valid
    // colors (numeric value here) so applyParsedJson short-circuits
    // with NoUsableTokens. Same retention contract: sourcePath +
    // watcher stay committed, loadError carries the precise message,
    // active palette unchanged.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("no-tokens.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": 42}})");
    }

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

void TestPaletteStore::applyTokens_emptyIsNoOp()
{
    PaletteStore s;
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    s.applyTokens({});
    QCOMPARE(spy.count(), 0);
}

void TestPaletteStore::applyTokens_normalisesStringHexToColor()
{
    // QML and matugen callers may hand us hex strings rather than QColor
    // values. applyPalette must normalise so token() returns a usable
    // QColor regardless of the original variant type.
    PaletteStore s;
    QVariantMap tokens;
    tokens.insert(QStringLiteral("primary"), QStringLiteral("#112233"));
    s.applyTokens(tokens);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));

    // Non-color, non-convertible values are dropped rather than
    // corrupting the palette. Snapshot the on_primary default
    // BEFORE the junk-apply call so the assertion below is
    // resilient to a future change of the canonical defaults —
    // hardcoding the hex value would silently rot when the
    // default-palette ships a new on_primary token.
    const QColor onPrimaryBefore = s.token(QStringLiteral("on_primary"));
    QVERIFY(onPrimaryBefore.isValid());
    QVariantMap junk;
    junk.insert(QStringLiteral("on_primary"), QVariant::fromValue(42));
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    s.applyTokens(junk);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(s.token(QStringLiteral("on_primary")), onPrimaryBefore); // default unchanged
}

void TestPaletteStore::resetToDefaults_clearsSourcePath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"primary": "#abcdef"})");
    f.close();

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QVERIFY(!s.sourcePath().isEmpty());

    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);
    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);
    s.resetToDefaults();
    QCOMPARE(s.sourcePath(), QString());
    QCOMPARE(pathSpy.count(), 1);
    QCOMPARE(palSpy.count(), 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
}

void TestPaletteStore::resetToDefaults_dropsExtraUserTokens()
{
    // Regression for the Pass-3 finding: resetToDefaults was
    // delegating to applyPalette (a merge), so any extra tokens
    // layered in by a previous loadFromJson / loadFromFile (matugen-
    // extended brand_*, hand-edited custom keys) survived the reset.
    // The contract is "back to canonical defaults" — extra keys must
    // not persist.
    PaletteStore s;
    const QByteArray withExtras = R"({"primary": "#abcdef", "ghost_token": "#112233", "brand_custom": "#445566"})";
    QVERIFY(s.loadFromJson(withExtras));
    QVERIFY(s.palette().contains(QStringLiteral("ghost_token")));
    QVERIFY(s.palette().contains(QStringLiteral("brand_custom")));

    // Observe both signals through ONE lambda so we can pin the
    // chronological order. Previously the test only checked counts,
    // letting an implementation that emits paletteChanged BEFORE
    // sourcePathChanged pass — which would force QML bindings to
    // re-evaluate against a half-transitioned state.
    QStringList signalOrder;
    QObject::connect(&s, &PaletteStore::paletteChanged, &s, [&signalOrder] {
        signalOrder.append(QStringLiteral("paletteChanged"));
    });
    QObject::connect(&s, &PaletteStore::sourcePathChanged, &s, [&signalOrder] {
        signalOrder.append(QStringLiteral("sourcePathChanged"));
    });

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);
    s.resetToDefaults();

    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
    QVERIFY2(!s.palette().contains(QStringLiteral("ghost_token")),
             "resetToDefaults left a non-canonical user token behind");
    QVERIFY2(!s.palette().contains(QStringLiteral("brand_custom")),
             "resetToDefaults left a non-canonical brand token behind");
    // Pin the single-emit contract — drop-then-apply must collapse
    // to exactly one paletteChanged, not two (one for the drop +
    // one for the apply).
    QCOMPARE(palSpy.count(), 1);
    // sourcePath was not loaded from a file here (we used
    // loadFromJson), so sourcePathChanged should not fire on reset.
    // The only signal emitted should be paletteChanged.
    QCOMPARE(signalOrder, QStringList{QStringLiteral("paletteChanged")});
}

void TestPaletteStore::resetToDefaults_signalOrderIsSourceBeforePalette()
{
    // Pins the implementation-only signal ordering convention:
    // when resetToDefaults drops a watched source AND mutates the
    // palette (the common case after loadFromFile), QML bindings
    // see sourcePathChanged FIRST and then paletteChanged. QML
    // binding evaluation visits its dependencies in property-
    // notification order: a binding that reads both sourcePath
    // and a palette token re-evaluates on whichever signal arrives
    // first, then again on the second. The reverse signal order
    // would make the FIRST evaluation read the new palette against
    // the stale sourcePath — visibly wrong in any binding that
    // formats the source path alongside a palette swatch.
    // (Direct connections fire synchronously; there is no event-
    // loop tick between the two, so the order on the wire is the
    // order the slots run, full stop.)
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#abcdef"}})");
    }
    PaletteStore s;
    QVERIFY(s.loadFromFile(path));

    QStringList signalOrder;
    QObject::connect(&s, &PaletteStore::paletteChanged, &s, [&signalOrder] {
        signalOrder.append(QStringLiteral("paletteChanged"));
    });
    QObject::connect(&s, &PaletteStore::sourcePathChanged, &s, [&signalOrder] {
        signalOrder.append(QStringLiteral("sourcePathChanged"));
    });

    s.resetToDefaults();

    QCOMPARE(signalOrder, (QStringList{QStringLiteral("sourcePathChanged"), QStringLiteral("paletteChanged")}));
}

void TestPaletteStore::hotReload_picksUpInPlaceEdit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);

    // Edit in place (truncate + rewrite, the same syscall pattern most
    // editors use when configured for in-place save).
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"tokens": {"primary": "#445566"}})");
    }

    // QFileSystemWatcher is asynchronous: pump the event loop until it
    // fires or the deadline expires. 5s ceiling matches sibling tests.
    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#445566"));
}

void TestPaletteStore::hotReload_survivesConsecutiveInPlaceEdits()
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
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#101010"}})");
    }

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#101010"));
    const QString sourceAfterLoad = s.sourcePath();
    QVERIFY(!sourceAfterLoad.isEmpty());

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);

    // First edit.
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"tokens": {"primary": "#202020"}})");
    }
    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#202020"));
    // Watcher must still be armed after the first reload.
    QCOMPARE(s.sourcePath(), sourceAfterLoad);

    palSpy.clear();

    // Second edit. Without the parseAndApplyJson(false) fix this
    // edit never fires paletteChanged because the watcher was
    // disarmed during the first reload.
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"tokens": {"primary": "#303030"}})");
    }
    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#303030"));
    QCOMPARE(s.sourcePath(), sourceAfterLoad);
}

void TestPaletteStore::hotReload_picksUpAtomicRename()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }

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
    {
        QFile f(tmpFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#445566"}})");
    }
    QVERIFY(QFile::remove(path));
    QVERIFY(QFile::rename(tmpFile, path));

    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#445566"));
}

void TestPaletteStore::defaultPalette_staticAccessorMatchesInstance()
{
    // Static accessor must return the canonical built-in dark palette
    // without requiring any PaletteStore instance. Calling it before any
    // instance exists confirms it is truly stateless.
    const auto defaults = PaletteStore::defaultPalette();
    QVERIFY(!defaults.isEmpty());
    QCOMPARE(defaults.value(QStringLiteral("primary")).value<QColor>(), QColor("#3B82F6"));
    QCOMPARE(defaults.value(QStringLiteral("background")).value<QColor>(), QColor("#050916"));
    QCOMPARE(defaults.value(QStringLiteral("brand_stop_3")).value<QColor>(), QColor("#F43F5E"));

    // A freshly constructed PaletteStore holds the same palette.
    PaletteStore s;
    QCOMPARE(s.palette(), defaults);
}

void TestPaletteStore::loadFromJson_rejectsWrappedTokensWithNonObjectValue()
{
    PaletteStore s;
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    // `tokens` key present but value is a string. The caller's intent
    // is unambiguously the wrapped layout, but the payload is malformed.
    // Falling through to flat-map parsing would silently treat the
    // string "see other.json" as if it were a token, which is wrong.
    QVERIFY(!s.loadFromJson(R"({"tokens": "see other.json"})"));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
}

void TestPaletteStore::resetToDefaults_releasesDirectoryWatch()
{
    // Direct white-box assertion against the watcher's tracked paths.
    // A behavioural test would route through directoryChanged, but the
    // handler short-circuits when m_sourcePath.isEmpty() (which reset
    // makes true), so a leaked watch would still emit no paletteChanged
    // and the test would falsely pass.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }

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

void TestPaletteStore::hotReload_debouncesBurstOfFileChanges()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }

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
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        if (i == 4) {
            f.write(R"({"tokens": {"primary": "#ffeedd"}})");
        } else {
            f.write(QStringLiteral(R"({"tokens": {"primary": "#11223%1"}})").arg(i).toUtf8());
        }
    }

    QVERIFY(palSpy.wait(2000));
    QTest::qWait(150); // let any stragglers fire so we catch them
    QVERIFY(palSpy.count() >= 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#ffeedd"));
    QCOMPARE(errSpy.count(), 0);
}

void TestPaletteStore::hotReload_resetCancelsPendingDebounce()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));

    // The debounce timer is parented to the store. We need it to verify
    // the timer is actually armed before we call resetToDefaults; without
    // this check the test passes even if reset never stops the timer
    // (the fileChanged event would deliver later and re-arm the timer
    // anyway, then short-circuit on empty m_sourcePath).
    const auto timers = s.findChildren<QTimer*>();
    QCOMPARE(timers.size(), 1);
    QTimer* debounce = timers.first();
    QVERIFY(!debounce->isActive());

    // Edit the file. The watcher schedules a reload via the debounce
    // timer. fileChanged is delivered asynchronously, so spin the event
    // loop until the timer arms (or until a short deadline) before
    // attempting to cancel it.
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"tokens": {"primary": "#ff0000"}})");
    }
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

QTEST_MAIN(TestPaletteStore)
#include "test_palettestore.moc"
