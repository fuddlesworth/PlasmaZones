// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2008 Cédric Borgese <cedric.borgese@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generic soft-body control lattice for interactive move/resize
// transitions — a faithful port of KWin's wobblywindows spring model
// (GPL-2.0-or-later), reduced to the move path and exposed as reusable
// infrastructure rather than a single effect. The host runs ONE lattice
// per held transition and publishes the solved node displacements as the
// `iMoveMesh` contract uniform; any geometry pack that declares it gets a
// neighbour-coupled, physically-simulated 4x4 control grid to deform with,
// without re-implementing the physics. Wobble is the first consumer; a
// flag / jello / drape pack would read the same lattice.
//
// The model: 16 nodes on a 4x4 grid whose rest positions (origins) track
// the LIVE window frame. ONLY the node nearest the cursor at grab is the grip
// (constrained → springs onto its origin, i.e. chases the frame); every other
// node (interior and edge alike) is free, coupled only to its NEIGHBOURS at
// ideal spacing, so a drag propagates node to node as a wave. No interior
// pinning — a rigid centre with wiggling edges read as a jiggling blob.
// Integrated in fixed <=10 ms
// substeps with KWin's ring-mean smoothing; a generic caller need only
// init, feed the live rect each frame, and read displacements.

#pragma once

#include <QPointF>
#include <QRectF>

namespace PlasmaZones {

/// Tunable spring constants. Defaults mirror KWin's middle "wobbliness"
/// preset (set_2); a pack may override via its metadata so a stiff jello
/// and a loose flag can share this one solver.
struct MeshSimParams
{
    // Sheet stiffness: LOW so the body drapes and trails like fabric rather
    // than snapping back. Grip stiffness is separate and HIGH so the grabbed
    // point stays under the cursor while the rest hangs off it — a dragged
    // cloth, not a jiggling blob. `drag` is KWin's velocity-RETENTION factor
    // (higher = more oscillation, counter-intuitively); kept modest so the
    // sheet settles by drifting, not by ringing (the "pudding" bounce).
    qreal stiffness = 0.018;
    qreal gripStiffness = 0.16;
    qreal drag = 0.82;
    qreal moveFactor = 0.16;
};

struct MeshSim
{
    static constexpr int kW = 4;
    static constexpr int kH = 4;
    static constexpr int kCount = kW * kH;

    QPointF origin[kCount];
    QPointF position[kCount];
    QPointF velocity[kCount];
    QPointF accel[kCount];
    QPointF buffer[kCount]; // scratch for the ring-mean smoothing pass
    bool constraint[kCount] = {};

    MeshSimParams params;
    QPointF lastFrameTopLeft;
    qreal accumMs = 0.0;
    bool initialized = false;
    /// False once the lattice has energy; the caller keeps the transition
    /// alive (and repainting) until this reads true again after release.
    bool settled = true;
};

namespace ShaderInternal {

/// Seed the lattice to @p frame (all nodes at rest on the frame grid) and
/// constrain the node nearest @p cursor as the grip. Call once at grab.
void initMeshSim(MeshSim& sim, const QRectF& frame, const QPointF& cursor, const MeshSimParams& params);

/// Advance the simulation by @p deltaMs of wall time (stepped internally in
/// <=10 ms increments), with the node origins re-derived from @p frame each
/// step so the constrained/pinned nodes chase the live window. Updates
/// `settled`.
void stepMeshSim(MeshSim& sim, const QRectF& frame, qreal deltaMs);

} // namespace ShaderInternal
} // namespace PlasmaZones
