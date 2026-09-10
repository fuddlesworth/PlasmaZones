// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The DRAWN path, as opposed to the ring that feeds it. PointerHistory stores
// samples; what the packs put on screen is a Catmull-Rom curve through those
// samples after an inverse-time smoothing kernel, and both of those live in
// data/pointer/shared/pointer_lib.glsl rather than in any C++ this library
// compiles. These tests mirror the two shared functions, pin the behaviour the
// mirror is supposed to have, and then pin the mirror itself against the
// shipped GLSL so a retune of the curve cannot leave them measuring the old one.

#include <PhosphorPointer/PointerFrameState.h>
#include <PhosphorPointer/PointerHistory.h>
#include <PhosphorPointer/PointerShaderContract.h>

#include <QFile>
#include <QLineF>
#include <QRegularExpression>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

using namespace PhosphorPointerShaders;

class TestPointerCurve : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRenderedPathNeverChangesOnceDrawn();
    void testSmoothingWeightsNeighboursByInverseTimeDistance();
    void testSpanIsACurveAtTheDeclaredTension();
    void testHeadRefreshTracksThePointerWithoutSpendingASlot();
    void testCurveConstantsMatchTheShippedShader();
};

namespace {

// Mirrors of the two shared-library functions the packs reconstruct the path
// with, pointerSmoothedAt() and pointerCurvePoint(), so this test measures
// what is actually DRAWN rather than what the ring happens to store. They have
// to be kept in step with data/pointer/shared/pointer_lib.glsl.
struct DrawnPath
{
    const PointerFrameState* state = nullptr;
    int live = 0;
    double smoothing = 0.0;

    [[nodiscard]] QPointF at(int i) const
    {
        const QVector4D v = state->trailAt(std::clamp(i, 0, std::max(live - 1, 0)));
        return QPointF(v.x(), v.y());
    }
    [[nodiscard]] double age(int i) const
    {
        return state->trailAt(std::clamp(i, 0, std::max(live - 1, 0))).z();
    }
    [[nodiscard]] QPointF smoothed(int i) const
    {
        // Clamped as pointerSmoothedAt clamps it: the mirror has to be a
        // mirror at the edges of the range too.
        const double s = std::clamp(smoothing, 0.0, 1.0);
        const QPointF here = at(i);
        if (s <= 0.0) {
            return here;
        }
        const double dtPrev = std::max(age(i) - age(i - 1), 0.0) + 1e-4;
        const double dtNext = std::max(age(i + 1) - age(i), 0.0) + 1e-4;
        const QPointF mean = (at(i - 1) * dtNext + at(i + 1) * dtPrev) / (dtPrev + dtNext);
        return here * (1.0 - s) + ((here + mean) / 2.0) * s;
    }
    /// Catmull-Rom position at @p t along the span between smoothed samples
    /// @p i and i + 1, at kPointerCurveTension.
    [[nodiscard]] QPointF curve(int i, double t) const
    {
        const QPointF c0 = smoothed(std::max(i - 1, 0));
        const QPointF c1 = smoothed(i);
        const QPointF c2 = smoothed(i + 1);
        const QPointF c3 = smoothed(i + 2);
        const QPointF m1 = (c2 - c0) * PointerShaderContract::kPointerCurveTension;
        const QPointF m2 = (c3 - c1) * PointerShaderContract::kPointerCurveTension;
        const double t2 = t * t;
        const double t3 = t2 * t;
        return c1 * (2 * t3 - 3 * t2 + 1) + m1 * (t3 - 2 * t2 + t) + c2 * (-2 * t3 + 3 * t2) + m2 * (t3 - t2);
    }
};

} // namespace

void TestPointerCurve::testRenderedPathNeverChangesOnceDrawn()
{
    // THE INVARIANT: a piece of trail, once drawn, must never be redrawn
    // somewhere else. The user is looking at it. Anything that mutates the
    // ring BEHIND the head breaks this, because the packs rebuild the whole
    // stroke from the ring every frame.
    //
    // The ring keeps it by construction: samples are only ever appended at the
    // head and dropped off the tail, so a sample's neighbours are the same
    // physical samples for its entire life. That is a stronger guarantee than
    // it looks, because a pack's curve reads TWO samples past each end of the
    // span it draws (the Catmull-Rom tangents), so dropping one sample from
    // the middle would reshape spans whose own endpoints never moved. An
    // earlier attempt to space the ring non-uniformly by dropping interior
    // samples measured 2.9 px of movement per frame here, which is what a
    // trail that re-smooths itself as it grows looks like.
    //
    // Pinned to an ABSOLUTE moment, never to an age: the point at a fixed AGE
    // names an earlier place on the path every frame, so it genuinely moves,
    // and tracking one would measure the pointer's speed and pass regardless.
    PointerHistory history;
    history.setTrailSeconds(0.9);
    const auto pathAt = [](double ms) {
        const double u = ms / 1000.0;
        return QPointF(300.0 + 400.0 * std::sin(u * 1.7), 300.0 + 300.0 * std::cos(u * 1.1));
    };
    qint64 t = 0;
    for (; t <= 2000; ++t) {
        history.notePointer(pathAt(double(t)), t);
    }

    const qint64 pinnedMs = t - 300;
    QPointF previous;
    bool havePrevious = false;
    double worstShift = 0.0;
    int frames = 0;
    for (int frame = 0; frame < 120; ++frame) {
        for (int k = 0; k < 8; ++k) {
            ++t;
            history.notePointer(pathAt(double(t)), t);
        }
        const PointerFrameState state = history.frameState(t, 1.0);
        const DrawnPath drawn{&state, state.trailSize(), 0.35};
        const double wantAge = double(t - pinnedMs) / 1000.0;
        if (drawn.live < 4 || wantAge >= drawn.age(drawn.live - 1)) {
            break; // the pinned moment has aged out of the ring
        }
        int i = 0;
        while (i + 1 < drawn.live && drawn.age(i + 1) < wantAge) {
            ++i;
        }
        // The last two spans are skipped, not because they may move but
        // because their tangents read CLAMPED neighbours: the oldest span's
        // shape does change as the run shortens under it. That happens at the
        // very end of the trail, where every pack has faded to nothing.
        if (i + 2 >= drawn.live - 1) {
            break;
        }
        const double a0 = drawn.age(i);
        const double a1 = drawn.age(i + 1);
        const double f = a1 > a0 ? (wantAge - a0) / (a1 - a0) : 0.0;
        const QPointF here = drawn.curve(i, f);
        if (havePrevious) {
            worstShift = std::max(worstShift, QLineF(previous, here).length());
        }
        previous = here;
        havePrevious = true;
        ++frames;
    }
    QVERIFY2(frames > 40, qPrintable(QStringLiteral("only %1 frames measured").arg(frames)));
    // EXACTLY still, not merely close. The reconstruction is a pure function
    // of samples that do not change, so the only tolerance needed is for the
    // float round trip through the frame state.
    QVERIFY2(worstShift < 0.01,
             qPrintable(QStringLiteral("drawn path moved %1 px after it was drawn").arg(worstShift)));
}

namespace {

/// A three-sample run whose head has been refreshed IN PLACE, so the two gaps
/// either side of index 1 differ. That asymmetry is the whole point: with
/// equal weights the middle sample is dragged toward whichever neighbour is
/// further away in time.
PointerHistory unevenRun()
{
    PointerHistory history;
    history.setTrailSeconds(0.9); // ceil(900 / 31) = 30 ms
    history.notePointer(QPointF(0.0, 0.0), 0);
    history.notePointer(QPointF(100.0, 0.0), 30);
    history.notePointer(QPointF(200.0, 0.0), 60);
    history.notePointer(QPointF(300.0, 0.0), 89); // 29 ms: refreshes the head
    return history;
}

} // namespace

void TestPointerCurve::testSmoothingWeightsNeighboursByInverseTimeDistance()
{
    // The defect this weighting fixes: equal weights pull a sample toward the
    // neighbour that is FURTHER AWAY IN TIME, and at the head -- refreshed in
    // place between appends, so its gap to index 1 is a whole interval while
    // its gap to its clamped self is nothing -- that put the stroke visibly
    // behind the cursor.
    PointerHistory history = unevenRun();
    const PointerFrameState state = history.frameState(89, 1.0);
    QCOMPARE(state.trailSize(), 3);
    const DrawnPath drawn{&state, state.trailSize(), 1.0};

    // The head is left where the pointer is. Equal weights would give
    // (300 + (300 + 100) / 2) / 2 = 250, i.e. 50 px behind it.
    QVERIFY2(std::abs(drawn.smoothed(0).x() - 300.0) < 1.0,
             qPrintable(QStringLiteral("head at %1, pointer at 300").arg(drawn.smoothed(0).x())));

    // An interior sample with unequal gaps: dtPrev 59 ms, dtNext 30 ms, so the
    // mean leans toward the CLOSER-in-time neighbour, giving 100.6. Equal
    // weights give 125.0, and swapped weights give 149.4, so this one pins the
    // orientation of the weighting and not merely that it is uneven.
    QVERIFY2(std::abs(drawn.smoothed(1).x() - 100.617) < 0.2,
             qPrintable(QStringLiteral("interior at %1, expected 100.617").arg(drawn.smoothed(1).x())));
}

void TestPointerCurve::testSpanIsACurveAtTheDeclaredTension()
{
    // Smoothing off, so this pins the curve alone: a failure here names the
    // Catmull-Rom arm and not the kernel in front of it.
    PointerHistory history;
    history.setTrailSeconds(0.9);
    history.notePointer(QPointF(0.0, 0.0), 0);
    history.notePointer(QPointF(0.0, 100.0), 30);
    history.notePointer(QPointF(100.0, 100.0), 60);
    history.notePointer(QPointF(200.0, 100.0), 90);
    const PointerFrameState state = history.frameState(90, 1.0);
    QCOMPARE(state.trailSize(), 4);
    const DrawnPath drawn{&state, 4, 0.0};

    // The span across the corner, at its midpoint. A straight chord would put
    // it at (50, 100); the textbook tension of 0.5 would put it at
    // (43.75, 106.25). Both are hundreds of times the tolerance away, so this
    // pins the VALUE of the tension and not just that the span bends.
    const QPointF mid = drawn.curve(1, 0.5);
    QVERIFY2(QLineF(mid, QPointF(46.875, 103.125)).length() < 0.01,
             qPrintable(QStringLiteral("span midpoint (%1, %2)").arg(mid.x()).arg(mid.y())));

    // And it interpolates its endpoints, which is what was chosen over a
    // B-spline: the drawn path goes where the pointer actually went.
    QVERIFY(QLineF(drawn.curve(1, 0.0), drawn.smoothed(1)).length() < 1e-9);
    QVERIFY(QLineF(drawn.curve(1, 1.0), drawn.smoothed(2)).length() < 1e-9);
}

void TestPointerCurve::testHeadRefreshTracksThePointerWithoutSpendingASlot()
{
    // The honest scope of the never-moves invariant. Inside the sample
    // interval the head slot is rewritten in place rather than appended, so
    // the head tracks the cursor exactly AND the newest span genuinely does
    // move between frames. The invariant starts behind it.
    PointerHistory history = unevenRun();
    const PointerFrameState state = history.frameState(89, 1.0);
    QCOMPARE(state.trailSize(), 3); // four events, three slots
    QCOMPARE(state.newestTrail().x(), 300.0f);
    QCOMPARE(state.trailAt(1).x(), 100.0f); // the slot behind was not rewritten

    PointerHistory second;
    second.setTrailSeconds(0.9);
    second.notePointer(QPointF(0.0, 0.0), 0);
    second.notePointer(QPointF(100.0, 0.0), 30);
    second.notePointer(QPointF(200.0, 0.0), 60);
    const PointerFrameState before = second.frameState(60, 1.0);
    const QPointF beforeMid = DrawnPath{&before, before.trailSize(), 0.0}.curve(0, 0.5);
    second.notePointer(QPointF(300.0, 0.0), 89);
    const PointerFrameState after = second.frameState(89, 1.0);
    const QPointF afterMid = DrawnPath{&after, after.trailSize(), 0.0}.curve(0, 0.5);
    QVERIFY2(QLineF(beforeMid, afterMid).length() > 20.0,
             "span 0 is redrawn every frame by design; the never-moves invariant starts at span 1");
}

void TestPointerCurve::testCurveConstantsMatchTheShippedShader()
{
    // The mirror above reconstructs what the packs draw, but nothing compiles
    // it against the GLSL the compositor actually runs. Retune the curve in
    // the shader and every guard here would stay green while measuring the old
    // one, so pin the handful of facts the mirror depends on to the file.
    QFile file(QStringLiteral(P_SOURCE_DIR "/data/pointer/shared/pointer_lib.glsl"));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.fileName()));
    const QString source = QString::fromUtf8(file.readAll());

    const QRegularExpression tension(QStringLiteral(R"(const\s+float\s+kPointerCurveTension\s*=\s*([0-9.]+)\s*;)"));
    const QRegularExpressionMatch tensionMatch = tension.match(source);
    QVERIFY2(tensionMatch.hasMatch(), "kPointerCurveTension not found in pointer_lib.glsl");
    QCOMPARE(tensionMatch.captured(1).toDouble(), PointerShaderContract::kPointerCurveTension);

    const QRegularExpression steps(QStringLiteral(R"(const\s+int\s+kPointerCurveSteps\s*=\s*([0-9]+)\s*;)"));
    const QRegularExpressionMatch stepsMatch = steps.match(source);
    QVERIFY2(stepsMatch.hasMatch(), "kPointerCurveSteps not found in pointer_lib.glsl");
    QCOMPARE(stepsMatch.captured(1).toInt(), PointerShaderContract::kPointerCurveSteps);

    // The one textual fact that separates inverse-time weighting from equal or
    // direct-time weighting. A swap of these two is caught by nothing else.
    QVERIFY2(source.contains(QLatin1String("prev.xy * dtNext")),
             "the smoothing kernel no longer weights the previous neighbour by the FORWARD gap");
    QVERIFY2(source.contains(QLatin1String("next.xy * dtPrev")),
             "the smoothing kernel no longer weights the next neighbour by the BACKWARD gap");
    // Both gap floors the arithmetic above depends on.
    QCOMPARE(source.count(QLatin1String("+ 1e-4")), 2);
}

QTEST_MAIN(TestPointerCurve)
#include "test_pointercurve.moc"
