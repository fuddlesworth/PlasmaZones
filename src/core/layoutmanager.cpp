// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutmanager.h"
#include "constants.h"
#include "logging.h"
#include "utils.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <algorithm>

namespace PlasmaZones {

LayoutManager::LayoutManager(QObject* parent)
    : QObject(parent)
    , ILayoutManager()
{
    // Default layout directory
    m_layoutDirectory =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasmazones/layouts");
    ensureLayoutDirectory();
}

LayoutManager::~LayoutManager()
{
    qDeleteAll(m_layouts);
}

void LayoutManager::setSettings(ISettings* settings)
{
    m_settings = settings;
}

void LayoutManager::setLayoutDirectory(const QString& directory)
{
    if (m_layoutDirectory != directory) {
        m_layoutDirectory = directory;
        ensureLayoutDirectory();
        Q_EMIT layoutDirectoryChanged();
    }
}

Layout* LayoutManager::layout(int index) const
{
    if (index >= 0 && index < m_layouts.size()) {
        return m_layouts.at(index);
    }
    return nullptr;
}

// Template helper for lookup methods
template<typename Predicate>
static Layout* findLayout(const QVector<Layout*>& layouts, Predicate pred)
{
    auto it = std::find_if(layouts.begin(), layouts.end(), pred);
    return it != layouts.end() ? *it : nullptr;
}

// Helper: emit layoutAssigned for a single screenId/layoutId pair
void LayoutManager::emitLayoutAssigned(const QString& screenId, const QString& layoutId)
{
    Layout* layout = LayoutId::isAutotile(layoutId) ? nullptr : layoutById(QUuid::fromString(layoutId));
    Q_EMIT layoutAssigned(screenId, layout);
}

// Helper for validating layout assignment
// Returns true if layoutId should be skipped (null or non-existent)
bool LayoutManager::shouldSkipLayoutAssignment(const QString& layoutId, const QString& context) const
{
    if (layoutId.isEmpty()) {
        return true;
    }
    if (LayoutId::isAutotile(layoutId)) {
        return false; // Autotile IDs are valid without Layout* lookup
    }
    if (!layoutById(QUuid::fromString(layoutId))) {
        qCWarning(lcLayout) << "Skipping non-existent layout for" << context << ":" << layoutId;
        return true;
    }
    return false;
}

Layout* LayoutManager::defaultLayout() const
{
    if (m_settings && !m_settings->defaultLayoutId().isEmpty()) {
        if (Layout* layout = layoutById(QUuid(m_settings->defaultLayoutId()))) {
            return layout;
        }
    }
    return m_layouts.isEmpty() ? nullptr : m_layouts.first();
}

// Helper for layout cycling
// direction: -1 for previous, +1 for next
// Filters out hidden layouts and respects visibility allow-lists
Layout* LayoutManager::cycleLayoutImpl(const QString& screenId, int direction)
{
    if (m_layouts.isEmpty()) {
        return nullptr;
    }

    // Translate connector name to screen ID for allowedScreens matching
    QString resolvedScreenId;
    if (!screenId.isEmpty()) {
        resolvedScreenId = Utils::isConnectorName(screenId) ? Utils::screenIdForName(screenId) : screenId;
    }

    // Build filtered list of visible layouts for current context
    QVector<Layout*> visible;
    for (Layout* l : m_layouts) {
        if (!l || l->hiddenFromSelector()) {
            continue;
        }
        if (!resolvedScreenId.isEmpty() && !l->allowedScreens().isEmpty()) {
            if (!l->allowedScreens().contains(resolvedScreenId)) {
                continue;
            }
        }
        if (m_currentVirtualDesktop > 0 && !l->allowedDesktops().isEmpty()) {
            if (!l->allowedDesktops().contains(m_currentVirtualDesktop)) {
                continue;
            }
        }
        if (!m_currentActivity.isEmpty() && !l->allowedActivities().isEmpty()) {
            if (!l->allowedActivities().contains(m_currentActivity)) {
                continue;
            }
        }
        visible.append(l);
    }

    if (visible.isEmpty()) {
        return nullptr;
    }

    // Use per-screen layout as reference for cycling so each screen cycles independently
    Layout* currentLayout = nullptr;
    if (!resolvedScreenId.isEmpty()) {
        currentLayout = layoutForScreen(resolvedScreenId, m_currentVirtualDesktop, m_currentActivity);
    }
    if (!currentLayout) {
        currentLayout = defaultLayout();
    }
    if (!currentLayout) {
        currentLayout = visible.first();
    }

    int currentIndex = visible.indexOf(currentLayout);
    if (currentIndex < 0) {
        // Current layout is not in the visible list (e.g. hidden).
        // For forward cycling, start before the first so we land on visible[0].
        // For backward cycling, start after the last so we land on the last visible.
        currentIndex = (direction > 0) ? -1 : visible.size();
    }

    // Wrap around: +size ensures positive modulo for direction=-1
    int newIndex = (currentIndex + direction + visible.size()) % visible.size();
    Layout* newLayout = visible.at(newIndex);

    // Per-screen assignment + update global active layout.
    // setActiveLayout MUST be called to update m_previousLayout and fire
    // activeLayoutChanged (needed for resnap buffer population, stale assignment
    // cleanup, OSD, etc.). Per-screen assignments are still respected by
    // resolveLayoutForScreen() since they take priority over the global active.
    if (!resolvedScreenId.isEmpty()) {
        assignLayout(resolvedScreenId, m_currentVirtualDesktop, m_currentActivity, newLayout);
    }
    setActiveLayout(newLayout);
    return newLayout;
}

Layout* LayoutManager::layoutById(const QUuid& id) const
{
    return findLayout(m_layouts, [&id](const Layout* l) {
        return l->id() == id;
    });
}

Layout* LayoutManager::layoutByName(const QString& name) const
{
    return findLayout(m_layouts, [&name](const Layout* l) {
        return l->name() == name;
    });
}

void LayoutManager::addLayout(Layout* layout)
{
    if (layout && !m_layouts.contains(layout)) {
        layout->setParent(this);
        m_layouts.append(layout);

        // Connect to save on modification (per-layout copy-on-write)
        connect(layout, &Layout::layoutModified, this, [this, layout]() { saveLayout(layout); });

        // New layouts need immediate persistence
        layout->markDirty();
        saveLayout(layout);

        Q_EMIT layoutAdded(layout);
        Q_EMIT layoutsChanged();
    }
}

void LayoutManager::removeLayout(Layout* layout)
{
    if (!layout || layout->isSystemLayout() || !m_layouts.contains(layout)) {
        return;
    }

    // Store state BEFORE any operations that might invalidate the pointer
    const QUuid layoutId = layout->id();
    const bool wasActive = (m_activeLayout == layout);
    const QString filePath = layoutFilePath(layoutId);
    const QString systemPath = layout->systemSourcePath();

    // Remove from layouts list
    m_layouts.removeOne(layout);

    const QString layoutIdStr = layoutId.toString();

    // Delete layout file (using stored path)
    QFile::remove(filePath);

    // Clear stale pointer before deletion
    if (m_previousLayout == layout) {
        m_previousLayout = nullptr;
    }

    Q_EMIT layoutRemoved(layout);
    layout->deleteLater();

    // If this was a user override of a system layout, restore the system original.
    // Uses the stored system path — no filesystem scanning needed.
    // NOTE: restoreSystemLayout() emits layoutAdded but not layoutsChanged;
    // the caller (this method) emits layoutsChanged below.
    Layout* restored = restoreSystemLayout(layoutId, systemPath);

    if (restored) {
        // System layout restored — assignments and shortcuts stay valid (same UUID).
        if (wasActive) {
            setActiveLayout(restored);
        }
    } else {
        // Truly deleted — clean up assignments and shortcuts referencing this layout
        for (auto it = m_assignments.begin(); it != m_assignments.end();) {
            if (it.value() == layoutIdStr) {
                it = m_assignments.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = m_quickLayoutShortcuts.begin(); it != m_quickLayoutShortcuts.end();) {
            if (it.value() == layoutIdStr) {
                it = m_quickLayoutShortcuts.erase(it);
            } else {
                ++it;
            }
        }

        if (wasActive) {
            setActiveLayout(defaultLayout());
        }
    }

    Q_EMIT layoutsChanged();
    saveAssignments();
}

void LayoutManager::removeLayoutById(const QUuid& id)
{
    removeLayout(layoutById(id));
}

Layout* LayoutManager::duplicateLayout(Layout* source)
{
    if (!source) {
        return nullptr;
    }

    auto newLayout = new Layout(*source);
    newLayout->setName(source->name() + QStringLiteral(" (Copy)"));
    // Note: Copy constructor already leaves sourcePath empty, making it a user layout

    // Reset visibility restrictions so duplicated layout starts fresh
    newLayout->setHiddenFromSelector(false);
    newLayout->setAllowedScreens({});
    newLayout->setAllowedDesktops({});
    newLayout->setAllowedActivities({});

    addLayout(newLayout);

    return newLayout;
}

void LayoutManager::setActiveLayout(Layout* layout)
{
    if (m_activeLayout != layout && (layout == nullptr || m_layouts.contains(layout))) {
        // Capture current as previous before changing (on first run, use layout as both)
        m_previousLayout = m_activeLayout ? m_activeLayout : layout;
        m_activeLayout = layout;
        Q_EMIT activeLayoutChanged(m_activeLayout);
    }
}

void LayoutManager::setActiveLayoutById(const QUuid& id)
{
    setActiveLayout(layoutById(id));
}

Layout* LayoutManager::layoutForShortcut(int number) const
{
    if (m_quickLayoutShortcuts.contains(number)) {
        const QString& id = m_quickLayoutShortcuts[number];
        if (LayoutId::isAutotile(id)) return nullptr;
        return layoutById(QUuid::fromString(id));
    }
    return nullptr;
}

void LayoutManager::applyQuickLayout(int number, const QString& screenId)
{
    qCInfo(lcLayout) << "applyQuickLayout called: number=" << number << "screen=" << screenId;

    auto layout = layoutForShortcut(number);
    if (layout) {
        qCDebug(lcLayout) << "Found layout for shortcut" << number << ":" << layout->name();
        // Assign to current monitor + current virtual desktop + current activity (not global default)
        assignLayout(screenId, m_currentVirtualDesktop, m_currentActivity, layout);
        setActiveLayout(layout);
    } else {
        // No layout assigned to this quick slot - try to use layout at index (number-1) as fallback
        qCInfo(lcLayout) << "No layout assigned to quick slot" << number << "- attempting fallback to layout index"
                          << (number - 1);
        if (number >= 1 && number <= m_layouts.size()) {
            layout = m_layouts.at(number - 1);
            if (layout) {
                qCInfo(lcLayout) << "Using fallback layout:" << layout->name();
                assignLayout(screenId, m_currentVirtualDesktop, m_currentActivity, layout);
                setActiveLayout(layout);
            }
        } else {
            qCWarning(lcLayout) << "No layout available for quick slot" << number << "(have" << m_layouts.size()
                                << "layouts)";
        }
    }
}

void LayoutManager::setQuickLayoutSlot(int number, const QString& layoutId)
{
    if (number < 1 || number > 9) {
        qCWarning(lcLayout) << "Invalid quick layout slot number:" << number << "(must be 1-9)";
        return;
    }

    if (layoutId.isEmpty()) {
        // Clear the slot
        m_quickLayoutShortcuts.remove(number);
        qCInfo(lcLayout) << "Cleared quick layout slot" << number;
    } else {
        // Verify layout exists (skip for autotile IDs — they don't have Layout*)
        if (!LayoutId::isAutotile(layoutId) && !layoutById(QUuid::fromString(layoutId))) {
            qCWarning(lcLayout) << "Cannot assign non-existent layout to quick slot:" << layoutId;
            return;
        }
        m_quickLayoutShortcuts[number] = layoutId;
        qCInfo(lcLayout) << "Assigned layout" << layoutId << "to quick slot" << number;
    }

    // Save changes
    saveAssignments();
}

void LayoutManager::setAllQuickLayoutSlots(const QHash<int, QString>& slots)
{
    // Clear all existing slots first
    m_quickLayoutShortcuts.clear();

    // Set new slots (validate each one)
    for (auto it = slots.begin(); it != slots.end(); ++it) {
        int number = it.key();
        const QString& layoutId = it.value();

        if (number < 1 || number > 9) {
            qCWarning(lcLayout) << "Skipping invalid quick layout slot number:" << number;
            continue;
        }

        if (layoutId.isEmpty()) {
            // Empty means clear this slot (already cleared above)
            continue;
        }

        // Verify layout exists (skip for autotile IDs)
        if (!LayoutId::isAutotile(layoutId) && !layoutById(QUuid::fromString(layoutId))) {
            qCWarning(lcLayout) << "Skipping non-existent layout for quick slot" << number << ":" << layoutId;
            continue;
        }

        m_quickLayoutShortcuts[number] = layoutId;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to quick slot" << number;
    }

    // Save once at the end
    saveAssignments();
    qCInfo(lcLayout) << "Batch set" << m_quickLayoutShortcuts.size() << "quick layout slots";
}
void LayoutManager::cycleToPreviousLayout(const QString& screenId)
{
    cycleLayoutImpl(screenId, -1);
}

void LayoutManager::cycleToNextLayout(const QString& screenId)
{
    cycleLayoutImpl(screenId, +1);
}

void LayoutManager::createBuiltInLayouts()
{
    // Don't duplicate if already created (check for system layouts)
    for (auto* layout : m_layouts) {
        if (layout->isSystemLayout()) {
            return;
        }
    }

    // Create standard templates
    addLayout(Layout::createColumnsLayout(2, this));
    addLayout(Layout::createColumnsLayout(3, this));
    addLayout(Layout::createRowsLayout(2, this));
    addLayout(Layout::createGridLayout(2, 2, this));
    addLayout(Layout::createGridLayout(3, 2, this));
    addLayout(Layout::createPriorityGridLayout(this));
    addLayout(Layout::createFocusLayout(this));
}

QVector<Layout*> LayoutManager::builtInLayouts() const
{
    QVector<Layout*> result;
    for (auto* layout : m_layouts) {
        if (layout->isSystemLayout()) {
            result.append(layout);
        }
    }
    return result;
}

} // namespace PlasmaZones
