// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal logging category for PhosphorScreens. NOT installed; this header
// stays under src/ so consumers don't accidentally inherit our category and
// drown their own logs in our debug output.
//
// The category is defined in PhosphorScreensCore but used by the D-Bus
// translation units in the PhosphorScreens target, so it is exported across
// that library boundary.

#include "phosphorscreenscore_export.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QThread>

namespace PhosphorScreens {

Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcPhosphorScreens, PHOSPHORSCREENSCORE_EXPORT)

/// Debug-only GUI-thread guard for ScreenManager accessors that touch the
/// mutable lazy caches (m_availableGeometryCache, m_cachedEffectiveScreenIds,
/// m_virtualGeometryCache, m_warnedEffectiveIdMisses). Compiles out in
/// release builds; in debug builds it catches worker-thread misuse at the
/// accessor boundary rather than leaving it as silent cache corruption to
/// debug later. Mirrors PS_SCREEN_IDENTITY_ASSERT_GUI_THREAD in
/// screenidentity.cpp so the contract is enforced uniformly across the lib.
#define PS_SCREEN_MANAGER_ASSERT_GUI_THREAD()                                                                          \
    Q_ASSERT_X(QCoreApplication::instance() != nullptr                                                                 \
                   && QThread::currentThread() == QCoreApplication::instance()->thread(),                              \
               Q_FUNC_INFO, "ScreenManager accessors must be called on the GUI thread")

} // namespace PhosphorScreens
