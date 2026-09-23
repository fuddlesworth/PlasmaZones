// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/SurfaceAnimator.h>

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <PhosphorLayer/PhosphorLayer.h>
#include <PhosphorShellPatterns/Patterns.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <thread>

using namespace PhosphorAnimationLayer;
using PhosphorAnimation::PhosphorProfileRegistry;
using PhosphorAnimation::Profile;
using PhosphorLayer::Surface;
using PhosphorLayer::SurfaceFactory;

namespace {

/// Build a Profile with explicit duration + an OutCubic-shaped Easing
/// curve. Tests use a short duration (15ms) so the spin-wait at the end
/// of show/hide takes a few frames at most.
Profile makeProfile(int durationMs)
{
    Profile p;
    p.duration = durationMs;
    auto easing = std::make_shared<PhosphorAnimation::Easing>();
    easing->type = PhosphorAnimation::Easing::Type::CubicBezier;
    easing->x1 = 0.215;
    easing->y1 = 0.61;
    easing->x2 = 0.355;
    easing->y2 = 1.0;
    p.curve = easing;
    return p;
}

/// Spin the event loop until @p predicate returns true or @p timeoutMs elapses.
/// Wraps the QtQuickClock + AnimatedValue tick path so tests can wait for
/// async completion without arbitrary fixed sleeps.
//
// `chrono::time_point::operator<` in libstdc++ is implemented via a
// `<=>` constraint that, on some libstdc++ versions, falls through a
// `std::common_comparison_category` SFINAE expression containing a
// literal `0`, tripping `-Wzero-as-null-pointer-constant` at every
// instantiation. Suppress locally — the comparison itself is correct,
// the warning is a libstdc++ template-expansion artefact.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
template<typename Predicate>
bool waitFor(Predicate p, int timeoutMs = 1000)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (p()) {
            return true;
        }
        QTest::qWait(5);
    }
    return p();
}
#pragma GCC diagnostic pop

SurfaceAnimator::Config defaultsForTesting()
{
    SurfaceAnimator::Config c;
    c.showProfile = QStringLiteral("test.show");
    c.hideProfile = QStringLiteral("test.hide");
    return c;
}

/// Resolve the QQuickItem the animator drives: when SurfaceConfig::contentItem
/// is provided, the Surface parents it under window->contentItem() and the
/// animator drives the inner item (`m_rootItem`). Reading the wrong item is
/// a common test foot-gun — centralise the lookup so every show/hide test
/// reads the same child the animator writes.
QQuickItem* animatedItem(Surface* surface)
{
    if (!surface || !surface->window()) {
        return nullptr;
    }
    auto* parent = surface->window()->contentItem();
    if (!parent) {
        return nullptr;
    }
    const auto children = parent->childItems();
    return children.isEmpty() ? parent : children.first();
}

} // namespace
