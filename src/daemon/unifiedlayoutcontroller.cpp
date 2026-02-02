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

UnifiedLayoutController::UnifiedLayoutController(LayoutManager* layoutManager, AutotileEngine* autotileEngine,
                                                   Settings* settings, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_autotileEngine(autotileEngine)
    , m_settings(settings)
{
    // Invalidate cache when layouts change
    if (m_layoutManager) {
        connect(m_layoutManager, &LayoutManager::layoutsChanged, this, [this]() {
            m_cacheValid = false;
            Q_EMIT layoutsChanged();
        });

        // Sync when active layout changes externally
        connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, [this](Layout* layout) {
            // Check m_autotileEngine for null - it may not be set if autotile is disabled
            if (layout && (!m_autotileEngine || !m_autotileEngine->isEnabled())) {
                QString newId = layout->id().toString();
                if (m_currentLayoutId != newId) {
                    setCurrentLayoutId(newId);
                }
            }
        });
    }

    // Invalidate cache when autotile enabled setting changes
    if (m_settings) {
        connect(m_settings, &Settings::autotileEnabledChanged, this, [this]() {
            m_cacheValid = false;
            Q_EMIT layoutsChanged();
        });

        // Also connect to settingsChanged - this is emitted when Settings::load() is called
        // (e.g., when KCM saves settings), which doesn't emit individual property signals
        connect(m_settings, &Settings::settingsChanged, this, [this]() {
            m_cacheValid = false;
            Q_EMIT layoutsChanged();
        });
    }

    // Sync when autotile changes externally
    if (m_autotileEngine) {
        connect(m_autotileEngine, &AutotileEngine::enabledChanged, this, [this](bool enabled) {
            if (enabled) {
                QString newId = LayoutId::makeAutotileId(m_autotileEngine->algorithm());
                if (m_currentLayoutId != newId) {
                    setCurrentLayoutId(newId);
                }
            }
        });

        connect(m_autotileEngine, &AutotileEngine::algorithmChanged, this, [this](const QString& algorithmId) {
            if (m_autotileEngine->isEnabled()) {
                QString newId = LayoutId::makeAutotileId(algorithmId);
                if (m_currentLayoutId != newId) {
                    setCurrentLayoutId(newId);
                }
            }
        });
    }

    // Initialize current layout ID from state
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
        // Only include autotile layouts if autotiling is enabled in settings
        const bool includeAutotile = m_settings && m_settings->autotileEnabled();
        m_cachedLayouts = LayoutUtils::buildUnifiedLayoutList(m_layoutManager, includeAutotile);
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
    // Determine current state from LayoutManager and AutotileEngine
    if (m_autotileEngine && m_autotileEngine->isEnabled()) {
        m_currentLayoutId = LayoutId::makeAutotileId(m_autotileEngine->algorithm());
    } else if (m_layoutManager && m_layoutManager->activeLayout()) {
        m_currentLayoutId = m_layoutManager->activeLayout()->id().toString();
    } else {
        m_currentLayoutId.clear();
    }
}

bool UnifiedLayoutController::applyEntry(const UnifiedLayoutEntry& entry)
{
    if (entry.isAutotile) {
        QString algorithmId = entry.algorithmId();
        if (algorithmId.isEmpty()) {
            qCWarning(lcDaemon) << "applyEntry: invalid autotile entry:" << entry.id;
            return false;
        }

        if (m_autotileEngine) {
            m_autotileEngine->setAlgorithm(algorithmId);
            m_autotileEngine->setEnabled(true);
            setCurrentLayoutId(entry.id);
            qCInfo(lcDaemon) << "Applied unified layout (autotile):" << entry.name;
            Q_EMIT autotileApplied(algorithmId);
            return true;
        }
    } else {
        // Manual layout - disable autotile and apply layout
        if (m_autotileEngine) {
            m_autotileEngine->setEnabled(false);
        }

        auto uuidOpt = Utils::parseUuid(entry.id);
        if (uuidOpt && m_layoutManager) {
            Layout* layout = m_layoutManager->layoutById(*uuidOpt);
            if (layout) {
                m_layoutManager->setActiveLayout(layout);
                setCurrentLayoutId(entry.id);
                qCInfo(lcDaemon) << "Applied unified layout (manual):" << entry.name;
                Q_EMIT layoutApplied(layout);
                return true;
            }
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
