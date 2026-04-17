// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>

#include <memory>

#include "phosphorshortcuts_export.h"

namespace Phosphor::Shortcuts {

class IBackend;

/**
 * Backend selection hint for createBackend().
 *
 * Most consumers should pass Auto (the default) and let the library detect
 * the running environment. Explicit hints exist for tests and for consumers
 * that want deterministic behaviour in CI.
 */
enum class BackendHint {
    Auto, ///< Detect via session bus service introspection.
    KGlobalAccel, ///< Force KGlobalAccel. Falls through to DBusTrigger if
                  ///< the library was built without PHOSPHORSHORTCUTS_HAVE_KGLOBALACCEL.
    Portal, ///< Force XDG Desktop Portal GlobalShortcuts.
    DBusTrigger, ///< Force D-Bus trigger fallback.
    Native, ///< Reserved for the future INativeGrabber-backed backend.
            ///< Falls through to DBusTrigger for now.
};

/**
 * Create a backend instance.
 *
 * Auto-detection order:
 *  1. No session bus → DBusTrigger.
 *  2. `org.kde.kglobalaccel` advertised AND KF6::GlobalAccel linked →
 *     KGlobalAccel.
 *  3. `org.freedesktop.portal.GlobalShortcuts` on the portal → Portal.
 *  4. Otherwise → DBusTrigger.
 *
 * The returned backend is empty (no shortcuts registered). Hand it to a
 * Registry, bind() some shortcuts, then flush().
 */
PHOSPHORSHORTCUTS_EXPORT std::unique_ptr<IBackend> createBackend(BackendHint hint = BackendHint::Auto,
                                                                 QObject* parent = nullptr);

} // namespace Phosphor::Shortcuts
