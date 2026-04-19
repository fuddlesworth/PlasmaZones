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
#define HANDLE_AUTOTILE_ONLY(name, engineCall)                                                                         \
    void Daemon::handle##name()                                                                                        \
    {                                                                                                                  \
        if (!m_autotileEngine || !m_autotileEngine->isEnabled())                                                       \
            return;                                                                                                    \
        const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);              \
        if (screenId.isEmpty() || !m_screenModeRouter || !m_screenModeRouter->isAutotileMode(screenId))                \
            return;                                                                                                    \
        if (isContextDisabled(m_settings.get(), screenId, currentDesktop(), currentActivity()))                        \
            return;                                                                                                    \
        m_autotileEngine->setActiveScreenHint(screenId);                                                               \
        m_autotileEngine->engineCall;                                                                                  \
    }
