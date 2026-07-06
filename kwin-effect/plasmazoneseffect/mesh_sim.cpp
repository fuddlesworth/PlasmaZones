// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2008 Cédric Borgese <cedric.borgese@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See mesh_sim.h. The acceleration, ring-mean smoothing and integration
// below are a direct transcription of KWin's wobblywindows effect
// (updateWindowWobblyDatas / heightRingLinearMean), with Qt's QPointF
// standing in for KWin's Pair and the resize-only edge-lock logic dropped
// (this path is move-driven). Constants and the 10 ms fixed step match
// KWin so the feel is the same.

#include "mesh_sim.h"

#include <cmath>

namespace PlasmaZones::ShaderInternal {

namespace {

constexpr qreal kIntegrationStepMs = 10.0;
// Settle thresholds (KWin's stopAcceleration / stopVelocity).
constexpr qreal kStopAccel = 0.5;
constexpr qreal kStopVel = 0.5;

// Rest-position grid over the frame rect: uniform 4x4, node (i,j) at
// topLeft + (i/3 w, j/3 h). (KWin's last-point-snap quirk reduces to this
// for a uniform grid; computing it directly avoids float drift.)
void updateOrigins(MeshSim& sim, const QRectF& rect)
{
    const qreal xl = rect.width() / (MeshSim::kW - 1.0);
    const qreal yl = rect.height() / (MeshSim::kH - 1.0);
    for (int j = 0; j < MeshSim::kH; ++j) {
        for (int i = 0; i < MeshSim::kW; ++i) {
            sim.origin[j * MeshSim::kW + i] = QPointF(rect.x() + i * xl, rect.y() + j * yl);
        }
    }
}

// KWin's heightRingLinearMean: a weighted average of each node with its
// ring neighbours (weight 3/5/8 on self for corner/edge/interior), applied
// to acceleration then velocity for a smooth, stable wobble. Reads `data`,
// writes the smoothed result back into `data` via the scratch buffer.
void ringMean(QPointF* data, MeshSim& sim)
{
    constexpr int W = MeshSim::kW;
    constexpr int H = MeshSim::kH;
    constexpr int N = MeshSim::kCount;
    QPointF* buf = sim.buffer;

    // corners (self*3 + 3 neighbours) / 6
    buf[0] = (data[1] + data[W] + data[W + 1] + 3.0 * data[0]) / 6.0;
    buf[W - 1] = (data[W - 2] + data[2 * W - 1] + data[2 * W - 2] + 3.0 * data[W - 1]) / 6.0;
    buf[W * (H - 1)] =
        (data[W * (H - 1) + 1] + data[W * (H - 2)] + data[W * (H - 2) + 1] + 3.0 * data[W * (H - 1)]) / 6.0;
    buf[N - 1] = (data[N - 2] + data[W * (H - 1) - 1] + data[W * (H - 1) - 2] + 3.0 * data[N - 1]) / 6.0;

    // top / bottom borders (self*5 + 5 neighbours) / 10
    for (int i = 1; i < W - 1; ++i) {
        buf[i] = (data[i - 1] + data[i + 1] + data[i + W] + data[i + W - 1] + data[i + W + 1] + 5.0 * data[i]) / 10.0;
    }
    for (int i = W * (H - 1) + 1; i < N - 1; ++i) {
        buf[i] = (data[i - 1] + data[i + 1] + data[i - W] + data[i - W - 1] + data[i - W + 1] + 5.0 * data[i]) / 10.0;
    }
    // left / right borders
    for (int i = W; i < W * (H - 1); i += W) {
        buf[i] = (data[i + 1] + data[i - W] + data[i + W] + data[i - W + 1] + data[i + W + 1] + 5.0 * data[i]) / 10.0;
    }
    for (int i = 2 * W - 1; i < N - 1; i += W) {
        buf[i] = (data[i - 1] + data[i - W] + data[i + W] + data[i - W - 1] + data[i + W - 1] + 5.0 * data[i]) / 10.0;
    }
    // interior (self*8 + 8 neighbours) / 16
    for (int j = 1; j < H - 1; ++j) {
        for (int i = 1; i < W - 1; ++i) {
            const int idx = i + j * W;
            buf[idx] = (data[idx - 1] + data[idx + 1] + data[idx - W] + data[idx + W] + data[idx - W - 1]
                        + data[idx - W + 1] + data[idx + W - 1] + data[idx + W + 1] + 8.0 * data[idx])
                / 16.0;
        }
    }

    for (int i = 0; i < N; ++i) {
        data[i] = buf[i];
    }
}

qreal absSum(const QPointF& p)
{
    return std::fabs(p.x()) + std::fabs(p.y());
}

// One 10 ms physics tick. Neighbour-spring accelerations per KWin's
// per-node cases, ring-smoothed, then Verlet-style velocity/position
// update. Returns acc_sum + vel_sum so the caller can detect settle.
void integrateOne(MeshSim& sim, const QRectF& rect, qreal time)
{
    constexpr int W = MeshSim::kW;
    constexpr int H = MeshSim::kH;
    constexpr int N = MeshSim::kCount;
    const qreal k = sim.params.stiffness;
    const qreal xl = rect.width() / (W - 1.0);
    const qreal yl = rect.height() / (H - 1.0);

    QPointF* pos = sim.position;
    QPointF* acc = sim.accel;

    // Constrained (grip) nodes spring to their origin with the STRONGER
    // gripStiffness so the grabbed point tracks the window tightly; free
    // nodes use the soft sheet stiffness `k` for their neighbour springs,
    // so the body drapes. This split is what makes it read as fabric held
    // at one point rather than a uniformly wobbling solid.
    const qreal gk = sim.params.gripStiffness;
    auto constrainAccel = [&](int idx) {
        const QPointF move = sim.origin[idx] - pos[idx];
        acc[idx] = QPointF(move.x() * gk, move.y() * gk);
    };

    // ── corners ──
    if (sim.constraint[0]) {
        constrainAccel(0);
    } else {
        const QPointF& p = pos[0];
        const QPointF n0 = pos[1], n1 = pos[W];
        acc[0] = QPointF((((n0.x() - p.x()) - xl) * k + (n1.x() - p.x()) * k) / 2.0,
                         (((n1.y() - p.y()) - yl) * k + (n0.y() - p.y()) * k) / 2.0);
    }
    if (sim.constraint[W - 1]) {
        constrainAccel(W - 1);
    } else {
        const QPointF& p = pos[W - 1];
        const QPointF n0 = pos[W - 2], n1 = pos[2 * W - 1];
        acc[W - 1] = QPointF(((xl - (p.x() - n0.x())) * k + (n1.x() - p.x()) * k) / 2.0,
                             (((n1.y() - p.y()) - yl) * k + (n0.y() - p.y()) * k) / 2.0);
    }
    if (sim.constraint[W * (H - 1)]) {
        constrainAccel(W * (H - 1));
    } else {
        const QPointF& p = pos[W * (H - 1)];
        const QPointF n0 = pos[W * (H - 1) + 1], n1 = pos[W * (H - 2)];
        acc[W * (H - 1)] = QPointF((((n0.x() - p.x()) - xl) * k + (n1.x() - p.x()) * k) / 2.0,
                                   ((yl - (p.y() - n1.y())) * k + (n0.y() - p.y()) * k) / 2.0);
    }
    if (sim.constraint[N - 1]) {
        constrainAccel(N - 1);
    } else {
        const QPointF& p = pos[N - 1];
        const QPointF n0 = pos[N - 2], n1 = pos[W * (H - 1) - 1];
        acc[N - 1] = QPointF(((xl - (p.x() - n0.x())) * k + (n1.x() - p.x()) * k) / 2.0,
                             ((yl - (p.y() - n1.y())) * k + (n0.y() - p.y()) * k) / 2.0);
    }

    // ── top border ──
    for (int i = 1; i < W - 1; ++i) {
        if (sim.constraint[i]) {
            constrainAccel(i);
            continue;
        }
        const QPointF& p = pos[i];
        const QPointF n0 = pos[i - 1], n1 = pos[i + 1], n2 = pos[i + W];
        acc[i] = QPointF(((xl - (p.x() - n0.x())) * k + ((n1.x() - p.x()) - xl) * k + (n2.x() - p.x()) * k) / 3.0,
                         (((n2.y() - p.y()) - yl) * k + (n0.y() - p.y()) * k + (n1.y() - p.y()) * k) / 3.0);
    }
    // ── bottom border ──
    for (int i = W * (H - 1) + 1; i < N - 1; ++i) {
        if (sim.constraint[i]) {
            constrainAccel(i);
            continue;
        }
        const QPointF& p = pos[i];
        const QPointF n0 = pos[i - 1], n1 = pos[i + 1], n2 = pos[i - W];
        acc[i] = QPointF(((xl - (p.x() - n0.x())) * k + ((n1.x() - p.x()) - xl) * k + (n2.x() - p.x()) * k) / 3.0,
                         ((yl - (p.y() - n2.y())) * k + (n0.y() - p.y()) * k + (n1.y() - p.y()) * k) / 3.0);
    }
    // ── left border ──
    for (int i = W; i < W * (H - 1); i += W) {
        if (sim.constraint[i]) {
            constrainAccel(i);
            continue;
        }
        const QPointF& p = pos[i];
        const QPointF n0 = pos[i + 1], n1 = pos[i - W], n2 = pos[i + W];
        acc[i] = QPointF((((n0.x() - p.x()) - xl) * k + (n1.x() - p.x()) * k + (n2.x() - p.x()) * k) / 3.0,
                         ((yl - (p.y() - n1.y())) * k + ((n2.y() - p.y()) - yl) * k + (n0.y() - p.y()) * k) / 3.0);
    }
    // ── right border ──
    for (int i = 2 * W - 1; i < N - 1; i += W) {
        if (sim.constraint[i]) {
            constrainAccel(i);
            continue;
        }
        const QPointF& p = pos[i];
        const QPointF n0 = pos[i - 1], n1 = pos[i - W], n2 = pos[i + W];
        acc[i] = QPointF(((xl - (p.x() - n0.x())) * k + (n1.x() - p.x()) * k + (n2.x() - p.x()) * k) / 3.0,
                         ((yl - (p.y() - n1.y())) * k + ((n2.y() - p.y()) - yl) * k + (n0.y() - p.y()) * k) / 3.0);
    }
    // ── interior ──
    for (int j = 1; j < H - 1; ++j) {
        for (int i = 1; i < W - 1; ++i) {
            const int idx = i + j * W;
            if (sim.constraint[idx]) {
                constrainAccel(idx);
                continue;
            }
            const QPointF& p = pos[idx];
            const QPointF n0 = pos[idx - 1], n1 = pos[idx + 1], n2 = pos[idx - W], n3 = pos[idx + W];
            acc[idx] = QPointF((((n0.x() - p.x()) - xl) * k + (xl - (p.x() - n1.x())) * k + (n2.x() - p.x()) * k
                                + (n3.x() - p.x()) * k)
                                   / 4.0,
                               ((yl - (p.y() - n2.y())) * k + ((n3.y() - p.y()) - yl) * k + (n0.y() - p.y()) * k
                                + (n1.y() - p.y()) * k)
                                   / 4.0);
        }
    }

    ringMean(acc, sim);

    // velocity: v = a*t + v*drag (KWin's exact form, not v += a*t)
    for (int i = 0; i < N; ++i) {
        sim.velocity[i] = QPointF(acc[i].x() * time + sim.velocity[i].x() * sim.params.drag,
                                  acc[i].y() * time + sim.velocity[i].y() * sim.params.drag);
    }
    ringMean(sim.velocity, sim);

    // position: p += v*t*moveFactor
    for (int i = 0; i < N; ++i) {
        sim.position[i] += sim.velocity[i] * (time * sim.params.moveFactor);
    }
}

} // namespace

void initMeshSim(MeshSim& sim, const QRectF& frame, const QPointF& cursor, const MeshSimParams& params)
{
    sim.params = params;
    updateOrigins(sim, frame);
    for (int i = 0; i < MeshSim::kCount; ++i) {
        sim.position[i] = sim.origin[i];
        sim.velocity[i] = QPointF();
        sim.accel[i] = QPointF();
        sim.constraint[i] = false;
    }
    // Grip: pin the node nearest the cursor (KWin's picked-point pick).
    const qreal xl = frame.width() / (MeshSim::kW - 1.0);
    const qreal yl = frame.height() / (MeshSim::kH - 1.0);
    int gi = int((cursor.x() - frame.x()) / (xl > 0 ? xl : 1.0) + 0.5);
    int gj = int((cursor.y() - frame.y()) / (yl > 0 ? yl : 1.0) + 0.5);
    gi = qBound(0, gi, MeshSim::kW - 1);
    gj = qBound(0, gj, MeshSim::kH - 1);
    sim.constraint[gj * MeshSim::kW + gi] = true;
    // ONLY the grip is constrained — the entire rest of the sheet hangs off
    // it and trails, like cloth dragged by one corner. NO interior pinning
    // (that made a rigid centre with wiggling edges = a jiggling blob) and
    // NO throb impulse (a symmetric squeeze = the pudding character). The
    // trailing drape falls out of the soft neighbour springs + the tight
    // grip alone.
    sim.lastFrameTopLeft = frame.topLeft();
    sim.accumMs = 0.0;
    sim.settled = false;
    sim.initialized = true;
}

void stepMeshSim(MeshSim& sim, const QRectF& frame, qreal deltaMs)
{
    if (!sim.initialized || frame.width() < 1.0 || frame.height() < 1.0) {
        return;
    }
    updateOrigins(sim, frame);
    sim.accumMs += qBound(0.0, deltaMs, 200.0);
    qreal accSum = 0.0;
    qreal velSum = 0.0;
    bool stepped = false;
    while (sim.accumMs >= kIntegrationStepMs) {
        sim.accumMs -= kIntegrationStepMs;
        integrateOne(sim, frame, kIntegrationStepMs);
        stepped = true;
    }
    if (stepped) {
        for (int i = 0; i < MeshSim::kCount; ++i) {
            accSum += absSum(sim.accel[i]);
            velSum += absSum(sim.velocity[i]);
        }
        sim.settled = (accSum < kStopAccel && velSum < kStopVel);
    }
}

} // namespace PlasmaZones::ShaderInternal
