// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Facade test for the public IdleService. The compositor-driven idle advance
// needs a live session, but the wiring that does not (stage configuration round
// trip + sort, inhibition ref-count toggling, inert defaults) is deterministic
// without a compositor (guiless QCoreApplication — no QPA at all) and pinned
// here. The stage-advance path itself is covered by the state-machine unit
// test against a fake source.
//
// Coverage boundary: the inhibit -> disarm-the-ladder bridge (inhibitedChanged
// driving setMonitoringEnabled) cannot be observed end-to-end here because the
// facade wires the real IdleNotifierSource factory and a guiless run has no
// compositor to fire a stage. Its two halves are each fully unit-tested:
// setMonitoringEnabled disarm/re-arm in test_statemachine, and the inhibition
// cookie edges in test_inhibition. The live composite is the CLI-validated path.

#include <PhosphorServiceIdle/IdleService.h>

#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

using namespace PhosphorServiceIdle;

class IdleFacadeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsActiveAndUninhibited();
    void stagesRoundTripSortedByTimeout();
    void nonPositiveTimeoutsDropped();
    void emptyStagesClears();
    void inhibitTogglesInhibited();
};

namespace {
QVariantMap stage(const QString& name, int timeoutMs)
{
    QVariantMap map;
    map.insert(QLatin1String("name"), name);
    map.insert(QLatin1String("timeoutMs"), timeoutMs);
    return map;
}
} // namespace

void IdleFacadeTest::startsActiveAndUninhibited()
{
    IdleService service;
    QCOMPARE(service.currentStage(), 0);
    QVERIFY(!service.isIdle());
    QVERIFY(!service.isInhibited());
    QVERIFY(service.currentStageName().isEmpty());
    QVERIFY(service.stages().isEmpty());
}

void IdleFacadeTest::stagesRoundTripSortedByTimeout()
{
    IdleService service;
    QSignalSpy spy(&service, &IdleService::stagesChanged);

    service.setStages({stage(QStringLiteral("off"), 900000), stage(QStringLiteral("dim"), 300000),
                       stage(QStringLiteral("lock"), 600000)});
    QCOMPARE(spy.count(), 1);

    const QVariantList out = service.stages();
    QCOMPARE(out.size(), 3);
    QCOMPARE(out.at(0).toMap().value(QLatin1String("name")).toString(), QStringLiteral("dim"));
    QCOMPARE(out.at(0).toMap().value(QLatin1String("timeoutMs")).toInt(), 300000);
    QCOMPARE(out.at(1).toMap().value(QLatin1String("name")).toString(), QStringLiteral("lock"));
    QCOMPARE(out.at(2).toMap().value(QLatin1String("name")).toString(), QStringLiteral("off"));

    // Re-applying the same ladder is a no-op: the forwarded stagesChanged is
    // edge-only, so it must not fire again.
    service.setStages({stage(QStringLiteral("off"), 900000), stage(QStringLiteral("dim"), 300000),
                       stage(QStringLiteral("lock"), 600000)});
    QCOMPARE(spy.count(), 1);
}

void IdleFacadeTest::nonPositiveTimeoutsDropped()
{
    IdleService service;
    service.setStages(
        {stage(QStringLiteral("dim"), 300000), stage(QStringLiteral("bad"), 0), stage(QStringLiteral("worse"), -5)});
    const QVariantList out = service.stages();
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0).toMap().value(QLatin1String("name")).toString(), QStringLiteral("dim"));
}

void IdleFacadeTest::emptyStagesClears()
{
    IdleService service;
    service.setStages({stage(QStringLiteral("dim"), 300000)});
    QCOMPARE(service.stages().size(), 1);
    service.setStages({});
    QVERIFY(service.stages().isEmpty());
}

void IdleFacadeTest::inhibitTogglesInhibited()
{
    IdleService service;
    QSignalSpy spy(&service, &IdleService::inhibitedChanged);

    const int cookie = service.inhibit();
    QVERIFY(service.isInhibited());
    QCOMPARE(spy.count(), 1);

    QVERIFY(service.release(cookie));
    QVERIFY(!service.isInhibited());
    QCOMPARE(spy.count(), 2);

    QVERIFY(!service.release(cookie)); // double release is a no-op
    QCOMPARE(spy.count(), 2);
}

QTEST_GUILESS_MAIN(IdleFacadeTest)
#include "test_facade.moc"
