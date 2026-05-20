// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_autotile_engine_pending_restore_filter.cpp
 * @brief AutotileEngine ShouldPersistRestorePredicate symmetry tests.
 *
 * Three filter points mirror the snap-side ShouldTrackPredicate (discussion #461 item 2):
 *   1. Live write gate in removeWindow() — closed-on-disabled-context never queues.
 *   2. Save-time filter in serializePendingRestores() — drops mid-session-disabled entries.
 *   3. Load-time filter in deserializePendingRestores() — drops entries from older
 *      daemons or contexts the user disabled while the daemon was off.
 *
 * Each test isolates a single filter point by clearing the predicate between
 * lifecycle and serialize/deserialize stages.
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "../helpers/AutotileTestHelpers.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

namespace {

QString makeComposite(const QString& appId, const QString& instanceId)
{
    return appId + QLatin1Char('|') + instanceId;
}

// Build a one-entry pending-restore JSON object the engine's deserialize will accept.
QJsonObject makePendingJson(const QString& appId, const QString& screenId, int desktop, const QString& activity)
{
    QJsonObject entry;
    entry[QLatin1String("position")] = 0;
    entry[QLatin1String("screen")] = screenId;
    entry[QLatin1String("desktop")] = desktop;
    entry[QLatin1String("activity")] = activity;
    entry[QLatin1String("wasFloating")] = false;

    QJsonArray queue;
    queue.append(entry);

    QJsonObject root;
    root[appId] = queue;
    return root;
}

} // namespace

class TestAutotileEnginePendingRestoreFilter : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ────────────────────────────────────────────────────────────────────
    // (1) Live write gate — removeWindow consults the predicate before
    //     appending. Predicate returns false → entry never queued → an
    //     immediate serialize sees nothing for that appId.
    // ────────────────────────────────────────────────────────────────────
    void liveGate_droppedWhenPredicateRejects()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        engine.setShouldPersistRestorePredicate([](const QString&, int, const QString&) -> bool {
            return false;
        });

        const QString windowId = makeComposite(QStringLiteral("firefox"), QStringLiteral("abc-1"));
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();
        engine.windowClosed(windowId);
        QCoreApplication::processEvents();

        // Clear the predicate so the save-time filter cannot also drop —
        // any absence in the JSON now reflects the live gate alone.
        engine.setShouldPersistRestorePredicate({});

        const QJsonObject serialized = engine.serializePendingRestores();
        QVERIFY2(serialized.isEmpty(), "Live gate failed to drop closed-on-disabled-context entry");
    }

    // ────────────────────────────────────────────────────────────────────
    // (1b) Live gate passes through when predicate accepts. The entry is
    //      queued normally — same path the no-predicate default takes.
    // ────────────────────────────────────────────────────────────────────
    void liveGate_keptWhenPredicateAccepts()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        engine.setShouldPersistRestorePredicate([](const QString&, int, const QString&) -> bool {
            return true;
        });

        const QString windowId = makeComposite(QStringLiteral("firefox"), QStringLiteral("abc-2"));
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();
        engine.windowClosed(windowId);
        QCoreApplication::processEvents();

        engine.setShouldPersistRestorePredicate({});

        const QJsonObject serialized = engine.serializePendingRestores();
        QVERIFY2(serialized.contains(QStringLiteral("firefox")),
                 "Live gate dropped an enabled-context entry that should have been queued");
        const QJsonArray queue = serialized.value(QStringLiteral("firefox")).toArray();
        QCOMPARE(queue.size(), 1);
        QCOMPARE(queue.at(0).toObject().value(QLatin1String("screen")).toString(), screen);
    }

    // ────────────────────────────────────────────────────────────────────
    // (1c) Backward compat: with no predicate set, the engine treats every
    //      context as active — the historical behaviour unit tests rely on.
    // ────────────────────────────────────────────────────────────────────
    void liveGate_noPredicateKeepsEverything()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        const QString windowId = makeComposite(QStringLiteral("kate"), QStringLiteral("abc-3"));
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();
        engine.windowClosed(windowId);
        QCoreApplication::processEvents();

        const QJsonObject serialized = engine.serializePendingRestores();
        QVERIFY2(serialized.contains(QStringLiteral("kate")),
                 "Default (no predicate) must keep all entries — back-compat invariant");
    }

    // ────────────────────────────────────────────────────────────────────
    // (2) Save-time filter — predicate set AFTER the close, before
    //     serialize. The entry sat in memory during the session and is
    //     dropped only when the user disabled the context mid-session.
    // ────────────────────────────────────────────────────────────────────
    void serializeFilter_dropsEntryWhenPredicateRejects()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        // No predicate while closing — entry enters m_pendingAutotileRestores.
        const QString windowId = makeComposite(QStringLiteral("firefox"), QStringLiteral("abc-4"));
        engine.windowOpened(windowId, screen);
        QCoreApplication::processEvents();
        engine.windowClosed(windowId);
        QCoreApplication::processEvents();

        // Sanity: entry IS in the queue when no predicate is set.
        QVERIFY(engine.serializePendingRestores().contains(QStringLiteral("firefox")));

        // Now disable the context. Serialize must drop the in-memory entry.
        engine.setShouldPersistRestorePredicate([&screen](const QString& s, int, const QString&) -> bool {
            return s != screen;
        });

        const QJsonObject serialized = engine.serializePendingRestores();
        QVERIFY2(serialized.isEmpty(), "Save-time filter failed to drop entry on now-disabled context");
    }

    // ────────────────────────────────────────────────────────────────────
    // (3) Load-time filter — JSON authored by an older daemon (or a
    //     session before the user disabled the context) is dropped on
    //     deserialize. Verify by serializing back: the dropped entries
    //     must not appear.
    // ────────────────────────────────────────────────────────────────────
    void deserializeFilter_dropsEntryWhenPredicateRejects()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString keptScreen = QStringLiteral("DP-1");
        const QString droppedScreen = QStringLiteral("HDMI-A-1");

        // Predicate rejects droppedScreen, accepts keptScreen.
        engine.setShouldPersistRestorePredicate([&droppedScreen](const QString& s, int, const QString&) -> bool {
            return s != droppedScreen;
        });

        QJsonObject root;
        root.insert(QStringLiteral("kept"),
                    makePendingJson(QStringLiteral("kept"), keptScreen, 1, QString()).value(QStringLiteral("kept")));
        root.insert(
            QStringLiteral("dropped"),
            makePendingJson(QStringLiteral("dropped"), droppedScreen, 1, QString()).value(QStringLiteral("dropped")));

        engine.deserializePendingRestores(root);

        // Clear the predicate before serializing back so the assertion
        // measures only what survived the load-time filter.
        engine.setShouldPersistRestorePredicate({});

        const QJsonObject roundtrip = engine.serializePendingRestores();
        QVERIFY2(roundtrip.contains(QStringLiteral("kept")), "Load-time filter dropped an enabled-context entry");
        QVERIFY2(!roundtrip.contains(QStringLiteral("dropped")),
                 "Load-time filter failed to drop a disabled-context entry");
    }

    // ────────────────────────────────────────────────────────────────────
    // (3b) Activity field threads through the predicate. Snap's
    //      ShouldTrackPredicate has no activity slot because SnapState
    //      doesn't track it — autotile pending restores do, so the
    //      engine's predicate must forward it.
    // ────────────────────────────────────────────────────────────────────
    void deserializeFilter_activityIsForwardedToPredicate()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        QString observedActivity;
        engine.setShouldPersistRestorePredicate([&observedActivity](const QString&, int, const QString& a) -> bool {
            observedActivity = a;
            return true;
        });

        const QString expectedActivity = QStringLiteral("activity-uuid-1");
        engine.deserializePendingRestores(
            makePendingJson(QStringLiteral("kate"), QStringLiteral("DP-1"), 1, expectedActivity));

        QCOMPARE(observedActivity, expectedActivity);
    }
};

QTEST_MAIN(TestAutotileEnginePendingRestoreFilter)
#include "test_autotile_engine_pending_restore_filter.moc"
