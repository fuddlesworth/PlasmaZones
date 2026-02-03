// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutmanager.h"
#include "constants.h"
#include "logging.h"
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

// Helper for layout cycling
// direction: -1 for previous, +1 for next
Layout* LayoutManager::cycleLayoutImpl(const QString& screenName, int direction)
{
    if (m_layouts.isEmpty()) {
        return nullptr;
    }

    // Use activeLayout as reference for cycling (not per-screen assignment)
    Layout* currentLayout = m_activeLayout;
    if (!currentLayout) {
        currentLayout = layoutForScreen(screenName);
    }
    if (!currentLayout) {
        currentLayout = m_layouts.first();
    }

    int currentIndex = m_layouts.indexOf(currentLayout);
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    // Wrap around: +size ensures positive modulo for direction=-1
    int newIndex = (currentIndex + direction + m_layouts.size()) % m_layouts.size();
    Layout* newLayout = m_layouts.at(newIndex);

    // Update both active layout and per-screen assignment
    setActiveLayout(newLayout);
    assignLayout(screenName, 0, QString(), newLayout);
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

        // Connect to save on modification
        connect(layout, &Layout::layoutModified, this, &LayoutManager::saveLayouts);

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
        m_activeLayout = m_layouts.isEmpty() ? nullptr : m_layouts.first();
        Q_EMIT activeLayoutChanged(m_activeLayout);
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
    addLayout(newLayout);
    saveLayouts();

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

void LayoutManager::assignLayout(const QString& screenName, int virtualDesktop, const QString& activity, Layout* layout)
{
    LayoutAssignmentKey key{screenName, virtualDesktop, activity};

    if (layout) {
        m_assignments[key] = layout->id();
    } else {
        m_assignments.remove(key);
    }

    Q_EMIT layoutAssigned(screenName, layout);
    saveAssignments();
}

void LayoutManager::assignLayoutById(const QString& screenName, int virtualDesktop, const QString& activity,
                                     const QUuid& layoutId)
{
    assignLayout(screenName, virtualDesktop, activity, layoutById(layoutId));
}

Layout* LayoutManager::layoutForScreen(const QString& screenName, int virtualDesktop, const QString& activity) const
{
    // Try exact match first
    LayoutAssignmentKey exactKey{screenName, virtualDesktop, activity};
    if (m_assignments.contains(exactKey)) {
        return layoutById(m_assignments[exactKey]);
    }

    // Try screen + desktop (any activity)
    LayoutAssignmentKey desktopKey{screenName, virtualDesktop, QString()};
    if (m_assignments.contains(desktopKey)) {
        return layoutById(m_assignments[desktopKey]);
    }

    // Try screen only (any desktop, any activity)
    LayoutAssignmentKey screenKey{screenName, 0, QString()};
    if (m_assignments.contains(screenKey)) {
        return layoutById(m_assignments[screenKey]);
    }

    // No assignment: use defaultLayoutId from settings when set, else first layout (by defaultOrder)
    if (m_settings && !m_settings->defaultLayoutId().isEmpty()) {
        if (Layout* L = layoutById(QUuid(m_settings->defaultLayoutId()))) {
            return L;
        }
    }
    return m_layouts.isEmpty() ? nullptr : m_layouts.first();
}

void LayoutManager::clearAssignment(const QString& screenName, int virtualDesktop, const QString& activity)
{
    assignLayout(screenName, virtualDesktop, activity, nullptr);
}

bool LayoutManager::hasExplicitAssignment(const QString& screenName, int virtualDesktop, const QString& activity) const
{
    LayoutAssignmentKey key{screenName, virtualDesktop, activity};
    return m_assignments.contains(key);
}

Layout* LayoutManager::layoutForShortcut(int number) const
{
    if (m_quickLayoutShortcuts.contains(number)) {
        return layoutById(m_quickLayoutShortcuts[number]);
    }
    return nullptr;
}

void LayoutManager::applyQuickLayout(int number, const QString& screenName)
{
    qCDebug(lcLayout) << "applyQuickLayout called: number=" << number << "screen=" << screenName;

    auto layout = layoutForShortcut(number);
    if (layout) {
        qCDebug(lcLayout) << "Found layout for shortcut" << number << ":" << layout->name();
        setActiveLayout(layout);
        // Persist the layout assignment to this monitor (all desktops, all activities)
        assignLayout(screenName, 0, QString(), layout);
    } else {
        // No layout assigned to this quick slot - try to use layout at index (number-1) as fallback
        qCDebug(lcLayout) << "No layout assigned to quick slot" << number << "- attempting fallback to layout index"
                          << (number - 1);
        if (number >= 1 && number <= m_layouts.size()) {
            layout = m_layouts.at(number - 1);
            if (layout) {
                qCDebug(lcLayout) << "Using fallback layout:" << layout->name();
                setActiveLayout(layout);
                assignLayout(screenName, 0, QString(), layout);
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
        qCDebug(lcLayout) << "Cleared quick layout slot" << number;
    } else {
        // Verify layout exists
        if (!layoutById(layoutId)) {
            qCWarning(lcLayout) << "Cannot assign non-existent layout to quick slot:" << layoutId;
            return;
        }
        m_quickLayoutShortcuts[number] = layoutId;
        qCDebug(lcLayout) << "Assigned layout" << layoutId << "to quick slot" << number;
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
    qCDebug(lcLayout) << "Batch set" << m_quickLayoutShortcuts.size() << "quick layout slots";
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
        const QString& screenName = it.key();
        const QUuid& layoutId = it.value();

        if (screenName.isEmpty()) {
            qCWarning(lcLayout) << "Skipping assignment with empty screen name";
            continue;
        }
        if (shouldSkipLayoutAssignment(layoutId, QStringLiteral("screen ") + screenName)) {
            continue;
        }

        LayoutAssignmentKey key{screenName, 0, QString()};
        m_assignments[key] = layoutId;
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to screen" << screenName;
    }

    saveAssignments();
    qCDebug(lcLayout) << "Batch set" << count << "screen assignments";
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
        const QString& screenName = it.key().first;
        int virtualDesktop = it.key().second;
        const QUuid& layoutId = it.value();

        if (screenName.isEmpty() || virtualDesktop < 1) {
            qCWarning(lcLayout) << "Skipping invalid desktop assignment:" << screenName << virtualDesktop;
            continue;
        }
        QString context = QStringLiteral("%1 desktop %2").arg(screenName).arg(virtualDesktop);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }

        LayoutAssignmentKey key{screenName, virtualDesktop, QString()};
        m_assignments[key] = layoutId;
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to" << screenName << "desktop" << virtualDesktop;
    }

    saveAssignments();
    qCDebug(lcLayout) << "Batch set" << count << "desktop assignments";
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
        const QString& screenName = it.key().first;
        const QString& activityId = it.key().second;
        const QUuid& layoutId = it.value();

        if (screenName.isEmpty() || activityId.isEmpty()) {
            qCWarning(lcLayout) << "Skipping invalid activity assignment:" << screenName << activityId;
            continue;
        }
        QString context = QStringLiteral("%1 activity %2").arg(screenName, activityId);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }

        LayoutAssignmentKey key{screenName, 0, activityId};
        m_assignments[key] = layoutId;
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to" << screenName << "activity" << activityId;
    }

    saveAssignments();
    qCDebug(lcLayout) << "Batch set" << count << "activity assignments";
}

QHash<QPair<QString, int>, QUuid> LayoutManager::desktopAssignments() const
{
    QHash<QPair<QString, int>, QUuid> result;

    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        // Per-desktop: virtualDesktop > 0 and activity is empty
        if (key.virtualDesktop > 0 && key.activity.isEmpty()) {
            result[qMakePair(key.screenName, key.virtualDesktop)] = it.value();
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
            result[qMakePair(key.screenName, key.activity)] = it.value();
        }
    }

    return result;
}

void LayoutManager::cycleToPreviousLayout(const QString& screenName)
{
    cycleLayoutImpl(screenName, -1);
}

void LayoutManager::cycleToNextLayout(const QString& screenName)
{
    cycleLayoutImpl(screenName, +1);
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

void LayoutManager::loadLayouts(const QString& defaultLayoutId)
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
        qCDebug(lcLayout) << "Loaded" << (m_layouts.size() - beforeCount) << "layouts from:" << dir;
    }

    qCDebug(lcLayout) << "Total layouts loaded:" << m_layouts.size();

    // Sort by defaultOrder (from layout JSON) so the preferred default is first when defaultLayoutId is empty
    std::stable_sort(m_layouts.begin(), m_layouts.end(), [](Layout* a, Layout* b) {
        return (a ? a->defaultOrder() : 999) < (b ? b->defaultOrder() : 999);
    });

    // Set initial active layout if none set: use defaultLayoutId when non-empty and found, else first
    if (!m_activeLayout && !m_layouts.isEmpty()) {
        Layout* initial = nullptr;
        if (!defaultLayoutId.isEmpty()) {
            initial = layoutById(QUuid(defaultLayoutId));
        }
        if (!initial) {
            initial = m_layouts.first();
        }
        qCInfo(lcLayout) << "Active layout:" << initial->name()
                         << "id=" << initial->id().toString()
                         << "zones=" << initial->zoneCount();
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
                connect(layout, &Layout::layoutModified, this, &LayoutManager::saveLayouts);
                qCDebug(lcLayout) << "Loaded layout:" << layout->name() << "with" << layout->zoneCount() << "zones"
                                  << (layout->isSystemLayout() ? "(system)" : "(custom)") << "from" << filePath;
            } else {
                // Duplicate ID found - user layouts (from .local) should override system layouts
                // Since we load system directories first, then user directories,
                // if we find a duplicate, the new one is from user directory and should replace
                if (!layout->isSystemLayout() && existing->isSystemLayout()) {
                    // User layout overrides system layout - replace the existing one
                    int index = m_layouts.indexOf(existing);
                    disconnect(existing, &Layout::layoutModified, this, &LayoutManager::saveLayouts);
                    m_layouts.replace(index, layout);
                    connect(layout, &Layout::layoutModified, this, &LayoutManager::saveLayouts);
                    delete existing;
                    qCDebug(lcLayout) << "User layout overrides system layout:" << layout->name() << "from" << filePath;
                } else {
                    // Same source type or system trying to override user - skip
                    qCDebug(lcLayout) << "Skipping duplicate layout:" << layout->name() << "(ID:" << layout->id()
                                      << ")";
                    delete layout;
                }
            }
        } else {
            qCWarning(lcLayout) << "Skipping invalid layout:" << entry << "(empty name or no zones)";
            delete layout;
        }
    }
}

void LayoutManager::saveLayouts()
{
    ensureLayoutDirectory();

    bool allSucceeded = true;
    for (auto* layout : m_layouts) {
        const QString filePath = layoutFilePath(layout->id());
        QFile file(filePath);

        if (!file.open(QIODevice::WriteOnly)) {
            qCWarning(lcLayout) << "Failed to open layout file for writing:" << filePath
                                << "Error:" << file.errorString();
            allSucceeded = false;
            continue;
        }

        QJsonDocument doc(layout->toJson());
        const QByteArray data = doc.toJson(QJsonDocument::Indented);

        if (file.write(data) != data.size()) {
            qCWarning(lcLayout) << "Failed to write layout file:" << filePath << "Error:" << file.errorString();
            allSucceeded = false;
            continue;
        }

        if (!file.flush()) {
            qCWarning(lcLayout) << "Failed to flush layout file:" << filePath << "Error:" << file.errorString();
            allSucceeded = false;
        } else {
            // Update sourcePath so isSystemLayout() returns correctly after saving
            layout->setSourcePath(filePath);
        }
    }

    if (!allSucceeded) {
        qCWarning(lcLayout) << "Some layouts failed to save";
    }

    // Emit layoutsChanged so UI refreshes (even if some saves failed)
    Q_EMIT layoutsChanged();

    Q_EMIT layoutsSaved();
}

void LayoutManager::loadAssignments()
{
    const QString filePath = m_layoutDirectory + QStringLiteral("/assignments.json");
    QFile file(filePath);

    if (!file.exists()) {
        qCDebug(lcLayout) << "Assignments file does not exist, using defaults:" << filePath;
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
        LayoutAssignmentKey key{obj[JsonKeys::Screen].toString(), obj[JsonKeys::Desktop].toInt(),
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

    qCInfo(lcLayout) << "Loaded" << m_assignments.size() << "layout assignments and"
                     << m_quickLayoutShortcuts.size() << "quick shortcuts";
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        Layout* layout = layoutById(it.value());
        QString layoutName = layout ? layout->name() : QStringLiteral("(unknown)");
        qCInfo(lcLayout) << "  Assignment: screen=" << it.key().screenName
                         << "desktop=" << it.key().virtualDesktop
                         << "activity=" << (it.key().activity.isEmpty() ? QStringLiteral("(all)") : it.key().activity)
                         << "-> layout" << layoutName;
    }
}

void LayoutManager::saveAssignments()
{
    ensureLayoutDirectory();

    QJsonObject root;

    // Save assignments
    QJsonArray assignmentsArray;
    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        QJsonObject obj;
        obj[JsonKeys::Screen] = it.key().screenName;
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

    qCDebug(lcLayout) << "Saved assignments to:" << filePath;
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
    addLayout(layout);
    saveLayouts();

    qCDebug(lcLayout) << "Successfully imported layout:" << layout->name() << "from" << filePath;
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

    qCDebug(lcLayout) << "Successfully exported layout:" << layout->name() << "to" << filePath;
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
