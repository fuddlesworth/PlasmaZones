// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>

#include "core/utils/unifiedlayoutlist.h"
// Complete type needed for the nested IPlacementEngine::LayoutSupport enum
// consumed by setCurrentLayoutSupport below.
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorLayoutApi/LayoutPreview.h>
// Layout must be COMPLETE here, not forward-declared: the layoutApplied signal
// below carries a PhosphorZones::Layout*, and moc's metatype registration asks
// whether that type is complete. Answering "no" and then completing the type
// later in the same translation unit is what -Wsfinae-incomplete reports.
#include <PhosphorZones/Layout.h>
#include <QObject>
#include <QPointer>
#include <QString>

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorLayout {
class ILayoutSource;
}

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
}

namespace PhosphorZones {
class LayoutRegistry;
class ScrollingTemplateStore;
}

namespace PhosphorEngine {
class PlacementEngineBase;
}

namespace PlasmaZones {

class Settings;

/**
 * @brief Controller for unified layout management (manual layouts)
 *
 * Handles:
 * - Quick layout switching (Meta+1-9)
 * - PhosphorZones::Layout cycling (Meta+[/])
 * - ID-based layout tracking
 *
 * Usage:
 * @code
 * auto *controller = new UnifiedLayoutController(layoutManager, settings, screenManager,
 *                                                algorithmRegistry, autotileEngine, parent);
 * controller->applyLayoutById(layoutId);
 * controller->cycleNext();
 * connect(controller, &UnifiedLayoutController::layoutApplied, this, &Daemon::showLayoutOsd);
 * @endcode
 *
 * @note Thread Safety: All methods should be called from the main thread.
 */
class UnifiedLayoutController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLayoutId READ currentLayoutId NOTIFY currentLayoutIdChanged)

public:
    /**
     * @brief Construct a UnifiedLayoutController.
     *
     * @param algorithmRegistry Injected tile-algorithm registry. Borrowed —
     *        composition root owns lifetime, must outlive the controller.
     *        Passed explicitly rather than pulled via
     *        @c autotileEngine->algorithmRegistry() so the DI contract is
     *        visible at the constructor signature and the controller
     *        keeps working in unit tests that stub the engine.
     */
    explicit UnifiedLayoutController(PhosphorZones::LayoutRegistry* layoutManager, Settings* settings,
                                     PhosphorScreens::ScreenManager* screenManager,
                                     PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                                     PhosphorEngine::PlacementEngineBase* autotileEngine = nullptr,
                                     QObject* parent = nullptr);
    ~UnifiedLayoutController() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // PhosphorZones::Layout access
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the current layout ID (layout UUID).
     */
    QString currentLayoutId() const
    {
        return m_currentLayoutId;
    }

    /**
     * @brief Get the full unified layout list
     */
    QVector<PhosphorLayout::LayoutPreview> layouts() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // PhosphorZones::Layout application
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Apply layout by ID
     *
     * @param layoutId PhosphorZones::Layout UUID
     * @return true if layout was applied successfully
     */
    Q_INVOKABLE bool applyLayoutById(const QString& layoutId);

    /**
     * @brief Apply layout by index (0-based)
     *
     * @param index Index in unified layout list
     * @return true if layout was applied successfully
     */
    Q_INVOKABLE bool applyLayoutByIndex(int index);

    // ═══════════════════════════════════════════════════════════════════════════
    // PhosphorZones::Layout cycling
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Cycle to the next layout (Meta+])
     */
    Q_INVOKABLE void cycleNext();

    /**
     * @brief Cycle to the previous layout (Meta+[)
     */
    Q_INVOKABLE void cyclePrevious();

    /**
     * @brief Cycle layouts in specified direction
     *
     * @param forward true for next, false for previous
     */
    Q_INVOKABLE void cycle(bool forward);

    // ═══════════════════════════════════════════════════════════════════════════
    // State synchronization
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Synchronize current layout ID from external state
     *
     * Call this when layout changes from other sources (zone selector, D-Bus).
     * Routes through setCurrentLayoutId(), so it emits currentLayoutIdChanged()
     * when the value actually moves. It has to: a property that declares NOTIFY
     * but mutates silently on some of its paths leaves observers latched on a
     * stale value, which is worse than having no NOTIFY at all.
     *
     * @param overrideId When set, use this as the current layout ID instead of
     *                   querying the global active layout. Used for per-desktop
     *                   sync where the assignment may be an autotile ID.
     *
     *                   It is std::optional rather than a possibly-empty QString
     *                   because both callers pass an assignment id that is
     *                   legitimately empty when the (screen, desktop, activity)
     *                   has no assignment. Treating empty as "no override" made
     *                   that case fall back to the GLOBAL active layout, which is
     *                   the very fallback the per-desktop path exists to avoid.
     *                   A set-but-empty override now clears the id, as it should.
     */
    void syncFromExternalState(std::optional<QString> overrideId = std::nullopt);

    /**
     * @brief The LIVE engine capability of the current screen, pushed by the
     * daemon alongside setCurrentScreenName at every layout-selection entry
     * point (quick slot, picker open, cycle). applyEntry requires BOTH this
     * to be Templates AND the cascade mode to be Scrolling before taking the
     * template branch: the router downgrades a disabled or switched-off
     * scrolling assignment to live snapping, and routing on the cascade
     * alone would write a dead template while the visible snap screen moved
     * nothing (the resolver-first-then-cascade double check
     * resolvePerScreenLayoutInclude already uses). Defaults to Placement,
     * the fail-safe: an un-pushed value can only under-route to the classic
     * placement assignment, never mis-route a pick into template state.
     */
    void setCurrentLayoutSupport(PhosphorEngine::IPlacementEngine::LayoutSupport support)
    {
        m_currentLayoutSupport = support;
    }

    /**
     * @brief The id the picker/cycling machinery should treat as @p screenId's
     * current selection for a stored @p assignmentId.
     *
     * Identity for every id except the bare "scrolling:" sentinel, which
     * substitutes the context's assigned TEMPLATE layout UUID when one exists
     * — the sentinel matches no picker card, and without the substitution a
     * Templates screen's cycle always restarted from the first entry and its
     * picker showed no active card. A template-less scrolling context keeps
     * the sentinel (correctly matches nothing).
     */
    QString displayIdForAssignment(const QString& screenId, const QString& assignmentId) const;

    /**
     * @brief Get current screen name
     */
    QString currentScreenName() const
    {
        return m_currentScreenName;
    }

    /**
     * @brief Set current screen name for per-screen visibility filtering
     */
    void setCurrentScreenName(const QString& screenId);

    /**
     * @brief Set current virtual desktop for visibility filtering
     */
    void setCurrentVirtualDesktop(int desktop);

    /**
     * @brief Set current activity for visibility filtering
     */
    void setCurrentActivity(const QString& activity);

    /**
     * @brief Set which layout types to include in cycling/shortcuts
     *
     * In manual mode: only manual layouts. In autotile mode: only dynamic layouts.
     * The autotile feature gate controls whether dynamic layouts are ever visible.
     *
     * @p includeScrollingTemplates is the third, exclusive arm: a Templates
     * screen (scrolling) browses native template cards, so the caller passes
     * it true together with both other flags false. It is not a union member
     * with the other two — the daemon-side filter (Daemon::
     * updateLayoutFilterForScreen) forces manual and autotile off whenever
     * this is set, so cycling and the quick slots see templates alone.
     */
    void setLayoutFilter(bool includeManual, bool includeAutotile, bool includeScrollingTemplates);

    /**
     * @brief Inject the daemon's bundle-owned autotile layout source.
     *
     * Optional — when set, @ref layouts reuses its preview cache across
     * calls instead of constructing a transient source per call. Borrowed —
     * caller owns it and must keep it alive for the controller's lifetime.
     *
     * @note Expected to be called at most once per controller, right after
     * construction. The controller subscribes to the source's own
     * @c contentsChanged here so cache invalidation routes through the
     * single notifier the source already bridges from the registry.
     * When the source pointer is replaced (currently unused, but a
     * future multi-bundle composition root might), the previous
     * subscription is disconnected first.
     */
    void setAutotileLayoutSource(PhosphorLayout::ILayoutSource* source);

    /**
     * @brief Inject the scroll engine's strip-axis answer for a screen.
     *
     * The picker's template cards depict the strip's bands, and a card drawn
     * the other way shows a shape that screen will never display — so the
     * per-screen build has to know the axis, and this controller deliberately
     * holds no scroll-engine handle to derive it itself. Injected from the
     * composition root beside OverlayService's identical provider, and
     * cleared alongside it in stop() (the grep-discoverable
     * clear-before-teardown contract; the lambda also null-checks, but the
     * clear is the documented mechanism).
     *
     * layouts() consults the provider per rebuild and compares its answer to
     * the cached one, so a rotation or an axis rule flipping the strip
     * invalidates the card list without a dedicated signal.
     */
    void setStripAxisProvider(std::function<bool(const QString&)> provider);

Q_SIGNALS:
    /**
     * @brief Emitted when a manual layout is applied (for OSD)
     */
    void layoutApplied(PhosphorZones::Layout* layout);

    /**
     * @brief Emitted when an autotile algorithm is applied
     * @param algorithmName Display name of the algorithm
     * @param windowCount Number of currently tiled windows (0 if unknown)
     */
    void autotileApplied(const QString& algorithmName, int windowCount);

    /// Emitted when a native scrolling template was applied through the
    /// picker (for the template OSD; parity with layoutApplied /
    /// autotileApplied above).
    void scrollingTemplateApplied(const QString& templateId, const QString& screenId);

    /// Emitted when the picker's generic None row was applied to a
    /// snapping/autotile context (the explicit no-layout opt-out). No OSD
    /// card follows — the None-pick silent posture — but the daemon still
    /// needs the press to dismiss snap assist and refresh the cheatsheet,
    /// which the three apply signals above trigger for their families.
    void noLayoutApplied(const QString& screenId);

    /**
     * @brief Emitted when the current layout ID changes.
     *
     * Backs the currentLayoutId Q_PROPERTY, which would otherwise be a
     * read-only view over a value that demonstrably mutates. Declared for
     * property completeness: the controller is a C++-only object today, held by
     * the daemon and never registered with QML or connected to, so this has no
     * consumer yet. Every path that writes m_currentLayoutId goes through
     * setCurrentLayoutId() so that stays true when one appears.
     */
    void currentLayoutIdChanged();

private:
    /**
     * @brief Apply a unified layout preview
     */
    bool applyEntry(const PhosphorLayout::LayoutPreview& preview);

    /**
     * @brief Update the current layout ID, emitting currentLayoutIdChanged
     *        when the value actually changes.
     */
    void setCurrentLayoutId(const QString& layoutId);

    /**
     * @brief Find current index in layout list
     */
    int findCurrentIndex() const;

    /**
     * @brief Subscribe cache invalidation to the registry's template store
     *
     * Called from layouts() because the store is late-bound: it is injected
     * into the registry after this controller exists, so a constructor-time
     * connect would find nothing. Re-subscribes if the store is swapped, and
     * does nothing while there is none. Duplicate connects are prevented by
     * the stored bound-store pointer and connection handle, not by
     * Qt::UniqueConnection, which asserts on a lambda slot.
     */
    void ensureTemplateStoreSubscription() const;

    QPointer<PhosphorZones::LayoutRegistry> m_layoutManager;
    QPointer<Settings> m_settings;
    QPointer<PhosphorScreens::ScreenManager> m_screenManager;
    PhosphorTiles::ITileAlgorithmRegistry* m_algorithmRegistry = nullptr; ///< Borrowed; outlives controller
    QPointer<PhosphorEngine::PlacementEngineBase> m_autotileEngine; ///< Auto-nulls if engine destroyed first
    PhosphorLayout::ILayoutSource* m_autotileLayoutSource = nullptr; ///< Borrowed; outlives controller (optional)
    QMetaObject::Connection m_autotileSourceConnection; ///< contentsChanged subscription on m_autotileLayoutSource

    QString m_currentLayoutId;
    QString m_currentScreenName;
    /// LIVE engine capability of the current screen, daemon-pushed at the
    /// layout-selection entry points — see setCurrentLayoutSupport.
    PhosphorEngine::IPlacementEngine::LayoutSupport m_currentLayoutSupport =
        PhosphorEngine::IPlacementEngine::LayoutSupport::Placement;
    // Change-guard only: layouts() resolves the desktop per-screen from the
    // layout manager, so this value never reaches the list builder. It exists
    // so setCurrentVirtualDesktop can invalidate the cache on a real change
    // rather than on every desktop-changed signal.
    //
    // It tracks the GLOBAL desktop while the cache is keyed on the CURRENT
    // SCREEN's desktop, so the two are only incidentally correlated. The setter
    // therefore invalidates on either changing, not on this member alone.
    int m_currentVirtualDesktop = 1;

    QString m_currentActivity;
    bool m_includeManualLayouts = true;
    bool m_includeScrollingTemplates = false;
    bool m_includeAutotileLayouts = false;
    mutable QVector<PhosphorLayout::LayoutPreview> m_cachedLayouts;
    mutable bool m_cacheValid = false;
    /// The per-screen desktop the cached list was built for, so the invalidation
    /// guard keys on the same thing layouts() does. Mutable for the same reason
    /// the cache itself is: layouts() is const and fills it lazily.
    mutable int m_cachedScreenDesktop = -1;
    /// See setStripAxisProvider. Empty when scrolling never wired one.
    std::function<bool(const QString&)> m_stripAxisProvider;
    /// The axis the cached template cards were drawn for: layouts() compares
    /// the provider's live answer against this and drops the cache on a
    /// mismatch, so the first picker open after a rotation (or an axis rule
    /// flip) rebuilds instead of serving wrong-axis cards.
    mutable bool m_cachedStripVertical = false;
    /// The template store the cache-invalidation subscription below is bound
    /// to. The store is injected into the registry after this controller is
    /// constructed, so the subscription is made on the first resolve that
    /// finds one (see ensureTemplateStoreSubscription) and re-made if the
    /// injected store is ever swapped. Borrowed, never dereferenced here.
    /// QPointer so a destroyed store nulls the latch: a raw pointer could be
    /// compared equal to a freshly allocated store landing at the same
    /// address, and the subscription would never be re-made.
    mutable QPointer<PhosphorZones::ScrollingTemplateStore> m_subscribedTemplateStore;
    mutable QMetaObject::Connection m_templateStoreConnection;
};

} // namespace PlasmaZones
