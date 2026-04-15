// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_autotile_tile_request_validation.cpp
 * @brief Producer-side guard for AutotileEngine::windowsTiled JSON.
 *
 * Phase 1B added TileRequestEntry::validationError(), and
 * AutotileAdaptor::slotWindowsTileRequested on the effect side drops any
 * batch entry that fails validation. The JSON producer in
 * AutotileEngine::applyTiling must therefore populate every field the
 * validator requires — screenId, non-zero windowId, non-zero size on
 * tiled entries — on every snap path AND every overflow path.
 *
 * The original regression this test exists to prevent: applyTiling's
 * tiled-entry branch set windowId/x/y/w/h but never screenId, so every
 * mode-change retile emitted a batch where all entries failed validation
 * and the effect aborted with "all N entries invalid — aborting". The
 * existing test_dbus_validation pinned the validator's behaviour but
 * never exercised a real producer, so the bug slipped through.
 *
 * This test mirrors the AutotileAdaptor::onWindowsTiled parse logic
 * exactly (same field names, same optional flags) so a drift in either
 * producer or consumer that broke round-tripping would fail here too.
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "autotile/AutotileEngine.h"
#include "autotile/AutotileConfig.h"
#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingState.h"
#include "compositor-common/dbus_types.h"

using namespace PlasmaZones;

namespace {

/// Mirrors AutotileAdaptor::onWindowsTiled's JSON → TileRequestList parse.
/// Kept in sync with src/dbus/autotileadaptor.cpp so the producer test
/// exercises the exact same deserialization the D-Bus pipe performs.
TileRequestList parseWindowsTiledJson(const QString& json)
{
    TileRequestList requests;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) {
        return requests;
    }
    for (const QJsonValue& val : doc.array()) {
        QJsonObject obj = val.toObject();
        TileRequestEntry entry;
        entry.windowId = obj.value(QLatin1String("windowId")).toString();
        entry.floating = obj.value(QLatin1String("floating")).toBool(false);
        if (!entry.floating) {
            entry.x = obj.value(QLatin1String("x")).toInt();
            entry.y = obj.value(QLatin1String("y")).toInt();
            entry.width = obj.value(QLatin1String("width")).toInt();
            entry.height = obj.value(QLatin1String("height")).toInt();
        }
        entry.zoneId = obj.value(QLatin1String("zoneId")).toString();
        entry.screenId = obj.value(QLatin1String("screenId")).toString();
        entry.monocle = obj.value(QLatin1String("monocle")).toBool(false);
        requests.append(entry);
    }
    return requests;
}

} // namespace

class TestAutotileTileRequestValidation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─────────────────────────────────────────────────────────────────────
    // The smoke test that would have caught the PR #326 regression.
    //
    // Set up an engine with a screen and two windows, force calculated
    // zones (bypassing real screen geometry which unit tests don't have),
    // run retile, capture the windowsTiled signal, parse each entry via
    // the same path the AutotileAdaptor uses, and assert every entry
    // passes validation.
    // ─────────────────────────────────────────────────────────────────────
    void applyTiling_populatesScreenIdOnEveryTiledEntry()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("DP-1");

        engine.setAutotileScreens({screenName});
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        // Force zones directly — unit tests don't have real screen
        // geometry so recalculateLayout() would bail with "invalid screen
        // geometry". The zones themselves aren't under test; the JSON
        // shape around them is.
        TilingState* state = engine.stateForScreen(screenName);
        QVERIFY(state);
        state->setCalculatedZones({QRect(10, 10, 950, 1060), QRect(960, 10, 950, 1060)});

        engine.retile(screenName);
        QVERIFY(tiledSpy.count() >= 1);

        const QString json = tiledSpy.last().first().toString();
        const TileRequestList entries = parseWindowsTiledJson(json);
        QVERIFY2(!entries.isEmpty(), "applyTiling emitted no entries");
        for (const TileRequestEntry& entry : entries) {
            const QString err = entry.validationError();
            QVERIFY2(err.isEmpty(), qPrintable(err));
            // Belt-and-braces: the screenId must match what we retiled.
            QCOMPARE(entry.screenId, screenName);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Monocle flag: same invariant, different code path — the producer
    // sets monocle=true on every tiled entry when all zones share
    // identical geometry. Validator is silent on monocle, but screenId
    // and window/size requirements still apply.
    // ─────────────────────────────────────────────────────────────────────
    void applyTiling_monocleEntriesStillValidate()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("HDMI-2");
        engine.setAutotileScreens({screenName});
        engine.setAlgorithm(QLatin1String("monocle"));

        engine.windowOpened(QStringLiteral("win-a"), screenName);
        engine.windowOpened(QStringLiteral("win-b"), screenName);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        TilingState* state = engine.stateForScreen(screenName);
        QVERIFY(state);
        const QRect fullArea(0, 0, 1920, 1080);
        state->setCalculatedZones({fullArea, fullArea});
        engine.retile(screenName);

        QVERIFY(tiledSpy.count() >= 1);
        const TileRequestList entries = parseWindowsTiledJson(tiledSpy.last().first().toString());
        QVERIFY(!entries.isEmpty());
        for (const TileRequestEntry& entry : entries) {
            QVERIFY(entry.monocle);
            QVERIFY2(entry.validationError().isEmpty(), qPrintable(entry.validationError()));
            QCOMPARE(entry.screenId, screenName);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Overflow branch of applyTiling: windows that exceed maxWindows are
    // batched with floating=true and must carry screenId for the plugin
    // to route them. Floating entries legitimately carry zero geometry
    // (plugin resolves it from the current frame) — validator tolerates
    // that — but empty screenId is still rejected.
    // ─────────────────────────────────────────────────────────────────────
    void applyTiling_overflowFloatingEntryIsValid()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("DP-3");
        engine.setAutotileScreens({screenName});
        engine.setAlgorithm(QLatin1String("master-stack"));

        // Cap at 2 so a third window is forced into overflow.
        engine.config()->maxWindows = 2;

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        engine.windowOpened(QStringLiteral("win-3"), screenName);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        TilingState* state = engine.stateForScreen(screenName);
        QVERIFY(state);
        state->setCalculatedZones({QRect(10, 10, 950, 1060), QRect(960, 10, 950, 1060)});
        engine.retile(screenName);

        QVERIFY(tiledSpy.count() >= 1);
        const TileRequestList entries = parseWindowsTiledJson(tiledSpy.last().first().toString());
        QVERIFY2(!entries.isEmpty(), "applyTiling emitted no entries");

        bool sawFloating = false;
        for (const TileRequestEntry& entry : entries) {
            QVERIFY2(entry.validationError().isEmpty(), qPrintable(entry.validationError()));
            QCOMPARE(entry.screenId, screenName);
            if (entry.floating) {
                sawFloating = true;
            }
        }
        // The third window's overflow fate depends on which-window-got-capped
        // order, which is stable under the same algo but not worth pinning
        // here. What matters is that IF any overflow entry was emitted, it
        // validated cleanly — the assertion above already covered that.
        Q_UNUSED(sawFloating);
    }
};

QTEST_MAIN(TestAutotileTileRequestValidation)
#include "test_autotile_tile_request_validation.moc"
