// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// DRY macro for autotile-only handlers that guard on engine enabled state
// and resolve the focused screen to ensure the correct virtual screen is targeted.
// Usage: HANDLE_AUTOTILE_ONLY(FocusMaster, focusMaster())
#define HANDLE_AUTOTILE_ONLY(name, engineCall)                                                                         \
    void Daemon::handle##name()                                                                                        \
    {                                                                                                                  \
        if (!m_autotileEngine || !m_autotileEngine->isEnabled())                                                       \
            return;                                                                                                    \
        const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);                                     \
        if (screenId.isEmpty() || !m_autotileEngine->isAutotileScreen(screenId))                                       \
            return;                                                                                                    \
        m_autotileEngine->engineCall;                                                                                  \
    }
