// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unifiedlayoutcontroller.h"
#include "../autotile/AutotileEngine.h"
#include "../config/settings.h"
#include "../core/constants.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <PhosphorTiles/AlgorithmRegistry.h>

namespace PlasmaZones {

UnifiedLayoutController::UnifiedLayoutController(LayoutManager* layoutManager, Settings* settings,
                                                 Phosphor::Screens::ScreenManager* screenManager,
                                                 AutotileEngine* autotileEngine, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
    , m_screenManager(screenManager)
    , m_autotileEngine(autotileEngine)
{
    if (m_layoutManager) {
        connect(m_layoutManager, &LayoutManager::layoutsChanged, this, [this]() {
            m_cacheValid = false;
        });

        connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, [this](PhosphorZones::Layout* layout) {
            if (layout) {
                QString newId = layout->id().toString();
                if (m_currentLayoutId != newId) {
                    setCurrentLayoutId(newId);
                }
            }
        });
    }

    if (m_settings) {
        connect(m_settings, &Settings::settingsChanged, this, [this]() {
            m_cacheValid = false;
        });
    }

    // Autotile entries enter the unified list via PhosphorTiles::AlgorithmRegistry,
    // not LayoutManager — without this subscription a hot-loaded user script (or
    // any built-in registration churn) leaves m_cachedLayouts stale, and Meta+1‥9
    // continues to cycle the pre-load list until something else fires
    // layoutsChanged. Mirror SettingsController's existing pattern.
    if (auto* registry = PhosphorTiles::AlgorithmRegistry::instance()) {
        connect(registry, &PhosphorTiles::AlgorithmRegistry::algorithmRegistered, this, [this](const QString&) {
            m_cacheValid = false;
        });
        connect(registry, &PhosphorTiles::AlgorithmRegistry::algorithmUnregistered, this, [this](const QString&, bool) {
            m_cacheValid = false;
        });
    }

    syncFromExternalState();
}

UnifiedLayoutController::~UnifiedLayoutController() = default;

QVector<PhosphorLayout::LayoutPreview> UnifiedLayoutController::layouts() const
{
    if (!m_cacheValid) {
        // Use filtered overload to respect visibility settings (hiddenFromSelector, allowed lists)
        // and mode-based filtering (manual-only vs autotile-only)
        m_cachedLayouts = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
            m_layoutManager, m_currentScreenName, m_currentVirtualDesktop, m_currentActivity, m_includeManualLayouts,
            m_includeAutotileLayouts, Utils::screenAspectRatio(m_screenManager, m_currentScreenName),
            m_settings && m_settings->filterLayoutsByAspectRatio(),
            PhosphorZones::LayoutUtils::buildCustomOrder(m_settings, m_includeManualLayouts, m_includeAutotileLayouts));

        m_cacheValid = true;
    }
    return m_cachedLayouts;
}

bool UnifiedLayoutController::applyLayoutByNumber(int number)
{
    // Convert 1-based number to 0-based index
    return applyLayoutByIndex(number - 1);
}

bool UnifiedLayoutController::applyLayoutById(const QString& layoutId)
{
    const auto list = layouts();
    const PhosphorLayout::LayoutPreview* preview = PhosphorZones::LayoutUtils::findLayout(list, layoutId);
    if (!preview) {
        qCWarning(lcDaemon) << "applyLayoutById: layout not found:" << layoutId;
        return false;
    }
    return applyEntry(*preview);
}

bool UnifiedLayoutController::applyLayoutByIndex(int index)
{
    const auto list = layouts();
    if (list.isEmpty()) {
        qCWarning(lcDaemon) << "applyLayoutByIndex: no layouts available";
        return false;
    }
    if (index < 0 || index >= list.size()) {
        qCWarning(lcDaemon) << "applyLayoutByIndex: invalid index" << index << "(valid: 0 to" << (list.size() - 1)
                            << ")";
        return false;
    }
    return applyEntry(list[index]);
}

void UnifiedLayoutController::cycleNext()
{
    cycle(true);
}

void UnifiedLayoutController::cyclePrevious()
{
    cycle(false);
}

void UnifiedLayoutController::cycle(bool forward)
{
    const auto list = layouts();
    if (list.isEmpty()) {
        qCWarning(lcDaemon) << "cycle: layout list is empty (manual=" << m_includeManualLayouts
                            << "autotile=" << m_includeAutotileLayouts << ")";
        return;
    }

    int currentIndex = findCurrentIndex();
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    // Calculate next index with wraparound
    int nextIndex;
    if (forward) {
        nextIndex = (currentIndex + 1) % list.size();
    } else {
        nextIndex = (currentIndex - 1 + list.size()) % list.size();
    }

    qCInfo(lcDaemon) << "cycle: listSize=" << list.size() << "currentIdx=" << currentIndex << "nextIdx=" << nextIndex
                     << "from=" << (currentIndex < list.size() ? list[currentIndex].displayName : QStringLiteral("?"))
                     << "to=" << (nextIndex < list.size() ? list[nextIndex].displayName : QStringLiteral("?"));

    applyLayoutByIndex(nextIndex);
}

void UnifiedLayoutController::syncFromExternalState(const QString& overrideId)
{
    if (!overrideId.isEmpty()) {
        m_currentLayoutId = overrideId;
    } else if (m_layoutManager && m_layoutManager->activeLayout()) {
        m_currentLayoutId = m_layoutManager->activeLayout()->id().toString();
    } else {
        m_currentLayoutId.clear();
    }
}

void UnifiedLayoutController::setCurrentScreenName(const QString& screenId)
{
    if (m_currentScreenName != screenId) {
        m_currentScreenName = screenId;
        m_cacheValid = false;

        // Sync current layout ID to what's actually assigned to this screen
        // (not the global active layout, which may belong to a different screen)
        if (m_layoutManager && !screenId.isEmpty()) {
            PhosphorZones::Layout* screenLayout =
                m_layoutManager->layoutForScreen(screenId, m_currentVirtualDesktop, m_currentActivity);
            if (screenLayout) {
                m_currentLayoutId = screenLayout->id().toString();
            }
        }
    }
}

void UnifiedLayoutController::setCurrentVirtualDesktop(int desktop)
{
    if (m_currentVirtualDesktop != desktop) {
        m_currentVirtualDesktop = desktop;
        m_cacheValid = false;
    }
}

void UnifiedLayoutController::setCurrentActivity(const QString& activity)
{
    if (m_currentActivity != activity) {
        m_currentActivity = activity;
        m_cacheValid = false;
    }
}

void UnifiedLayoutController::setLayoutFilter(bool includeManual, bool includeAutotile)
{
    if (m_includeManualLayouts == includeManual && m_includeAutotileLayouts == includeAutotile) {
        return;
    }
    m_includeManualLayouts = includeManual;
    m_includeAutotileLayouts = includeAutotile;
    m_cacheValid = false;
}

bool UnifiedLayoutController::applyEntry(const PhosphorLayout::LayoutPreview& preview)
{
    // Handle autotile entries: assign autotile ID to the current screen.
    // The daemon's layoutAssigned handler calls updateAutotileScreens() which
    // derives per-screen autotile state from assignments automatically.
    if (preview.isAutotile()) {
        if (m_autotileEngine && m_layoutManager) {
            QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(preview.id);
            // Assign layout FIRST so that layoutAssigned → updateAutotileScreens()
            // updates per-screen overrides before setAlgorithm's deferred retile.
            // Without this ordering, setAlgorithm's retile uses stale per-screen
            // overrides (old algorithm), producing wrong zone geometries.
            if (!m_currentScreenName.isEmpty()) {
                // Write to the current context (screen, desktop, activity).
                // Most specific context wins — an activity-keyed entry takes
                // priority over a desktop-only entry in the cascade, and a
                // different activity falls through to the broader entry.
                m_layoutManager->assignLayoutById(m_currentScreenName, m_currentVirtualDesktop, m_currentActivity,
                                                  preview.id);
            }
            m_autotileEngine->setAlgorithm(algoId);
            setCurrentLayoutId(preview.id);
            qCInfo(lcDaemon) << "Applied autotile algorithm=" << preview.displayName;
            Q_EMIT autotileApplied(preview.displayName, 0);
            return true;
        }
        qCWarning(lcDaemon) << "applyEntry: autotile engine not available for" << preview.id;
        return false;
    }

    // Manual layout: assign the UUID to the current screen.
    // If the previous assignment was autotile, it gets replaced and
    // updateAutotileScreens() will remove the screen from autotile set.
    auto uuidOpt = Utils::parseUuid(preview.id);
    if (uuidOpt && m_layoutManager) {
        PhosphorZones::Layout* layout = m_layoutManager->layoutById(*uuidOpt);
        if (layout) {
            if (!m_currentScreenName.isEmpty()) {
                // Write to the current context (screen, desktop, activity).
                // Only the per-context assignment is stored — the global
                // activeLayout pointer is NOT updated so that other screens
                // and contexts keep their existing fallback layout.
                m_layoutManager->assignLayout(m_currentScreenName, m_currentVirtualDesktop, m_currentActivity, layout);
            }
            setCurrentLayoutId(preview.id);
            qCInfo(lcDaemon) << "Applied unified layout=" << preview.displayName << "screen=" << m_currentScreenName;
            Q_EMIT layoutApplied(layout);
            return true;
        }
    }
    return false;
}

void UnifiedLayoutController::setCurrentLayoutId(const QString& layoutId)
{
    if (m_currentLayoutId == layoutId) {
        return;
    }

    m_currentLayoutId = layoutId;
}

int UnifiedLayoutController::findCurrentIndex() const
{
    const auto list = layouts();
    return PhosphorZones::LayoutUtils::findLayoutIndex(list, m_currentLayoutId);
}

} // namespace PlasmaZones
