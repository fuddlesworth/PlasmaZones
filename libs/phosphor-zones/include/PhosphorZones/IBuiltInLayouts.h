// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// IBuiltInLayouts — system-installed layout templates (ships-with-app).
//
// Split out of ILayoutManager so code that only needs to list or
// (re-)create the bundled layouts (e.g. the reset-to-defaults button)
// can depend on this 2-method contract.

#include <phosphorzones_export.h>

#include <QVector>

namespace PhosphorZones {

class Layout;

/**
 * @brief Control surface for the layouts shipped with PlasmaZones.
 *
 * Built-in layouts are the set of templates the application installs
 * as system layouts; @c createBuiltInLayouts seeds a fresh user
 * profile with them. @c builtInLayouts returns the currently loaded
 * set (borrowed pointers owned by the layout registry).
 */
class PHOSPHORZONES_EXPORT IBuiltInLayouts
{
public:
    IBuiltInLayouts() = default;
    virtual ~IBuiltInLayouts();

    /// (Re-)create the bundled layouts in the registry. Idempotent —
    /// re-running does not duplicate existing entries.
    virtual void createBuiltInLayouts() = 0;

    /// Borrowed pointers owned by the layout registry. Do not delete.
    virtual QVector<Layout*> builtInLayouts() const = 0;

protected:
    IBuiltInLayouts(const IBuiltInLayouts&) = default;
    IBuiltInLayouts& operator=(const IBuiltInLayouts&) = default;
};

} // namespace PhosphorZones
