// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_registry.cpp
 * @brief Unit tests for WindowRegistry — the single source of truth for
 *        compositor-supplied window identity and mutable metadata.
 *
 * Covers the core invariants:
 *  - instanceId is the primary key; appId/desktopFile/title are mutable attributes
 *  - upsert is idempotent (no-op + no signals when metadata unchanged)
 *  - metadataChanged fires with old and new records
 *  - appId mutation is the Emby scenario: instanceId stays, class swaps
 *  - appIdFor reflects the latest class
 *  - instancesWithAppId reverse index stays in sync across mutations and removes
 *  - remove emits windowDisappeared
 *  - clear emits windowDisappeared for every tracked id
 */

#include <PhosphorEngine/WindowRegistry.h>
#include <QSignalSpy>
#include <QTest>

#include <functional>

using PhosphorEngine::WindowMetadata;
using PhosphorEngine::WindowRegistry;

namespace {
WindowMetadata make(const QString& appId, const QString& desktopFile = {}, const QString& title = {})
{
    WindowMetadata m;
    m.appId = appId;
    m.desktopFile = desktopFile;
    m.title = title;
    return m;
}
} // namespace

class TestWindowRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ────────────────────────────────────────────────────────────────────
    // upsert / lifecycle
    // ────────────────────────────────────────────────────────────────────

    void upsert_newInstance_emitsWindowAppeared()
    {
        WindowRegistry reg;
        QSignalSpy appeared(&reg, &WindowRegistry::windowAppeared);
        QSignalSpy changed(&reg, &WindowRegistry::metadataChanged);

        reg.upsert(QStringLiteral("uuid-1"), make(QStringLiteral("firefox")));

        QCOMPARE(appeared.size(), 1);
        QCOMPARE(appeared.first().at(0).toString(), QStringLiteral("uuid-1"));
        QCOMPARE(changed.size(), 0);
        QCOMPARE(reg.size(), 1);
        QCOMPARE(reg.appIdFor(QStringLiteral("uuid-1")), QStringLiteral("firefox"));
    }

    void upsert_sameMetadata_isNoop()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("uuid-1"), make(QStringLiteral("firefox")));

        QSignalSpy appeared(&reg, &WindowRegistry::windowAppeared);
        QSignalSpy changed(&reg, &WindowRegistry::metadataChanged);

        // Bridges call upsert unconditionally on every observation. The
        // registry MUST de-dupe to avoid churn for downstream subscribers.
        reg.upsert(QStringLiteral("uuid-1"), make(QStringLiteral("firefox")));

        QCOMPARE(appeared.size(), 0);
        QCOMPARE(changed.size(), 0);
    }

    void upsert_rejectsEmptyInstanceId()
    {
        WindowRegistry reg;
        QSignalSpy appeared(&reg, &WindowRegistry::windowAppeared);

        reg.upsert(QString(), make(QStringLiteral("firefox")));

        QCOMPARE(appeared.size(), 0);
        QCOMPARE(reg.size(), 0);
    }

    // ────────────────────────────────────────────────────────────────────
    // Class mutation — the Emby scenario
    // ────────────────────────────────────────────────────────────────────

    void appIdMutation_updatesRecord_emitsMetadataChangedOnly()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("cef1ba31"), make(QStringLiteral("emby-beta")));

        QSignalSpy appeared(&reg, &WindowRegistry::windowAppeared);
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);
        QSignalSpy changed(&reg, &WindowRegistry::metadataChanged);

        reg.upsert(QStringLiteral("cef1ba31"), make(QStringLiteral("media.emby.client.beta")));

        QCOMPARE(appeared.size(), 0); // Same window — no appearance event
        QCOMPARE(disappeared.size(), 0); // Same window — no disappearance event
        QCOMPARE(changed.size(), 1);

        // metadataChanged carries old AND new so subscribers can diff.
        const auto args = changed.first();
        QCOMPARE(args.at(0).toString(), QStringLiteral("cef1ba31"));
        const auto oldMeta = args.at(1).value<WindowMetadata>();
        const auto newMeta = args.at(2).value<WindowMetadata>();
        QCOMPARE(oldMeta.appId, QStringLiteral("emby-beta"));
        QCOMPARE(newMeta.appId, QStringLiteral("media.emby.client.beta"));

        // Latest lookup reflects the new class.
        QCOMPARE(reg.appIdFor(QStringLiteral("cef1ba31")), QStringLiteral("media.emby.client.beta"));
    }

    void appIdMutation_keepsReverseIndexInSync()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("cef1ba31"), make(QStringLiteral("emby-beta")));
        QCOMPARE(reg.instancesWithAppId(QStringLiteral("emby-beta")), QStringList{QStringLiteral("cef1ba31")});

        reg.upsert(QStringLiteral("cef1ba31"), make(QStringLiteral("media.emby.client.beta")));

        // Old class must have ZERO instances; new class must own it.
        QVERIFY(reg.instancesWithAppId(QStringLiteral("emby-beta")).isEmpty());
        QCOMPARE(reg.instancesWithAppId(QStringLiteral("media.emby.client.beta")),
                 QStringList{QStringLiteral("cef1ba31")});
    }

    // ────────────────────────────────────────────────────────────────────
    // Reverse index (appId → instances)
    // ────────────────────────────────────────────────────────────────────

    void instancesWithAppId_returnsAllMatching()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("u1"), make(QStringLiteral("firefox")));
        reg.upsert(QStringLiteral("u2"), make(QStringLiteral("firefox")));
        reg.upsert(QStringLiteral("u3"), make(QStringLiteral("kate")));

        auto firefox = reg.instancesWithAppId(QStringLiteral("firefox"));
        std::sort(firefox.begin(), firefox.end()); // QMultiHash::values is unordered
        QCOMPARE(firefox, (QStringList{QStringLiteral("u1"), QStringLiteral("u2")}));

        QCOMPARE(reg.instancesWithAppId(QStringLiteral("kate")), QStringList{QStringLiteral("u3")});
    }

    void instancesWithAppId_emptyClass_notIndexed()
    {
        // Transient whitespace-only classes from Plasma shell surfaces show
        // up in the log as " |uuid". The registry must not pollute the
        // reverse index with empty keys.
        WindowRegistry reg;
        reg.upsert(QStringLiteral("u1"), make(QString()));

        QVERIFY(reg.instancesWithAppId(QString()).isEmpty());
        // But the record still exists — consumers can still look it up by id.
        QVERIFY(reg.contains(QStringLiteral("u1")));
    }

    // ────────────────────────────────────────────────────────────────────
    // remove + clear
    // ────────────────────────────────────────────────────────────────────

    void remove_knownInstance_emitsDisappeared()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("u1"), make(QStringLiteral("firefox")));
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        reg.remove(QStringLiteral("u1"));

        QCOMPARE(disappeared.size(), 1);
        QCOMPARE(disappeared.first().at(0).toString(), QStringLiteral("u1"));
        QVERIFY(!reg.contains(QStringLiteral("u1")));
        QVERIFY(reg.instancesWithAppId(QStringLiteral("firefox")).isEmpty());
    }

    void remove_unknownInstance_isNoop()
    {
        WindowRegistry reg;
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        reg.remove(QStringLiteral("never-registered"));

        QCOMPARE(disappeared.size(), 0);
    }

    void clear_emitsDisappearedForEach()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("u1"), make(QStringLiteral("firefox")));
        reg.upsert(QStringLiteral("u2"), make(QStringLiteral("kate")));
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        reg.clear();

        QCOMPARE(disappeared.size(), 2);
        QCOMPARE(reg.size(), 0);
        QVERIFY(reg.instancesWithAppId(QStringLiteral("firefox")).isEmpty());
    }

    // ────────────────────────────────────────────────────────────────────
    // Wide metadata — windowRole / pid / virtualDesktop / activity / windowType
    // ────────────────────────────────────────────────────────────────────

    void upsert_identicalWideMetadata_isNoop()
    {
        WindowMetadata wide;
        wide.appId = QStringLiteral("firefox");
        wide.desktopFile = QStringLiteral("firefox.desktop");
        wide.title = QStringLiteral("Mozilla Firefox");
        wide.windowRole = QStringLiteral("browser");
        wide.pid = 4242;
        wide.virtualDesktop = 2;
        wide.activity = QStringLiteral("activity-uuid");
        wide.windowType = PhosphorProtocol::WindowType::Normal;

        WindowRegistry reg;
        reg.upsert(QStringLiteral("u1"), wide);

        QSignalSpy changed(&reg, &WindowRegistry::metadataChanged);
        reg.upsert(QStringLiteral("u1"), wide);
        QCOMPARE(changed.size(), 0);
    }

    void wideMetadataChange_perField_emitsMetadataChanged()
    {
        WindowMetadata base;
        base.appId = QStringLiteral("firefox");
        base.windowRole = QStringLiteral("browser");
        base.pid = 1000;
        base.virtualDesktop = 1;
        base.activity = QStringLiteral("act-a");
        base.windowType = PhosphorProtocol::WindowType::Normal;

        // Each mutator changes exactly one non-appId field. The widened
        // operator== must detect it so metadataChanged fires — the
        // window-rule match engine relies on this for cache invalidation.
        const QList<std::function<void(WindowMetadata&)>> mutators = {
            [](WindowMetadata& m) {
                m.windowRole = QStringLiteral("popup");
            },
            [](WindowMetadata& m) {
                m.pid = 2000;
            },
            [](WindowMetadata& m) {
                m.virtualDesktop = 3;
            },
            [](WindowMetadata& m) {
                m.activity = QStringLiteral("act-b");
            },
            [](WindowMetadata& m) {
                m.windowType = PhosphorProtocol::WindowType::Dialog;
            },
        };

        for (const auto& mutate : mutators) {
            WindowRegistry reg;
            reg.upsert(QStringLiteral("u1"), base);
            QSignalSpy changed(&reg, &WindowRegistry::metadataChanged);

            WindowMetadata next = base;
            mutate(next);
            reg.upsert(QStringLiteral("u1"), next);

            QCOMPARE(changed.size(), 1);
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // appIdFor default
    // ────────────────────────────────────────────────────────────────────

    void appIdFor_unknownInstance_returnsEmpty()
    {
        WindowRegistry reg;
        QCOMPARE(reg.appIdFor(QStringLiteral("unknown")), QString());
    }

    // ────────────────────────────────────────────────────────────────────
    // pruneStaleInstances — defensive batch cleanup for signal-less deaths
    // ────────────────────────────────────────────────────────────────────

    void pruneStaleInstances_removesAbsentRecordsAndCanonical_emitsDisappeared()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("alive-1"), make(QStringLiteral("firefox")));
        reg.upsert(QStringLiteral("dead-1"), make(QStringLiteral("konsole")));
        reg.upsert(QStringLiteral("dead-2"), make(QStringLiteral("dolphin")));
        // Seed a canonical translation for a dead window (composite id is
        // appId|instanceId; the registry keys canonical on the instance part).
        reg.canonicalizeWindowId(QStringLiteral("konsole|dead-1"));

        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        const int pruned = reg.pruneStaleInstances({QStringLiteral("alive-1")});

        QCOMPARE(pruned, 2);
        QCOMPARE(reg.size(), 1);
        QVERIFY(reg.contains(QStringLiteral("alive-1")));
        QVERIFY(!reg.contains(QStringLiteral("dead-1")));
        QVERIFY(!reg.contains(QStringLiteral("dead-2")));
        // windowDisappeared fired for each dead record so subscribers (e.g.
        // saved-autotile-order cleanup) drop their ghost state.
        QCOMPARE(disappeared.size(), 2);
        // The dead window's canonical translation is gone — a re-observation
        // under a mutated appId no longer resolves to the stale canonical.
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("konsole-renamed|dead-1")),
                 QStringLiteral("konsole-renamed|dead-1"));
        // The alive window is untouched.
        QCOMPARE(reg.appIdFor(QStringLiteral("alive-1")), QStringLiteral("firefox"));
    }

    void pruneStaleInstances_allAlive_isNoop()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("u1"), make(QStringLiteral("firefox")));
        reg.upsert(QStringLiteral("u2"), make(QStringLiteral("konsole")));
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        const int pruned = reg.pruneStaleInstances({QStringLiteral("u1"), QStringLiteral("u2")});

        QCOMPARE(pruned, 0);
        QCOMPARE(disappeared.size(), 0);
        QCOMPARE(reg.size(), 2);
    }

    void pruneStaleInstances_canonicalWithoutRecord_isStillSwept()
    {
        // A window can hold a canonical translation with no metadata record
        // (it was canonicalized but never upserted, or its record was already
        // removed). The sweep must still drop the orphan canonical entry.
        WindowRegistry reg;
        reg.canonicalizeWindowId(QStringLiteral("ghost|orphan-1"));

        const int pruned = reg.pruneStaleInstances({QStringLiteral("alive-1")});

        QCOMPARE(pruned, 1);
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("ghost-2|orphan-1")), QStringLiteral("ghost-2|orphan-1"));
    }
};

// metadataChanged carries WindowMetadata by value in its signal payload;
// QSignalSpy stores QVariants so the type must be registered to round-trip.
Q_DECLARE_METATYPE(PhosphorEngine::WindowMetadata)

int main(int argc, char** argv)
{
    qRegisterMetaType<PhosphorEngine::WindowMetadata>("PhosphorEngine::WindowMetadata");
    QCoreApplication app(argc, argv);
    TestWindowRegistry tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_window_registry.moc"
