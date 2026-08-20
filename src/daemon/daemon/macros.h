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
// The stateForScreen gate mirrors handleRetile (navigation.cpp): the router
// answers Autotile for the explicit algorithm opt-out, but the engine holds
// no state for such a screen and rejects the hint (isAutotileScreen is
// m_autotileScreens membership, which the opt-out arm of updateEngineScreens
// keeps the screen out of). The engine's NavigationController would then fall
// back to the first entry of m_autotileScreens — hash-ordered — and mutate an
// UNRELATED screen's tiling state. The overload is non-creating, so probing
// here allocates nothing.
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
