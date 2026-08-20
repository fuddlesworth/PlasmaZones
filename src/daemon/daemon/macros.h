// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// DRY macro for autotile-only handlers that guard on engine enabled state
// and resolve the focused screen to ensure the correct virtual screen is targeted.
// Sets the engine's active screen hint so parameterless methods (focusMaster,
// increaseMasterCount, etc.) operate on the correct screen.
//
// Mode ownership is resolved through m_screenModeRouter — the single source
// of truth for "which engine owns screen X". This replaces the previous
// m_autotileEngine->isAutotileScreen() inline check so daemon handlers and
// adaptors agree on the same dispatch decision.
//
// Usage: HANDLE_AUTOTILE_ONLY(FocusMaster, focusMaster())
//
// Disable cascade collapsed through PhosphorContext::ContextResolver — was
// a 3-line `(isContextDisabled + currentDesktop + currentActivity)` chain
// inside the macro body; now one resolver call with an explicit
// Autotile-mode handle (the macro is autotile-only by definition, so
// handleForMode skips the router round-trip).
//
// The stateForScreen gate stops these handlers acting on the WRONG screen.
// setActiveScreenHint itself accepts any id; the filtering happens inside
// NavigationController, and it has two different fallbacks:
//
//   - resolveActiveScreen (the master-count path) drops a non-member and
//     falls back to the first entry of the engine set, hash-ordered.
//   - tiledWindowsForFocusedScreen (the focusMaster / swapWithMaster path)
//     takes the hinted screen ONLY when a TilingState exists for it, and
//     otherwise runs a ranked scan that returns ANOTHER screen's state.
//
// So membership alone is not enough here: a screen that is a member but holds
// no state still routes focus and window-order changes onto an unrelated
// monitor. Gating on the state covers both fallbacks for all four users. The
// overload is non-creating, so the probe allocates nothing.
//
// The cost is that a stateless screen answers a press with silence rather
// than the engine's own "no windows" feedback. That is the right trade: the
// screen has no tiled windows for these verbs to act on either way, and a
// missing card is better than acting on a monitor the user was not looking at.
// The two out-of-line master-ratio handlers gate on MEMBERSHIP instead, which
// is sufficient there because they only reach resolveActiveScreen.
#define HANDLE_AUTOTILE_ONLY(name, engineCall)                                                                         \
    void Daemon::handle##name()                                                                                        \
    {                                                                                                                  \
        if (!m_autotileEngine || !m_autotileEngine->isEnabled())                                                       \
            return;                                                                                                    \
        const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);              \
        if (screenId.isEmpty() || !m_screenModeRouter || !m_screenModeRouter->isAutotileMode(screenId))                \
            return;                                                                                                    \
        if (isFocusedContextGatedForMode(screenId, PhosphorZones::AssignmentEntry::Autotile))                          \
            return;                                                                                                    \
        if (!m_autotileEngine->stateForScreen(screenId))                                                               \
            return;                                                                                                    \
        m_autotileEngine->setActiveScreenHint(screenId);                                                               \
        m_autotileEngine->engineCall;                                                                                  \
    }
