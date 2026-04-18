// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ILayoutPersistence — disk I/O + import/export for manual layouts.
//
// Split out of ILayoutManager so file-watcher / save-on-modify paths
// can depend on this narrower surface. Also isolates the layout
// directory configuration knob from the in-memory mutation API.

#include <phosphorzones_export.h>

#include <QString>

namespace PhosphorZones {

class Layout;

/**
 * @brief Persistence + import/export for manual layouts and assignments.
 *
 * Layouts persist to individual JSON files under @c layoutDirectory()
 * (typically ~/.local/share/plasmazones/layouts/). Assignments persist
 * via the settings backend — saved separately from the layout files.
 *
 * This interface is non-reactive — it has no change signals. Callers that
 * need to observe layout-directory changes, reload completion, or save
 * completion must subscribe on the concrete @c LayoutManager (which owns
 * the Qt signals) rather than this narrower contract.
 */
class PHOSPHORZONES_EXPORT ILayoutPersistence
{
public:
    ILayoutPersistence() = default;
    virtual ~ILayoutPersistence();

    // Layout directory (where per-layout JSON files live).
    virtual QString layoutDirectory() const = 0;
    virtual void setLayoutDirectory(const QString& directory) = 0;

    // Layout set persistence.
    virtual void loadLayouts() = 0;
    virtual void saveLayouts() = 0;
    /// @param layout Borrowed — no ownership transfer. Persists the
    ///               single layout's JSON file; cheaper than saveLayouts
    ///               when only one layout is dirty.
    virtual void saveLayout(Layout* layout) = 0;

    // Assignment persistence (via the settings backend).
    virtual void loadAssignments() = 0;
    virtual void saveAssignments() = 0;

    // Single-layout import / export — used by the KCM and the editor.
    virtual void importLayout(const QString& filePath) = 0;
    /// @param layout Borrowed — no ownership transfer.
    virtual void exportLayout(Layout* layout, const QString& filePath) = 0;

protected:
    ILayoutPersistence(const ILayoutPersistence&) = default;
    ILayoutPersistence& operator=(const ILayoutPersistence&) = default;
};

} // namespace PhosphorZones
