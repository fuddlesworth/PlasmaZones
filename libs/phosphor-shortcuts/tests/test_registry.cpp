// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShortcuts/IBackend.h>
#include <PhosphorShortcuts/Registry.h>

#include <QKeySequence>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QVector>

namespace PhosphorShortcuts::tests {

/**
 * FakeBackend: records every method call into inspectable vectors so tests
 * can assert on the exact sequence of backend operations Registry produced
 * for a given input. activate() exposes the IBackend::activated signal so
 * tests can simulate key-press events without a real portal / KGlobalAccel
 * round-trip.
 */
class FakeBackend : public IBackend
{
    Q_OBJECT
public:
    struct RegisterCall
    {
        QString id;
        QKeySequence defaultSeq;
        QKeySequence currentSeq;
        QString description;
        bool persistent = true;
    };
    struct UpdateCall
    {
        QString id;
        QKeySequence defaultSeq;
        QKeySequence newTrigger;
    };

    using IBackend::IBackend;

    void registerShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& currentSeq,
                          const QString& description, bool persistent) override
    {
        registers.push_back({id, defaultSeq, currentSeq, description, persistent});
    }

    void updateShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& newTrigger) override
    {
        updates.push_back({id, defaultSeq, newTrigger});
    }

    void unregisterShortcut(const QString& id) override
    {
        unregisters.push_back(id);
    }

    void flush() override
    {
        ++flushes;
        Q_EMIT ready();
    }

    void activate(const QString& id)
    {
        Q_EMIT activated(id);
    }

    QVector<RegisterCall> registers;
    QVector<UpdateCall> updates;
    QVector<QString> unregisters;
    int flushes = 0;
};

class TestRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void bindThenFlush_queuesRegisterOnly()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.test"), QKeySequence(QStringLiteral("Ctrl+Shift+T")),
                      QStringLiteral("Test shortcut"));

        // bind() queues but does not flush on its own — backend should be
        // untouched until flush() is called.
        QCOMPARE(backend.registers.size(), 0);
        QCOMPARE(backend.updates.size(), 0);
        QCOMPARE(backend.flushes, 0);

        registry.flush();

        // First flush: registerShortcut only. updateShortcut is NOT called
        // for a fresh registration — the Registry distinguishes internally.
        // Default and current match here because no rebind ran between bind
        // and flush — the dedicated test below covers the divergent case.
        QCOMPARE(backend.registers.size(), 1);
        QCOMPARE(backend.registers[0].id, QStringLiteral("pz.test"));
        QCOMPARE(backend.registers[0].defaultSeq, QKeySequence(QStringLiteral("Ctrl+Shift+T")));
        QCOMPARE(backend.registers[0].currentSeq, QKeySequence(QStringLiteral("Ctrl+Shift+T")));
        QCOMPARE(backend.registers[0].description, QStringLiteral("Test shortcut"));
        QCOMPARE(backend.updates.size(), 0);
        QCOMPARE(backend.flushes, 1);
    }

    void registerPassesBothDefaultAndCurrent_whenUserRebindBeforeFirstFlush()
    {
        // Critical: if the consumer applies a user-customised value via
        // rebind() BEFORE the initial flush, register() must receive the
        // compiled-in default AND the user's current value as two separate
        // args. Conflating them (older Registry behaviour) made KGlobalAccel
        // persist the user's value as the "reset to default" target.
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.x"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("X"));
        registry.rebind(QStringLiteral("pz.x"), QKeySequence(QStringLiteral("Meta+9")));
        registry.flush();

        QCOMPARE(backend.registers.size(), 1);
        QCOMPARE(backend.registers[0].defaultSeq, QKeySequence(QStringLiteral("Meta+1")));
        QCOMPARE(backend.registers[0].currentSeq, QKeySequence(QStringLiteral("Meta+9")));
        // No follow-up updateShortcut — the values are delivered in one call.
        QCOMPARE(backend.updates.size(), 0);
    }

    void bindWithEmptyDefault_doesNotRegister()
    {
        // Guard against the stale-grab hazard from discussion #155: an
        // entry whose compiled-in default is empty must not reach the
        // backend until a rebind() supplies a non-empty sequence.
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.empty"), QKeySequence(), QStringLiteral("Empty default"));
        registry.flush();

        QCOMPARE(backend.registers.size(), 0);
        QCOMPARE(backend.updates.size(), 0);
        // Entry is still in the registry — a later rebind will promote it.
        QCOMPARE(registry.bindings().size(), 1);
    }

    void bindWithEmptyDefault_thenRebind_registersOnce()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.empty"), QKeySequence(), QStringLiteral("Empty default"));
        registry.flush();

        registry.rebind(QStringLiteral("pz.empty"), QKeySequence(QStringLiteral("Meta+E")));
        registry.flush();

        // Now that currentSeq is non-empty we should see a single register
        // (not an update — this is the first time the backend hears about
        // this id) with currentSeq = the rebind value. defaultSeq stays
        // empty because that's what the consumer bound with.
        QCOMPARE(backend.registers.size(), 1);
        QCOMPARE(backend.registers[0].defaultSeq, QKeySequence());
        QCOMPARE(backend.registers[0].currentSeq, QKeySequence(QStringLiteral("Meta+E")));
        QCOMPARE(backend.updates.size(), 0);
    }

    void secondFlushAfterRebind_emitsUpdateOnly()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();

        backend.registers.clear();
        backend.updates.clear();

        registry.rebind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+2")));
        registry.flush();

        // Subsequent change: updateShortcut only, no re-registration.
        QCOMPARE(backend.registers.size(), 0);
        QCOMPARE(backend.updates.size(), 1);
        QCOMPARE(backend.updates[0].newTrigger, QKeySequence(QStringLiteral("Meta+2")));
    }

    void flushIdempotentWhenClean()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();

        backend.registers.clear();
        backend.updates.clear();

        // Second flush with no pending changes — backend should see only the
        // flush call itself, no duplicate registrations.
        registry.flush();

        QCOMPARE(backend.registers.size(), 0);
        QCOMPARE(backend.updates.size(), 0);
        QCOMPARE(backend.flushes, 2);
    }

    void rebindSameSequence_shortCircuits()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();

        backend.registers.clear();
        backend.updates.clear();

        registry.rebind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();

        // Same seq → no-op; backend should only see the flush, no updates.
        QCOMPARE(backend.updates.size(), 0);
    }

    void rebindEmptySequence_releasesGrabButKeepsEntry()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();

        backend.updates.clear();

        // Rebind to an empty sequence should NOT leave the grab registered
        // with an empty key (the pre-library stale-grab hazard) — the
        // backend drops it immediately. But the entry must survive so a
        // later rebind can re-enable the shortcut (discussion #809).
        registry.rebind(QStringLiteral("pz.a"), QKeySequence());

        QCOMPARE(backend.unregisters.size(), 1);
        QCOMPARE(backend.unregisters[0], QStringLiteral("pz.a"));
        QCOMPARE(backend.updates.size(), 0);
        QCOMPARE(registry.bindings().size(), 1);
        QCOMPARE(registry.shortcut(QStringLiteral("pz.a")), QKeySequence());
    }

    void rebindEmptySequence_whenNeverRegistered_skipsBackend()
    {
        FakeBackend backend;
        Registry registry(&backend);

        // bind + immediate clear BEFORE any flush — the backend never heard
        // about this id, so clearing must not send it a spurious unregister
        // (KGlobalAccel's unregister purges the persistent on-disk record).
        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.rebind(QStringLiteral("pz.a"), QKeySequence());
        registry.flush();

        QCOMPARE(backend.registers.size(), 0);
        QCOMPARE(backend.unregisters.size(), 0);
        QCOMPARE(registry.bindings().size(), 1);
    }

    void rebindAfterClear_reRegistersWithBackend()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A"));
        registry.flush();

        // Clear, then re-assign in the same session — the exact sequence a
        // settings UI produces when the user empties a shortcut field and
        // later types a new combo. Pre-fix the entry evaporated on clear and
        // the re-assign was a warning no-op until process restart.
        registry.rebind(QStringLiteral("pz.a"), QKeySequence());
        registry.flush();

        backend.registers.clear();
        backend.updates.clear();

        registry.rebind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+2")));
        registry.flush();

        // Fresh registerShortcut (not update) — the backend forgot the id on
        // unregister, so it needs default + current + description again.
        QCOMPARE(backend.registers.size(), 1);
        QCOMPARE(backend.registers[0].id, QStringLiteral("pz.a"));
        QCOMPARE(backend.registers[0].defaultSeq, QKeySequence(QStringLiteral("Meta+1")));
        QCOMPARE(backend.registers[0].currentSeq, QKeySequence(QStringLiteral("Meta+2")));
        QCOMPARE(backend.updates.size(), 0);

        // The re-registered shortcut still activates.
        int fired = 0;
        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A"), [&] {
            ++fired;
        });
        backend.activate(QStringLiteral("pz.a"));
        QCOMPARE(fired, 1);
    }

    void rebindUnknownId_isIgnored()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.rebind(QStringLiteral("pz.nonexistent"), QKeySequence(QStringLiteral("Meta+X")));
        registry.flush();

        QCOMPARE(backend.registers.size(), 0);
        QCOMPARE(backend.updates.size(), 0);
    }

    void unbind_releasesBackendGrab()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();

        registry.unbind(QStringLiteral("pz.a"));

        QCOMPARE(backend.unregisters.size(), 1);
        QCOMPARE(backend.unregisters[0], QStringLiteral("pz.a"));
        QCOMPARE(registry.bindings().size(), 0);
    }

    void unbindUnknownId_isIdempotent()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.unbind(QStringLiteral("pz.never-bound"));
        QCOMPARE(backend.unregisters.size(), 0);
    }

    void activation_emitsTriggeredAndInvokesCallback()
    {
        FakeBackend backend;
        Registry registry(&backend);

        int callbackCount = 0;
        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A"), [&] {
            ++callbackCount;
        });
        registry.flush();

        QSignalSpy triggeredSpy(&registry, &Registry::triggered);

        backend.activate(QStringLiteral("pz.a"));

        QCOMPARE(callbackCount, 1);
        QCOMPARE(triggeredSpy.count(), 1);
        QCOMPARE(triggeredSpy.at(0).at(0).toString(), QStringLiteral("pz.a"));
    }

    void activation_withoutCallback_stillEmitsTriggered()
    {
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();

        QSignalSpy triggeredSpy(&registry, &Registry::triggered);

        backend.activate(QStringLiteral("pz.a"));

        QCOMPARE(triggeredSpy.count(), 1);
        QCOMPARE(triggeredSpy.at(0).at(0).toString(), QStringLiteral("pz.a"));
    }

    void activation_unknownId_stillEmitsTriggered()
    {
        // Consumers may use triggered() as a centralised dispatcher that logs
        // or routes unknown ids — Registry shouldn't silently drop them.
        FakeBackend backend;
        Registry registry(&backend);

        QSignalSpy triggeredSpy(&registry, &Registry::triggered);

        backend.activate(QStringLiteral("pz.ghost"));

        QCOMPARE(triggeredSpy.count(), 1);
        QCOMPARE(triggeredSpy.at(0).at(0).toString(), QStringLiteral("pz.ghost"));
    }

    void activationAfterUnbind_isSuppressed()
    {
        FakeBackend backend;
        Registry registry(&backend);

        int callbackCount = 0;
        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A"), [&] {
            ++callbackCount;
        });
        registry.flush();

        registry.unbind(QStringLiteral("pz.a"));

        backend.activate(QStringLiteral("pz.a"));

        QCOMPARE(callbackCount, 0);
    }

    void readySignal_forwardsFromBackend()
    {
        FakeBackend backend;
        Registry registry(&backend);

        QSignalSpy readySpy(&registry, &Registry::ready);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();

        QCOMPARE(readySpy.count(), 1);
    }

    void secondBind_preservesCurrentSequenceAndReplacesCallback()
    {
        FakeBackend backend;
        Registry registry(&backend);

        int firstCount = 0;
        int secondCount = 0;

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A"), [&] {
            ++firstCount;
        });
        // Apply a user rebind BEFORE the second bind() comes along — e.g.
        // from a config-load in ShortcutManager::registerShortcuts().
        registry.rebind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+9")));

        // Second bind() for the same id must preserve currentSeq (user's
        // Meta+9) while replacing description + callback. Previously this
        // would have clobbered Meta+9 back to Meta+2.
        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+2")), QStringLiteral("A updated"), [&] {
            ++secondCount;
        });
        registry.flush();

        QCOMPARE(registry.shortcut(QStringLiteral("pz.a")), QKeySequence(QStringLiteral("Meta+9")));

        backend.activate(QStringLiteral("pz.a"));
        QCOMPARE(firstCount, 0);
        QCOMPARE(secondCount, 1);

        const auto bindings = registry.bindings();
        QCOMPARE(bindings.size(), 1);
        QCOMPARE(bindings[0].description, QStringLiteral("A updated"));
        QCOMPARE(bindings[0].defaultSeq, QKeySequence(QStringLiteral("Meta+2")));
        QCOMPARE(bindings[0].currentSeq, QKeySequence(QStringLiteral("Meta+9")));

        // And the backend register saw the NEW default but the preserved
        // current — proof that the register call separates the two.
        QCOMPARE(backend.registers.size(), 1);
        QCOMPARE(backend.registers[0].defaultSeq, QKeySequence(QStringLiteral("Meta+2")));
        QCOMPARE(backend.registers[0].currentSeq, QKeySequence(QStringLiteral("Meta+9")));
    }

    void shortcut_returnsCurrentSequence()
    {
        FakeBackend backend;
        Registry registry(&backend);

        QCOMPARE(registry.shortcut(QStringLiteral("pz.missing")), QKeySequence());

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        QCOMPARE(registry.shortcut(QStringLiteral("pz.a")), QKeySequence(QStringLiteral("Meta+1")));

        registry.rebind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+2")));
        QCOMPARE(registry.shortcut(QStringLiteral("pz.a")), QKeySequence(QStringLiteral("Meta+2")));
    }

    void bindings_returnsSortedById()
    {
        FakeBackend backend;
        Registry registry(&backend);

        // Insert in reverse-alpha order so the natural QHash iteration is
        // unlikely to return them alphabetised by chance.
        registry.bind(QStringLiteral("pz.c"), QKeySequence(QStringLiteral("Meta+3")));
        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.bind(QStringLiteral("pz.b"), QKeySequence(QStringLiteral("Meta+2")));

        const auto bindings = registry.bindings();
        QCOMPARE(bindings.size(), 3);
        QCOMPARE(bindings[0].id, QStringLiteral("pz.a"));
        QCOMPARE(bindings[1].id, QStringLiteral("pz.b"));
        QCOMPARE(bindings[2].id, QStringLiteral("pz.c"));
    }

    void callbackThatUnbindsSelf_isSafe()
    {
        FakeBackend backend;
        Registry registry(&backend);

        bool fired = false;
        // Callback unbinds its own id from inside the Registry::triggered
        // emit path. The QPointer guard added in onBackendActivated makes
        // this safe; before the fix, a callback that tore down the Registry
        // would have caused a use-after-free on the following Q_EMIT.
        registry.bind(QStringLiteral("pz.self-unbind"), QKeySequence(QStringLiteral("Meta+Q")),
                      QStringLiteral("Self-unbind"), [&] {
                          fired = true;
                          registry.unbind(QStringLiteral("pz.self-unbind"));
                      });
        registry.flush();

        QSignalSpy triggeredSpy(&registry, &Registry::triggered);

        backend.activate(QStringLiteral("pz.self-unbind"));

        QVERIFY(fired);
        QCOMPARE(triggeredSpy.count(), 1);
        QCOMPARE(registry.bindings().size(), 0);
    }

    void defaultChangeAfterFirstFlush_reRegisters()
    {
        // Pins the fix for the silent-drop regression: once an id was
        // registered, a subsequent bind() with a NEW defaultSeq has to
        // refresh the backend's "reset to default" target. Prior Registry
        // behaviour only ever called registerShortcut once per id and used
        // updateShortcut for everything after, so a default hot-reload
        // would reach the Registry but never reach KGlobalAccel /
        // preferred_trigger.
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A"));
        registry.flush();

        QCOMPARE(backend.registers.size(), 1);
        QCOMPARE(backend.registers[0].defaultSeq, QKeySequence(QStringLiteral("Meta+1")));
        backend.registers.clear();

        // Same id, new default. Registry must re-invoke registerShortcut on
        // the backend so the compiled-in default propagates.
        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+F1")), QStringLiteral("A"));
        registry.flush();

        QCOMPARE(backend.registers.size(), 1);
        QCOMPARE(backend.registers[0].defaultSeq, QKeySequence(QStringLiteral("Meta+F1")));
        // currentSeq preserved because no rebind happened between the two
        // binds — bind() never clobbers a user-applied rebind.
        QCOMPARE(backend.registers[0].currentSeq, QKeySequence(QStringLiteral("Meta+1")));
        QCOMPARE(backend.updates.size(), 0);
    }

    void defaultUnchanged_plusCurrentChange_emitsUpdateOnly()
    {
        // Counterpart to the test above: when only currentSeq changed,
        // updateShortcut is the right backend call — registerShortcut would
        // be a D-Bus round-trip + kglobalshortcutsrc write for no gain.
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();
        backend.registers.clear();
        backend.updates.clear();

        registry.rebind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+2")));
        registry.flush();

        QCOMPARE(backend.registers.size(), 0);
        QCOMPARE(backend.updates.size(), 1);
        QCOMPARE(backend.updates[0].newTrigger, QKeySequence(QStringLiteral("Meta+2")));
    }

    void registerForwardsPersistentFlag_toBackend()
    {
        // Regression: the persistent flag must reach the backend, not just
        // gate the bindings() filter. KGlobalAccelBackend uses it to decide
        // whether to purge kglobalshortcutsrc on destruction; sending only
        // `true` would have left the cancel-overlay Escape grab persisting
        // across daemon crashes (discussion #461 item 14).
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.user"), QKeySequence(QStringLiteral("Meta+U")), QStringLiteral("User"), {},
                      /*persistent=*/true);
        registry.bind(QStringLiteral("pz.adhoc"), QKeySequence(QStringLiteral("Esc")), QStringLiteral("Adhoc"), {},
                      /*persistent=*/false);
        registry.flush();

        QCOMPARE(backend.registers.size(), 2);
        // QHash iteration order is unspecified — locate by id rather than
        // index.
        bool sawUserPersistent = false;
        bool sawAdhocTransient = false;
        for (const auto& call : backend.registers) {
            if (call.id == QStringLiteral("pz.user")) {
                QVERIFY(call.persistent);
                sawUserPersistent = true;
            } else if (call.id == QStringLiteral("pz.adhoc")) {
                QVERIFY(!call.persistent);
                sawAdhocTransient = true;
            }
        }
        QVERIFY(sawUserPersistent);
        QVERIFY(sawAdhocTransient);
    }

    void rebindWithFlippedPersistentFlag_reReachesBackend()
    {
        // Regression: a re-bind() that flips `persistent` without changing the
        // key must still reach the backend. flush()'s change-detection keys off
        // the sequences AND the persistent flag — without the persistent term a
        // same-key/same-default re-bind would be treated as a no-op, the
        // backend would never learn the new flag, and KGlobalAccelBackend's
        // crash-purge decision would be stale (discussion #461 item 14).
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.flip"), QKeySequence(QStringLiteral("Meta+F")), QStringLiteral("Flip"), {},
                      /*persistent=*/true);
        registry.flush();
        QCOMPARE(backend.registers.size(), 1);
        QVERIFY(backend.registers[0].persistent);

        // Same id, same key, persistent flipped to false.
        registry.bind(QStringLiteral("pz.flip"), QKeySequence(QStringLiteral("Meta+F")), QStringLiteral("Flip"), {},
                      /*persistent=*/false);
        registry.flush();
        QCOMPARE(backend.registers.size(), 2);
        QVERIFY(!backend.registers[1].persistent);

        // A further flush with nothing changed must NOT re-register.
        registry.flush();
        QCOMPARE(backend.registers.size(), 2);
    }

    void bindings_persistentOnly_filtersTransient()
    {
        // Adhoc / transient bindings (persistent=false) must not surface in
        // bindings(persistentOnly=true). Consumer KCMs use that overload to
        // enumerate user-visible shortcuts and shouldn't see internal
        // ad-hoc grabs like the drag-cancel Escape.
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.user"), QKeySequence(QStringLiteral("Meta+U")), QStringLiteral("User"));
        registry.bind(QStringLiteral("pz.adhoc"), QKeySequence(QStringLiteral("Esc")), QStringLiteral("Adhoc"), {},
                      /*persistent=*/false);

        const auto all = registry.bindings();
        QCOMPARE(all.size(), 2);

        const auto userFacing = registry.bindings(/*persistentOnly=*/true);
        QCOMPARE(userFacing.size(), 1);
        QCOMPARE(userFacing[0].id, QStringLiteral("pz.user"));
    }

    void callbackRunsBeforeTriggeredSignal()
    {
        // Documented contract: Registry::onBackendActivated invokes the
        // per-binding callback first, then Q_EMIT triggered. Consumers that
        // depend on "slot has synchronous read of mutated state" rely on
        // this ordering.
        FakeBackend backend;
        Registry registry(&backend);

        QVector<QString> events;
        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A"), [&] {
            events.push_back(QStringLiteral("callback"));
        });
        registry.flush();

        connect(&registry, &Registry::triggered, [&](const QString&) {
            events.push_back(QStringLiteral("signal"));
        });

        backend.activate(QStringLiteral("pz.a"));

        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0], QStringLiteral("callback"));
        QCOMPARE(events[1], QStringLiteral("signal"));
    }

    void updateShortcut_carriesDefaultSeqForBackendConsistency()
    {
        // Pins the IBackend contract change: updateShortcut takes both
        // defaultSeq and newTrigger. PortalBackend uses defaultSeq for
        // preferred_trigger consistently with registerShortcut; KGlobalAccel
        // ignores it (its setDefaultShortcut is refreshed via the register
        // path on default deltas). Test that Registry passes the
        // currently-bound defaultSeq through unchanged so backends can rely
        // on it.
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();
        backend.updates.clear();

        registry.rebind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+9")));
        registry.flush();

        QCOMPARE(backend.updates.size(), 1);
        QCOMPARE(backend.updates[0].id, QStringLiteral("pz.a"));
        QCOMPARE(backend.updates[0].defaultSeq, QKeySequence(QStringLiteral("Meta+1")));
        QCOMPARE(backend.updates[0].newTrigger, QKeySequence(QStringLiteral("Meta+9")));
    }

    void flushAfterBackendDestroyed_isSafeAndWarns()
    {
        // Pins the QPointer guard in flush(): if the backend is destroyed
        // before the Registry, flush() must not crash. (Real consumers like
        // ShortcutManager own both via unique_ptr in declaration order, so
        // this can happen during teardown.)
        auto backend = std::make_unique<FakeBackend>();
        Registry registry(backend.get());

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")));
        registry.flush();
        QCOMPARE(backend->registers.size(), 1);

        backend.reset(); // QPointer in Registry::m_backend goes null

        // Should not crash, should not register, and should leave entries
        // intact for later (non-existent) re-flush.
        registry.flush();
        registry.rebind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+2")));
        registry.flush();

        QCOMPARE(registry.bindings().size(), 1);
        QCOMPARE(registry.shortcut(QStringLiteral("pz.a")), QKeySequence(QStringLiteral("Meta+2")));
    }

    void descriptionChange_isLocalOnly()
    {
        // A re-bind that only changes the description is stored in the
        // Registry but does NOT trigger a backend register/update — the
        // IBackend::updateShortcut signature has no description slot, so
        // there's nothing to propagate. Documented in Registry.h and the
        // API docs.
        FakeBackend backend;
        Registry registry(&backend);

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A"));
        registry.flush();
        backend.registers.clear();
        backend.updates.clear();

        registry.bind(QStringLiteral("pz.a"), QKeySequence(QStringLiteral("Meta+1")), QStringLiteral("A (renamed)"));
        registry.flush();

        QCOMPARE(backend.registers.size(), 0);
        QCOMPARE(backend.updates.size(), 0);

        // Local state DOES reflect the new description — consumers can
        // read it via bindings() for non-backend-facing uses.
        const auto bindings = registry.bindings();
        QCOMPARE(bindings.size(), 1);
        QCOMPARE(bindings[0].description, QStringLiteral("A (renamed)"));
    }
};

} // namespace PhosphorShortcuts::tests

QTEST_MAIN(PhosphorShortcuts::tests::TestRegistry)
#include "test_registry.moc"
