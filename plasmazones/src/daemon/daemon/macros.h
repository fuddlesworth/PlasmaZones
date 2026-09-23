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
// This gate stops these handlers acting on the WRONG screen.
// setActiveScreenHint itself accepts any id; the filtering happens inside
// NavigationController, which has two different fallbacks, each with its own
// admission test:
//
//   - resolveActiveScreen (the master-count path) drops a NON-MEMBER and
//     falls back to the first entry of the engine set, hash-ordered.
//   - tiledWindowsForFocusedScreen (the focusMaster / swapWithMaster path)
//     takes the hinted screen only when it is a member AND has a TilingState
//     AND that state holds tiled windows; otherwise it runs a ranked scan
//     that returns ANOTHER screen's state.
//
// Both conditions are needed, and neither implies the other. A non-member can
// still hold a live state for the current key — setAutotileScreens prunes only
// the removed screens' states for the CURRENT desktop, and deliberately leaves
// other-context states behind — so a state probe alone would let a screen the
// engine dropped misroute the count verbs. Conversely a member can hold an
// existing-but-EMPTY state (nothing reaps a state when its last window
// leaves), which the focus verbs reject, sending them to the cross-screen
// scan. Testing membership AND tiled windows is exactly the pair the hinted
// branch requires, so a press that passes here can only act on this screen.
//
// None of these four verbs mean anything on a screen with no tiled windows, so
// the cost is only that such a press is silent instead of drawing the engine's
// own "no windows" card. stateForScreen is non-creating, so the probe
// allocates nothing.
//
// The two out-of-line master-ratio handlers reach only resolveActiveScreen, so
// membership alone is sufficient there.
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
        if (!m_autotileEngine->isActiveOnScreen(screenId))                                                             \
            return;                                                                                                    \
        const PhosphorEngine::IPlacementState* tilingState = m_autotileEngine->stateForScreen(screenId);               \
        if (!tilingState || tilingState->tiledWindowCount() == 0)                                                      \
            return;                                                                                                    \
        m_autotileEngine->setActiveScreenHint(screenId);                                                               \
        m_autotileEngine->engineCall;                                                                                  \
    }
