// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

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
