// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// IZoneLayoutRegistry - enumeration + mutation of the catalog of
// manual zone layouts.
//
// Split out of ILayoutManager so callers that need the layout set
// (editor save path, layout-import flow, settings create-layout
// button, read-only preview renderers) can depend on one contract
// instead of the full manager. "Active layout" selection lives here
// because it mutates the manager's active-layout slot.
//
// Inherits PhosphorLayout::ILayoutSourceRegistry so concrete registries
// (LayoutManager) carry the unified `contentsChanged` signal that
// ZonesLayoutSource subscribes to - matching the pattern every other
// provider library (phosphor-tiles, future phosphor-scrolling, …)
// follows. Inheriting QObject via the unified base rather than
// directly keeps ILayoutManager's non-virtual multi-inheritance safe:
// every path through ILayoutManager reaches QObject exactly once, so
// LayoutManager has a single QObject subobject.

#include <phosphorzones_export.h>

#include <PhosphorLayoutApi/ILayoutSourceRegistry.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>

#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>

namespace PhosphorZones {

/**
 * @brief Enumeration + mutation surface for the in-memory zone-layout
 * catalog.
 *
 * Fixture tests can stub this contract without implementing
 * persistence / assignments / quick-slots.
 */
class PHOSPHORZONES_EXPORT IZoneLayoutRegistry : public PhosphorLayout::ILayoutSourceRegistry
{
    Q_OBJECT

    // Live in the interface so QML-bound consumers can target the contract
    // without depending on the concrete @ref LayoutRegistry. moc's NOTIFY
    // check resolves the signal in the same class scope it sees the READ
    // method, so the property + the signal must travel together.
    Q_PROPERTY(Layout* activeLayout READ activeLayout NOTIFY activeLayoutChanged)

public:
    explicit IZoneLayoutRegistry(QObject* parent = nullptr);
    ~IZoneLayoutRegistry() override;

    /// Enumerate every known layout. Borrowed pointers - owned by the
    /// concrete registry (typically @c LayoutManager). Order is the
    /// registry's natural iteration order.
    virtual QVector<Layout*> layouts() const = 0;

    virtual int layoutCount() const = 0;
    virtual Layout* layout(int index) const = 0;
    virtual Layout* layoutByName(const QString& name) const = 0;

    /// Resolve a layout by its stable UUID. Returns nullptr when no
    /// layout with that id is known to the registry.
    virtual Layout* layoutById(const QUuid& id) const = 0;

    /// @param layout Ownership transferred - the registry adopts @p layout
    ///               and is responsible for its lifetime from this call on.
    virtual void addLayout(Layout* layout) = 0;
    /// @param layout Borrowed - caller hands the pointer in; the
    ///               registry un-registers it and schedules deletion
    ///               via @c deleteLater (matching how the registry
    ///               adopted it in @ref addLayout). Callers must drop
    ///               any other references before this call returns.
    virtual void removeLayout(Layout* layout) = 0;
    virtual void removeLayoutById(const QUuid& id) = 0;
    /// @param source Borrowed - caller retains ownership.
    /// @return       Newly allocated copy; ownership transferred to the
    ///               registry (mirrors @c addLayout semantics). Returns
    ///               nullptr if @p source is unknown.
    virtual Layout* duplicateLayout(Layout* source) = 0;

    // Active layout (internal - used for resnap / geometry / overlay
    // machinery). Borrowed pointer owned by the registry.
    virtual Layout* activeLayout() const = 0;
    virtual void setActiveLayout(Layout* layout) = 0;
    virtual void setActiveLayoutById(const QUuid& id) = 0;

    // ─── Per-screen layout resolution (cascade-aware) ─────────────────────
    //
    // These queries resolve a layout for a (screen, desktop, activity)
    // context by walking the assignment cascade and falling back to the
    // global default. Overlay/geometry/animation consumers depend on this
    // shape, so it lives on the interface - callers can target the
    // contract without depending on the concrete @c LayoutRegistry.

    /// Cascade-resolve the manual layout for @p screenId. Returns
    /// @c defaultLayout() when no explicit assignment matches.
    virtual Layout* layoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                    const QString& activity = QString()) const = 0;

    /// Convenience: resolve a layout using the registry's current
    /// (desktop, activity) context.
    virtual Layout* resolveLayoutForScreen(const QString& screenId) const = 0;

    /// Raw assignment id (manual-layout UUID or @c "autotile:<algorithmId>")
    /// for @p screenId, with cascade + level-1 provider fallback.
    virtual QString assignmentIdForScreen(const QString& screenId, int virtualDesktop = 0,
                                          const QString& activity = QString()) const = 0;

    /// Effective global default layout (snap-only fallback).
    virtual Layout* defaultLayout() const = 0;

    // ─── Session context (current desktop / activity) ─────────────────────
    //
    // The registry holds session-context state because per-context
    // assignment resolution needs it. Consumers that drive context-aware
    // queries (overlay re-layout on desktop switch, autotile re-run on
    // activity change) read it through the interface.

    virtual int currentVirtualDesktop() const = 0;
    /// This screen's current virtual desktop (Plasma 6.7 per-output virtual
    /// desktops, #648). Default ignores the screen and returns the global
    /// currentVirtualDesktop(), so non-per-output implementers are unaffected.
    virtual int currentVirtualDesktopForScreen(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return currentVirtualDesktop();
    }
    virtual QString currentActivity() const = 0;

    /// Per-algorithm autotile settings (gaps, shader, hiddenFromSelector, …)
    /// stored in the unified layout-settings.json sidecar, keyed by raw
    /// algorithm id. Default returns empty so non-persisting implementers and
    /// the unified-list builder degrade to "no overrides". Concrete registries
    /// (LayoutRegistry) read the sidecar.
    virtual QJsonObject loadAutotileOverrides(const QString& algorithmId) const
    {
        Q_UNUSED(algorithmId)
        return {};
    }

    /// Resolve the per-context gap override (zone padding + outer gaps) that
    /// window rules pin for the (@p screenId, @p virtualDesktop, @p activity)
    /// context — the same resolution the daemon's geometry resolver uses on the
    /// snap-commit path. Context-aware geometry consumers (drag preview, empty-
    /// zone overlay, zone-detection query) call this through the interface so
    /// their geometry matches the committed result. The default returns an empty
    /// override (no rule gaps); a registry that does not model context rules —
    /// e.g. a fixture stub — keeps the legacy per-screen/layout/global cascade.
    virtual ContextGapOverride resolveContextGaps(const QString& screenId, int virtualDesktop,
                                                  const QString& activity) const
    {
        Q_UNUSED(screenId);
        Q_UNUSED(virtualDesktop);
        Q_UNUSED(activity);
        return {};
    }

    /// Resolve whether window rules lock the active layout for the
    /// (@p screenId, @p virtualDesktop, @p activity) context — the rule-driven
    /// counterpart to the manual ToggleLayoutLock shortcut. A context rule
    /// carrying an `ActionType::LockContext` action whose `value` is true locks
    /// the context; the daemon ORs this into its context-lock check (across
    /// both engine modes) so a locked context refuses layout switches. Mode-
    /// agnostic and never persisted. The default returns false (no rule lock); a
    /// registry that does not model context rules — e.g. a fixture stub — keeps
    /// only the persisted manual-lock behaviour.
    virtual bool resolveContextLocked(const QString& screenId, int virtualDesktop, const QString& activity) const
    {
        Q_UNUSED(screenId);
        Q_UNUSED(virtualDesktop);
        Q_UNUSED(activity);
        return false;
    }

    /// Resolve the per-context overlay-property override for the
    /// (@p screenId, @p virtualDesktop, @p activity) context — a per-slot read
    /// across all matching context rules (mirrors @ref resolveContextGaps), so
    /// independent shader / style rules compose. The overlay service
    /// applies a populated field over the active layout's own value. The default
    /// returns an empty override (no rule overlay overrides); a registry that
    /// does not model context rules — e.g. a fixture stub — keeps the layout's
    /// own overlay properties.
    virtual ContextOverlayOverride resolveContextOverlay(const QString& screenId, int virtualDesktop,
                                                         const QString& activity) const
    {
        Q_UNUSED(screenId);
        Q_UNUSED(virtualDesktop);
        Q_UNUSED(activity);
        return {};
    }

Q_SIGNALS:
    // Catalog mutation. @c addLayout / @c duplicateLayout fire `layoutAdded`;
    // @c removeLayout / @c removeLayoutById fire `layoutRemoved`.
    void layoutAdded(Layout* layout);
    void layoutRemoved(Layout* layout);

    // Active-layout selection. Fires from @c setActiveLayout /
    // @c setActiveLayoutById only when the active-layout pointer actually
    // changes (concrete implementer guards with an equality check -
    // matches the project rule "only emit signals when value actually
    // changes").
    void activeLayoutChanged(Layout* layout);

    // Assignment churn. Fires when a (screenId, virtualDesktop)
    // assignment changes; activity context is intentionally omitted
    // from the signal - consumers that care about activity-keyed
    // assignments re-query via @c layoutForScreen with their current
    // activity. Concrete impl emits only on actual change (matches the
    // project "emit only when value changes" rule).
    void layoutAssigned(const QString& screenId, int virtualDesktop, Layout* layout);
};

} // namespace PhosphorZones
