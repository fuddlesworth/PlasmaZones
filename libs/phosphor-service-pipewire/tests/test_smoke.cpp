// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PipeWireHost.h>
#include <PhosphorServicePipeWire/PwNode.h>
#include <PhosphorServicePipeWire/PwNodeModel.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QSet>
#include <QSignalSpy>
#include <QtGlobal>
#include <QtTest/QtTest>

#include <iterator>

class TestSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Construction must not block on a missing daemon and must leave
    /// the connection in the "no PipeWire, no error yet" baseline so a
    /// QML status indicator binding starts at the right value.
    void constructionDefaults()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QCOMPARE(conn.isConnected(), false);
        QCOMPARE(conn.isDaemonAvailable(), false);
    }

    /// disconnectFromDaemon() before connectToDaemon() is a documented
    /// no-op; calling it must not emit spurious signals or crash the
    /// loop thread.
    void disconnectBeforeConnectIsNoop()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QSignalSpy connSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::connectedChanged);
        QSignalSpy availSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::daemonAvailableChanged);
        // Also spy on `error`: a pw_loop_invoke queue-side failure on
        // disconnectFromDaemon's dispatch path would fire an error()
        // signal (lifecycle.cpp's dispatch-failure branch), and the
        // previous test missed that contract — the no-op disconnect
        // must produce NO observable signals of any kind.
        QSignalSpy errorSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::error);

        conn.disconnectFromDaemon();
        // Process any queued events from the loop thread to make sure
        // a misbehaving doDisconnect doesn't leak signals into the GUI
        // queue. 200ms covers a slow CI host's scheduler latency; the
        // no-op path itself is fast (synchronous early-out + no thread
        // wakeup), so the extra budget is pure margin, not a contract.
        QTest::qWait(200);

        QCOMPARE(connSpy.count(), 0);
        QCOMPARE(availSpy.count(), 0);
        QCOMPARE(errorSpy.count(), 0);
    }

    /// Multiple connectToDaemon() calls collapse into one effective
    /// attempt; pin idempotency by spying on `connectedChanged`. Even
    /// if a real daemon answers the handshake, the property may flip at
    /// most once (false to true). On a no-daemon host the count stays
    /// at 0. Any value above 1 means a second connectToDaemon()
    /// re-armed the property transition path, which is the regression
    /// we want to catch.
    void connectIdempotent()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        // The spy attaches after construction, so any construction-time
        // emission would already have fired (or be queued behind a
        // qWait) and would not be observable here. constructionDefaults
        // already pins the baseline property values; rely on that and
        // let connectedSpy below capture only post-connect transitions.
        QSignalSpy connectedSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::connectedChanged);
        conn.connectToDaemon();
        conn.connectToDaemon();
        conn.connectToDaemon();
        // Poll for either a successful connect (the one legitimate flip
        // landed) or the 2000ms test-side budget elapsing. 2000ms is a
        // generous test-only budget — the library itself does NOT
        // enforce a connect timeout, PipeWire's connect is non-blocking
        // and lifetime is owned by the caller — so this value is purely
        // how long the test will wait for a daemon handshake before
        // declaring no-daemon. On a no-daemon host the loop exits at
        // the first 200ms tick with isConnected() still false, instead
        // of burning the full 2000ms. The previous fixed qWait(2000)
        // wasted the entire budget on every no-daemon CI run.
        QElapsedTimer timer;
        timer.start();
        while (!conn.isConnected() && timer.elapsed() < 2000) {
            QTest::qWait(200);
        }
        QVERIFY2(connectedSpy.count() <= 1,
                 qPrintable(QStringLiteral("connectedChanged fired %1 times").arg(connectedSpy.count())));
    }

    /// Destruction must join the loop thread cleanly even when a
    /// connect attempt is in flight. The test crashes with use-after-
    /// free or hangs if the teardown sequence in ~PipeWireConnection
    /// regresses. Bound the join with an elapsed-time check: 2s is far
    /// longer than any healthy teardown but well under any sane CI
    /// per-test timeout, so a hang surfaces as a failed assertion here
    /// rather than a watchdog-killed process.
    void destructionWhileConnectInFlightIsSafe()
    {
        auto conn = std::make_unique<PhosphorServicePipeWire::PipeWireConnection>();
        conn->connectToDaemon();
        // Time only the teardown — construction and the non-blocking
        // connect dispatch are not what this bound is about. Start the
        // clock immediately before the drop so the asserted budget
        // measures the ~PipeWireConnection join in isolation.
        QElapsedTimer timer;
        timer.start();
        // Drop the unique_ptr without waiting; ~PipeWireConnection has
        // to win the race against the loop thread's pw_context_connect.
        conn.reset();
        const qint64 elapsedMs = timer.elapsed();
        QVERIFY2(elapsedMs < 2000,
                 qPrintable(QStringLiteral("destruction took %1ms (expected < 2000ms)").arg(elapsedMs)));
    }

    /// `nodes()` returns an empty list pre-connectToDaemon.
    void nodesEmptyBeforeConnect()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        const auto nodes = conn.nodes();
        QCOMPARE(nodes.size(), 0);
    }

    /// Run a real handshake against the local daemon (if present) and
    /// observe registry events. On the developer host this surfaces
    /// every audio sink + source + stream PipeWire reports; CI hosts
    /// without a daemon just stay on the empty list with no signals.
    /// In either case the test crashes or hangs if the registry path
    /// regresses.
    void registryEnumerationSurvivesLiveHandshake()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QSignalSpy addedSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::nodeAdded);
        QSignalSpy removedSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::nodeRemoved);
        QSignalSpy connectedSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::connectedChanged);

        conn.connectToDaemon();
        // 250ms is plenty for PipeWire's local-socket handshake +
        // initial registry walk on a developer host; on a no-daemon
        // host the wait just times out cleanly.
        QTest::qWait(250);

        if (conn.isConnected()) {
            // Daemon present — registry should have surfaced at least
            // the system's default audio sink on session-manager-managed
            // hosts (the developer workstation case). Bare-daemon hosts
            // (headless containers running pipewired without WirePlumber
            // / no audio modules, video-only embedded targets) can
            // legitimately surface zero audio nodes — detect that case
            // via the absence of a WirePlumber-published default-sink
            // name and downgrade to a QSKIP rather than asserting the
            // registry walk is broken. Every node we did receive must
            // still carry a valid audio media class.
            QVERIFY(connectedSpy.count() >= 1);
            const auto nodes = conn.nodes();
            if (nodes.empty() && conn.defaultSinkName().isEmpty()) {
                QSKIP("daemon connected but no session manager / audio nodes — bare-daemon host");
            }
            QVERIFY2(!nodes.empty(),
                     "connected daemon with session manager surfaced zero audio nodes — registry walk likely broken");
            for (auto* node : nodes) {
                QVERIFY(node != nullptr);
                const QString mc = node->mediaClass();
                QVERIFY2(mc == QLatin1String("Audio/Sink") || mc == QLatin1String("Audio/Source")
                             || mc == QLatin1String("Stream/Output/Audio") || mc == QLatin1String("Stream/Input/Audio"),
                         qPrintable(QStringLiteral("unexpected mediaClass: %1").arg(mc)));
            }
            // disconnectFromDaemon() should fire a nodeRemoved for at
            // least every node still tracked at disconnect time. Use >=
            // rather than == because the daemon may add or remove nodes
            // mid-shutdown (a hot-plugged USB sink, a Firefox stream
            // ending) and exact-equality would flake on a live host.
            //
            // Snapshot the spy count FIRST, then re-fetch the tracked
            // node count for expectedRemovals. The opposite order (or
            // reusing the stale `nodes` snapshot from the media-class
            // loop) leaves a window where a spurious nodeRemoved
            // arriving between the two reads would simultaneously drop
            // a node from the tracked snapshot AND get folded into
            // removedBeforeDisconnect, masking the disconnect-driven
            // removal count. Reading the spy first ensures any
            // spurious removal counted in the baseline is also
            // reflected in the tracked snapshot we compare against.
            const int removedBeforeDisconnect = removedSpy.count();
            const int expectedRemovals = conn.nodes().size();
            conn.disconnectFromDaemon();
            QTest::qWait(150);
            const int disconnectDrivenRemovals = removedSpy.count() - removedBeforeDisconnect;
            QVERIFY2(disconnectDrivenRemovals >= expectedRemovals,
                     qPrintable(QStringLiteral("disconnect-driven removed %1, expected >= %2")
                                    .arg(disconnectDrivenRemovals)
                                    .arg(expectedRemovals)));
            QCOMPARE(conn.nodes().size(), 0);
        } else {
            // No daemon — confirm the absence is graceful: no nodes,
            // no spurious adds. QSKIP after the negative checks so CI
            // dashboards distinguish the no-daemon path from a real
            // pass on the live-handshake branch.
            QCOMPARE(addedSpy.count(), 0);
            QCOMPARE(conn.nodes().size(), 0);
            QSKIP("no PipeWire daemon present; live-handshake branch not exercised");
        }
    }

    /// Pin the role-name → integer mapping. QML consumers key on the
    /// string names, but breaking the int values would silently break
    /// any C++ caller doing `data(idx, PwNodeModel::VolumesRole)`. The
    /// only way to evolve these is intentionally, with a coordinated
    /// QML migration.
    void roleNamesArePinned()
    {
        using PhosphorServicePipeWire::PwNodeModel;
        PwNodeModel model;
        const auto names = model.roleNames();

        // Role-name table: each entry pins one role's expected
        // QByteArray name. Named-pair pattern (vs. a flat sequence of
        // bare QCOMPAREs) so a reordering regression surfaces the
        // specific role that drifted in the assertion message instead
        // of a generic "value mismatch" buried in a wall of identical-
        // looking lines.
        struct RoleNameCase
        {
            const char* roleName;
            int role;
            QByteArray expected;
        };
        const RoleNameCase nameCases[] = {
            {"NodeRole", int(PwNodeModel::NodeRole), QByteArrayLiteral("node")},
            {"IdRole", int(PwNodeModel::IdRole), QByteArrayLiteral("id")},
            {"NameRole", int(PwNodeModel::NameRole), QByteArrayLiteral("name")},
            {"NickRole", int(PwNodeModel::NickRole), QByteArrayLiteral("nick")},
            {"DescriptionRole", int(PwNodeModel::DescriptionRole), QByteArrayLiteral("description")},
            {"MediaClassRole", int(PwNodeModel::MediaClassRole), QByteArrayLiteral("mediaClass")},
            {"ChannelCountRole", int(PwNodeModel::ChannelCountRole), QByteArrayLiteral("channelCount")},
            {"VolumesRole", int(PwNodeModel::VolumesRole), QByteArrayLiteral("volumes")},
            {"MutedRole", int(PwNodeModel::MutedRole), QByteArrayLiteral("muted")},
            {"Qt::DisplayRole", int(Qt::DisplayRole), QByteArrayLiteral("display")},
        };
        for (const auto& c : nameCases) {
            const QByteArray actual = names.value(c.role);
            QVERIFY2(actual == c.expected,
                     qPrintable(QStringLiteral("role-name regression: %1 expected '%2', got '%3'")
                                    .arg(QLatin1String(c.roleName))
                                    .arg(QLatin1String(c.expected))
                                    .arg(QLatin1String(actual))));
        }

        // Completeness guard: the table above spot-checks each known role,
        // but a newly-added role (e.g. UserRole+10) would slip past it
        // silently — names.value() on an unlisted role is simply never
        // queried. Pin the total count to the pinned table size so adding
        // a role to roleNames() forces a matching entry here.
        QCOMPARE(names.size(), int(std::size(nameCases)));

        // Role-value table: same pattern, for the int role enum values.
        // A reordering regression here breaks every C++ caller doing
        // `data(idx, PwNodeModel::VolumesRole)`, so the named-pair
        // diagnostic points the operator straight at which role's
        // integer slot changed.
        struct RoleValueCase
        {
            const char* roleName;
            int actual;
            int expected;
        };
        const RoleValueCase valueCases[] = {
            {"Qt::DisplayRole", int(Qt::DisplayRole), 0},
            {"NodeRole", int(PwNodeModel::NodeRole), int(Qt::UserRole) + 1},
            {"IdRole", int(PwNodeModel::IdRole), int(Qt::UserRole) + 2},
            {"NameRole", int(PwNodeModel::NameRole), int(Qt::UserRole) + 3},
            {"NickRole", int(PwNodeModel::NickRole), int(Qt::UserRole) + 4},
            {"DescriptionRole", int(PwNodeModel::DescriptionRole), int(Qt::UserRole) + 5},
            {"MediaClassRole", int(PwNodeModel::MediaClassRole), int(Qt::UserRole) + 6},
            {"ChannelCountRole", int(PwNodeModel::ChannelCountRole), int(Qt::UserRole) + 7},
            {"VolumesRole", int(PwNodeModel::VolumesRole), int(Qt::UserRole) + 8},
            {"MutedRole", int(PwNodeModel::MutedRole), int(Qt::UserRole) + 9},
        };
        for (const auto& c : valueCases) {
            QVERIFY2(c.actual == c.expected,
                     qPrintable(QStringLiteral("role-value regression: %1 expected %2, got %3")
                                    .arg(QLatin1String(c.roleName))
                                    .arg(c.expected)
                                    .arg(c.actual)));
        }
    }

    /// PwSinkModel / PwSourceModel / PwStreamModel pre-set their
    /// filters; verify the contract so a stray edit in the convenience
    /// constructors trips the test before it reaches a consumer.
    /// Compare as QSet so a functionally equivalent reordering of the
    /// internal filter list (e.g. swapping the order of the two stream
    /// classes) doesn't flake this test. Order is not part of the
    /// pinned contract; set membership is.
    void convenienceSubclassesPinTheirFilters()
    {
        PhosphorServicePipeWire::PwSinkModel sinks;
        PhosphorServicePipeWire::PwSourceModel sources;
        PhosphorServicePipeWire::PwStreamModel streams;
        const auto toSet = [](const QStringList& list) {
            return QSet<QString>(list.cbegin(), list.cend());
        };
        // Assert both set membership AND list size so a stutter regression
        // (e.g. {"Audio/Sink", "Audio/Sink"}) doesn't pass — QSet membership
        // alone would collapse the duplicate.
        QCOMPARE(sinks.mediaClasses().size(), 1);
        QCOMPARE(toSet(sinks.mediaClasses()), (QSet<QString>{QStringLiteral("Audio/Sink")}));
        QCOMPARE(sources.mediaClasses().size(), 1);
        QCOMPARE(toSet(sources.mediaClasses()), (QSet<QString>{QStringLiteral("Audio/Source")}));
        QCOMPARE(streams.mediaClasses().size(), 2);
        QCOMPARE(toSet(streams.mediaClasses()),
                 (QSet<QString>{QStringLiteral("Stream/Output/Audio"), QStringLiteral("Stream/Input/Audio")}));
    }

    /// Write APIs must survive being called for a non-existent node id
    /// and before connectToDaemon(). The dispatch goes via
    /// pw_loop_invoke, so a buggy implementation would either crash on
    /// the unique_ptr guard or leak the request struct.
    ///
    /// Observability: we exercise the early-out path explicitly by
    /// asserting state (disconnected before connect; still-connected
    /// after the unknown-id writes when a daemon is present), pin that
    /// the connection does not flip to `error` (which it would if a
    /// buggy implementation dereferenced a null proxy and then
    /// surfaced the failure through the core-error path), AND (when a
    /// live daemon is available) issue a known-good write to a real
    /// audio node afterwards and assert it still echoes via
    /// `propsChanged`. A regression that silently swallowed every
    /// subsequent write (e.g. by tripping a one-shot guard) would still
    /// pass the negative half of this test but fail the positive half.
    void writeAPIsAreSafeForUnknownTargets()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QSignalSpy errorSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::error);

        // Pre-connect writes must early-out cleanly (loop running but
        // no core/registry yet, so the node id lookup will fail).
        conn.writeVolumes(99999u, {0.5, 0.5});
        conn.writeMuted(99999u, true);
        QTest::qWait(50);
        // Pre-connect: still disconnected, no error surfaced.
        QCOMPARE(conn.isConnected(), false);
        QCOMPARE(errorSpy.count(), 0);

        // Post-connect writes for a non-existent id should still be
        // safe: the loop-thread handler logs at debug level and returns
        // without touching a proxy. The connection must survive without
        // an error fire and without flipping out of the connected state.
        conn.connectToDaemon();
        QTest::qWait(250);
        const bool wasConnected = conn.isConnected();
        conn.writeVolumes(99999u, {0.5});
        conn.writeMuted(99999u, false);
        QTest::qWait(100);

        if (wasConnected) {
            // Live daemon: the connection survived and stays connected
            // after the unknown-id writes. A regression here (e.g. null
            // deref in the loop handler) would either crash the test
            // binary or fire `error` and flip connected to false.
            QVERIFY(conn.isConnected());
            QCOMPARE(errorSpy.count(), 0);

            // Positive assertion: a follow-up known-good write must
            // still round-trip. Without this, a silent-swallow
            // regression (e.g. a one-shot guard that latched after the
            // bad write) would pass the negative half above.
            PhosphorServicePipeWire::PwNode* sink = nullptr;
            for (auto* node : conn.nodes()) {
                if (node && node->mediaClass() == QLatin1String("Audio/Sink")) {
                    sink = node;
                    break;
                }
            }
            if (sink) {
                // Opt-out for hosts where toggling a real sink's mute
                // state is unacceptable (active recording / call,
                // session manager that fights back hard, etc.). See
                // tests/CMakeLists.txt for the full env-var contract.
                //
                // Default: PHOSPHOR_PW_TESTS_ALLOW_WRITE unset OR set
                // to any value other than "0" — the live-write half
                // runs (preserving the historical behaviour for
                // anyone whose CI / dev workflow does NOT set the
                // variable). Set to "0" to skip the live write; the
                // negative half (unknown-id writes are safe) still
                // ran above, so the contract this test pins for
                // headless / no-daemon hosts is still exercised.
                const QByteArray allowWrite = qgetenv("PHOSPHOR_PW_TESTS_ALLOW_WRITE");
                if (allowWrite == QByteArrayLiteral("0")) {
                    QSKIP(
                        "PHOSPHOR_PW_TESTS_ALLOW_WRITE=0 — skipping live-write half "
                        "(mutates host audio state). The unknown-id no-crash half "
                        "above still ran.");
                }
                QSignalSpy propsSpy(sink, &PhosphorServicePipeWire::PwNode::propsChanged);
                // Flip mute to the opposite of the current value so the
                // daemon has work to do; if the node was already in the
                // requested state the echo may not fire and that's fine
                // for the contract this test pins (it's the live-write
                // path we care about).
                const bool flipTo = !sink->muted();
                sink->setMuted(flipTo);
                // Wait up to ~500ms for the echo to land. We don't
                // assert the echoed value (a parallel session manager
                // could re-flip it), only that the write path is still
                // alive and producing signals.
                propsSpy.wait(500);
                QVERIFY2(propsSpy.count() >= 1,
                         "subsequent known-good write produced no propsChanged echo (silent-swallow regression?)");
                // Restore the previous mute state so this test is
                // side-effect-clean for the developer host. Wait for
                // the restore to take effect by polling the observable
                // state (sink->muted() == !flipTo) instead of counting
                // propsChanged emissions: a parallel propsChanged from
                // a competing session manager can inflate the spy
                // counter without our restore write ever landing, and
                // conversely the daemon may coalesce our restore into
                // an existing in-flight emission so the count delta is
                // unreliable either way.
                sink->setMuted(!flipTo);
                QElapsedTimer restoreTimer;
                restoreTimer.start();
                while (sink->muted() != !flipTo && restoreTimer.elapsed() < 500) {
                    propsSpy.wait(50);
                }
                // Restore is best-effort cleanup: if a competing session
                // manager re-flipped the mute state between our restore
                // write and the echo wait, the observable state may
                // disagree with our write even though the write path
                // works. Log instead of failing so the test's actual
                // contract (the known-good write echoes) isn't dragged
                // down by cleanup flakiness.
                if (sink->muted() != !flipTo) {
                    QWARN("restore write did not converge to requested mute state (cleanup may have left dirty state)");
                }
            }
            // No sink available (rare: a connected daemon with zero
            // audio sinks). The negative half still pinned no-crash
            // and no error; we skip the positive half rather than
            // synthesise a fake assertion.
        } else {
            // No daemon: the writes early-out without a registry; we
            // only assert no crash and the disconnected state, then
            // QSKIP so CI dashboards distinguish the no-daemon path
            // from a real pass against a live daemon.
            QCOMPARE(conn.isConnected(), false);
            QCOMPARE(errorSpy.count(), 0);
            QSKIP("no PipeWire daemon present; live-write branch not exercised");
        }
    }

    /// PipeWireHost auto-connects on construction and forwards every
    /// PipeWireConnection signal. Verify the forwarding wires + the
    /// connection accessor.
    void hostAutoConnectsAndForwardsSignals()
    {
        PhosphorServicePipeWire::PipeWireHost host;
        QVERIFY(host.connection() != nullptr);
        // Signals must be wired so observers binding to the host see
        // changes without reaching for `.connection.connected`.
        QSignalSpy connSpy(&host, &PhosphorServicePipeWire::PipeWireHost::connectedChanged);
        QSignalSpy availSpy(&host, &PhosphorServicePipeWire::PipeWireHost::daemonAvailableChanged);
        // Host construction kicked off connectToDaemon() — give it a
        // 2000ms test-side budget (the library itself does not enforce
        // a connect timeout; this is purely how long the test waits)
        // so the handshake has every chance to land before we decide a
        // daemon isn't present. Poll-with-budget rather than a fixed
        // qWait so a no-daemon CI run exits at the first 200ms tick
        // instead of burning the full budget (matches the
        // connectIdempotent test's polling shape).
        {
            QElapsedTimer timer;
            timer.start();
            while (!host.isConnected() && timer.elapsed() < 2000) {
                QTest::qWait(200);
            }
        }
        if (!host.isConnected()) {
            // Surface a real skip in the CI dashboard rather than a
            // silent fallthrough; a "no-daemon" run looks identical to a
            // "real check" run otherwise, hiding regressions on the
            // forwarding path.
            QSKIP("no PipeWire daemon present");
        }
        QVERIFY(connSpy.count() >= 1);
        QVERIFY(availSpy.count() >= 1);
        // Property forwarding matches the connection's truth.
        QCOMPARE(host.defaultSinkName(), host.connection()->defaultSinkName());
        // reconnect() exercises the disconnectFromDaemon → connectToDaemon
        // cycle. After it returns, the connection accessor must still be
        // live and the connectedChanged spy must have observed at least
        // one more transition (the disconnect half of the cycle) plus
        // the second reconnect half if the daemon answered the new
        // handshake.
        const int reconnectBaseline = connSpy.count();
        host.reconnect();
        // Wait up to 2000ms for the reconnect cycle to surface a
        // connectedChanged emission (test-side budget; no library-side
        // timeout enforces this). 150ms was tight enough on slow hosts
        // that the reconnect was still in-flight when the isConnected()
        // guard ran, silently skipping the post-reconnect spy-count
        // assertion.
        connSpy.wait(2000);
        QVERIFY(host.connection() != nullptr);
        if (host.isConnected())
            QVERIFY(connSpy.count() >= reconnectBaseline + 1);
    }

    /// WirePlumber's default metadata should surface a non-empty
    /// default sink name after a successful handshake when running
    /// against a real session-managed PipeWire instance. No-daemon
    /// hosts and bare-daemon (no WirePlumber) hosts both legitimately
    /// stay empty — assert only the daemon-and-WirePlumber-present
    /// path to avoid false failures.
    void defaultSinkNameSurfacesFromWirePlumber()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QSignalSpy sinkSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::defaultSinkNameChanged);
        conn.connectToDaemon();
        QTest::qWait(300);
        // Surface a real skip in the CI dashboard rather than a silent
        // pass; a "no-daemon" run looks identical to a "real check"
        // run otherwise, which hides regressions on the daemon path.
        if (!conn.isConnected())
            QSKIP("no PipeWire daemon present");
        // After handshake + initial registry walk, defaults should
        // have landed if WirePlumber is running. If they didn't, we
        // accept that (bare-daemon edge case) without failing.
        if (!conn.defaultSinkName().isEmpty())
            QVERIFY(sinkSpy.count() >= 1);
    }

    /// Models hooked to a live connection should populate based on the
    /// daemon's actual audio nodes — sinks-only model sees only sinks,
    /// streams model sees only streams, etc. On a no-daemon host the
    /// models stay empty.
    void modelsFilterLiveRegistry()
    {
        using PhosphorServicePipeWire::PwNodeModel;
        PhosphorServicePipeWire::PipeWireConnection conn;
        PhosphorServicePipeWire::PwSinkModel sinks;
        PhosphorServicePipeWire::PwSourceModel sources;
        PhosphorServicePipeWire::PwStreamModel streams;
        sinks.setConnection(&conn);
        sources.setConnection(&conn);
        streams.setConnection(&conn);

        conn.connectToDaemon();
        QTest::qWait(300);

        // Surface a real skip in the CI dashboard rather than a silent
        // pass; a "no-daemon" run looks identical to a "real check"
        // run otherwise, which hides regressions on the daemon path.
        if (!conn.isConnected())
            QSKIP("no PipeWire daemon present");

        // Every row of each model must match the model's filter.
        for (int i = 0; i < sinks.rowCount(); ++i) {
            QCOMPARE(sinks.data(sinks.index(i), PwNodeModel::MediaClassRole).toString(), QStringLiteral("Audio/Sink"));
        }
        for (int i = 0; i < sources.rowCount(); ++i) {
            QCOMPARE(sources.data(sources.index(i), PwNodeModel::MediaClassRole).toString(),
                     QStringLiteral("Audio/Source"));
        }
        for (int i = 0; i < streams.rowCount(); ++i) {
            const QString mc = streams.data(streams.index(i), PwNodeModel::MediaClassRole).toString();
            QVERIFY2(mc == QLatin1String("Stream/Output/Audio") || mc == QLatin1String("Stream/Input/Audio"),
                     qPrintable(mc));
        }
        // Positive assertion: on a connected daemon the sinks model
        // must surface at least one row. A connected PipeWire instance
        // always carries a default audio sink, so a zero-row result
        // here means setConnection() snapshotted before any nodes had
        // arrived and the model isn't observing later registry adds —
        // a regression the row-by-row loops above would silently miss
        // (an empty model trivially satisfies every "matches filter"
        // assertion). Streams and sources can legitimately be zero on
        // a quiet host with no input devices, so we only pin sinks.
        QVERIFY2(sinks.rowCount() > 0,
                 qPrintable(QStringLiteral("PwSinkModel observed zero sinks on a connected daemon — likely a "
                                           "setConnection snapshot regression that ignores later registry adds")));
    }

    /// Exercise the dangling-pointer recovery path inside PwNodeModel.
    /// PwNodeModel uses a `QPointer<PipeWireConnection>` to detect a
    /// connection that was destroyed externally without going through
    /// `setConnection(nullptr)` first. When that happens, the next call
    /// to a slot that walks the connection (e.g. `setMediaClasses`)
    /// must drop the now-dangling node rows via beginResetModel /
    /// endResetModel rather than dereferencing the QPointer.
    ///
    /// Setup: attach a PwSinkModel to a heap-allocated PipeWireConnection,
    /// delete the connection externally so the QPointer goes null, then
    /// drive `setMediaClasses` to force the recovery path. The test
    /// passes if the model handles the dangling-pointer case cleanly
    /// (no crash, no asserts) and ends up with zero rows.
    ///
    /// We don't attempt to populate rows pre-deletion: the test runs on
    /// both daemon-present and no-daemon hosts, and the recovery path
    /// doesn't depend on whether rows existed — it depends only on the
    /// QPointer-null branch in `setMediaClasses` being reachable. The
    /// branch fires whenever the model has an attached (now-dangling)
    /// connection and setMediaClasses is called with a different filter.
    void pwNodeModelSurvivesExternalConnectionDestruction()
    {
        PhosphorServicePipeWire::PwSinkModel model;
        // Heap-allocate the connection so we can destroy it explicitly
        // mid-test without waiting for stack unwind ordering.
        auto* conn = new PhosphorServicePipeWire::PipeWireConnection();
        model.setConnection(conn);
        QCOMPARE(model.connection(), conn);

        // Destroy the connection externally — bypassing
        // setConnection(nullptr). After this, the QPointer<PipeWireConnection>
        // inside the model's Private is observably null, but the
        // model's row bookkeeping (nodes, rowIndex, nodeWires,
        // connectionWires) is whatever state the rebuild left behind.
        // Qt auto-releases the QMetaObject::Connection wires when their
        // sender died, so callback delivery is already safe — the QHash
        // entries themselves are stale.
        delete conn;
        QCOMPARE(model.connection(), nullptr);

        // Drive the recovery path. Changing mediaClasses while the
        // connection is dangling routes through the `else if
        // (!d->nodes.isEmpty())` branch in setMediaClasses, which is
        // the dangling-recovery branch we want to exercise. Even when
        // nodes is empty (no-daemon host, no rows ever populated), the
        // call must not crash and must leave the model in a coherent
        // zero-row state.
        QSignalSpy mediaClassesSpy(&model, &PhosphorServicePipeWire::PwNodeModel::mediaClassesChanged);
        model.setMediaClasses({QStringLiteral("Audio/Source")});
        // mediaClassesChanged must fire exactly once for the filter
        // change — the recovery path emits after the rebuild/reset
        // completes, same as the normal path.
        QCOMPARE(mediaClassesSpy.count(), 1);
        QCOMPARE(model.mediaClasses(), QStringList{QStringLiteral("Audio/Source")});
        // Post-recovery the model must report zero rows: the dangling
        // PwNode pointers have been flushed (either via the reset
        // branch, or because there were never any rows to begin with).
        QCOMPARE(model.rowCount(), 0);
        // A subsequent data() probe at the (invalid) row 0 must return
        // a default QVariant rather than dereferencing a dangling
        // PwNode*. This is belt-and-braces — rowCount being 0 already
        // implies any sensible view stops here — but it pins the
        // contract for direct C++ callers.
        QVERIFY(!model.data(model.index(0), PhosphorServicePipeWire::PwNodeModel::NodeRole).isValid());
    }

    /// Pin the pre-connect empty-defaults baseline plus the
    /// post-handshake survival contract: when no default metadata has
    /// been bound yet (pre-connect, or on a bare-daemon host with no
    /// WirePlumber), `defaultSinkName()` / `defaultSourceName()` must
    /// stay empty and the connection must not crash. On the
    /// daemon-connected path we only assert that the connection stayed
    /// up — we can't distinguish "bare-daemon, no WirePlumber" from
    /// "WirePlumber present" without probing further, so this is
    /// post-handshake survival, NOT a daemon-side empty-defaults
    /// assertion. The populated-default path lives in
    /// `defaultSinkNameSurfacesFromWirePlumber`. CLI sentinel handling
    /// (resolveTarget) is the CLI's own responsibility and the CLI
    /// rejects them up-front in cmdSetDefault.
    void connectionSurvivesPostHandshake()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        // Pre-connect: no metadata has been bound yet. These
        // assertions are checked unconditionally so the test reports
        // a real verification regardless of host configuration.
        QCOMPARE(conn.defaultSinkName(), QString());
        QCOMPARE(conn.defaultSourceName(), QString());

        // Post-connect on a bare-daemon (no WirePlumber) host the
        // default metadata never lands; on a no-daemon host the
        // connection never reaches connected. Either way the defaults
        // remain empty and the connection survives.
        conn.connectToDaemon();
        QTest::qWait(250);
        if (!conn.isConnected()) {
            // No daemon at all — surface a real skip in the CI dashboard
            // so the post-connect contract isn't silently treated as
            // verified on a no-daemon host.
            QSKIP("no PipeWire daemon present");
        }
        // Daemon connected. We can't distinguish "bare-daemon, no
        // WirePlumber" from "WirePlumber present" without probing
        // further, but either way the connection must survive without
        // crashing. The populated-default path is covered by
        // `defaultSinkNameSurfacesFromWirePlumber`; here we only pin
        // that an empty-default daemon doesn't crash the connection.
        QVERIFY(conn.isConnected());
    }

    /// After disconnectFromDaemon() any tracked nodes must be removed
    /// (the connection emits `nodeRemoved` for each one as part of
    /// teardown so observers/models can detach before the PwNode
    /// destructors run). On a no-daemon host the registry never
    /// populated, so the spy stays at zero, which is also a valid clean
    /// teardown.
    void nodeRemovedAfterDisconnect()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QSignalSpy removedSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::nodeRemoved);
        conn.connectToDaemon();
        QTest::qWait(250);
        // Snapshot the removed-count FIRST, then the tracked-node count.
        // The opposite order leaves a window where a spurious nodeRemoved
        // arriving between the two reads would simultaneously drop a
        // node from `trackedBeforeDisconnect` AND get folded into
        // `removedBeforeDisconnect`, masking the disconnect-driven
        // removal count. Reading the spy first ensures any spurious
        // removal counted in the baseline is also reflected in the
        // tracked snapshot we compare against.
        const int removedBeforeDisconnect = removedSpy.count();
        const int trackedBeforeDisconnect = conn.nodes().size();
        conn.disconnectFromDaemon();
        QTest::qWait(150);
        // Every node that was being tracked at disconnect time must
        // have a matching nodeRemoved emission attributable to the
        // disconnect itself. Use >= because the daemon may also remove
        // nodes mid-shutdown.
        const int disconnectDrivenRemovals = removedSpy.count() - removedBeforeDisconnect;
        QVERIFY2(disconnectDrivenRemovals >= trackedBeforeDisconnect,
                 qPrintable(QStringLiteral("disconnect-driven removed %1, expected >= %2")
                                .arg(disconnectDrivenRemovals)
                                .arg(trackedBeforeDisconnect)));
        // Post-disconnect the snapshot must be empty regardless of
        // whether a daemon was present.
        QCOMPARE(conn.nodes().size(), 0);
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
