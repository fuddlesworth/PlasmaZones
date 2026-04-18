// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Curve.h>

namespace PhosphorAnimation {

void Curve::step(qreal dt, CurveState& state, qreal target) const
{
    // Default: advance state.time by dt and lerp from state.startValue
    // to target via the parametric evaluate(t). Callers of stateless
    // curves are expected to pre-scale dt by 1/duration before calling
    // so state.time reaches 1 at animation end.
    //
    // Numerical velocity is the derivative across this step, preserved
    // so stateful-curve retargets can ingest parametric predecessors
    // without a velocity discontinuity.
    const qreal prevValue = state.value;
    state.time += dt;
    const qreal t = qBound(0.0, state.time, 1.0);
    const qreal progress = evaluate(t);
    state.value = state.startValue + progress * (target - state.startValue);
    state.velocity = (dt > 0.0) ? (state.value - prevValue) / dt : 0.0;
}

bool Curve::equals(const Curve& other) const
{
    // Fallback equality via string round-trip. Subclasses with
    // floating-point parameters that get rounded by toString()
    // (Easing / Spring) override this for tight comparison.
    return typeId() == other.typeId() && toString() == other.toString();
}

} // namespace PhosphorAnimation
