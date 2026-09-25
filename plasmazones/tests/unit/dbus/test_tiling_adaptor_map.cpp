// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_adaptor_map.cpp
 *
 * Pins the placement-map surfaces of TilingAdaptor, the ones the Phosphor
 * shell's map rebuilds a screen from and follows focus through:
 *
 *  1. currentTilesJson is "[]" before any batch, replays the last batch
 *     that named a screen restricted to that screen's entries with the keys
 *     the relay parses, is replaced per screen by a newer batch, prunes a
 *     window on close, and is dropped by clearEngine and by a screen leaving
 *     the managed set.
 *  2. focusedWindowChanged emits ONCE per actual change of the engine's
 *     managed focus, across every hook that re-reads it (the focus report,
 *     the engine-driven activation, a close, the coalesced announce), and
 *     managedFocusedWindow answers the live value with strict ownership.
 *
 * The focus half runs against a REAL ScrollEngine with headless geometry
 * providers, wired the way init_engines.cpp wires it (activation and
 * placementChanged forwarded to the adaptor's signals, and the compositor's
 * answering focus report simulated through notifyWindowFocused), because
 * the gate's whole job is to coalesce those overlapping hooks.
 *
 * NOTE: the adaptor is parented to a plain QObject rather than a
 * D-Bus-registered object, same as its sibling suites.
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include "dbus/tilingadaptor/tilingadaptor.h"

using namespace PlasmaZones;

namespace {

QString batchJson(const QList<QJsonObject>& entries)
{
    QJsonArray arr;
    for (const QJsonObject& obj : entries) {
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QJsonObject tiled(const QString& windowId, const QString& screenId, int x, int y, int w, int h)
{
    QJsonObject obj;
    obj[QLatin1String("windowId")] = windowId;
    obj[QLatin1String("screenId")] = screenId;
    obj[QLatin1String("x")] = x;
    obj[QLatin1String("y")] = y;
    obj[QLatin1String("width")] = w;
    obj[QLatin1String("height")] = h;
    return obj;
}

QJsonObject floating(const QString& windowId, const QString& screenId)
{
    QJsonObject obj;
    obj[QLatin1String("windowId")] = windowId;
    obj[QLatin1String("screenId")] = screenId;
    obj[QLatin1String("floating")] = true;
    return obj;
}

QJsonArray parseArray(const QString& json)
{
    return QJsonDocument::fromJson(json.toUtf8()).array();
}

} // namespace

class TestTilingAdaptorMap : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testCurrentTilesJson_replaysLastBatchPerScreen()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);

        // Nothing relayed yet, and an empty screen id, both answer "[]".
        QCOMPARE(adaptor.currentTilesJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
        QCOMPARE(adaptor.currentTilesJson(QString()), QStringLiteral("[]"));

        QSignalSpy wire(&adaptor, &TilingAdaptor::windowsTileRequested);
        QVERIFY(wire.isValid());

        // One batch naming two screens: the replay is per screen, restricted
        // to that screen's entries, and carries the parser's keys.
        adaptor.relayTileRequestsJson(
            batchJson({tiled(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0, 600, 760),
                       tiled(QStringLiteral("app|b"), QStringLiteral("DP-1"), 600, 0, 600, 760),
                       floating(QStringLiteral("app|c"), QStringLiteral("DP-2"))}));
        QCOMPARE(wire.count(), 1);

        const QJsonArray dp1 = parseArray(adaptor.currentTilesJson(QStringLiteral("DP-1")));
        QCOMPARE(dp1.size(), 2);
        const QJsonObject a = dp1.at(0).toObject();
        QCOMPARE(a.value(QLatin1String("windowId")).toString(), QStringLiteral("app|a"));
        QCOMPARE(a.value(QLatin1String("x")).toInt(), 0);
        QCOMPARE(a.value(QLatin1String("y")).toInt(), 0);
        QCOMPARE(a.value(QLatin1String("width")).toInt(), 600);
        QCOMPARE(a.value(QLatin1String("height")).toInt(), 760);
        QCOMPARE(a.value(QLatin1String("screenId")).toString(), QStringLiteral("DP-1"));
        QCOMPARE(a.value(QLatin1String("monocle")).toBool(), false);
        QCOMPARE(a.value(QLatin1String("floating")).toBool(), false);
        QVERIFY(a.contains(QLatin1String("zoneId")));
        QVERIFY(a.value(QLatin1String("zoneId")).toString().isEmpty());
        QCOMPARE(dp1.at(1).toObject().value(QLatin1String("windowId")).toString(), QStringLiteral("app|b"));
        QCOMPARE(dp1.at(1).toObject().value(QLatin1String("x")).toInt(), 600);

        // The floating entry replays as floating with a zero rect, one shape
        // per entry so a reader branches on `floating` alone.
        const QJsonArray dp2 = parseArray(adaptor.currentTilesJson(QStringLiteral("DP-2")));
        QCOMPARE(dp2.size(), 1);
        const QJsonObject c = dp2.at(0).toObject();
        QCOMPARE(c.value(QLatin1String("windowId")).toString(), QStringLiteral("app|c"));
        QCOMPARE(c.value(QLatin1String("floating")).toBool(), true);
        QCOMPARE(c.value(QLatin1String("width")).toInt(), 0);
        QCOMPARE(c.value(QLatin1String("height")).toInt(), 0);

        // A newer batch naming only DP-1 REPLACES DP-1 and leaves DP-2 alone.
        adaptor.relayTileRequestsJson(
            batchJson({tiled(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0, 1200, 760)}));
        QCOMPARE(wire.count(), 2);
        const QJsonArray dp1Next = parseArray(adaptor.currentTilesJson(QStringLiteral("DP-1")));
        QCOMPARE(dp1Next.size(), 1);
        QCOMPARE(dp1Next.at(0).toObject().value(QLatin1String("width")).toInt(), 1200);
        QCOMPARE(parseArray(adaptor.currentTilesJson(QStringLiteral("DP-2"))).size(), 1);

        // A screen no batch ever named stays empty.
        QCOMPARE(adaptor.currentTilesJson(QStringLiteral("HDMI-1")), QStringLiteral("[]"));

        // Compact JSON, as every other payload on this surface.
        const QString payload = adaptor.currentTilesJson(QStringLiteral("DP-1"));
        QVERIFY2(!payload.contains(QLatin1Char('\n')) && !payload.contains(QLatin1Char(' ')),
                 qPrintable(QStringLiteral("payload is not compact JSON: %1").arg(payload)));
    }

    void testCurrentTilesJson_prunesClosedWindowsAndClears()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.relayTileRequestsJson(
            batchJson({tiled(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0, 600, 760),
                       tiled(QStringLiteral("app|b"), QStringLiteral("DP-1"), 600, 0, 600, 760),
                       tiled(QStringLiteral("app|c"), QStringLiteral("DP-2"), 0, 0, 800, 600)}));
        QCOMPARE(parseArray(adaptor.currentTilesJson(QStringLiteral("DP-1"))).size(), 2);

        // A close prunes the window from the replay even with NO pipeline:
        // the bookkeeping runs before the pipeline gate, like the dedup
        // evictions beside it.
        adaptor.windowClosed(QStringLiteral("app|a"));
        const QJsonArray afterClose = parseArray(adaptor.currentTilesJson(QStringLiteral("DP-1")));
        QCOMPARE(afterClose.size(), 1);
        QCOMPARE(afterClose.at(0).toObject().value(QLatin1String("windowId")).toString(), QStringLiteral("app|b"));

        // The other close paths prune the same way.
        adaptor.releaseWindowTracking(QStringLiteral("app|b"));
        QCOMPARE(adaptor.currentTilesJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
        adaptor.onTrackedWindowDestroyed(QStringLiteral("app|c"));
        QCOMPARE(adaptor.currentTilesJson(QStringLiteral("DP-2")), QStringLiteral("[]"));

        // Shutdown drops whatever is left.
        adaptor.relayTileRequestsJson(
            batchJson({tiled(QStringLiteral("app|d"), QStringLiteral("DP-1"), 0, 0, 600, 760)}));
        QCOMPARE(parseArray(adaptor.currentTilesJson(QStringLiteral("DP-1"))).size(), 1);
        adaptor.clearEngine();
        QCOMPARE(adaptor.currentTilesJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
    }

    void testFocusedWindowChanged_emitsOncePerChange()
    {
        // A real scroll engine with headless geometry, wired like the
        // composition root wires it: activation and placementChanged forward
        // to the adaptor's own signals, and the compositor's answering focus
        // report comes back through notifyWindowFocused.
        PhosphorScrollEngine::ScrollEngine engine(nullptr, nullptr);
        engine.setScreenGeometryProviders(
            [](const QString&) {
                return QRect(0, 40, 1200, 760);
            },
            [](const QString&) {
                return QRect(0, 0, 1200, 800);
            });
        engine.setActiveScreens({QStringLiteral("DP-1")});

        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        connect(&engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, &adaptor,
                &TilingAdaptor::focusWindowRequested);
        connect(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged, &adaptor,
                &TilingAdaptor::tilingChanged);
        connect(&engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, &adaptor,
                [&](const QString& windowId) {
                    adaptor.notifyWindowFocused(windowId, engine.screenForTrackedWindow(windowId));
                });

        QSignalSpy spy(&adaptor, &TilingAdaptor::focusedWindowChanged);
        QVERIFY(spy.isValid());

        // Nothing managed yet: the live read is empty, and so is a foreign
        // screen's (strict ownership, no primary fallback).
        QVERIFY(adaptor.managedFocusedWindow(QStringLiteral("DP-1")).isEmpty());
        QVERIFY(adaptor.managedFocusedWindow(QStringLiteral("HDMI-2")).isEmpty());
        QVERIFY(adaptor.managedFocusedWindow(QString()).isEmpty());

        // An open that takes focus: the activation hook, the placementChanged
        // hook and the simulated focus report ALL re-read, and one change
        // reaches the wire.
        adaptor.windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("DP-1"));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("app|a"));
        QCOMPARE(adaptor.managedFocusedWindow(QStringLiteral("DP-1")), QStringLiteral("app|a"));

        // A repeated focus report for the same window is not a change.
        adaptor.notifyWindowFocused(QStringLiteral("app|a"), QStringLiteral("DP-1"));
        QCOMPARE(spy.count(), 1);

        // A second open moves focus: exactly one more emission.
        adaptor.windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(1).toString(), QStringLiteral("app|b"));

        // A user focus report back to app|a: one emission, and the engine's
        // own answer agrees with the live read.
        adaptor.notifyWindowFocused(QStringLiteral("app|a"), QStringLiteral("DP-1"));
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(2).at(1).toString(), QStringLiteral("app|a"));
        QCOMPARE(adaptor.managedFocusedWindow(QStringLiteral("DP-1")),
                 engine.managedFocusedWindow(QStringLiteral("DP-1")));

        // Closing the focused window: focus falls to the survivor, announced
        // once even though the close path and the relayout both re-read.
        adaptor.windowClosed(QStringLiteral("app|a"));
        QCOMPARE(spy.count(), 4);
        QCOMPARE(spy.at(3).at(1).toString(), QStringLiteral("app|b"));

        // A foreign screen's report changes nothing on the wire.
        adaptor.notifyWindowFocused(QStringLiteral("app|b"), QStringLiteral("HDMI-2"));
        QCOMPARE(spy.count(), 4);

        // The screen leaves the managed set: the coalesced announce clears
        // the focus (one emission, empty id) and drops the screen's replay.
        adaptor.relayTileRequestsJson(
            batchJson({tiled(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 40, 1200, 760)}));
        QCOMPARE(parseArray(adaptor.currentTilesJson(QStringLiteral("DP-1"))).size(), 1);
        engine.setActiveScreens({});
        adaptor.notifyEngineScreensChanged(false);
        QTRY_COMPARE(spy.count(), 5);
        QCOMPARE(spy.at(4).at(0).toString(), QStringLiteral("DP-1"));
        QVERIFY(spy.at(4).at(1).toString().isEmpty());
        QCOMPARE(adaptor.currentTilesJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
        QVERIFY(adaptor.managedFocusedWindow(QStringLiteral("DP-1")).isEmpty());

        // A second announce with nothing changed stays silent.
        adaptor.notifyEngineScreensChanged(false);
        QTest::qWait(10);
        QCOMPARE(spy.count(), 5);

        adaptor.clearEngine();
    }
};

QTEST_GUILESS_MAIN(TestTilingAdaptorMap)
#include "test_tiling_adaptor_map.moc"
