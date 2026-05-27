// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/MatugenRunner.h>

#include <QColor>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QUrl>
#include <QVariantMap>

using namespace PhosphorTheme;

// Matugen integration is exercised via parseMatugenJson with prebaked
// JSON fixtures. We do NOT spawn matugen in tests, that would either
// require matugen on PATH (CI flake) or a mock binary (more code than
// the parser itself).

class TestMatugenRunner : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parse_handlesWrappedDarkLight();
    void parse_handlesSingleModeColorsObject();
    void parse_handlesBareModeAtRoot();
    void parse_rejectsMalformed();
    void parse_ignoresNonStringValues();
    void parse_handlesV4PerTokenNesting();
    void parse_v4FallsBackToDefaultWhenModeMissing();
    void run_emitsFailedOnMissingWallpaper();
    void runUrl_rejectsNonLocalUrl();
    void runUrl_routesLocalUrlThroughStringOverload();
    void run_normalisesRelativePathToAbsolute();
    void run_syncFailDoesNotEmitDoubleRunningChanged();
    void cancel_onIdleRunnerIsNoOp();
    void setters_onlyEmitOnChange();
};

void TestMatugenRunner::parse_handlesWrappedDarkLight()
{
    MatugenRunner r;
    const QByteArray payload = R"({
        "colors": {
            "dark":  { "primary": "#112233", "on_primary": "#FFFFFF" },
            "light": { "primary": "#AABBCC", "on_primary": "#000000" }
        }
    })";
    const auto dark = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(dark.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));

    const auto light = r.parseMatugenJson(payload, QStringLiteral("light"));
    QCOMPARE(light.value(QStringLiteral("primary")).value<QColor>(), QColor("#AABBCC"));
}

void TestMatugenRunner::parse_handlesSingleModeColorsObject()
{
    MatugenRunner r;
    // Older matugen schema: colors map directly under "colors", no
    // dark/light wrapper. Treat it as the requested mode.
    const QByteArray payload = R"({"colors": {"primary": "#112233"}})";
    const auto m = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(m.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));
}

void TestMatugenRunner::parse_handlesBareModeAtRoot()
{
    MatugenRunner r;
    const QByteArray payload = R"({"dark": {"primary": "#112233"}})";
    const auto m = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(m.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));
}

void TestMatugenRunner::parse_rejectsMalformed()
{
    MatugenRunner r;
    QVERIFY(r.parseMatugenJson("not json", QStringLiteral("dark")).isEmpty());
    QVERIFY(r.parseMatugenJson("[]", QStringLiteral("dark")).isEmpty());
}

void TestMatugenRunner::parse_ignoresNonStringValues()
{
    MatugenRunner r;
    const QByteArray payload = R"({
        "colors": { "dark": { "primary": "#112233", "version": 1, "junk": null } }
    })";
    const auto m = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(m.size(), 1);
    QCOMPARE(m.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));
}

void TestMatugenRunner::parse_handlesV4PerTokenNesting()
{
    // matugen v4+ shape: each token is its own object containing
    // `dark` / `light` / `default` sub-keys, each holding `{ "color": "..." }`.
    MatugenRunner r;
    const QByteArray payload = R"({
        "colors": {
            "primary": {
                "dark":    { "color": "#112233" },
                "light":   { "color": "#AABBCC" },
                "default": { "color": "#112233" }
            },
            "background": {
                "dark":    { "color": "#000000" },
                "light":   { "color": "#FFFFFF" },
                "default": { "color": "#000000" }
            }
        }
    })";
    const auto dark = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(dark.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));
    QCOMPARE(dark.value(QStringLiteral("background")).value<QColor>(), QColor("#000000"));

    const auto light = r.parseMatugenJson(payload, QStringLiteral("light"));
    QCOMPARE(light.value(QStringLiteral("primary")).value<QColor>(), QColor("#AABBCC"));
    QCOMPARE(light.value(QStringLiteral("background")).value<QColor>(), QColor("#FFFFFF"));
}

void TestMatugenRunner::parse_v4FallsBackToDefaultWhenModeMissing()
{
    // If the requested mode isn't present for a token, fall back to
    // `default` rather than dropping the token outright. Hardens us
    // against future matugen schema changes that might omit a mode.
    MatugenRunner r;
    const QByteArray payload = R"({
        "colors": {
            "primary": { "default": { "color": "#445566" } }
        }
    })";
    const auto m = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(m.value(QStringLiteral("primary")).value<QColor>(), QColor("#445566"));
}

void TestMatugenRunner::run_emitsFailedOnMissingWallpaper()
{
    // Path-validation runs synchronously before spawning matugen, so
    // this test doesn't depend on the binary being installed.
    MatugenRunner r;
    QSignalSpy failedSpy(&r, &MatugenRunner::failed);
    QSignalSpy runSpy(&r, &MatugenRunner::runningChanged);
    r.run(QStringLiteral("/definitely/does/not/exist.jpg"));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toString(), QStringLiteral("/definitely/does/not/exist.jpg"));
    QVERIFY(!r.isRunning());
    // The sync-fail path returns before constructing m_process, so
    // runningChanged must not fire.
    QCOMPARE(runSpy.count(), 0);
}

void TestMatugenRunner::runUrl_rejectsNonLocalUrl()
{
    // The QUrl overload is the QML entry point. A non-file URL must be
    // rejected via the failed signal so a passing-but-wrong QML call
    // surfaces visibly instead of spawning matugen with a malformed
    // argument.
    MatugenRunner r;
    QSignalSpy failedSpy(&r, &MatugenRunner::failed);
    QSignalSpy runSpy(&r, &MatugenRunner::runningChanged);
    r.run(QUrl(QStringLiteral("https://example.com/img.jpg")));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toString(), QStringLiteral("https://example.com/img.jpg"));
    QVERIFY(failedSpy.first().at(1).toString().contains(QStringLiteral("not a local file")));
    QCOMPARE(runSpy.count(), 0);
    QVERIFY(!r.isRunning());
}

void TestMatugenRunner::runUrl_routesLocalUrlThroughStringOverload()
{
    // A local-file URL pointing at a missing path must route through the
    // string overload and produce the same "wallpaper does not exist"
    // failure as a raw missing path, with the converted local path in
    // the signal payload (not the URL string).
    MatugenRunner r;
    QSignalSpy failedSpy(&r, &MatugenRunner::failed);
    const QString missing = QStringLiteral("/definitely/does/not/exist-url.jpg");
    r.run(QUrl::fromLocalFile(missing));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toString(), missing);
}

void TestMatugenRunner::run_normalisesRelativePathToAbsolute()
{
    // Flag-injection hardening: a wallpaper named "-foo.jpg" passed as
    // a relative path would otherwise be parsed by matugen's clap as a
    // flag. run() canonicalises to an absolute "/..." path before the
    // subprocess sees it. We can observe this only through the failed
    // signal's payload, since the sync-success path requires a real
    // binary, so we point matugenBinary at /nonexistent to force a
    // synchronous FailedToStart and inspect the path that was passed.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString fileName = QStringLiteral("-flag-shaped-name.jpg");
    const QString absolutePath = tmp.filePath(fileName);
    {
        QFile f(absolutePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a real image");
    }

    MatugenRunner r;
    r.setMatugenBinary(QStringLiteral("/nonexistent/binary/matugen"));
    QSignalSpy failedSpy(&r, &MatugenRunner::failed);

    // Pass relative path; chdir to the temp dir so the relative form
    // resolves correctly. Restore cwd at scope end.
    const QString originalCwd = QDir::currentPath();
    QVERIFY(QDir::setCurrent(tmp.path()));
    r.run(QStringLiteral("./") + fileName);
    QVERIFY(QDir::setCurrent(originalCwd));

    QVERIFY(failedSpy.wait(2000) || failedSpy.count() >= 1);
    QVERIFY(failedSpy.count() >= 1);
    const QString reported = failedSpy.first().at(0).toString();
    QVERIFY2(reported.startsWith(QLatin1Char('/')),
             qPrintable(QStringLiteral("expected absolute path, got '%1'").arg(reported)));
    QVERIFY(reported.endsWith(fileName));
}

void TestMatugenRunner::run_syncFailDoesNotEmitDoubleRunningChanged()
{
    // runningChanged contract. A run against a missing binary flips
    // state to Starting synchronously inside QProcess::start() and then
    // back to NotRunning asynchronously when Qt delivers errorOccurred.
    // With the edge-trigger tracking in place we get exactly TWO emits.
    //   1. false to true after start() returns (state == Starting)
    //   2. true to false after errorOccurred runs disposeProcess
    // An even-count assertion would pass a never-emit regression with
    // count == 0; a count == 1 catches the original stray-emit bug; a
    // count > 2 catches the spurious-edge regression. The right shape
    // is exactly 2.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString wallpaper = tmp.filePath(QStringLiteral("w.jpg"));
    {
        QFile f(wallpaper);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a real image");
    }

    MatugenRunner r;
    r.setMatugenBinary(QStringLiteral("/nonexistent/binary/matugen"));
    QSignalSpy runSpy(&r, &MatugenRunner::runningChanged);
    QSignalSpy failedSpy(&r, &MatugenRunner::failed);

    r.run(wallpaper);
    QVERIFY(failedSpy.wait(2000) || failedSpy.count() >= 1);
    QVERIFY(failedSpy.count() >= 1);
    QCOMPARE(runSpy.count(), 2);
    QVERIFY(!r.isRunning());
}

void TestMatugenRunner::cancel_onIdleRunnerIsNoOp()
{
    // cancel() on an idle runner must not spuriously emit
    // runningChanged. Spurious signals would invalidate UI busy
    // indicators bound to the property.
    MatugenRunner r;
    QSignalSpy runSpy(&r, &MatugenRunner::runningChanged);
    r.cancel();
    QCOMPARE(runSpy.count(), 0);
}

void TestMatugenRunner::setters_onlyEmitOnChange()
{
    MatugenRunner r;
    QSignalSpy binSpy(&r, &MatugenRunner::matugenBinaryChanged);
    QSignalSpy modeSpy(&r, &MatugenRunner::modeChanged);
    QSignalSpy prefSpy(&r, &MatugenRunner::preferChanged);

    // Same value is a no-op (CLAUDE.md "only emit signals when value
    // actually changes").
    r.setMatugenBinary(r.matugenBinary());
    r.setMode(r.mode());
    r.setPrefer(r.prefer());
    QCOMPARE(binSpy.count(), 0);
    QCOMPARE(modeSpy.count(), 0);
    QCOMPARE(prefSpy.count(), 0);

    r.setMatugenBinary(QStringLiteral("/usr/local/bin/matugen"));
    r.setMode(QStringLiteral("light"));
    r.setPrefer(QStringLiteral("darkness"));
    QCOMPARE(binSpy.count(), 1);
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(prefSpy.count(), 1);
}

QTEST_MAIN(TestMatugenRunner)
#include "test_matugenrunner.moc"
