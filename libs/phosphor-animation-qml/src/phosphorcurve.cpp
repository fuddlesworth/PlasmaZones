// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationQml/PhosphorCurve.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>
#include <PhosphorAnimationQml/PhosphorEasing.h>
#include <PhosphorAnimationQml/PhosphorSpring.h>

namespace PhosphorAnimation {

std::atomic<CurveRegistry*> PhosphorCurve::s_registry{nullptr};

void PhosphorCurve::setDefaultRegistry(CurveRegistry* registry)
{
    // Relaxed store: the publishing thread must have fully constructed
    // *registry before calling this method, and the consumer-side
    // load-and-use sequence below is a single pointer dereference that
    // needs no happens-before with any other memory. Matches the
    // process-lifetime-singleton contract documented on setDefaultRegistry.
    s_registry.store(registry, std::memory_order_relaxed);
}

PhosphorCurve PhosphorCurve::fromString(const QString& str)
{
    // Route through CurveRegistry so every curve type the registry
    // knows resolves — including user-authored curves registered by
    // CurveLoader (Phase 4 decision U). Returning a null handle on
    // parse failure (rather than a default-constructed Easing) gives
    // QML callers an `isNull()` signal they can check.
    CurveRegistry* registry = s_registry.load(std::memory_order_relaxed);
    if (!registry) {
        return PhosphorCurve();
    }
    auto curve = registry->tryCreate(str);
    return PhosphorCurve(std::move(curve));
}

PhosphorCurve PhosphorCurve::fromEasing(const PhosphorEasing& easing)
{
    // Copy into a heap-allocated Easing; the shared_ptr<const Curve>
    // takes ownership. PhosphorEasing's stored value stays untouched
    // — the QML-side wrapper retains value semantics after the
    // promotion.
    return PhosphorCurve(std::make_shared<const Easing>(easing.value()));
}

PhosphorCurve PhosphorCurve::fromSpring(const PhosphorSpring& spring)
{
    return PhosphorCurve(std::make_shared<const Spring>(spring.value()));
}

} // namespace PhosphorAnimation
