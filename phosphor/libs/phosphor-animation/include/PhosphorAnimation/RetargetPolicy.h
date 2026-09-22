// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorAnimation {

/// How an in-flight AnimatedValue<T> reshapes on retarget().
enum class RetargetPolicy {
    /// Carry velocity across the segment boundary, re-scaled to the new distance. Default.
    PreserveVelocity,
    /// Zero velocity on retarget; motion restarts from rest toward the new target.
    ResetVelocity,
    /// Position-continuous only; velocity treatment delegated to the curve's natural behaviour.
    PreservePosition,
};

} // namespace PhosphorAnimation
