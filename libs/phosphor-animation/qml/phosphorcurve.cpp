// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorCurve.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>
#include <PhosphorAnimation/qml/PhosphorEasing.h>
#include <PhosphorAnimation/qml/PhosphorSpring.h>

namespace PhosphorAnimation {

CurveRegistry* PhosphorCurve::s_registry = nullptr;

void PhosphorCurve::setDefaultRegistry(CurveRegistry* registry)
{
    s_registry = registry;
}

PhosphorCurve PhosphorCurve::fromString(const QString& str)
{
    // Route through CurveRegistry so every curve type the registry
    // knows resolves — including user-authored curves registered by
    // CurveLoader (Phase 4 decision U). Returning a null handle on
    // parse failure (rather than a default-constructed Easing) gives
    // QML callers an `isNull()` signal they can check.
    if (!s_registry) {
        return PhosphorCurve();
    }
    auto curve = s_registry->tryCreate(str);
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
