// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Home for `IMotionClock`'s out-of-line members. The interface itself
// is header-only (all methods are pure virtual or short inline
// predicates), but the shared epoch sentinel needs a unique address
// exported from a single TU across the process — see the doc at
// `IMotionClock::steadyClockEpoch()` for the rationale.

#include <PhosphorAnimation/IMotionClock.h>

namespace PhosphorAnimation {

const void* IMotionClock::steadyClockEpoch()
{
    // Shared sentinel address for every std::chrono::steady_clock-backed
    // IMotionClock. The pointee is never dereferenced; only the pointer
    // identity matters for the rebind-compatibility test in
    // AnimatedValue::rebindClock / AnimationController::advanceAnimations.
    // Exporting this from a single TU guarantees one unique address
    // across the process.
    static const char kSentinel{};
    return &kSentinel;
}

} // namespace PhosphorAnimation
