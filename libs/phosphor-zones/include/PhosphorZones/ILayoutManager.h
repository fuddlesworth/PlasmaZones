// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ILayoutManager — abstract interface for managing manual-layout definitions
// and per-context (screen/desktop/activity) assignments.
//
// Split out of interfaces.h so layout-owning code can include just this
// contract without pulling ISettings / IZoneDetector / IOverlayService.
// Prerequisite for eventually moving Layout + ILayoutManager into a
// standalone phosphor-zones library.

#include <phosphorzones_export.h>

#include <QHash>
#include <QPair>
#include <QString>
#include <QUuid>
#include <QVector>

namespace PhosphorZones {

class Layout;

/**
 * @brief Read-only catalog of manual zone layouts.
 *
 * Minimal ISP-compliant subset used by consumers that only need to
 * enumerate and look up layouts — e.g. @c ZonesLayoutSource or any UI
 * that paints previews without mutating state. Fixture tests can stub
 * this two-method interface instead of carrying the 30-method
 * @c ILayoutManager surface.
 *
 * @c ILayoutManager inherits from @c ILayoutCatalog so any callsite
 * accepting an @c ILayoutCatalog also accepts a full manager.
 */
class PHOSPHORZONES_EXPORT ILayoutCatalog
{
public:
    ILayoutCatalog() = default;
    virtual ~ILayoutCatalog();

    /// Enumerate every known layout. Borrowed pointers — owned by the
    /// concrete catalog (typically @c LayoutManager). Order is the
    /// catalog's natural iteration order.
    virtual QVector<Layout*> layouts() const = 0;

    /// Resolve a layout by its stable UUID. Returns nullptr when no
    /// layout with that id is known to the catalog.
    virtual Layout* layoutById(const QUuid& id) const = 0;

protected:
    ILayoutCatalog(const ILayoutCatalog&) = default;
    ILayoutCatalog& operator=(const ILayoutCatalog&) = default;
};

/**
 * @brief Abstract interface for layout management
 *
 * Note: This is a non-QObject interface (pure virtual abstract class).
 *
 * Design rationale: Qt's signal system doesn't work well with abstract interfaces
 * because signal shadowing between base and derived classes causes heap corruption
 * when using new-style Qt::connect with function pointers. By making this interface
 * pure virtual (no QObject), we avoid the shadowing problem entirely.
 *
 * Components needing signals should use LayoutManager* directly.
 */
class PHOSPHORZONES_EXPORT ILayoutManager : public ILayoutCatalog
{
public:
    ILayoutManager() = default;
    virtual ~ILayoutManager();

    // Layout directory
    virtual QString layoutDirectory() const = 0;
    virtual void setLayoutDirectory(const QString& directory) = 0;

    // Layout management
    virtual int layoutCount() const = 0;
    virtual Layout* layout(int index) const = 0;
    virtual Layout* layoutByName(const QString& name) const = 0;

    virtual void addLayout(Layout* layout) = 0;
    virtual void removeLayout(Layout* layout) = 0;
    virtual void removeLayoutById(const QUuid& id) = 0;
    virtual Layout* duplicateLayout(Layout* source) = 0;

    // Active layout (internal — used for resnap/geometry/overlay machinery)
    virtual Layout* activeLayout() const = 0;
    virtual void setActiveLayout(Layout* layout) = 0;
    virtual void setActiveLayoutById(const QUuid& id) = 0;

    // Default layout (settings-based fallback for the layout cascade)
    virtual Layout* defaultLayout() const = 0;

    // Current context for per-screen layout lookups
    virtual int currentVirtualDesktop() const = 0;
    virtual QString currentActivity() const = 0;

    /**
     * @brief Convenience: resolve layout for screen using current desktop/activity context
     *
     * Equivalent to layoutForScreen(screenId, currentVirtualDesktop(), currentActivity())
     * with a fallback to defaultLayout() when no per-screen assignment matches.
     * Use this everywhere a "give me the layout for this screen right now" is needed.
     *
     * @param screenId Stable EDID-based screen identifier (or connector name — auto-resolved)
     */
    Layout* resolveLayoutForScreen(const QString& screenId) const
    {
        Layout* layout = layoutForScreen(screenId, currentVirtualDesktop(), currentActivity());
        return layout ? layout : defaultLayout();
    }

    // Layout assignments (screenId: stable EDID-based identifier or connector name fallback)
    virtual Layout* layoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                    const QString& activity = QString()) const = 0;
    virtual void assignLayout(const QString& screenId, int virtualDesktop, const QString& activity, Layout* layout) = 0;
    virtual void assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                  const QString& layoutId) = 0;
    virtual void clearAssignment(const QString& screenId, int virtualDesktop = 0,
                                 const QString& activity = QString()) = 0;
    virtual bool hasExplicitAssignment(const QString& screenId, int virtualDesktop = 0,
                                       const QString& activity = QString()) const = 0;
    virtual void setAllScreenAssignments(const QHash<QString, QString>& assignments) = 0; // Batch set - saves once
    virtual void
    setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments) = 0; // Batch per-desktop
    virtual void
    setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments) = 0; // Batch per-activity

    // Quick layout switch
    virtual Layout* layoutForShortcut(int number) const = 0;
    virtual void applyQuickLayout(int number, const QString& screenId) = 0;
    virtual void setQuickLayoutSlot(int number, const QString& layoutId) = 0;
    virtual void setAllQuickLayoutSlots(const QHash<int, QString>& slots) = 0; // Batch set - saves once
    virtual QHash<int, QString> quickLayoutSlots() const = 0;

    // Built-in layouts
    virtual void createBuiltInLayouts() = 0;
    virtual QVector<Layout*> builtInLayouts() const = 0;

    // Persistence
    virtual void loadLayouts() = 0;
    virtual void saveLayouts() = 0;
    virtual void saveLayout(Layout* layout) = 0;
    virtual void loadAssignments() = 0;
    virtual void saveAssignments() = 0;
    virtual void importLayout(const QString& filePath) = 0;
    virtual void exportLayout(Layout* layout, const QString& filePath) = 0;
};

} // namespace PhosphorZones
