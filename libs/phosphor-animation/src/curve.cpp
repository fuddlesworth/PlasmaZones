// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Curve.h>

namespace PhosphorAnimation {

void Curve::step(qreal dt, CurveState& state, qreal target) const
{
    // Default: dt is real seconds, consistent with Spring::step. Map
    // elapsed seconds into the parametric domain via state.duration.
    // Zero/negative duration completes the animation immediately so the
    // default matches WindowMotion's "zero-duration = snap-to-target"
    // behavior and avoids a divide-by-zero.
    const qreal prevValue = state.value;
    state.time += dt;

    if (state.duration <= 0.0) {
        state.value = target;
        state.velocity = 0.0;
        return;
    }

    const qreal t = qBound(0.0, state.time / state.duration, 1.0);
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
