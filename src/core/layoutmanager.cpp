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

// Helper for validating layout assignment
// Returns true if layoutId should be skipped (null or non-existent)
bool LayoutManager::shouldSkipLayoutAssignment(const QUuid& layoutId, const QString& context) const
{
    if (layoutId.isNull()) {
        // Empty/null means clear (handled by caller)
        return true;
    }
    if (!layoutById(layoutId)) {
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

    // Store ID and state BEFORE any operations that might invalidate the pointer
    const QUuid layoutId = layout->id();
    const bool wasActive = (m_activeLayout == layout);
    const QString filePath = layoutFilePath(layoutId);

    // Remove from layouts list
    m_layouts.removeOne(layout);

    // Remove any assignments using stored ID (safe - ID is copied)
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.value() == layoutId) {
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Remove from quick shortcuts using stored ID
    for (auto it = m_quickLayoutShortcuts.begin(); it != m_quickLayoutShortcuts.end();) {
        if (it.value() == layoutId) {
            it = m_quickLayoutShortcuts.erase(it);
        } else {
            ++it;
        }
    }

    // Update active layout if needed
    if (wasActive) {
        setActiveLayout(defaultLayout());
    }

    // Delete layout file (using stored path)
    QFile::remove(filePath);

    // Emit signals before deleting
    Q_EMIT layoutRemoved(layout);
    Q_EMIT layoutsChanged();

    // Schedule deletion (safe - we've already removed from all containers)
    layout->deleteLater();

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

void LayoutManager::assignLayout(const QString& screenId, int virtualDesktop, const QString& activity, Layout* layout)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};

    if (layout) {
        m_assignments[key] = layout->id();
    } else {
        m_assignments.remove(key);
    }

    Q_EMIT layoutAssigned(screenId, layout);
    saveAssignments();
}

void LayoutManager::assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                     const QUuid& layoutId)
{
    assignLayout(screenId, virtualDesktop, activity, layoutById(layoutId));
}

Layout* LayoutManager::layoutForScreen(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Try exact match first
    LayoutAssignmentKey exactKey{screenId, virtualDesktop, activity};
    if (m_assignments.contains(exactKey)) {
        return layoutById(m_assignments[exactKey]);
    }

    // Try screen + desktop (any activity)
    LayoutAssignmentKey desktopKey{screenId, virtualDesktop, QString()};
    if (m_assignments.contains(desktopKey)) {
        return layoutById(m_assignments[desktopKey]);
    }

    // Try screen only (any desktop, any activity)
    LayoutAssignmentKey screenKey{screenId, 0, QString()};
    if (m_assignments.contains(screenKey)) {
        return layoutById(m_assignments[screenKey]);
    }

    // Fallback: if screenId looks like a connector name (no colons), try resolving
    // to a screen ID and looking up again. This handles callers that haven't been
    // migrated to pass screen IDs yet.
    if (Utils::isConnectorName(screenId)) {
        QString resolved = Utils::screenIdForName(screenId);
        if (resolved != screenId) {
            return layoutForScreen(resolved, virtualDesktop, activity);
        }
    }

    // No assignment: use defaultLayoutId from settings when set, else first layout (by defaultOrder)
    if (m_settings && !m_settings->defaultLayoutId().isEmpty()) {
        if (Layout* L = layoutById(QUuid(m_settings->defaultLayoutId()))) {
            return L;
        }
    }
    return m_layouts.isEmpty() ? nullptr : m_layouts.first();
}

void LayoutManager::clearAssignment(const QString& screenId, int virtualDesktop, const QString& activity)
{
    assignLayout(screenId, virtualDesktop, activity, nullptr);
}

bool LayoutManager::hasExplicitAssignment(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};
    return m_assignments.contains(key);
}

Layout* LayoutManager::layoutForShortcut(int number) const
{
    if (m_quickLayoutShortcuts.contains(number)) {
        return layoutById(m_quickLayoutShortcuts[number]);
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

void LayoutManager::setQuickLayoutSlot(int number, const QUuid& layoutId)
{
    if (number < 1 || number > 9) {
        qCWarning(lcLayout) << "Invalid quick layout slot number:" << number << "(must be 1-9)";
        return;
    }

    if (layoutId.isNull()) {
        // Clear the slot
        m_quickLayoutShortcuts.remove(number);
        qCInfo(lcLayout) << "Cleared quick layout slot" << number;
    } else {
        // Verify layout exists
        if (!layoutById(layoutId)) {
            qCWarning(lcLayout) << "Cannot assign non-existent layout to quick slot:" << layoutId;
            return;
        }
        m_quickLayoutShortcuts[number] = layoutId;
        qCInfo(lcLayout) << "Assigned layout" << layoutId << "to quick slot" << number;
    }

    // Save changes
    saveAssignments();
}

void LayoutManager::setAllQuickLayoutSlots(const QHash<int, QUuid>& slots)
{
    // Clear all existing slots first
    m_quickLayoutShortcuts.clear();

    // Set new slots (validate each one)
    for (auto it = slots.begin(); it != slots.end(); ++it) {
        int number = it.key();
        const QUuid& layoutId = it.value();

        if (number < 1 || number > 9) {
            qCWarning(lcLayout) << "Skipping invalid quick layout slot number:" << number;
            continue;
        }

        if (layoutId.isNull()) {
            // Empty/null means clear this slot (already cleared above)
            continue;
        }

        // Verify layout exists
        if (!layoutById(layoutId)) {
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

void LayoutManager::setAllScreenAssignments(const QHash<QString, QUuid>& assignments)
{
    // Clear existing base screen assignments (desktop=0, activity=empty)
    // Keep per-desktop and per-activity assignments intact
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.key().virtualDesktop == 0 && it.key().activity.isEmpty()) {
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments
    int count = 0;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key();
        const QUuid& layoutId = it.value();

        if (screenId.isEmpty()) {
            qCWarning(lcLayout) << "Skipping assignment with empty screen ID";
            continue;
        }
        if (shouldSkipLayoutAssignment(layoutId, QStringLiteral("screen ") + screenId)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, 0, QString()};
        m_assignments[key] = layoutId;
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to screen" << screenId;
    }

    saveAssignments();
    qCInfo(lcLayout) << "Batch set" << count << "screen assignments";
}

void LayoutManager::setAllDesktopAssignments(const QHash<QPair<QString, int>, QUuid>& assignments)
{
    // Clear existing per-desktop assignments (desktop > 0, activity empty)
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.key().virtualDesktop > 0 && it.key().activity.isEmpty()) {
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments
    int count = 0;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        int virtualDesktop = it.key().second;
        const QUuid& layoutId = it.value();

        if (screenId.isEmpty() || virtualDesktop < 1) {
            qCWarning(lcLayout) << "Skipping invalid desktop assignment:" << screenId << virtualDesktop;
            continue;
        }
        QString context = QStringLiteral("%1 desktop %2").arg(screenId).arg(virtualDesktop);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, virtualDesktop, QString()};
        m_assignments[key] = layoutId;
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to" << screenId << "desktop" << virtualDesktop;
    }

    saveAssignments();
    qCInfo(lcLayout) << "Batch set" << count << "desktop assignments";
}

void LayoutManager::setAllActivityAssignments(const QHash<QPair<QString, QString>, QUuid>& assignments)
{
    // Clear existing per-activity assignments (activity non-empty, desktop=0)
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (!it.key().activity.isEmpty() && it.key().virtualDesktop == 0) {
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments
    int count = 0;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        const QString& activityId = it.key().second;
        const QUuid& layoutId = it.value();

        if (screenId.isEmpty() || activityId.isEmpty()) {
            qCWarning(lcLayout) << "Skipping invalid activity assignment:" << screenId << activityId;
            continue;
        }
        QString context = QStringLiteral("%1 activity %2").arg(screenId, activityId);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, 0, activityId};
        m_assignments[key] = layoutId;
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to" << screenId << "activity" << activityId;
    }

    saveAssignments();
    qCInfo(lcLayout) << "Batch set" << count << "activity assignments";
}

QHash<QPair<QString, int>, QUuid> LayoutManager::desktopAssignments() const
{
    QHash<QPair<QString, int>, QUuid> result;

    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        // Per-desktop: virtualDesktop > 0 and activity is empty
        if (key.virtualDesktop > 0 && key.activity.isEmpty()) {
            result[qMakePair(key.screenId, key.virtualDesktop)] = it.value();
        }
    }

    return result;
}

QHash<QPair<QString, QString>, QUuid> LayoutManager::activityAssignments() const
{
    QHash<QPair<QString, QString>, QUuid> result;

    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        // Per-activity: activity is non-empty (for any desktop value)
        if (!key.activity.isEmpty()) {
            result[qMakePair(key.screenId, key.activity)] = it.value();
        }
    }

    return result;
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

void LayoutManager::loadLayouts()
{
    ensureLayoutDirectory();

    // Load from ALL data locations (system directories first, then user)
    // locateAll() returns paths in priority order: user first, system last
    // We reverse to load system first, so user layouts can override
    QStringList allDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/layouts"), QStandardPaths::LocateDirectory);

    // Reverse order: load system layouts first, user layouts last (override)
    std::reverse(allDirs.begin(), allDirs.end());

    for (const QString& dir : allDirs) {
        int beforeCount = m_layouts.size();
        loadLayoutsFromDirectory(dir);
        qCInfo(lcLayout) << "Loaded layouts= " << (m_layouts.size() - beforeCount) << " from= " << dir;
    }

    qCInfo(lcLayout) << "Total layouts= " << m_layouts.size();

    // Sort by defaultOrder (from layout JSON) so the preferred default is first when defaultLayoutId is empty
    std::stable_sort(m_layouts.begin(), m_layouts.end(), [](Layout* a, Layout* b) {
        return (a ? a->defaultOrder() : 999) < (b ? b->defaultOrder() : 999);
    });

    // Set initial active layout if none set: use defaultLayout() (settings-based fallback)
    if (!m_activeLayout && !m_layouts.isEmpty()) {
        Layout* initial = defaultLayout();
        if (initial) {
            qCInfo(lcLayout) << "Active layout name= " << initial->name()
                             << " id= " << initial->id().toString()
                             << " zones= " << initial->zoneCount();
        }
        setActiveLayout(initial);
    }

    Q_EMIT layoutsLoaded();
    Q_EMIT layoutsChanged();
}

void LayoutManager::loadLayoutsFromDirectory(const QString& directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        qCWarning(lcLayout) << "Layout directory does not exist:" << directory;
        return;
    }

    if (!dir.isReadable()) {
        qCWarning(lcLayout) << "Layout directory is not readable:" << directory;
        return;
    }

    const auto entries = dir.entryList({QStringLiteral("*.json")}, QDir::Files);

    for (const auto& entry : entries) {
        if (entry == QStringLiteral("assignments.json")) {
            continue; // Skip assignments file
        }

        const QString filePath = dir.absoluteFilePath(entry);
        QFile file(filePath);

        if (!file.exists()) {
            qCWarning(lcLayout) << "Layout file does not exist:" << filePath;
            continue;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcLayout) << "Failed to open layout file:" << filePath << "Error:" << file.errorString();
            continue;
        }

        const QByteArray data = file.readAll();
        if (data.isEmpty()) {
            qCWarning(lcLayout) << "Layout file is empty:" << filePath;
            continue;
        }

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(lcLayout) << "Failed to parse layout file:" << filePath << "Error:" << parseError.errorString()
                                << "at offset" << parseError.offset;
            continue;
        }

        auto layout = Layout::fromJson(doc.object(), this);
        if (!layout) {
            qCWarning(lcLayout) << "Failed to create layout from JSON:" << filePath;
            continue;
        }

        // Set source path - this determines if layout is system or user layout
        // isSystemLayout() checks if sourcePath is outside user's writable directory
        layout->setSourcePath(filePath);

        if (!layout->name().isEmpty() && layout->zoneCount() > 0) {
            // Check for duplicate IDs
            Layout* existing = layoutById(layout->id());
            if (!existing) {
                // No duplicate - add the layout
                m_layouts.append(layout);
                connect(layout, &Layout::layoutModified, this, [this, layout]() { saveLayout(layout); });
                qCInfo(lcLayout) << "  Loaded layout name= " << layout->name() << " zones= " << layout->zoneCount()
                                  << " source= " << (layout->isSystemLayout() ? "system" : "user") << " from= " << filePath;
            } else {
                // Duplicate ID found - user layouts (from .local) should override system layouts
                // Since we load system directories first, then user directories,
                // if we find a duplicate, the new one is from user directory and should replace
                if (!layout->isSystemLayout() && existing->isSystemLayout()) {
                    // User layout overrides system layout - replace the existing one
                    int index = m_layouts.indexOf(existing);
                    disconnect(existing, &Layout::layoutModified, this, nullptr);
                    m_layouts.replace(index, layout);
                    connect(layout, &Layout::layoutModified, this, [this, layout]() { saveLayout(layout); });
                    delete existing;
                    qCInfo(lcLayout) << "  User layout overrides system layout name= " << layout->name() << " from= " << filePath;
                } else {
                    // Same source type or system trying to override user - skip
                    qCInfo(lcLayout) << "  Skipping duplicate layout name= " << layout->name() << " id= " << layout->id();
                    delete layout;
                }
            }
        } else {
            qCWarning(lcLayout) << "Skipping invalid layout entry= " << entry << " reason= empty name or no zones";
            delete layout;
        }
    }
}

void LayoutManager::saveLayout(Layout* layout)
{
    if (!layout || !layout->isDirty()) {
        return;
    }

    ensureLayoutDirectory();

    const QString filePath = layoutFilePath(layout->id());
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcLayout) << "Failed to open layout file for writing:" << filePath
                            << "Error:" << file.errorString();
        return;
    }

    QJsonDocument doc(layout->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcLayout) << "Failed to write layout file:" << filePath << "Error:" << file.errorString();
        return;
    }

    if (!file.flush()) {
        qCWarning(lcLayout) << "Failed to flush layout file:" << filePath << "Error:" << file.errorString();
        return;
    }

    // Update sourcePath so isSystemLayout() returns correctly after saving
    layout->setSourcePath(filePath);
    layout->clearDirty();
}

void LayoutManager::saveLayouts()
{
    for (auto* layout : m_layouts) {
        saveLayout(layout);
    }

    Q_EMIT layoutsChanged();
    Q_EMIT layoutsSaved();
}

void LayoutManager::loadAssignments()
{
    const QString filePath = m_layoutDirectory + QStringLiteral("/assignments.json");
    QFile file(filePath);

    if (!file.exists()) {
        qCInfo(lcLayout) << "Assignments file does not exist, using defaults:" << filePath;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcLayout) << "Failed to open assignments file:" << filePath << "Error:" << file.errorString();
        return;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        qCWarning(lcLayout) << "Assignments file is empty:" << filePath;
        return;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcLayout) << "Failed to parse assignments file:" << filePath << "Error:" << parseError.errorString()
                            << "at offset" << parseError.offset;
        return;
    }

    const auto root = doc.object();

    // Load assignments with error handling
    const auto assignmentsArray = root[JsonKeys::Assignments].toArray();
    for (const auto& value : assignmentsArray) {
        if (!value.isObject()) {
            qCWarning(lcLayout) << "Invalid assignment entry (not an object), skipping";
            continue;
        }

        const auto obj = value.toObject();
        // Prefer screenId (EDID-based); fall back to screen (connector name) for legacy configs
        QString sid = obj[JsonKeys::ScreenId].toString();
        if (sid.isEmpty()) {
            sid = obj[JsonKeys::Screen].toString();
        }
        LayoutAssignmentKey key{sid, obj[JsonKeys::Desktop].toInt(),
                                obj[JsonKeys::Activity].toString()};

        const QString layoutIdStr = obj[JsonKeys::LayoutId].toString();
        const QUuid layoutId = QUuid::fromString(layoutIdStr);

        if (layoutId.isNull()) {
            qCWarning(lcLayout) << "Invalid layout ID in assignment:" << layoutIdStr << "skipping";
            continue;
        }

        m_assignments[key] = layoutId;
    }

    // Load quick shortcuts with error handling
    const auto shortcutsObj = root[JsonKeys::QuickShortcuts].toObject();
    for (auto it = shortcutsObj.begin(); it != shortcutsObj.end(); ++it) {
        bool ok = false;
        const int number = it.key().toInt(&ok);
        if (!ok) {
            qCWarning(lcLayout) << "Invalid shortcut number:" << it.key() << "skipping";
            continue;
        }

        const QString layoutIdStr = it.value().toString();
        const QUuid layoutId = QUuid::fromString(layoutIdStr);

        if (layoutId.isNull()) {
            qCWarning(lcLayout) << "Invalid layout ID in shortcut:" << layoutIdStr << "skipping";
            continue;
        }

        m_quickLayoutShortcuts[number] = layoutId;
    }

    qCInfo(lcLayout) << "Loaded assignments= " << m_assignments.size() << " quickShortcuts= " << m_quickLayoutShortcuts.size();
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        Layout* layout = layoutById(it.value());
        QString layoutName = layout ? layout->name() : QStringLiteral("(unknown)");
        qCDebug(lcLayout) << "  Assignment screenId= " << it.key().screenId
                         << " desktop= " << it.key().virtualDesktop
                         << " activity= " << (it.key().activity.isEmpty() ? QStringLiteral("(all)") : it.key().activity)
                         << " layout= " << layoutName;
    }
}

void LayoutManager::saveAssignments()
{
    ensureLayoutDirectory();

    QJsonObject root;

    // Save assignments (write both screenId and screen for backward compat)
    QJsonArray assignmentsArray;
    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        QJsonObject obj;
        obj[JsonKeys::ScreenId] = it.key().screenId;
        // Write connector name for backward compat and debugging
        QString connectorName = Utils::screenNameForId(it.key().screenId);
        obj[JsonKeys::Screen] = connectorName.isEmpty() ? it.key().screenId : connectorName;
        obj[JsonKeys::Desktop] = it.key().virtualDesktop;
        obj[JsonKeys::Activity] = it.key().activity;
        obj[JsonKeys::LayoutId] = it.value().toString();
        assignmentsArray.append(obj);
    }
    root[JsonKeys::Assignments] = assignmentsArray;

    // Save quick shortcuts
    QJsonObject shortcutsObj;
    for (auto it = m_quickLayoutShortcuts.begin(); it != m_quickLayoutShortcuts.end(); ++it) {
        shortcutsObj[QString::number(it.key())] = it.value().toString();
    }
    root[JsonKeys::QuickShortcuts] = shortcutsObj;

    const QString filePath = m_layoutDirectory + QStringLiteral("/assignments.json");
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcLayout) << "Failed to open assignments file for writing:" << filePath
                            << "Error:" << file.errorString();
        return;
    }

    QJsonDocument doc(root);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcLayout) << "Failed to write assignments file:" << filePath << "Error:" << file.errorString();
        return;
    }

    if (!file.flush()) {
        qCWarning(lcLayout) << "Failed to flush assignments file:" << filePath << "Error:" << file.errorString();
    }

    qCInfo(lcLayout) << "Saved assignments to:" << filePath;
}

void LayoutManager::importLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        qCWarning(lcLayout) << "Cannot import layout: file path is empty";
        return;
    }

    QFile file(filePath);
    if (!file.exists()) {
        qCWarning(lcLayout) << "Cannot import layout: file does not exist:" << filePath;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcLayout) << "Failed to open layout file for import:" << filePath << "Error:" << file.errorString();
        return;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        qCWarning(lcLayout) << "Cannot import layout: file is empty:" << filePath;
        return;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcLayout) << "Failed to parse layout file for import:" << filePath
                            << "Error:" << parseError.errorString() << "at offset" << parseError.offset;
        return;
    }

    auto layout = Layout::fromJson(doc.object(), this);
    if (!layout) {
        qCWarning(lcLayout) << "Failed to create layout from imported JSON:" << filePath;
        return;
    }

    // Imported layouts have no source path - they'll be saved to user directory
    // (sourcePath is already empty from fromJson)

    // Reset visibility restrictions since screen/desktop/activity names are machine-specific
    layout->setHiddenFromSelector(false);
    layout->setAllowedScreens({});
    layout->setAllowedDesktops({});
    layout->setAllowedActivities({});

    addLayout(layout);

    qCInfo(lcLayout) << "Successfully imported layout:" << layout->name() << "from" << filePath;
}

void LayoutManager::exportLayout(Layout* layout, const QString& filePath)
{
    if (!layout) {
        qCWarning(lcLayout) << "Cannot export layout: layout is null";
        return;
    }

    if (filePath.isEmpty()) {
        qCWarning(lcLayout) << "Cannot export layout: file path is empty";
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcLayout) << "Failed to open file for layout export:" << filePath << "Error:" << file.errorString();
        return;
    }

    QJsonDocument doc(layout->toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (file.write(data) != data.size()) {
        qCWarning(lcLayout) << "Failed to write layout to file:" << filePath << "Error:" << file.errorString();
        return;
    }

    if (!file.flush()) {
        qCWarning(lcLayout) << "Failed to flush layout export file:" << filePath << "Error:" << file.errorString();
    }

    qCInfo(lcLayout) << "Successfully exported layout:" << layout->name() << "to" << filePath;
}

void LayoutManager::ensureLayoutDirectory()
{
    QDir dir(m_layoutDirectory);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
}

QString LayoutManager::layoutFilePath(const QUuid& id) const
{
    // Strip braces only for filesystem path (avoid { } in filenames). Everywhere else we use default (with braces).
    return m_layoutDirectory + QStringLiteral("/") + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
}

} // namespace PlasmaZones
