// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include "core/utils/unifiedlayoutlist.h"
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
 * auto *controller = new UnifiedLayoutController(layoutManager, settings, parent);
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
     */
    void setLayoutFilter(bool includeManual, bool includeAutotile);

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

    QPointer<PhosphorZones::LayoutRegistry> m_layoutManager;
    QPointer<Settings> m_settings;
    QPointer<PhosphorScreens::ScreenManager> m_screenManager;
    PhosphorTiles::ITileAlgorithmRegistry* m_algorithmRegistry = nullptr; ///< Borrowed; outlives controller
    QPointer<PhosphorEngine::PlacementEngineBase> m_autotileEngine; ///< Auto-nulls if engine destroyed first
    PhosphorLayout::ILayoutSource* m_autotileLayoutSource = nullptr; ///< Borrowed; outlives controller (optional)
    QMetaObject::Connection m_autotileSourceConnection; ///< contentsChanged subscription on m_autotileLayoutSource

    QString m_currentLayoutId;
    QString m_currentScreenName;
    // Change-guard only: layouts() resolves the desktop per-screen from the
    // layout manager, so this value never reaches the list builder. It exists
    // so setCurrentVirtualDesktop can invalidate the cache on a real change
    // rather than on every desktop-changed signal.
    int m_currentVirtualDesktop = 1;
    QString m_currentActivity;
    bool m_includeManualLayouts = true;
    bool m_includeAutotileLayouts = false;
    mutable QVector<PhosphorLayout::LayoutPreview> m_cachedLayouts;
    mutable bool m_cacheValid = false;
};

} // namespace PlasmaZones
