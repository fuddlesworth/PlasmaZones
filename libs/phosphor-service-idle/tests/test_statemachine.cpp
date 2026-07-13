// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Unit test for the idle stage state machine. It drives the machine with a fake
// idle source that fires the idled / resumed edges directly, so the whole ladder
// (advance on idle, reset on resume, sort by timeout, rebuild on reconfigure) is
// exercised deterministically with no live compositor. The fake links nothing
// from phosphor-wayland; the production IdleNotifier adapter is exercised through
// the CLI demo against a real session.

#include "idlestatemachine.h"

#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <vector>

using namespace PhosphorServiceIdle;
using namespace std::chrono_literals;

namespace {

// A test double for IIdleSource. fireIdle() / fireResume() simulate the edges the
// real ext-idle-notify client would deliver.
class FakeIdleSource : public IIdleSource
{
public:
    void setTimeout(std::chrono::milliseconds timeout) override
    {
        m_timeout = timeout;
    }
    [[nodiscard]] std::chrono::milliseconds timeout() const override
    {
        return m_timeout;
    }

    void fireIdle()
    {
        Q_EMIT idled();
    }
    void fireResume()
    {
        Q_EMIT resumed();
    }

    /// The real Wayland-backed source can fail to arm (no seat yet). The fake has to be able
    /// to say so, or the machine's all-of fold over its sources is pinned by nothing.
    [[nodiscard]] bool isArmed() const override
    {
        return m_armed;
    }
    void setArmed(bool armed)
    {
        m_armed = armed;
    }

private:
    std::chrono::milliseconds m_timeout{0};
    bool m_armed = true;
};

// Records the fakes it hands out so the test can fire their edges. The fakes are
// owned by the state machine; the recorded pointers stay valid until the machine
// rebuilds (which destroys them).
struct RecordingFactory
{
    std::vector<FakeIdleSource*> created;

    IdleSourceFactory factory()
    {
        return [this]() -> IIdleSource::Ptr {
            auto source = std::make_unique<FakeIdleSource>();
            created.push_back(source.get());
            return source;
        };
    }
};

} // namespace

class IdleStateMachineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyStagesStaysActive();
    void singleStageAdvancesAndResets();
    void multiStageMonotonicAdvance();
    void resumeFromDeepStageResets();
    void stagesSortedByAscendingTimeout();
    void reconfigureWhileIdleResets();
    void clearStagesWhileIdleResumes();
    void monitoringPauseDisarmsAndResumeRearms();
    void redundantSetStagesIsNoOpAndDropsInvalid();
    void isArmedReportsLadderHealth();
};

void IdleStateMachineTest::emptyStagesStaysActive()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());
    machine.setStages({});

    // Nothing is watching, so nothing can report. Three separate doc comments hang the
    // daemon's arm-retry on this exact answer and not one test pinned it.
    QVERIFY(!machine.isArmed());
    QCOMPARE(machine.currentStage(), 0);
    QVERIFY(!machine.isIdle());
    QVERIFY(machine.currentStageName().isEmpty());
    QVERIFY(rec.created.empty());
}

void IdleStateMachineTest::singleStageAdvancesAndResets()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());
    QSignalSpy idledSpy(&machine, &IdleStateMachine::idled);
    QSignalSpy resumedSpy(&machine, &IdleStateMachine::resumed);
    QSignalSpy changedSpy(&machine, &IdleStateMachine::currentStageChanged);

    machine.setStages({{QStringLiteral("dim"), 5min}});
    QCOMPARE(rec.created.size(), size_t{1});
    QCOMPARE(rec.created.at(0)->timeout(), std::chrono::milliseconds(5min));

    rec.created.at(0)->fireIdle();
    QCOMPARE(machine.currentStage(), 1);
    QVERIFY(machine.isIdle());
    QCOMPARE(machine.currentStageName(), QStringLiteral("dim"));
    QCOMPARE(idledSpy.count(), 1);
    QCOMPARE(idledSpy.takeFirst().at(0).toInt(), 1);
    QCOMPARE(changedSpy.count(), 1);

    rec.created.at(0)->fireResume();
    QCOMPARE(machine.currentStage(), 0);
    QVERIFY(!machine.isIdle());
    QCOMPARE(resumedSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 2);
}

void IdleStateMachineTest::multiStageMonotonicAdvance()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());
    QSignalSpy idledSpy(&machine, &IdleStateMachine::idled);

    machine.setStages({{QStringLiteral("dim"), 5min}, {QStringLiteral("lock"), 10min}, {QStringLiteral("off"), 15min}});
    QCOMPARE(rec.created.size(), size_t{3});

    rec.created.at(0)->fireIdle();
    QCOMPARE(machine.currentStage(), 1);
    rec.created.at(1)->fireIdle();
    QCOMPARE(machine.currentStage(), 2);
    rec.created.at(2)->fireIdle();
    QCOMPARE(machine.currentStage(), 3);
    QCOMPARE(machine.currentStageName(), QStringLiteral("off"));

    QCOMPARE(idledSpy.count(), 3);
    QCOMPARE(idledSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(idledSpy.at(1).at(0).toInt(), 2);
    QCOMPARE(idledSpy.at(2).at(0).toInt(), 3);
}

void IdleStateMachineTest::resumeFromDeepStageResets()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());

    machine.setStages({{QStringLiteral("dim"), 5min}, {QStringLiteral("lock"), 10min}, {QStringLiteral("off"), 15min}});
    rec.created.at(0)->fireIdle();
    rec.created.at(1)->fireIdle();
    rec.created.at(2)->fireIdle();
    QCOMPARE(machine.currentStage(), 3);

    QSignalSpy resumedSpy(&machine, &IdleStateMachine::resumed);
    // Any source resuming resets the whole ladder; the first resume wins and later
    // resumes from other fired sources are no-ops. (Source 0 here is an arbitrary
    // choice, not a requirement.)
    rec.created.at(0)->fireResume();
    QCOMPARE(machine.currentStage(), 0);
    QCOMPARE(resumedSpy.count(), 1);

    rec.created.at(1)->fireResume();
    rec.created.at(2)->fireResume();
    QCOMPARE(machine.currentStage(), 0);
    QCOMPARE(resumedSpy.count(), 1); // still one: later resumes are no-ops
}

void IdleStateMachineTest::stagesSortedByAscendingTimeout()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());

    // Supplied out of order; the machine sorts by timeout so stage index is
    // monotonic in inactivity.
    machine.setStages({{QStringLiteral("off"), 15min}, {QStringLiteral("dim"), 5min}, {QStringLiteral("lock"), 10min}});

    const QList<IdleStage> stages = machine.stages();
    QCOMPARE(stages.size(), 3);
    QCOMPARE(stages.at(0).name, QStringLiteral("dim"));
    QCOMPARE(stages.at(1).name, QStringLiteral("lock"));
    QCOMPARE(stages.at(2).name, QStringLiteral("off"));
    QCOMPARE(rec.created.at(0)->timeout(), std::chrono::milliseconds(5min));
    QCOMPARE(rec.created.at(2)->timeout(), std::chrono::milliseconds(15min));

    rec.created.at(0)->fireIdle();
    QCOMPARE(machine.currentStageName(), QStringLiteral("dim"));
}

void IdleStateMachineTest::reconfigureWhileIdleResets()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());

    machine.setStages({{QStringLiteral("dim"), 5min}, {QStringLiteral("lock"), 10min}});
    rec.created.at(0)->fireIdle();
    rec.created.at(1)->fireIdle();
    QCOMPARE(machine.currentStage(), 2);

    QSignalSpy resumedSpy(&machine, &IdleStateMachine::resumed);
    // Reconfiguring rebuilds the sources and clears any prior idle state.
    machine.setStages({{QStringLiteral("sleep"), 20min}});
    QCOMPARE(machine.currentStage(), 0);
    QVERIFY(!machine.isIdle());
    QCOMPARE(resumedSpy.count(), 1);

    // The new ladder works from active.
    FakeIdleSource* fresh = rec.created.back();
    fresh->fireIdle();
    QCOMPARE(machine.currentStage(), 1);
    QCOMPARE(machine.currentStageName(), QStringLiteral("sleep"));
}

// Emptying the ladder WHILE IDLE must resume, not just disarm.
//
// reconfigureWhileIdleResets above only covers replacing one ladder with another. This pins
// the library's contract for clearing it: an empty ladder means "nothing is being watched",
// and a machine that stayed latched at idle would report an idleness it can no longer
// observe. So the state machine resumes on the way down, and an optimisation that skipped
// rebuild() for an empty ladder would break that silently with no other test noticing.
//
// Deliberately NOT justified by "this is what the PlasmaZones daemon does when the user
// turns pause-when-idle off". It is not — the daemon leaves its ladder armed for exactly
// the reason this test describes, because an empty ladder cannot tell it the seat went idle
// while the feature was off (see src/daemon/daemon/idle.cpp). Citing a consumer that behaves
// the opposite way would be an argument for reintroducing the bug that design avoids.
void IdleStateMachineTest::clearStagesWhileIdleResumes()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());

    machine.setStages({{QStringLiteral("decorations"), 30s}});
    rec.created.at(0)->fireIdle();
    QCOMPARE(machine.currentStage(), 1);
    QVERIFY(machine.isIdle());

    QSignalSpy resumedSpy(&machine, &IdleStateMachine::resumed);
    machine.setStages({});

    QCOMPARE(resumedSpy.count(), 1);
    QCOMPARE(machine.currentStage(), 0);
    QVERIFY(!machine.isIdle());
    QVERIFY(machine.stages().isEmpty());
}

void IdleStateMachineTest::monitoringPauseDisarmsAndResumeRearms()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());
    QVERIFY(machine.isMonitoringEnabled());

    machine.setStages({{QStringLiteral("dim"), 5min}, {QStringLiteral("lock"), 10min}});
    QCOMPARE(rec.created.size(), size_t{2});
    rec.created.at(0)->fireIdle();
    rec.created.at(1)->fireIdle();
    QCOMPARE(machine.currentStage(), 2);

    QSignalSpy resumedSpy(&machine, &IdleStateMachine::resumed);
    // Disabling monitoring (idle inhibited) tears the sources down and resets to
    // active, so the compositor delivers no further idle notifications.
    machine.setMonitoringEnabled(false);
    QVERIFY(!machine.isMonitoringEnabled());
    QCOMPARE(machine.currentStage(), 0);
    QCOMPARE(resumedSpy.count(), 1);

    // Re-enabling re-arms the same ladder; the prior fakes were destroyed, so the
    // factory hands out fresh ones.
    const size_t before = rec.created.size();
    machine.setMonitoringEnabled(true);
    QVERIFY(machine.isMonitoringEnabled());
    QCOMPARE(rec.created.size(), before + 2);
    rec.created.at(before)->fireIdle();
    QCOMPARE(machine.currentStage(), 1);
    QCOMPARE(machine.currentStageName(), QStringLiteral("dim"));

    // Setting the same state again is a no-op: no rebuild, no fresh sources.
    const size_t afterReenable = rec.created.size();
    machine.setMonitoringEnabled(true);
    QCOMPARE(rec.created.size(), afterReenable);
}

void IdleStateMachineTest::redundantSetStagesIsNoOpAndDropsInvalid()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());
    QSignalSpy stagesSpy(&machine, &IdleStateMachine::stagesChanged);

    machine.setStages({{QStringLiteral("dim"), 5min}, {QStringLiteral("lock"), 10min}});
    QCOMPARE(rec.created.size(), size_t{2});
    QCOMPARE(stagesSpy.count(), 1);

    // The same ladder supplied in a different order sorts equal, so it is a no-op:
    // the live sources are NOT torn down (no fresh fakes) and stagesChanged does
    // not re-fire.
    machine.setStages({{QStringLiteral("lock"), 10min}, {QStringLiteral("dim"), 5min}});
    QCOMPARE(rec.created.size(), size_t{2});
    QCOMPARE(stagesSpy.count(), 1);

    // The machine itself drops non-positive timeouts (a direct C++ caller bypasses
    // the facade filter): an all-invalid ladder clears the stages, which is a real
    // change from the prior non-empty ladder.
    machine.setStages({{QStringLiteral("bad"), 0ms}, {QStringLiteral("worse"), std::chrono::milliseconds(-5)}});
    QVERIFY(machine.stages().isEmpty());
    QCOMPARE(machine.currentStage(), 0);
    QCOMPARE(stagesSpy.count(), 2);

    // Re-applying the now-empty ladder is again a no-op.
    machine.setStages({});
    QCOMPARE(stagesSpy.count(), 2);
}

QTEST_GUILESS_MAIN(IdleStateMachineTest)
#include "test_statemachine.moc"

// isArmed() answers "is anything WRONG", which is not the same question as "is anything
// watching" — and the difference is load-bearing, because the daemon responds to false by
// rebuilding the ladder and, after a bounded number of tries, switching idle detection off
// for the whole session. Three states have to be told apart:
//   a healthy ladder                 -> true
//   a source that would not arm      -> false  (the login race this exists for)
//   a ladder INHIBITED on purpose    -> true   (paused, not broken)
// The third is the one that bit: an inhibited machine tears its sources down, so an all-of
// over an empty list reported failure and would have killed a perfectly good feature.
void IdleStateMachineTest::isArmedReportsLadderHealth()
{
    RecordingFactory rec;
    IdleStateMachine machine(rec.factory());
    machine.setStages({{QStringLiteral("a"), 1000ms}, {QStringLiteral("b"), 2000ms}});
    QVERIFY(machine.isArmed());
    QCOMPARE(rec.created.size(), 2);

    // ONE unarmed stage out of two is enough: a ladder is only as armed as its weakest rung,
    // and a stage that never fires is a stage whose idleness is never reported.
    rec.created.at(1)->setArmed(false);
    QVERIFY(!machine.isArmed());
    rec.created.at(1)->setArmed(true);
    QVERIFY(machine.isArmed());

    // Inhibition is deliberate, so it reports HEALTHY even though it is watching nothing.
    machine.setMonitoringEnabled(false);
    QVERIFY(machine.isArmed());
    machine.setMonitoringEnabled(true);
    QVERIFY(machine.isArmed());
}
