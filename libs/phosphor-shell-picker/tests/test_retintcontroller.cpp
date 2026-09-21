// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// RetintController against a fake runner (MatugenRunner's run / cancel /
// paletteReady / failed surface, with the calls recorded) and the REAL
// PaletteStore, which is what pins the brand-stop guarantee: whatever a
// preview sends, brand_stop_0..3 read the same before and after.

#include <PhosphorShellPicker/RetintController.h>

#include <PhosphorTheme/IThemeService.h>
#include <PhosphorTheme/PaletteStore.h>

#include <QColor>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using PhosphorShellPicker::RetintController;
using PhosphorTheme::PaletteStore;
using PhosphorTheme::TokenNames;

// The runner's contract, by name: what the controller invokes and what it
// connects to. No subprocess.
class FakeRunner : public QObject
{
    Q_OBJECT

public:
    QStringList runs;
    int cancels = 0;

    Q_INVOKABLE void run(const QString& path)
    {
        runs.append(path);
    }
    Q_INVOKABLE void cancel()
    {
        ++cancels;
    }

    void finish(const QString& path, const QVariantMap& tokens)
    {
        Q_EMIT paletteReady(tokens, path);
    }

Q_SIGNALS:
    void paletteReady(const QVariantMap& tokens, const QString& wallpaperPath);
    void failed(const QString& wallpaperPath, const QString& reason);
};

namespace {

constexpr int kDebounce = 20;

QVariantMap brandStops(const PaletteStore& store)
{
    QVariantMap stops;
    for (const char* key :
         {TokenNames::BrandStop0, TokenNames::BrandStop1, TokenNames::BrandStop2, TokenNames::BrandStop3}) {
        stops.insert(QString::fromLatin1(key), store.token(QString::fromLatin1(key)));
    }
    return stops;
}

} // namespace

class TestRetintController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void previewDebouncesToTheLastCandidate()
    {
        FakeRunner runner;
        PaletteStore store;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);

        retint.preview(QStringLiteral("/a.png"));
        retint.preview(QStringLiteral("/b.png"));
        retint.preview(QStringLiteral("/c.png"));
        QVERIFY(runner.runs.isEmpty());
        QVERIFY(retint.isPreviewing());
        QTRY_COMPARE(runner.runs.size(), 1);
        QCOMPARE(runner.runs.first(), QStringLiteral("/c.png"));
        // The previous run is cancelled before each new run.
        QCOMPARE(runner.cancels, 1);
        QVERIFY(retint.isBusy());
    }

    void newPreviewCancelsTheInFlightRun()
    {
        FakeRunner runner;
        PaletteStore store;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);

        retint.preview(QStringLiteral("/a.png"));
        QTRY_COMPARE(runner.runs.size(), 1);
        retint.preview(QStringLiteral("/b.png"));
        QTRY_COMPARE(runner.runs.size(), 2);
        QCOMPARE(runner.cancels, 2);

        // A late result for the abandoned candidate never lands.
        QSignalSpy applied(&retint, &RetintController::previewApplied);
        runner.finish(QStringLiteral("/a.png"),
                      {{QString::fromLatin1(TokenNames::Primary), QColor(QStringLiteral("#123456"))}});
        QCOMPARE(applied.count(), 0);
        QVERIFY(store.token(QString::fromLatin1(TokenNames::Primary)) != QColor(QStringLiteral("#123456")));

        runner.finish(QStringLiteral("/b.png"),
                      {{QString::fromLatin1(TokenNames::Primary), QColor(QStringLiteral("#654321"))}});
        QCOMPARE(applied.count(), 1);
        QCOMPARE(store.token(QString::fromLatin1(TokenNames::Primary)), QColor(QStringLiteral("#654321")));
        QCOMPARE(retint.previewPath(), QStringLiteral("/b.png"));
        QVERIFY(!retint.isBusy());
    }

    void brandStopsSurviveAPreviewThroughTheRealStore()
    {
        FakeRunner runner;
        PaletteStore store;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);
        const QVariantMap before = brandStops(store);
        QCOMPARE(before.size(), 4);

        retint.preview(QStringLiteral("/w.png"));
        QTRY_COMPARE(runner.runs.size(), 1);
        // A palette that tries to move every brand stop, plus a real token.
        QVariantMap tokens{
            {QString::fromLatin1(TokenNames::BrandStop0), QColor(QStringLiteral("#ff0000"))},
            {QString::fromLatin1(TokenNames::BrandStop1), QColor(QStringLiteral("#00ff00"))},
            {QString::fromLatin1(TokenNames::BrandStop2), QColor(QStringLiteral("#0000ff"))},
            {QString::fromLatin1(TokenNames::BrandStop3), QColor(QStringLiteral("#ffffff"))},
            {QString::fromLatin1(TokenNames::Surface), QColor(QStringLiteral("#101010"))},
        };
        runner.finish(QStringLiteral("/w.png"), tokens);
        QCOMPARE(store.token(QString::fromLatin1(TokenNames::Surface)), QColor(QStringLiteral("#101010")));
        QCOMPARE(brandStops(store), before);

        // Same through the theme-tile path.
        retint.previewTokens(tokens, QStringLiteral("Tile"));
        QCOMPARE(brandStops(store), before);

        // And the static helper strips exactly those four.
        const QVariantMap stripped = RetintController::withoutBrandStops(tokens);
        QCOMPARE(stripped.size(), 1);
        QVERIFY(stripped.contains(QString::fromLatin1(TokenNames::Surface)));
    }

    void clearPreviewRestoresTheSnapshot()
    {
        FakeRunner runner;
        PaletteStore store;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);
        const QVariantMap original = store.palette();

        retint.preview(QStringLiteral("/w.png"));
        QTRY_COMPARE(runner.runs.size(), 1);
        runner.finish(QStringLiteral("/w.png"),
                      {{QString::fromLatin1(TokenNames::Primary), QColor(QStringLiteral("#abcdef"))},
                       {QString::fromLatin1(TokenNames::OnSurface), QColor(QStringLiteral("#fedcba"))},
                       // A key the palette does not publish is never added.
                       {QStringLiteral("surface_tint"), QColor(QStringLiteral("#111111"))}});
        QVERIFY(store.palette() != original);
        QVERIFY(!store.palette().contains(QStringLiteral("surface_tint")));

        QSignalSpy changed(&store, &PaletteStore::paletteChanged);
        retint.clearPreview();
        QCOMPARE(store.palette(), original);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!retint.isPreviewing());
        QVERIFY(retint.previewPath().isEmpty());

        // Clearing again is a no-op.
        retint.clearPreview();
        QCOMPARE(changed.count(), 1);
    }

    void clearPreviewWhilePendingCancelsTheRunner()
    {
        FakeRunner runner;
        PaletteStore store;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);
        const QVariantMap original = store.palette();

        retint.preview(QStringLiteral("/w.png"));
        QTRY_COMPARE(runner.runs.size(), 1);
        retint.clearPreview();
        QCOMPARE(runner.cancels, 2);
        QVERIFY(!retint.isBusy());
        // The run it cancelled cannot land afterwards.
        runner.finish(QStringLiteral("/w.png"),
                      {{QString::fromLatin1(TokenNames::Primary), QColor(QStringLiteral("#abcdef"))}});
        QCOMPARE(store.palette(), original);
    }

    void commitKeepsTheLivePaletteAndPersists()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("palettes/current.json"));

        FakeRunner runner;
        PaletteStore store;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);
        retint.setPersistPath(path);

        retint.preview(QStringLiteral("/w.png"));
        QTRY_COMPARE(runner.runs.size(), 1);
        runner.finish(QStringLiteral("/w.png"),
                      {{QString::fromLatin1(TokenNames::Primary), QColor(QStringLiteral("#abcdef"))}});
        retint.commit();
        QVERIFY(!retint.isPreviewing());
        QCOMPARE(store.token(QString::fromLatin1(TokenNames::Primary)), QColor(QStringLiteral("#abcdef")));
        // A clear after a commit has nothing to restore.
        retint.clearPreview();
        QCOMPARE(store.token(QString::fromLatin1(TokenNames::Primary)), QColor(QStringLiteral("#abcdef")));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject tokens = root.value(QLatin1String("tokens")).toObject();
        QCOMPARE(tokens.value(QLatin1String(TokenNames::Primary)).toString(), QStringLiteral("#abcdef"));
        // The written file round-trips through the store's own loader.
        PaletteStore reader;
        QVERIFY(reader.loadFromFile(path));
        QCOMPARE(reader.token(QString::fromLatin1(TokenNames::Primary)), QColor(QStringLiteral("#abcdef")));
    }

    void commitWhileInFlightLetsTheRunLand()
    {
        FakeRunner runner;
        PaletteStore store;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);

        retint.preview(QStringLiteral("/w.png"));
        QTRY_COMPARE(runner.runs.size(), 1);
        retint.commit();
        QVERIFY(!retint.isPreviewing());
        runner.finish(QStringLiteral("/w.png"),
                      {{QString::fromLatin1(TokenNames::Primary), QColor(QStringLiteral("#abcdef"))}});
        QCOMPARE(store.token(QString::fromLatin1(TokenNames::Primary)), QColor(QStringLiteral("#abcdef")));
        QCOMPARE(retint.previewPath(), QStringLiteral("/w.png"));
    }

    void runnerFailureIsReportedAndDoesNotTouchTheStore()
    {
        FakeRunner runner;
        PaletteStore store;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);
        const QVariantMap original = store.palette();

        QSignalSpy failed(&retint, &RetintController::previewFailed);
        retint.preview(QStringLiteral("/w.png"));
        QTRY_COMPARE(runner.runs.size(), 1);
        Q_EMIT runner.failed(QStringLiteral("/w.png"), QStringLiteral("matugen not found"));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(retint.lastError(), QStringLiteral("matugen not found"));
        QVERIFY(!retint.isBusy());
        QCOMPARE(store.palette(), original);
    }

    void previewWithoutARunnerFails()
    {
        PaletteStore store;
        RetintController retint;
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);
        QSignalSpy failed(&retint, &RetintController::previewFailed);
        retint.preview(QStringLiteral("/w.png"));
        QTRY_COMPARE(failed.count(), 1);
        // The refusal must not leave the strip reading "retinting".
        QVERIFY(!retint.isBusy());
    }

    // A runner that goes away with a run in flight. `busy` drives a
    // user-visible label and both preview() guards, so latching it true
    // strands the strip: it reads "retinting" for the life of the controller
    // and every later hover is refused.
    void aRunnerLostMidRunDoesNotLatchBusy()
    {
        PaletteStore store;
        RetintController retint;
        retint.setStore(&store);
        retint.setDebounceMs(kDebounce);

        {
            FakeRunner runner;
            retint.setRunner(&runner);
            retint.preview(QStringLiteral("/a.png"));
            QTRY_COMPARE(runner.runs.size(), 1);
            QVERIFY(retint.isBusy());
        }
        // The runner is gone; m_runner is a QPointer and is now null.
        retint.clearPreview();
        QVERIFY(!retint.isBusy());

        // And the controller is still usable: a new runner takes a run.
        FakeRunner replacement;
        retint.setRunner(&replacement);
        QVERIFY(!retint.isBusy());
        retint.preview(QStringLiteral("/b.png"));
        QTRY_COMPARE(replacement.runs.size(), 1);
    }

    // Swapping the store must drop the snapshot taken from the previous one,
    // or clearPreview() writes the old store's tokens into the new store.
    void swappingTheStoreDropsTheOldSnapshot()
    {
        PaletteStore first;
        PaletteStore second;
        FakeRunner runner;
        RetintController retint;
        retint.setRunner(&runner);
        retint.setStore(&first);
        retint.setDebounceMs(kDebounce);

        retint.previewTokens({{QStringLiteral("primary"), QColor(Qt::red)}}, QStringLiteral("Ember"));
        QVERIFY(retint.isPreviewing());

        retint.setStore(&second);
        QVERIFY(!retint.isPreviewing());
    }
};

QTEST_GUILESS_MAIN(TestRetintController)
#include "test_retintcontroller.moc"
