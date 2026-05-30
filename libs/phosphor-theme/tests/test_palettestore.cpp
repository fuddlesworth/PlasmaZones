// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Tests for PaletteStore. Covers the in-process API surface:
//   - canonical defaults / static defaultPalette() accessor
//   - loadFromJson (wrapped + flat shapes, merge contract, validation)
//   - applyTokens (normalisation + no-op contracts)
//   - resetToDefaults (clears source path, drops extra tokens, signal
//     ordering)
//
// loadFromFile + watcher-arming slots live in
// test_palettestore_files.cpp. Hot-reload + the white-box
// resetToDefaults_releasesDirectoryWatch slot live in
// test_palettestore_hotreload.cpp. The split keeps every TU under the
// project's 800-line cap and lets the three files share the
// QTemporaryDir + QFile fixture-write scaffolding via
// test_palettestore_helpers.h.

#include "test_palettestore_helpers.h"

#include <PhosphorTheme/PaletteStore.h>

#include <QColor>
#include <QFileSystemWatcher>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using namespace PhosphorTheme;
using namespace PhosphorThemeTestHelpers;

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
    void loadFromJson_signalOrderIsSourceBeforePalette();
    void loadFromJson_rejectsWrappedTokensWithNonObjectValue();
    void applyTokens_emptyIsNoOp();
    void applyTokens_normalisesStringHexToColor();
    void resetToDefaults_clearsSourcePath();
    void resetToDefaults_dropsExtraUserTokens();
    void resetToDefaults_signalOrderIsSourceBeforePalette();
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
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    const QByteArray payload = R"({"primary": "#aabbcc"})";
    QVERIFY(s.loadFromJson(payload));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#aabbcc"));
    // Parity with the wrapped-shape test: a successful flat-shape load
    // must emit paletteChanged exactly once.
    QCOMPARE(spy.count(), 1);
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
    QString goodPath;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("good.json"), goodPath));

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

void TestPaletteStore::loadFromJson_signalOrderIsSourceBeforePalette()
{
    // Pin loadFromJson's signal ordering: when the call drops a
    // previously-watched source AND mutates the palette, QML
    // bindings must observe sourcePathChanged FIRST and then
    // paletteChanged. Mirrors resetToDefaults_signalOrderIsSourceBeforePalette
    // — the rationale is identical (a binding reading both reaches
    // the new-source / new-palette pair on the first re-evaluation
    // instead of palette-against-stale-source on the first tick and
    // only catching up on the second). A previous implementation
    // ran applyPalette BEFORE dropWatcherAndClearSourcePath, which
    // emitted the signals in the opposite order: a regression here
    // would resurface that bug.
    QTemporaryDir tmp;
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), defaultWrappedPayload("#abcdef"), path));
    PaletteStore s;
    QVERIFY(s.loadFromFile(path));

    QStringList signalOrder;
    QObject::connect(&s, &PaletteStore::paletteChanged, &s, [&signalOrder] {
        signalOrder.append(QStringLiteral("paletteChanged"));
    });
    QObject::connect(&s, &PaletteStore::sourcePathChanged, &s, [&signalOrder] {
        signalOrder.append(QStringLiteral("sourcePathChanged"));
    });

    // Apply a new in-process palette via JSON blob. The store had a
    // file source from loadFromFile above, so the loadFromJson call
    // must (a) drop the watcher + clear m_sourcePath (firing
    // sourcePathChanged) and (b) apply the new palette (firing
    // paletteChanged) — in that order.
    QVERIFY(s.loadFromJson(R"({"tokens": {"primary": "#000000"}})"));

    QCOMPARE(signalOrder, (QStringList{QStringLiteral("sourcePathChanged"), QStringLiteral("paletteChanged")}));
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
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), QByteArray(R"({"primary": "#abcdef"})"), path));

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
    QString path;
    QVERIFY(seedTempJsonFile(tmp, QStringLiteral("p.json"), defaultWrappedPayload("#abcdef"), path));
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

QTEST_GUILESS_MAIN(TestPaletteStore)
#include "test_palettestore.moc"
