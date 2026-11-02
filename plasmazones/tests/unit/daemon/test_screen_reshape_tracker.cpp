// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins ScreenReshapeTracker, the daemon's bookkeeping for a screen whose shape
// changes under its assignment verdict: the consistency test a compositor
// pixel-size report must pass before it applies, the parked / awaited state per
// connector across the orders a report and its QScreen change can arrive in,
// and the reshape records that keep a verdict from being absorbed or applied
// twice. Each case drives the repeat its fix exists for. The last cases run the
// daemon's admit step (admitPixelSizeReport) over a real ScreenManager, whose
// pending reports are what keeps a parked connector out of another
// connector's apply.

#include <QHash>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTest>

#include "FakePhysicalScreenSource.h"

#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/PhysicalScreen.h>

#include <functional>

#include "common/daemonscreenscale.h"
#include "daemon/daemon/screenreshapetracker.h"

using namespace PlasmaZones;
using Action = ScreenReshapeTracker::ReportAction;
using Facts = ScreenReshapeTracker::ReportFacts;

namespace {

const QString Dp1 = QStringLiteral("DP-1");

/// 2560x1440 at 150 % is a 1707x960 logical screen, measured by a mapped
/// sensor.
Facts consistentReport()
{
    return Facts{.tracked = true,
                 .pendingSize = QSize(2560, 1440),
                 .logicalSize = QSize(1707, 960),
                 .logicalScale = 1.5,
                 .scaleSensor = true,
                 .scaleSensorMapped = true};
}

/// The same report against a scale it does not agree with.
Facts inconsistentReport()
{
    Facts facts = consistentReport();
    facts.logicalScale = 2.0;
    return facts;
}

/// The same report before the screen's sensor has mapped, while the scale is
/// still the coarse integer fallback.
Facts unmappedReport()
{
    Facts facts = inconsistentReport();
    facts.scaleSensorMapped = false;
    return facts;
}

Facts clearedReport()
{
    Facts facts = consistentReport();
    facts.pendingSize = QSize();
    return facts;
}

} // namespace

class TestScreenReshapeTracker : public QObject
{
    Q_OBJECT

private:
    /// The daemon's snapshot and the registry's current shape, as the tests
    /// move them.
    QHash<QString, ScreenReshapeTracker::SnapshotEntry> m_snapshot;
    QHash<QString, QString> m_shape;

    /// A tracker over @ref m_snapshot, reading the current shape from
    /// @p shapeToken, or from @ref m_shape when none is given.
    ScreenReshapeTracker makeTracker(std::function<QString(const QString& screenId)> shapeToken = {})
    {
        ScreenReshapeTracker::Lookups lookups;
        lookups.snapshot = [this](const QString& screenId) -> std::optional<ScreenReshapeTracker::SnapshotEntry> {
            const auto it = m_snapshot.constFind(screenId);
            if (it == m_snapshot.constEnd()) {
                return std::nullopt;
            }
            return *it;
        };
        if (shapeToken) {
            lookups.shapeToken = std::move(shapeToken);
        } else {
            lookups.shapeToken = [this](const QString& screenId) {
                return m_shape.value(screenId);
            };
        }
        return ScreenReshapeTracker(std::move(lookups));
    }

    /// A screen the snapshot resolved to @p assignmentId under @p shape, which
    /// is also its current shape.
    void settle(const QString& screenId, const QString& assignmentId, const QString& shape)
    {
        m_snapshot.insert(screenId, {assignmentId, shape});
        m_shape.insert(screenId, shape);
    }

private Q_SLOTS:
    void init()
    {
        m_snapshot.clear();
        m_shape.clear();
    }

    // ─── Consistency ────────────────────────────────────────────────────

    // The mode of the output whose logical size the daemon tracks agrees. A
    // coarse integer scale, a rotation the geometry has not followed, another
    // monitor's size, and the old geometry of a KVM mode change do not.
    void pixelSizeMatchesLogicalSize_acceptsOnlyTheTrackedScreensMode()
    {
        // 2560x1440 at 150 % is a 1707x960 logical screen.
        QVERIFY(pixelSizeMatchesLogicalSize(QSize(2560, 1440), QSize(1707, 960), 1.5));
        // Before the sensor maps, the scale is the integer ceiling.
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(2560, 1440), QSize(1707, 960), 2.0));
        // Rotated report, unrotated geometry.
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(1440, 2560), QSize(1707, 960), 1.5));
        // Another monitor on the same connector.
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(1920, 1080), QSize(1707, 960), 1.5));
        // A split mode halves a 5120x1440 panel: the report lands before the
        // geometry follows, then agrees with the new geometry.
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(2560, 1440), QSize(3413, 960), 1.5));
        QVERIFY(pixelSizeMatchesLogicalSize(QSize(2560, 1440), QSize(1707, 960), 1.5));
        // The tolerance is scale + 1 device pixels per side, plus one per 240
        // logical pixels of the side, rounded up: 1 + 1 + 1 for a 200 px side.
        QVERIFY(pixelSizeMatchesLogicalSize(QSize(203, 200), QSize(200, 200), 1.0));
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(204, 200), QSize(200, 200), 1.0));
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(200, 204), QSize(200, 200), 1.0));
        // Nothing to compare.
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(1920, 1080), QSize(1920, 1080), 0.0));
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(), QSize(1920, 1080), 1.0));
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(1920, 1080), QSize(), 1.0));
    }

    // A client sees the compositor's fractional scale only as a numerator
    // over 120 (wp_fractional_scale_v1), so the scale it multiplies by can sit
    // up to 1/240 off the one the compositor divided by, and the product
    // drifts with the side. A wide panel's real mode must still agree.
    void pixelSizeMatchesLogicalSize_allowsTheQuantizedScale()
    {
        // A 7680x2160 output at a scale the 1/120 grid cannot carry: 1.004 is
        // sent as round(120.48) = 120, so the client reads 1.0, while the
        // compositor divides the mode by 1.004 for the logical size.
        const QSize mode(7680, 2160);
        const QSize logical(qRound(7680 / 1.004), qRound(2160 / 1.004));
        QCOMPARE(logical, QSize(7649, 2151));
        // Logical times the client's scale misses the mode by 31 px on the
        // wide side, past scale + 1, but within the 1/240 allowance
        // (ceil(7649 / 240) = 32).
        QVERIFY(pixelSizeMatchesLogicalSize(mode, logical, 1.0));
        // A different monitor of the same width class still lands outside.
        QVERIFY(!pixelSizeMatchesLogicalSize(QSize(7680, 2400), logical, 1.0));
    }

    // ─── Report states ──────────────────────────────────────────────────

    // A mode change whose report lands before the QScreen geometry (F78): the
    // report is parked, nothing is saved or seeded, and it applies once, with
    // an OSD, when the geometry catches up.
    void reportBeforeGeometry_parksThenAppliesOnce()
    {
        ScreenReshapeTracker tracker = makeTracker();
        tracker.screenAdded(Dp1);
        QCOMPARE(tracker.evaluateReport(Dp1, consistentReport()).action, Action::Apply);

        const auto parked = tracker.evaluateReport(Dp1, inconsistentReport());
        QCOMPARE(parked.action, Action::Park);
        QVERIFY(!parked.persist);
        QVERIFY(tracker.isParked(Dp1));

        // The same report again while the geometry still lags: still parked.
        QCOMPARE(tracker.evaluateReport(Dp1, inconsistentReport()).action, Action::Park);

        const auto applied = tracker.evaluateReport(Dp1, consistentReport());
        QCOMPARE(applied.action, Action::Apply);
        QVERIFY(applied.persist);
        QVERIFY(!applied.silent);
        QVERIFY(!tracker.isParked(Dp1));
    }

    // Login: the report arrives before the output's sensor maps and is
    // parked, the screen stays awaited meanwhile, and the report applies
    // silently once the map brings the real scale.
    void loginUnmappedSensor_parksAndAppliesSilently()
    {
        ScreenReshapeTracker tracker = makeTracker();
        tracker.screenAdded(Dp1);
        QCOMPARE(tracker.evaluateReport(Dp1, unmappedReport()).action, Action::Park);
        QVERIFY(tracker.isParked(Dp1));
        QVERIFY(tracker.isAwaitingFirstReport(Dp1));

        const auto applied = tracker.evaluateReport(Dp1, consistentReport());
        QCOMPARE(applied.action, Action::Apply);
        QVERIFY(applied.silent);
        QVERIFY(applied.persist);
        QVERIFY(!tracker.isAwaitingFirstReport(Dp1));

        // The next mode change is the user's, so it gets its OSD.
        QVERIFY(!tracker.evaluateReport(Dp1, consistentReport()).silent);
    }

    // Same-connector swap, report first (F78 + C2#1): the new monitor's report
    // disagrees with the outgoing one the daemon still tracks, so nothing is
    // filed under the outgoing identifier. The new monitor's add applies it
    // as the screen settling.
    void sameConnectorSwap_reportFirst()
    {
        ScreenReshapeTracker tracker = makeTracker();
        tracker.screenAdded(Dp1);
        tracker.evaluateReport(Dp1, consistentReport());

        const auto parked = tracker.evaluateReport(Dp1, inconsistentReport());
        QCOMPARE(parked.action, Action::Park);
        QVERIFY(!parked.persist);

        tracker.screenRemoved(Dp1);
        QVERIFY(!tracker.isParked(Dp1));
        tracker.screenAdded(Dp1);
        const auto applied = tracker.evaluateReport(Dp1, consistentReport());
        QCOMPARE(applied.action, Action::Apply);
        QVERIFY(applied.persist);
        QVERIFY(applied.silent);
    }

    // Same-connector swap, QScreen first: the add finds the outgoing
    // monitor's report still standing, which disagrees, so it is parked and
    // the screen awaited. The effect's clear and its fresh report then settle
    // the screen silently, and only the fresh report is saved.
    void sameConnectorSwap_geometryFirst()
    {
        ScreenReshapeTracker tracker = makeTracker();
        tracker.screenAdded(Dp1);
        tracker.evaluateReport(Dp1, consistentReport());

        tracker.screenRemoved(Dp1);
        tracker.screenAdded(Dp1);
        const auto stale = tracker.evaluateReport(Dp1, inconsistentReport());
        QCOMPARE(stale.action, Action::Park);
        QVERIFY(!stale.persist);

        const auto cleared = tracker.evaluateReport(Dp1, clearedReport());
        QCOMPARE(cleared.action, Action::Apply);
        QVERIFY(cleared.silent);
        QVERIFY(!cleared.persist);
        QVERIFY(!tracker.isParked(Dp1));

        const auto fresh = tracker.evaluateReport(Dp1, consistentReport());
        QCOMPARE(fresh.action, Action::Apply);
        QVERIFY(fresh.silent);
        QVERIFY(fresh.persist);
    }

    // A clear on a tracked connector re-arms the first report, so the report
    // after it applies silently. The one after that does not.
    void clearThenReport_reArmsTheFirstReport()
    {
        ScreenReshapeTracker tracker = makeTracker();
        tracker.screenAdded(Dp1);
        QVERIFY(tracker.evaluateReport(Dp1, consistentReport()).silent);
        QVERIFY(!tracker.isAwaitingFirstReport(Dp1));

        tracker.evaluateReport(Dp1, clearedReport());
        QVERIFY(tracker.isAwaitingFirstReport(Dp1));
        QVERIFY(tracker.evaluateReport(Dp1, consistentReport()).silent);
        QVERIFY(!tracker.evaluateReport(Dp1, consistentReport()).silent);
    }

    // A report for a connector the daemon does not track changes nothing and
    // parks nothing; the connector's add evaluates it.
    void untrackedReport_waitsForTheAdd()
    {
        ScreenReshapeTracker tracker = makeTracker();
        Facts untracked = consistentReport();
        untracked.tracked = false;
        const auto ignored = tracker.evaluateReport(Dp1, untracked);
        QCOMPARE(ignored.action, Action::Ignore);
        QVERIFY(!ignored.persist);
        QVERIFY(!tracker.isParked(Dp1));

        tracker.screenAdded(Dp1);
        const auto applied = tracker.evaluateReport(Dp1, consistentReport());
        QCOMPARE(applied.action, Action::Apply);
        QVERIFY(applied.silent);
        QVERIFY(applied.persist);
    }

    // A screen with no geometry sensor keeps the coarse scale for good, and
    // no scale change would ever evaluate a parked report again, so its
    // report applies unchecked.
    void sensorlessScreen_appliesUnchecked()
    {
        ScreenReshapeTracker tracker = makeTracker();
        tracker.screenAdded(Dp1);
        QCOMPARE(tracker.evaluateReport(Dp1, inconsistentReport()).action, Action::Park);

        Facts sensorless = unmappedReport();
        sensorless.scaleSensor = false;
        const auto applied = tracker.evaluateReport(Dp1, sensorless);
        QCOMPARE(applied.action, Action::Apply);
        QVERIFY(applied.persist);
        QVERIFY(applied.silent);
        QVERIFY(!tracker.isParked(Dp1));
    }

    // A screen whose sensor exists but has not mapped parks every report
    // unchecked, even one the coarse fallback scale agrees with. A 5K monitor
    // at 200 % and a 4K one at 150 % share a 2560x1440 logical size, and the
    // 4K's fallback is the integer ceiling 2, so the outgoing 5K's report
    // passes the check against the incoming 4K. Once the sensor maps, the
    // real 1.5 rejects it, and the 4K's own report applies as its first.
    void unmappedSensor_parksAReportTheFallbackWouldPass()
    {
        ScreenReshapeTracker tracker = makeTracker();
        tracker.screenAdded(Dp1);
        Facts stale{.tracked = true,
                    .pendingSize = QSize(5120, 2880),
                    .logicalSize = QSize(2560, 1440),
                    .logicalScale = 2.0,
                    .scaleSensor = true,
                    .scaleSensorMapped = false};
        QVERIFY(pixelSizeMatchesLogicalSize(stale.pendingSize, stale.logicalSize, stale.logicalScale));
        const auto parked = tracker.evaluateReport(Dp1, stale);
        QCOMPARE(parked.action, Action::Park);
        QVERIFY(!parked.persist);
        QVERIFY(tracker.isParked(Dp1));

        // The map brings 1.5, against which the 5K's report disagrees.
        stale.scaleSensorMapped = true;
        stale.logicalScale = 1.5;
        QCOMPARE(tracker.evaluateReport(Dp1, stale).action, Action::Park);

        Facts own = stale;
        own.pendingSize = QSize(3840, 2160);
        const auto applied = tracker.evaluateReport(Dp1, own);
        QCOMPARE(applied.action, Action::Apply);
        QVERIFY(applied.silent);
        QVERIFY(applied.persist);
    }

    // ─── Over a real ScreenManager ──────────────────────────────────────

    // Two monitors dock at once. DP-1's sensor has not mapped, so its report
    // parks; DP-2's is measured and agrees, so it applies. The parked report
    // is still PENDING in the manager, so DP-1's shape keeps reading its
    // seed: DP-2's apply records only DP-2, and the verdict changes the
    // reconcile after it takes leave DP-1 alone. DP-1 moves only at its own
    // accept, once its sensor maps, and silently, as the screen settling.
    // Only an applied report is re-seeded and persisted, under its own
    // monitor's identifier.
    void parkedConnector_isNotAppliedByAnotherConnectorsApply()
    {
        const QString dp2 = QStringLiteral("DP-2");
        const QString idA = QStringLiteral("Dell:U27:1");
        const QString idB = QStringLiteral("LG:Ultra:9");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(Dp1, QRect(0, 0, 1707, 960), idA);
        fake.addScreen(dp2, QRect(1707, 0, 1920, 1080), idB);
        PhosphorScreens::ScreenManager mgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        // Last session's sizes for both monitors.
        mgr.seedPixelSize(idA, QSize(1920, 1080));
        mgr.seedPixelSize(idB, QSize(1920, 1080));

        // The registry's shape, read the way the daemon's pixel-size provider
        // feeds it: ScreenManager::pixelSize.
        ScreenReshapeTracker tracker = makeTracker([&mgr](const QString& screenId) {
            const QSize size = mgr.pixelSize(screenId).value_or(QSize());
            return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
        });
        m_snapshot.insert(idA, {QStringLiteral("layout-a"), QStringLiteral("1920x1080")});
        m_snapshot.insert(idB, {QStringLiteral("layout-b"), QStringLiteral("1920x1080")});

        // What the daemon's sink would save.
        QHash<QString, QSize> persisted;
        const PixelSizePersistSink persist = [&persisted](const QString& screenIdentifier, const QSize& pixelSize) {
            persisted.insert(screenIdentifier, pixelSize);
        };
        // A synthetic screen has no QScreen and no sensor, so the test
        // supplies the scale the daemon would read and whether its sensor has
        // mapped.
        const auto admit = [&](const QString& connector, qreal scale, bool mapped) {
            return admitPixelSizeReport(tracker, mgr, connector, persist,
                                        [scale, mapped](const PhosphorScreens::PhysicalScreen&, Facts& facts) {
                                            facts.logicalScale = scale;
                                            facts.scaleSensor = true;
                                            facts.scaleSensorMapped = mapped;
                                        });
        };

        tracker.screenAdded(Dp1);
        tracker.screenAdded(dp2);
        mgr.setCompositorPixelSize(Dp1, QSize(2560, 1440));
        mgr.setCompositorPixelSize(dp2, QSize(3840, 2160));

        QCOMPARE(admit(Dp1, 2.0, false).action, Action::Park);
        QCOMPARE(admit(dp2, 2.0, true).action, Action::Apply);

        // The parked report reads nowhere and is saved nowhere.
        QCOMPARE(mgr.pixelSize(idA).value_or(QSize()), QSize(1920, 1080));
        QVERIFY(!mgr.isPixelSizeReported(idA));
        QCOMPARE(mgr.pixelSize(idB).value_or(QSize()), QSize(3840, 2160));
        QVERIFY(mgr.isPixelSizeReported(idB));
        QCOMPARE(persisted, (QHash<QString, QSize>{{idB, QSize(3840, 2160)}}));
        QCOMPARE(tracker.noteReshaped({idA, idB}), QStringList{idB});
        QVERIFY(!tracker.hasRecord(idA));

        // DP-2's reconcile resolves its new verdict. DP-1's shape did not
        // move, so its verdict did not either.
        m_snapshot.insert(idB, {QStringLiteral("layout-b-4k"), QStringLiteral("3840x2160")});
        QCOMPARE(tracker.takeVerdictChanges(), QSet<QString>{idB});

        // DP-1's sensor maps with the real scale: its report applies now,
        // silently, and is saved under DP-1's monitor.
        const ScreenReshapeTracker::ReportVerdict settled = admit(Dp1, 1.5, true);
        QCOMPARE(settled.action, Action::Apply);
        QVERIFY(settled.silent);
        QCOMPARE(mgr.pixelSize(idA).value_or(QSize()), QSize(2560, 1440));
        QCOMPARE(persisted.value(idA), QSize(2560, 1440));
        QCOMPARE(tracker.noteReshaped({idA, idB}), QStringList{idA});

        // The re-seed is what a replug of the same monitor reads until a
        // report is accepted for it again.
        fake.removeScreen(Dp1);
        fake.addScreen(Dp1, QRect(0, 0, 1707, 960), idA);
        QVERIFY(!mgr.isPixelSizeReported(idA));
        QCOMPARE(mgr.pixelSize(idA).value_or(QSize()), QSize(2560, 1440));
    }

    // The admit step's other arms. A report for a connector the manager does
    // not track is ignored and left pending, with nothing accepted or saved.
    // A cleared report on a tracked one is accepted, so the screen falls back
    // to its seed, and nothing is saved for it.
    void admit_untrackedIsIgnoredAndAClearIsAccepted()
    {
        const QString idA = QStringLiteral("Dell:U27:1");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(Dp1, QRect(0, 0, 1920, 1080), idA);
        PhosphorScreens::ScreenManager mgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        mgr.start();
        mgr.seedPixelSize(idA, QSize(1920, 1080));
        ScreenReshapeTracker tracker = makeTracker();
        int persistCalls = 0;
        const PixelSizePersistSink persist = [&persistCalls](const QString&, const QSize&) {
            ++persistCalls;
        };

        const QString hdmi = QStringLiteral("HDMI-A-1");
        mgr.setCompositorPixelSize(hdmi, QSize(2560, 1440));
        QCOMPARE(admitPixelSizeReport(tracker, mgr, hdmi, persist).action, Action::Ignore);
        QCOMPARE(mgr.pendingPixelSize(hdmi).value_or(QSize()), QSize(2560, 1440));
        QCOMPARE(persistCalls, 0);

        // A synthetic screen has no sensor, so without a scale reader its
        // report applies unchecked, read from the manager alone.
        tracker.screenAdded(Dp1);
        mgr.setCompositorPixelSize(Dp1, QSize(3840, 2160));
        const ScreenReshapeTracker::ReportVerdict applied = admitPixelSizeReport(tracker, mgr, Dp1, persist);
        QCOMPARE(applied.action, Action::Apply);
        QVERIFY(applied.persist);
        QCOMPARE(persistCalls, 1);
        QCOMPARE(mgr.pixelSize(idA).value_or(QSize()), QSize(3840, 2160));

        mgr.setCompositorPixelSize(Dp1, QSize());
        const ScreenReshapeTracker::ReportVerdict cleared = admitPixelSizeReport(tracker, mgr, Dp1, persist);
        QCOMPARE(cleared.action, Action::Apply);
        QVERIFY(cleared.silent);
        QVERIFY(!cleared.persist);
        QCOMPARE(persistCalls, 1);
        QVERIFY(!mgr.isPixelSizeReported(idA));
        // The seed now holds what was accepted before the clear.
        QCOMPARE(mgr.pixelSize(idA).value_or(QSize()), QSize(3840, 2160));
    }

    // ─── Reshape records ────────────────────────────────────────────────

    // A report that confirms the seed leaves the shape where the snapshot saw
    // it: nothing is recorded, so the daemon reconciles nothing.
    void seedEqualReport_recordsNothing()
    {
        ScreenReshapeTracker tracker = makeTracker();
        settle(Dp1, QStringLiteral("layout-a"), QStringLiteral("2560x1440"));
        QVERIFY(tracker.noteReshaped({Dp1}).isEmpty());
        QVERIFY(!tracker.hasRecord(Dp1));
        QVERIFY(tracker.takeVerdictChanges().isEmpty());
    }

    // A screen the snapshot does not hold yet is primed by its first diff,
    // not recorded.
    void unknownScreen_isNotRecorded()
    {
        ScreenReshapeTracker tracker = makeTracker();
        m_shape.insert(Dp1, QStringLiteral("2560x1440"));
        QVERIFY(tracker.noteReshaped({Dp1}).isEmpty());
        QVERIFY(!tracker.hasRecord(Dp1));
    }

    // A diff between the reshape and the reconcile that folds the new verdict
    // into the snapshot without applying it (a screen add, a layout list
    // change) does not hide it: the record still reports the verdict moved,
    // once.
    void absorbingDiff_keepsTheVerdict()
    {
        ScreenReshapeTracker tracker = makeTracker();
        settle(Dp1, QStringLiteral("layout-a"), QStringLiteral("5120x1440"));
        m_shape.insert(Dp1, QStringLiteral("2560x1440"));
        QCOMPARE(tracker.noteReshaped({Dp1}), QStringList{Dp1});

        settle(Dp1, QStringLiteral("layout-b"), QStringLiteral("2560x1440"));
        QVERIFY(tracker.noteReshaped({Dp1}).isEmpty());
        QCOMPARE(tracker.takeVerdictChanges(), QSet<QString>{Dp1});
        QVERIFY(tracker.takeVerdictChanges().isEmpty());
    }

    // A second reshape before the reconcile compares against the verdict the
    // windows were laid out under, not the intermediate one: a shape that
    // went away and came back to the original verdict moves nothing.
    void firstRecordWins()
    {
        ScreenReshapeTracker tracker = makeTracker();
        settle(Dp1, QStringLiteral("layout-a"), QStringLiteral("5120x1440"));
        m_shape.insert(Dp1, QStringLiteral("2560x1440"));
        tracker.noteReshaped({Dp1});

        settle(Dp1, QStringLiteral("layout-b"), QStringLiteral("2560x1440"));
        m_shape.insert(Dp1, QStringLiteral("5120x1440"));
        QCOMPARE(tracker.noteReshaped({Dp1}), QStringList{Dp1});

        settle(Dp1, QStringLiteral("layout-a"), QStringLiteral("5120x1440"));
        QVERIFY(tracker.takeVerdictChanges().isEmpty());
    }

    // A desktop switch lays the output out under its current shape and moves
    // the snapshot to the new desktop's verdict. The record compared against
    // the desktop left would read that as a change and apply it again, so
    // the switch drops it (C2#4), for the output's virtual screens too and
    // for no other output.
    void desktopSwitch_dropsTheOutputsRecords()
    {
        ScreenReshapeTracker tracker = makeTracker();
        const QString vs0 = QStringLiteral("DP-1/vs:0");
        const QString vs1 = QStringLiteral("DP-1/vs:1");
        const QString dp2 = QStringLiteral("DP-2");
        for (const QString& screenId : {vs0, vs1, dp2}) {
            settle(screenId, QStringLiteral("desktop-1-layout"), QStringLiteral("1280x1440"));
            m_shape.insert(screenId, QStringLiteral("1440x2560"));
        }
        QCOMPARE(tracker.noteReshaped({vs0, vs1, dp2}).size(), 3);

        tracker.dropRecordsWhere([](const QString& screenId) {
            return screenId.startsWith(QLatin1String("DP-1"));
        });
        for (const QString& screenId : {vs0, vs1, dp2}) {
            settle(screenId, QStringLiteral("desktop-2-layout"), QStringLiteral("1440x2560"));
        }
        QCOMPARE(tracker.takeVerdictChanges(), QSet<QString>{dp2});
    }

    // An apply that laid a screen out spends its record, and only that one.
    void apply_dropsOnlyTheAppliedScreens()
    {
        ScreenReshapeTracker tracker = makeTracker();
        const QString dp2 = QStringLiteral("DP-2");
        for (const QString& screenId : {Dp1, dp2}) {
            settle(screenId, QStringLiteral("layout-a"), QStringLiteral("5120x1440"));
            m_shape.insert(screenId, QStringLiteral("2560x1440"));
        }
        tracker.noteReshaped({Dp1, dp2});
        tracker.dropRecords({Dp1});
        QVERIFY(!tracker.hasRecord(Dp1));
        QVERIFY(tracker.hasRecord(dp2));
        tracker.clearRecords();
        QVERIFY(!tracker.hasRecord(dp2));
    }

    // ─── Silent set and reset ───────────────────────────────────────────

    void silentSet_andReset()
    {
        ScreenReshapeTracker tracker = makeTracker();
        tracker.markSilent({Dp1});
        QVERIFY(tracker.isSilent(Dp1));
        tracker.clearSilent();
        QVERIFY(!tracker.isSilent(Dp1));

        settle(Dp1, QStringLiteral("layout-a"), QStringLiteral("5120x1440"));
        m_shape.insert(Dp1, QStringLiteral("2560x1440"));
        tracker.noteReshaped({Dp1});
        tracker.markSilent({Dp1});
        tracker.screenAdded(Dp1);
        tracker.evaluateReport(Dp1, inconsistentReport());
        tracker.reset();
        QVERIFY(!tracker.hasRecord(Dp1));
        QVERIFY(!tracker.isSilent(Dp1));
        QVERIFY(!tracker.isParked(Dp1));
        QVERIFY(!tracker.isAwaitingFirstReport(Dp1));
    }
};

QTEST_MAIN(TestScreenReshapeTracker)
#include "test_screen_reshape_tracker.moc"
