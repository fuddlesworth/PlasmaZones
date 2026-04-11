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

#include "../../../src/core/windowregistry.h"
#include <QSignalSpy>
#include <QTest>

using PlasmaZones::WindowMetadata;
using PlasmaZones::WindowRegistry;

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
    // appIdFor default
    // ────────────────────────────────────────────────────────────────────

    void appIdFor_unknownInstance_returnsEmpty()
    {
        WindowRegistry reg;
        QCOMPARE(reg.appIdFor(QStringLiteral("unknown")), QString());
    }
};

// metadataChanged carries WindowMetadata by value in its signal payload;
// QSignalSpy stores QVariants so the type must be registered to round-trip.
Q_DECLARE_METATYPE(PlasmaZones::WindowMetadata)

int main(int argc, char** argv)
{
    qRegisterMetaType<PlasmaZones::WindowMetadata>("PlasmaZones::WindowMetadata");
    QCoreApplication app(argc, argv);
    TestWindowRegistry tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_window_registry.moc"
