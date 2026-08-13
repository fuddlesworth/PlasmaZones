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
 *  - canonical freeze: the first id seen for an instance is frozen and never
 *    re-seeded, including an appId-less "|instanceId" placeholder, so a
 *    window keeps ONE key for its whole life; an empty id is exempt and
 *    seeds nothing
 *  - canonical retirement: remove/clear emit before retiring the canonical
 *    record, and a subscriber-reseeded canonical survives the removal; a
 *    subscriber's re-UPSERT during a bulk clear does not, since the clear
 *    drops the replay and must not leave the mapping behind either; outside
 *    a clear the mapping IS kept, for the record that does get replayed
 *  - pruneStaleInstances fails closed on an empty alive set
 *  - wide metadata: windowRole / pid / virtualDesktop / activity / windowType
 *    round-trip and emit per-field change signals
 *  - rejection of an empty instance id, and appIdFor on an unknown instance
 *  - re-entrancy: nested clear/remove/upsert from signal receivers (including
 *    receiver-deletes-registry) leave the registry consistent and emit once
 *  - isMinimized resolves composite windowIds to the instance record
 *  - pruneStaleInstances sweeps absent records and orphaned canonicals
 */

#include <PhosphorEngine/WindowRegistry.h>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <functional>

using PhosphorEngine::WindowMetadata;
using PhosphorEngine::WindowRegistry;

// QSignalSpy stores signal arguments as QVariants, so the record type carried
// by metadataChanged must be metatype-registered for the spies below.
Q_DECLARE_METATYPE(PhosphorEngine::WindowMetadata)

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
    void initTestCase()
    {
        qRegisterMetaType<PhosphorEngine::WindowMetadata>();
    }

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
        WindowMetadata oldValue =
            make(QStringLiteral("emby-beta"), QStringLiteral("emby.desktop"), QStringLiteral("Library"));
        oldValue.windowRole = QStringLiteral("browser");
        oldValue.pid = 1001;
        oldValue.virtualDesktop = 2;
        oldValue.virtualDesktops = {2, 3};
        oldValue.activity = QStringLiteral("activity-a");
        oldValue.windowType = PhosphorProtocol::WindowType::Normal;
        oldValue.isMinimized = false;
        oldValue.width = 1280;
        oldValue.height = 720;
        reg.upsert(QStringLiteral("cef1ba31"), oldValue);

        QSignalSpy appeared(&reg, &WindowRegistry::windowAppeared);
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);
        QSignalSpy changed(&reg, &WindowRegistry::metadataChanged);

        WindowMetadata newValue = oldValue;
        newValue.appId = QStringLiteral("media.emby.client.beta");
        newValue.title = QStringLiteral("Now Playing");
        newValue.isMinimized = true;
        newValue.width = 1920;
        reg.upsert(QStringLiteral("cef1ba31"), newValue);

        QCOMPARE(appeared.size(), 0); // Same window — no appearance event
        QCOMPARE(disappeared.size(), 0); // Same window — no disappearance event
        QCOMPARE(changed.size(), 1);

        // metadataChanged carries old AND new so subscribers can diff.
        const auto args = changed.first();
        QCOMPARE(args.at(0).toString(), QStringLiteral("cef1ba31"));
        const auto oldMeta = args.at(1).value<WindowMetadata>();
        const auto newMeta = args.at(2).value<WindowMetadata>();
        QVERIFY(oldMeta == oldValue);
        QVERIFY(newMeta == newValue);

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
    // canonical freeze
    // ────────────────────────────────────────────────────────────────────

    void canonicalizeWindowId_firstSeenId_isFrozen()
    {
        WindowRegistry reg;

        QCOMPARE(reg.canonicalizeWindowId(QStringLiteral("firefox|u1")), QStringLiteral("firefox|u1"));
        // Single-shot: a mutated class never re-seeds the mapping.
        QCOMPARE(reg.canonicalizeWindowId(QStringLiteral("renamed|u1")), QStringLiteral("firefox|u1"));
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("renamed|u1")), QStringLiteral("firefox|u1"));
    }

    void canonicalizeWindowId_appIdLessFirstContact_isStillFrozen()
    {
        // KWin reports a blank class for a surface it has not finished
        // mapping, so the first push for a window can carry "|instanceId".
        // That shape is frozen like any other ON PURPOSE. Exempting it would
        // make the canonical CHANGE once the real class arrived, and the
        // engines key their membership maps at adopt time with no re-key
        // path: close would stop removing the window and the alive-set walk
        // in pruneStaleWindows would force-remove a live one from its layout.
        // The appId such a window is missing is served by appIdFor, which
        // self-corrects on the class-change push — never by parsing the id.
        WindowRegistry reg;

        QCOMPARE(reg.canonicalizeWindowId(QStringLiteral("|u1")), QStringLiteral("|u1"));
        // One key for the window's life: the later well-formed push resolves
        // BACK to the frozen id rather than replacing it.
        QCOMPARE(reg.canonicalizeWindowId(QStringLiteral("ghostty|u1")), QStringLiteral("|u1"));
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("ghostty|u1")), QStringLiteral("|u1"));

        // And the identity is recoverable from the metadata record, which is
        // where callers are told to get it.
        reg.upsert(QStringLiteral("u1"), make(QStringLiteral("ghostty")));
        QCOMPARE(reg.appIdFor(QStringLiteral("u1")), QStringLiteral("ghostty"));
    }

    void canonicalizeWindowId_emptyId_isNotFrozen()
    {
        WindowRegistry reg;

        QCOMPARE(reg.canonicalizeWindowId(QString()), QString());
        // The empty early return must not have seeded a mapping under an
        // empty instance id.
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("firefox|")), QStringLiteral("firefox|"));
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
        reg.canonicalizeWindowId(QStringLiteral("ghost|canonical-only"));
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        reg.clear();

        QStringList ids;
        for (const QList<QVariant>& args : disappeared) {
            ids.append(args.at(0).toString());
        }
        ids.sort();
        QCOMPARE(ids, (QStringList{QStringLiteral("canonical-only"), QStringLiteral("u1"), QStringLiteral("u2")}));
        QCOMPARE(reg.size(), 0);
        QVERIFY(reg.instancesWithAppId(QStringLiteral("firefox")).isEmpty());
    }

    void clear_subscriberReupsertsDuringEmit_leavesNothingBehind()
    {
        // clear() drops a re-entrant upsert on purpose (the bulk reset
        // supersedes it), so the canonical must be dropped with it. Keeping
        // the mapping for a record that is never replayed left the instance
        // behind as a canonical-only survivor of a reset whose whole contract
        // is that the registry ends up empty.
        //
        // Scoped to the re-UPSERT shape deliberately. A subscriber that seeds
        // a canonical for an instance that had NONE takes the
        // postEmit != canonical branch instead, which skips this retirement
        // entirely and lets the fresh mapping live — that is a re-announce,
        // covered by remove_subscriberReseededCanonical_survivesRemoval, and
        // it is not what this gate governs. (A subscriber calling
        // canonicalizeWindowId for an instance that still HAS its mapping
        // just gets the frozen id back and changes nothing.)
        WindowRegistry reg;
        reg.canonicalizeWindowId(QStringLiteral("firefox|u1"));
        reg.upsert(QStringLiteral("u1"), make(QStringLiteral("firefox")));
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString& instanceId) {
            // A re-announce racing the clear.
            reg.upsert(instanceId, make(QStringLiteral("firefox")));
        });

        reg.clear();

        QCOMPARE(reg.size(), 0);
        QVERIFY(!reg.contains(QStringLiteral("u1")));
        // No canonical-only remnant: nothing resolves for this instance any
        // more. (The discriminating assertion — without the m_clearing gate
        // the retired mapping is re-inserted and this returns "firefox|u1".)
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("renamed|u1")), QStringLiteral("renamed|u1"));
    }

    void remove_reannouncingSubscriber_keepsCanonicalOutsideClear()
    {
        // The non-clearing leg of the same gate: outside a bulk clear the
        // re-entrant upsert IS replayed, so the canonical must be kept for it
        // rather than retired. Without this, a close racing a re-announce
        // would strip the identity translation the replayed record needs.
        WindowRegistry reg;
        reg.canonicalizeWindowId(QStringLiteral("firefox|u1"));
        reg.upsert(QStringLiteral("u1"), make(QStringLiteral("firefox")));
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString& instanceId) {
            reg.upsert(instanceId, make(QStringLiteral("firefox")));
        });

        reg.remove(QStringLiteral("u1"));

        // The replayed record is back, and still reachable through the id the
        // stores were keyed with.
        QVERIFY(reg.contains(QStringLiteral("u1")));
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("renamed|u1")), QStringLiteral("firefox|u1"));
    }

    void remove_canonicalOnly_emitsBeforeRetiringCanonical()
    {
        WindowRegistry reg;
        const QString canonical = QStringLiteral("ghost|canonical-only");
        reg.canonicalizeWindowId(canonical);
        QString resolvedDuringSignal;
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString&) {
            // The mapping must still resolve for synchronous subscribers.
            resolvedDuringSignal = reg.canonicalizeForLookup(QStringLiteral("renamed|canonical-only"));
        });
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        reg.remove(QStringLiteral("canonical-only"));

        QCOMPARE(disappeared.size(), 1);
        QCOMPARE(resolvedDuringSignal, canonical);
        // Retired after the emit completes.
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("renamed|canonical-only")),
                 QStringLiteral("renamed|canonical-only"));
    }

    void remove_subscriberReseededCanonical_survivesRemoval()
    {
        WindowRegistry reg;
        const QString instanceId = QStringLiteral("reseed-instance");
        const QString freshCanonical = QStringLiteral("fresh|reseed-instance");
        // A record WITHOUT a canonical mapping: the effect's metadata push
        // seeds the canonical, but a record can arrive through other
        // notifications first.
        reg.upsert(instanceId, make(QStringLiteral("old")));
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString&) {
            // A re-announce racing the close seeds a fresh canonical for the
            // same instance while the removal is in flight.
            reg.canonicalizeWindowId(freshCanonical);
        });
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        reg.remove(instanceId);

        QCOMPARE(disappeared.size(), 1);
        // The subscriber's fresh mapping must not be clobbered by the
        // post-emit retirement of the pre-emit one.
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("later|reseed-instance")), freshCanonical);
    }

    void clear_nestedClearAndReentrantUpsert_leavesRegistryEmpty()
    {
        WindowRegistry reg;
        reg.upsert(QStringLiteral("n1"), make(QStringLiteral("appA")));
        reg.upsert(QStringLiteral("n2"), make(QStringLiteral("appB")));
        reg.upsert(QStringLiteral("n3"), make(QStringLiteral("appC")));
        bool nested = false;
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString& removedId) {
            // First disappearance: a subscriber both re-upserts the dying
            // instance AND triggers a nested bulk clear while two records
            // remain. Neither may leave residue — the clear supersedes the
            // re-entrant upsert (it is dropped, not replayed).
            if (!nested) {
                nested = true;
                reg.upsert(removedId, make(QStringLiteral("resurrected")));
                reg.clear();
            }
        });

        reg.clear();

        QCOMPARE(reg.size(), 0);
        QVERIFY(!reg.contains(QStringLiteral("n1")));
        QVERIFY(!reg.contains(QStringLiteral("n2")));
        QVERIFY(!reg.contains(QStringLiteral("n3")));
    }

    void remove_reentrantCrossRemoval_removesBoth()
    {
        WindowRegistry reg;
        const QString first = QStringLiteral("cross-a");
        const QString second = QStringLiteral("cross-b");
        reg.upsert(first, make(QStringLiteral("app")));
        reg.upsert(second, make(QStringLiteral("app")));
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString& removedId) {
            if (removedId == first) {
                // A subscriber tearing down a DIFFERENT window re-entrantly
                // (grouped-close handlers do this) must not corrupt the
                // outer removal's bookkeeping.
                reg.remove(second);
            }
        });
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        reg.remove(first);

        QCOMPARE(disappeared.size(), 2);
        QVERIFY(!reg.contains(first));
        QVERIFY(!reg.contains(second));
        QCOMPARE(reg.size(), 0);
        QVERIFY(reg.instancesWithAppId(QStringLiteral("app")).isEmpty());
    }

    void remove_reentrantRemoval_emitsOnce()
    {
        WindowRegistry reg;
        const QString instanceId = QStringLiteral("reentrant-instance");
        const QString canonical = QStringLiteral("app|reentrant-instance");
        reg.upsert(instanceId, make(QStringLiteral("app")));
        reg.canonicalizeWindowId(canonical);
        // No QCOMPARE inside the lambda: a failing compare there returns from
        // the LAMBDA, not the test — capture and assert after.
        QString observedRemovedId;
        QString observedCanonical;
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString& removedId) {
            observedRemovedId = removedId;
            observedCanonical = reg.canonicalizeForLookup(QStringLiteral("renamed|reentrant-instance"));
            reg.remove(instanceId);
            reg.clear();
        });
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        reg.remove(instanceId);

        QCOMPARE(disappeared.size(), 1);
        QCOMPARE(observedRemovedId, instanceId);
        QCOMPARE(observedCanonical, canonical);
        QVERIFY(!reg.contains(instanceId));
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("renamed|reentrant-instance")),
                 QStringLiteral("renamed|reentrant-instance"));
    }

    void remove_reinsertedInstance_honorsNestedRemoval()
    {
        WindowRegistry reg;
        const QString instanceId = QStringLiteral("reinserted-instance");
        reg.upsert(instanceId, make(QStringLiteral("old-app")));
        bool reinserted = false;
        // Event ACCUMULATOR (id-carrying), not a bare count: two
        // disappearances of the wrong ids would satisfy a count-only pin.
        QStringList events;
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString& removedId) {
            events.append(QStringLiteral("disappeared:") + removedId);
            if (!reinserted) {
                reinserted = true;
                reg.upsert(removedId, make(QStringLiteral("new-app")));
            }
        });
        connect(&reg, &WindowRegistry::windowAppeared, &reg, [&](const QString& appearedId) {
            events.append(QStringLiteral("appeared:") + appearedId);
            if (reinserted) {
                reg.remove(appearedId);
            }
        });

        reg.remove(instanceId);

        QCOMPARE(events,
                 (QStringList{QStringLiteral("disappeared:") + instanceId, QStringLiteral("appeared:") + instanceId,
                              QStringLiteral("disappeared:") + instanceId}));
        QVERIFY(!reg.contains(instanceId));
        QVERIFY(reg.instancesWithAppId(QStringLiteral("new-app")).isEmpty());
    }

    void remove_reentrantUpsertRunsAfterDisappearance()
    {
        WindowRegistry reg;
        const QString instanceId = QStringLiteral("reappearing-instance");
        reg.upsert(instanceId, make(QStringLiteral("old-app")));
        QStringList events;
        connect(&reg, &WindowRegistry::windowDisappeared, &reg, [&](const QString& removedId) {
            events.append(QStringLiteral("disappeared"));
            reg.upsert(removedId, make(QStringLiteral("new-app")));
        });
        connect(&reg, &WindowRegistry::windowAppeared, &reg, [&](const QString&) {
            events.append(QStringLiteral("appeared"));
        });

        reg.remove(instanceId);

        QCOMPARE(events, (QStringList{QStringLiteral("disappeared"), QStringLiteral("appeared")}));
        QVERIFY(reg.contains(instanceId));
        QCOMPARE(reg.appIdFor(instanceId), QStringLiteral("new-app"));
        QVERIFY(reg.instancesWithAppId(QStringLiteral("old-app")).isEmpty());
        QCOMPARE(reg.instancesWithAppId(QStringLiteral("new-app")), QStringList{instanceId});
    }

    void clear_receiverMayDeleteRegistry()
    {
        QPointer<WindowRegistry> reg = new WindowRegistry;
        reg->upsert(QStringLiteral("u1"), make(QStringLiteral("firefox")));
        reg->upsert(QStringLiteral("u2"), make(QStringLiteral("kate")));
        connect(reg.data(), &WindowRegistry::windowDisappeared, this, [&reg](const QString&) {
            delete reg.data();
        });

        reg->clear();

        QVERIFY(reg.isNull());
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
            // Extended-field representatives, one per shape: the span list,
            // an optional<int>, and an optional<bool> — pins that the widened
            // operator== covers the extended block, not just the wide scalars.
            [](WindowMetadata& m) {
                m.virtualDesktops = QList<int>{1, 2};
            },
            [](WindowMetadata& m) {
                m.width = 1280;
            },
            [](WindowMetadata& m) {
                m.isMinimized = true;
            },
        };

        // Accumulate failures instead of QCOMPARE-ing inside the loop: an
        // in-loop abort hides which LATER mutators also regressed.
        QStringList failures;
        for (int i = 0; i < mutators.size(); ++i) {
            WindowRegistry reg;
            reg.upsert(QStringLiteral("u1"), base);
            QSignalSpy changed(&reg, &WindowRegistry::metadataChanged);

            WindowMetadata next = base;
            mutators.at(i)(next);
            reg.upsert(QStringLiteral("u1"), next);

            if (changed.size() != 1) {
                failures.append(QStringLiteral("mutator %1 emitted %2 (expected 1)").arg(i).arg(changed.size()));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
    }

    // ────────────────────────────────────────────────────────────────────
    // appIdFor default
    // ────────────────────────────────────────────────────────────────────

    void appIdFor_unknownInstance_returnsEmpty()
    {
        WindowRegistry reg;
        QCOMPARE(reg.appIdFor(QStringLiteral("unknown")), QString());
    }

    void isMinimized_resolvesCompositeWindowId()
    {
        WindowRegistry reg;
        WindowMetadata metadata = make(QStringLiteral("firefox"));
        reg.upsert(QStringLiteral("uuid-1"), metadata);

        QVERIFY(!reg.isMinimized(QStringLiteral("uuid-1")));
        QVERIFY(!reg.isMinimized(QStringLiteral("firefox|uuid-1")));

        QSignalSpy changed(&reg, &WindowRegistry::metadataChanged);
        metadata.isMinimized = true;
        reg.upsert(QStringLiteral("uuid-1"), metadata);

        QVERIFY(reg.isMinimized(QStringLiteral("uuid-1")));
        QVERIFY(reg.isMinimized(QStringLiteral("firefox|uuid-1")));
        QCOMPARE(changed.size(), 1);

        metadata.isMinimized = false;
        reg.upsert(QStringLiteral("uuid-1"), metadata);
        QVERIFY(!reg.isMinimized(QStringLiteral("uuid-1")));
        QVERIFY(!reg.isMinimized(QStringLiteral("firefox|uuid-1")));
        QCOMPARE(changed.size(), 2);

        reg.upsert(QStringLiteral("uuid-1"), metadata);
        QCOMPARE(changed.size(), 2);
        QVERIFY(!reg.isMinimized(QStringLiteral("firefox|unknown")));
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
        QStringList disappearedIds;
        for (const QList<QVariant>& args : disappeared) {
            disappearedIds.append(args.at(0).toString());
        }
        disappearedIds.sort();
        QCOMPARE(disappearedIds, (QStringList{QStringLiteral("dead-1"), QStringLiteral("dead-2")}));
        // The dead window's canonical translation is gone — a re-observation
        // under a mutated appId no longer resolves to the stale canonical.
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("konsole-renamed|dead-1")),
                 QStringLiteral("konsole-renamed|dead-1"));
        // The alive window is untouched.
        QCOMPARE(reg.appIdFor(QStringLiteral("alive-1")), QStringLiteral("firefox"));
    }

    void pruneStaleInstances_emptyAliveSet_isRefused()
    {
        // Fail-closed guard. The effect's one-shot alive report at daemon-ready
        // fires before session-restored apps have mapped their windows, so an
        // empty set arrives meaning "I have not looked yet", not "everything
        // died". Honouring it would destroy the whole registry — every window's
        // identity translation — in one call.
        WindowRegistry reg;
        reg.upsert(QStringLiteral("u1"), make(QStringLiteral("firefox")));
        reg.canonicalizeWindowId(QStringLiteral("konsole|u2"));
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        const int pruned = reg.pruneStaleInstances({});

        QCOMPARE(pruned, 0);
        QCOMPARE(disappeared.size(), 0);
        QCOMPARE(reg.size(), 1);
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("renamed|u2")), QStringLiteral("konsole|u2"));
    }

    void pruneStaleInstances_emptySetOnEmptyRegistry_isNoop()
    {
        // The one legitimately-empty case: nothing tracked, nothing alive. It
        // must fall through the guard rather than trip it.
        WindowRegistry reg;
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        QCOMPARE(reg.pruneStaleInstances({}), 0);
        QCOMPARE(disappeared.size(), 0);
        QCOMPARE(reg.size(), 0);
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
        QSignalSpy disappeared(&reg, &WindowRegistry::windowDisappeared);

        const int pruned = reg.pruneStaleInstances({QStringLiteral("alive-1")});

        QCOMPARE(pruned, 1);
        QCOMPARE(disappeared.size(), 1);
        QCOMPARE(disappeared.first().at(0).toString(), QStringLiteral("orphan-1"));
        QCOMPARE(reg.canonicalizeForLookup(QStringLiteral("ghost-2|orphan-1")), QStringLiteral("ghost-2|orphan-1"));
    }

    void pruneStaleInstances_receiverMayDeleteRegistry()
    {
        QPointer<WindowRegistry> reg = new WindowRegistry;
        reg->upsert(QStringLiteral("dead-1"), make(QStringLiteral("konsole")));
        reg->upsert(QStringLiteral("dead-2"), make(QStringLiteral("dolphin")));
        reg->upsert(QStringLiteral("alive-1"), make(QStringLiteral("kate")));
        connect(reg.data(), &WindowRegistry::windowDisappeared, this, [&reg](const QString&) {
            delete reg.data();
        });

        // Non-empty alive set: the empty set is a fail-closed no-op (a
        // premature alive report must not wipe the registry), so the
        // receiver-deletes-registry sweep needs a survivor.
        reg->pruneStaleInstances({QStringLiteral("alive-1")});

        QVERIFY(reg.isNull());
    }
};

QTEST_MAIN(TestWindowRegistry)
#include "test_window_registry.moc"
