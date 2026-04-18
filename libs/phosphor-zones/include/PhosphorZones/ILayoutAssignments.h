// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ILayoutAssignments — per-context (screen/desktop/activity) layout
// assignments + the "default layout" fallback used when no explicit
// assignment matches.
//
// Split out of ILayoutManager so assignment-resolving callers (overlay
// service, zone selector, D-Bus KCM adaptors) can depend on this
// narrower contract. Does NOT include persistence — callers that
// mutate assignments are expected to invoke persistence separately (or
// rely on the concrete manager's autosave behaviour).

#include <phosphorzones_export.h>

#include <QHash>
#include <QPair>
#include <QString>

namespace PhosphorZones {

class Layout;

/**
 * @brief Per-context assignment resolution + configured default layout.
 *
 * Assignment keys are triples (screenId, virtualDesktop, activity).
 * @c virtualDesktop == 0 means "any desktop"; @c activity.isEmpty()
 * means "any activity". Narrower keys win over wider ones per the
 * concrete manager's resolution order (see LayoutManager::layoutForScreen).
 */
class PHOSPHORZONES_EXPORT ILayoutAssignments
{
public:
    ILayoutAssignments() = default;
    virtual ~ILayoutAssignments();

    /// Settings-based default layout — returned when no explicit
    /// assignment matches. Borrowed pointer owned by the layout
    /// registry.
    virtual Layout* defaultLayout() const = 0;

    // Context queries (delegated to VirtualDesktopManager /
    // ActivityManager by the concrete implementation).
    virtual int currentVirtualDesktop() const = 0;
    virtual QString currentActivity() const = 0;

    /**
     * @brief Convenience: resolve layout for screen using current desktop/activity context.
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

    // Explicit-context lookups (screenId: stable EDID-based identifier or
    // connector name fallback). Borrowed pointers owned by the layout
    // registry.
    virtual Layout* layoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                    const QString& activity = QString()) const = 0;
    /// @param layout Borrowed — typically a layout already registered via
    ///               the layout registry. Stored in the assignment table;
    ///               no ownership transfer.
    virtual void assignLayout(const QString& screenId, int virtualDesktop, const QString& activity, Layout* layout) = 0;
    virtual void assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                  const QString& layoutId) = 0;
    virtual void clearAssignment(const QString& screenId, int virtualDesktop = 0,
                                 const QString& activity = QString()) = 0;
    virtual bool hasExplicitAssignment(const QString& screenId, int virtualDesktop = 0,
                                       const QString& activity = QString()) const = 0;

    /**
     * @brief Raw assignment id for a (screen, desktop, activity) context.
     *
     * Returns the stored id string — either a manual-layout UUID (braced
     * form) or an `"autotile:<algorithmId>"` preview id — without
     * resolving to a Layout*. Lets callers branch on assignment KIND
     * (manual vs autotile) before deciding how to render. Returns an
     * empty string when no explicit assignment matches the context.
     */
    virtual QString assignmentIdForScreen(const QString& screenId, int virtualDesktop = 0,
                                          const QString& activity = QString()) const = 0;

    // Batch setters — saves once at the end, cheaper than per-entry calls.
    virtual void setAllScreenAssignments(const QHash<QString, QString>& assignments) = 0;
    virtual void setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments) = 0;
    virtual void setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments) = 0;

protected:
    ILayoutAssignments(const ILayoutAssignments&) = default;
    ILayoutAssignments& operator=(const ILayoutAssignments&) = default;
};

} // namespace PhosphorZones
