// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Curve.h>

namespace PhosphorAnimation {

void Curve::step(qreal dt, CurveState& state, qreal target) const
{
    // Default: advance state.time by dt and recompute value parametrically
    // using a duration of 1.0. Callers of stateless curves are expected to
    // pre-scale dt by 1/duration before calling step() — the curve itself
    // has no duration knowledge.
    //
    // Numerical velocity is the approximate derivative across this step,
    // preserved so spring-style retargets can ingest parametric predecessors
    // without a velocity discontinuity.
    const qreal prevValue = state.value;
    state.time += dt;
    const qreal t = qBound(0.0, state.time, 1.0);
    state.value = evaluate(t) * target;
    state.velocity = (dt > 0.0) ? (state.value - prevValue) / dt : 0.0;
}

bool Curve::equals(const Curve& other) const
{
    // Fallback equality via string round-trip. Subclasses override when
    // their toString() form is lossy (e.g. floating-point truncation).
    return typeId() == other.typeId() && toString() == other.toString();
}

} // namespace PhosphorAnimation
