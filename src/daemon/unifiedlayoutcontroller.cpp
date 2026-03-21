// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unifiedlayoutcontroller.h"
#include "../autotile/AutotileEngine.h"
#include "../config/settings.h"
#include "../core/constants.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include "../core/utils.h"

namespace PlasmaZones {

UnifiedLayoutController::UnifiedLayoutController(LayoutManager* layoutManager, Settings* settings,
                                                 AutotileEngine* autotileEngine, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
    , m_autotileEngine(autotileEngine)
{
    if (m_layoutManager) {
        connect(m_layoutManager, &LayoutManager::layoutsChanged, this, [this]() {
            m_cacheValid = false;
        });

        connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, [this](Layout* layout) {
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

    syncFromExternalState();
}

UnifiedLayoutController::~UnifiedLayoutController() = default;

QVector<UnifiedLayoutEntry> UnifiedLayoutController::layouts() const
{
    if (!m_cacheValid) {
        // Use filtered overload to respect visibility settings (hiddenFromSelector, allowed lists)
        // and mode-based filtering (manual-only vs autotile-only)
        m_cachedLayouts =
            LayoutUtils::buildUnifiedLayoutList(m_layoutManager, m_currentScreenName, m_currentVirtualDesktop,
                                                m_currentActivity, m_includeManualLayouts, m_includeAutotileLayouts);

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
    const UnifiedLayoutEntry* entry = LayoutUtils::findLayout(list, layoutId);
    if (!entry) {
        qCWarning(lcDaemon) << "applyLayoutById: layout not found:" << layoutId;
        return false;
    }
    return applyEntry(*entry);
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
                     << "from=" << (currentIndex < list.size() ? list[currentIndex].name : QStringLiteral("?"))
                     << "to=" << (nextIndex < list.size() ? list[nextIndex].name : QStringLiteral("?"));

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
            Layout* screenLayout =
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

bool UnifiedLayoutController::applyEntry(const UnifiedLayoutEntry& entry)
{
    // Handle autotile entries: assign autotile ID to the current screen.
    // The daemon's layoutAssigned handler calls updateAutotileScreens() which
    // derives per-screen autotile state from assignments automatically.
    if (entry.isAutotile) {
        if (m_autotileEngine && m_layoutManager) {
            QString algoId = LayoutId::extractAlgorithmId(entry.id);
            // Assign layout FIRST so that layoutAssigned → updateAutotileScreens()
            // updates per-screen overrides before setAlgorithm's deferred retile.
            // Without this ordering, setAlgorithm's retile uses stale per-screen
            // overrides (old algorithm), producing wrong zone geometries.
            if (!m_currentScreenName.isEmpty()) {
                // Write per-desktop assignment with empty activity so it applies
                // regardless of which activity is active.  Activity-specific
                // overrides are a separate KCM-only feature.
                if (!m_currentActivity.isEmpty()) {
                    m_layoutManager->clearAssignment(m_currentScreenName, m_currentVirtualDesktop, m_currentActivity);
                }
                m_layoutManager->assignLayoutById(m_currentScreenName, m_currentVirtualDesktop, QString(), entry.id);
            }
            m_autotileEngine->setAlgorithm(algoId);
            setCurrentLayoutId(entry.id);
            qCInfo(lcDaemon) << "Applied autotile algorithm=" << entry.name;
            Q_EMIT autotileApplied(entry.name, 0);
            return true;
        }
        qCWarning(lcDaemon) << "applyEntry: autotile engine not available for" << entry.id;
        return false;
    }

    // Manual layout: assign the UUID to the current screen.
    // If the previous assignment was autotile, it gets replaced and
    // updateAutotileScreens() will remove the screen from autotile set.
    auto uuidOpt = Utils::parseUuid(entry.id);
    if (uuidOpt && m_layoutManager) {
        Layout* layout = m_layoutManager->layoutById(*uuidOpt);
        if (layout) {
            if (!m_currentScreenName.isEmpty()) {
                // Write per-desktop assignment with empty activity so it applies
                // regardless of which activity is active.  Activity-specific
                // overrides are a separate KCM-only feature.
                // Clear any stale activity-keyed entry that would shadow this one
                // in the cascade (exact match takes priority over desktop-only).
                if (!m_currentActivity.isEmpty()) {
                    m_layoutManager->clearAssignment(m_currentScreenName, m_currentVirtualDesktop, m_currentActivity);
                }
                // assignLayout FIRST so the per-desktop assignment is stored
                // before setActiveLayout fires activeLayoutChanged →
                // onLayoutChanged(). That handler calls resolveLayoutForScreen()
                // which reads per-desktop assignments — it must see the new
                // assignment to correctly populate the resnap buffer.
                m_layoutManager->assignLayout(m_currentScreenName, m_currentVirtualDesktop, QString(), layout);
            }
            // Update global active layout pointer so overlay/zone detector
            // queries see the new layout, but suppress activeLayoutChanged
            // to prevent resnap buffer population and zone recalculation on
            // ALL screens. The per-screen assignment (assignLayout above)
            // already fired layoutAssigned which handles per-screen zone
            // recalculation. Without blocking, setActiveLayout fires
            // activeLayoutChanged → onLayoutChanged → resnap/recalc on
            // every screen, not just the target.
            {
                const QSignalBlocker blocker(m_layoutManager);
                m_layoutManager->setActiveLayout(layout);
            }
            setCurrentLayoutId(entry.id);
            qCInfo(lcDaemon) << "Applied unified layout=" << entry.name << "screen=" << m_currentScreenName;
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
    return LayoutUtils::findLayoutIndex(list, m_currentLayoutId);
}

} // namespace PlasmaZones
