// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unifiedlayoutcontroller.h"
#include "../config/settings.h"
#include "../core/constants.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include "../core/utils.h"

namespace PlasmaZones {

UnifiedLayoutController::UnifiedLayoutController(LayoutManager* layoutManager, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
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
        m_cachedLayouts = LayoutUtils::buildUnifiedLayoutList(
            m_layoutManager, m_currentScreenName, m_currentVirtualDesktop, m_currentActivity);
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
        qCWarning(lcDaemon) << "applyLayoutByIndex: invalid index" << index
                            << "(valid: 0 to" << (list.size() - 1) << ")";
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

    applyLayoutByIndex(nextIndex);
}

void UnifiedLayoutController::syncFromExternalState()
{
    if (m_layoutManager && m_layoutManager->activeLayout()) {
        m_currentLayoutId = m_layoutManager->activeLayout()->id().toString();
    } else {
        m_currentLayoutId.clear();
    }
}

void UnifiedLayoutController::setCurrentScreenName(const QString& screenName)
{
    if (m_currentScreenName != screenName) {
        m_currentScreenName = screenName;
        m_cacheValid = false;

        // Sync current layout ID to what's actually assigned to this screen
        // (not the global active layout, which may belong to a different screen)
        if (m_layoutManager && !screenName.isEmpty()) {
            Layout* screenLayout = m_layoutManager->layoutForScreen(
                screenName, m_currentVirtualDesktop, m_currentActivity);
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

bool UnifiedLayoutController::applyEntry(const UnifiedLayoutEntry& entry)
{
    auto uuidOpt = Utils::parseUuid(entry.id);
    if (uuidOpt && m_layoutManager) {
        Layout* layout = m_layoutManager->layoutById(*uuidOpt);
        if (layout) {
            if (!m_currentScreenName.isEmpty()) {
                // Per-screen assignment + update global active layout.
                // setActiveLayout MUST also be called to update m_previousLayout
                // and fire activeLayoutChanged (needed by resnap buffer, stale
                // assignment cleanup, OSD, etc.). Per-screen assignments are still
                // respected by resolveLayoutForScreen() since they take priority.
                m_layoutManager->assignLayout(m_currentScreenName, m_currentVirtualDesktop,
                                              m_currentActivity, layout);
            }
            // Always update global active layout (fires activeLayoutChanged)
            m_layoutManager->setActiveLayout(layout);
            setCurrentLayoutId(entry.id);
            qCInfo(lcDaemon) << "Applied unified layout:" << entry.name
                             << "to screen:" << m_currentScreenName;
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
