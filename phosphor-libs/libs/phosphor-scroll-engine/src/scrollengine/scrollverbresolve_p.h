// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/StripAxis.h>

#include <QString>

namespace PhosphorScrollEngine::Detail {

/// ROLE step -> PHYSICAL direction token, for the verbs and navigation paths
/// that hold a main/cross step and owe the rest of the system a physical
/// word: the navigation OSD renders it as an arrow, the cross-surface
/// resolver reads it geometrically, and the sibling engines consume it
/// verbatim.
///
/// Shared by engine_navigation.cpp and engine_verbs.cpp, ONE definition, for
/// the same reason P_SCROLL_RESOLVE lives here: a verb whose arrow disagreed
/// with the direction focus actually moved would be a silent lie on screen.
inline QString physicalTokenForMain(int delta, StripAxis axis)
{
    if (delta == 0) {
        return {};
    }
    if (axis.isHorizontal()) {
        return delta < 0 ? QStringLiteral("left") : QStringLiteral("right");
    }
    return delta < 0 ? QStringLiteral("up") : QStringLiteral("down");
}

/// The same for a step ACROSS the strip, within a column. Note the words swap
/// with the axis exactly as the main-axis pair does — on a vertical strip the
/// stack runs left-to-right.
inline QString physicalTokenForCross(int delta, StripAxis axis)
{
    if (delta == 0) {
        return {};
    }
    if (axis.isHorizontal()) {
        return delta < 0 ? QStringLiteral("up") : QStringLiteral("down");
    }
    return delta < 0 ? QStringLiteral("left") : QStringLiteral("right");
}

} // namespace PhosphorScrollEngine::Detail

// Shared preamble for every strip operation: resolve the target screen and
// its current-context state. Emits no feedback itself — callers own that.
// Included by engine_navigation.cpp and engine_verbs.cpp; ONE definition so
// the two TUs cannot silently diverge. The macro stays defined through each
// including TU (no per-file #undef — under a unity build the header's
// include guard makes an #undef in one file erase it for the next).
//
// `params` is resolved only once the state is known non-null: the resolve
// pays a ScreenManager query plus a context-gap-provider invocation, and a
// no-op press on a screen with no strip must not pay it. Every caller bails
// on `!state` before reading `params`, so the empty value is never consumed.
#define P_SCROLL_RESOLVE(screenIdExpr)                                                                                 \
    const QString screen = resolveOperationScreen(screenIdExpr);                                                       \
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);                 \
    const ScrollLayoutParams params = state ? layoutParamsForScreen(screen) : ScrollLayoutParams                       \
    {                                                                                                                  \
    }
