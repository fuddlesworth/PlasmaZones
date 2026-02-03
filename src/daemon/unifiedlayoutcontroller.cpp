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
            Q_EMIT layoutsChanged();
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
            Q_EMIT layoutsChanged();
        });
    }

    syncFromExternalState();
}

UnifiedLayoutController::~UnifiedLayoutController() = default;

std::optional<UnifiedLayoutEntry> UnifiedLayoutController::currentLayout() const
{
    // Ensure cache is populated, then search directly in cached member
    // to avoid returning pointer to temporary
    layouts();  // Populates m_cachedLayouts if needed
    int index = LayoutUtils::findLayoutIndex(m_cachedLayouts, m_currentLayoutId);
    if (index >= 0) {
        return m_cachedLayouts[index];
    }
    return std::nullopt;
}

QVector<UnifiedLayoutEntry> UnifiedLayoutController::layouts() const
{
    if (!m_cacheValid) {
        m_cachedLayouts = LayoutUtils::buildUnifiedLayoutList(m_layoutManager);
        m_cacheValid = true;
    }
    return m_cachedLayouts;
}

QVariantList UnifiedLayoutController::layoutsAsVariantList() const
{
    return LayoutUtils::toVariantList(layouts());
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

bool UnifiedLayoutController::applyEntry(const UnifiedLayoutEntry& entry)
{
    auto uuidOpt = Utils::parseUuid(entry.id);
    if (uuidOpt && m_layoutManager) {
        Layout* layout = m_layoutManager->layoutById(*uuidOpt);
        if (layout) {
            m_layoutManager->setActiveLayout(layout);
            setCurrentLayoutId(entry.id);
            qCInfo(lcDaemon) << "Applied unified layout:" << entry.name;
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
    Q_EMIT currentLayoutIdChanged(layoutId);
}

int UnifiedLayoutController::findCurrentIndex() const
{
    const auto list = layouts();
    return LayoutUtils::findLayoutIndex(list, m_currentLayoutId);
}

} // namespace PlasmaZones
