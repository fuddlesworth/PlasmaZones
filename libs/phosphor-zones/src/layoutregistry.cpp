// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/LayoutRegistry.h>

#include "zoneslogging.h"

#include <PhosphorScreens/ScreenIdentity.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <algorithm>

namespace PhosphorZones {

LayoutRegistry::LayoutRegistry(std::unique_ptr<PhosphorConfig::IBackend> backend, QString layoutSubdirectory,
                               QObject* parent)
    : IZoneLayoutRegistry(parent)
    , m_ownedBackend(std::move(backend))
    , m_configBackend(m_ownedBackend.get())
    , m_layoutSubdirectory(std::move(layoutSubdirectory))
{
    Q_ASSERT_X(m_configBackend != nullptr, "LayoutRegistry",
               "backend is required — every persistence method dereferences it");
    Q_ASSERT_X(!m_layoutSubdirectory.isEmpty(), "LayoutRegistry", "layoutSubdirectory is required");
    // The subdirectory is appended to XDG data roots, so it must be a
    // relative path without traversal segments. An absolute path would
    // ignore the XDG root entirely; ".." would escape the user-writable
    // area. Reject both — this is a developer error (composition-root
    // configuration), not user input, so assertion is the right signal.
    Q_ASSERT_X(!m_layoutSubdirectory.startsWith(QLatin1Char('/')), "LayoutRegistry",
               "layoutSubdirectory must be a relative XDG path, not absolute");
    Q_ASSERT_X(!m_layoutSubdirectory.contains(QLatin1String("..")), "LayoutRegistry",
               "layoutSubdirectory must not contain '..' traversal");

    m_layoutDirectory =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + m_layoutSubdirectory;
    ensureLayoutDirectory();

    // Forward the detailed layoutsChanged signal into the unified
    // ILayoutSourceRegistry::contentsChanged notifier so any subscribed
    // ILayoutSource (e.g. ZonesLayoutSource) refreshes automatically.
    connect(this, &LayoutRegistry::layoutsChanged, this, &PhosphorLayout::ILayoutSourceRegistry::contentsChanged);
}

LayoutRegistry::~LayoutRegistry()
{
    qDeleteAll(m_layouts);
}

void LayoutRegistry::setDefaultLayoutIdProvider(std::function<QString()> provider)
{
    m_defaultLayoutIdProvider = std::move(provider);
}

void LayoutRegistry::setLayoutDirectory(const QString& directory)
{
    if (m_layoutDirectory != directory) {
        m_layoutDirectory = directory;
        ensureLayoutDirectory();
        Q_EMIT layoutDirectoryChanged();
    }
}

PhosphorZones::Layout* LayoutRegistry::layout(int index) const
{
    if (index >= 0 && index < m_layouts.size()) {
        return m_layouts.at(index);
    }
    return nullptr;
}

// Template helper for lookup methods
template<typename Predicate>
static PhosphorZones::Layout* findLayout(const QVector<PhosphorZones::Layout*>& layouts, Predicate pred)
{
    auto it = std::find_if(layouts.begin(), layouts.end(), pred);
    return it != layouts.end() ? *it : nullptr;
}

// Helper: emit layoutAssigned for a single screenId/layoutId pair
void LayoutRegistry::emitLayoutAssigned(const QString& screenId, int virtualDesktop, const QString& layoutId)
{
    PhosphorZones::Layout* layout =
        PhosphorLayout::LayoutId::isAutotile(layoutId) ? nullptr : layoutById(QUuid::fromString(layoutId));
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
}

// Helper for validating layout assignment
// Returns true if layoutId should be skipped (null or non-existent)
bool LayoutRegistry::shouldSkipLayoutAssignment(const QString& layoutId, const QString& context) const
{
    if (layoutId.isEmpty()) {
        return true;
    }
    if (PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        return false; // Autotile IDs are valid without PhosphorZones::Layout* lookup
    }
    if (!layoutById(QUuid::fromString(layoutId))) {
        qCWarning(lcZonesLib) << "Skipping non-existent layout for" << context << ":" << layoutId;
        return true;
    }
    return false;
}

PhosphorZones::Layout* LayoutRegistry::defaultLayout() const
{
    if (m_defaultLayoutIdProvider) {
        const QString configuredId = m_defaultLayoutIdProvider();
        if (!configuredId.isEmpty()) {
            if (PhosphorZones::Layout* layout = layoutById(QUuid(configuredId))) {
                return layout;
            }
        }
    }
    return m_layouts.isEmpty() ? nullptr : m_layouts.first();
}

// Helper for layout cycling
// direction: -1 for previous, +1 for next
// Filters out hidden layouts and respects visibility allow-lists
PhosphorZones::Layout* LayoutRegistry::cycleLayoutImpl(const QString& screenId, int direction)
{
    if (m_layouts.isEmpty()) {
        return nullptr;
    }

    // Translate connector name to screen ID for allowedScreens matching
    QString resolvedScreenId;
    if (!screenId.isEmpty()) {
        resolvedScreenId = Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)
            ? Phosphor::Screens::ScreenIdentity::idForName(screenId)
            : screenId;
    }

    // Build filtered list of visible layouts for current context
    QVector<PhosphorZones::Layout*> visible;
    for (PhosphorZones::Layout* l : m_layouts) {
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
    PhosphorZones::Layout* currentLayout = nullptr;
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
    PhosphorZones::Layout* newLayout = visible.at(newIndex);

    // Per-screen assignment + suppressed global-active-layout update —
    // shared with applyQuickLayout. Cycling with an empty screenId
    // (uncommon but not invalid — happens when no focused screen is
    // known) just updates the global active layout.
    applyLayoutToScreen(resolvedScreenId, newLayout);
    return newLayout;
}

void LayoutRegistry::applyLayoutToScreen(const QString& screenId, PhosphorZones::Layout* layout)
{
    // Per-screen assignment: write per-desktop assignment first so the
    // layoutAssigned handler recalculates zone geometry for this screen.
    if (!screenId.isEmpty()) {
        // Write per-desktop assignment with empty activity so it applies
        // regardless of which activity is active. Activity-specific
        // overrides are a separate KCM-only feature. Clear any stale
        // activity-keyed entry that would shadow this one in the cascade.
        if (!m_currentActivity.isEmpty()) {
            clearAssignment(screenId, m_currentVirtualDesktop, m_currentActivity);
        }
        assignLayout(screenId, m_currentVirtualDesktop, QString(), layout);
    }
    // Update the global active layout pointer (for overlay/zone detector
    // queries) but suppress activeLayoutChanged to prevent resnap buffer
    // population and zone recalculation on ALL screens. The per-screen
    // assignment above already handles the target screen via layoutAssigned.
    {
        const QSignalBlocker blocker(this);
        setActiveLayout(layout);
    }
}

PhosphorZones::Layout* LayoutRegistry::layoutById(const QUuid& id) const
{
    return findLayout(m_layouts, [&id](const PhosphorZones::Layout* l) {
        return l->id() == id;
    });
}

PhosphorZones::Layout* LayoutRegistry::layoutByName(const QString& name) const
{
    return findLayout(m_layouts, [&name](const PhosphorZones::Layout* l) {
        return l->name() == name;
    });
}

void LayoutRegistry::addLayout(PhosphorZones::Layout* layout)
{
    if (layout && !m_layouts.contains(layout)) {
        layout->setParent(this);
        m_layouts.append(layout);

        // Connect to save on modification (per-layout copy-on-write)
        connect(layout, &PhosphorZones::Layout::layoutModified, this, [this, layout]() {
            saveLayout(layout);
        });

        // New layouts need immediate persistence
        layout->markDirty();
        saveLayout(layout);

        Q_EMIT layoutAdded(layout);
        Q_EMIT layoutsChanged();
    }
}

void LayoutRegistry::removeLayout(PhosphorZones::Layout* layout)
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

    // Clear every pointer that could capture the about-to-deleteLater
    // layout. m_previousLayout is obvious. m_activeLayout is the subtle
    // one: the wasActive branches below call setActiveLayout(newLayout),
    // whose prologue does `m_previousLayout = m_activeLayout ? m_activeLayout
    // : layout` — so if we leave m_activeLayout pointing at `layout`, the
    // dying pointer slides straight back into m_previousLayout and
    // previousLayout() returns a freed object the next event-loop tick.
    // Zeroing both here makes the subsequent setActiveLayout start from a
    // clean slate (previous == new on first-run, same contract as ctor).
    if (m_previousLayout == layout) {
        m_previousLayout = nullptr;
    }
    if (wasActive) {
        m_activeLayout = nullptr;
    }

    Q_EMIT layoutRemoved(layout);
    layout->deleteLater();

    // If this was a user override of a system layout, restore the system original.
    // Uses the stored system path — no filesystem scanning needed.
    // NOTE: restoreSystemLayout() emits layoutAdded but not layoutsChanged;
    // the caller (this method) emits layoutsChanged below.
    PhosphorZones::Layout* restored = restoreSystemLayout(layoutId, systemPath);

    if (restored) {
        // System layout restored — assignments and shortcuts stay valid (same UUID).
        if (wasActive) {
            setActiveLayout(restored);
        }
    } else {
        // Truly deleted — clean up assignments and shortcuts referencing this layout
        for (auto it = m_assignments.begin(); it != m_assignments.end();) {
            if (it.value().snappingLayout == layoutIdStr || it.value().activeLayoutId() == layoutIdStr) {
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

void LayoutRegistry::removeLayoutById(const QUuid& id)
{
    removeLayout(layoutById(id));
}

PhosphorZones::Layout* LayoutRegistry::duplicateLayout(PhosphorZones::Layout* source)
{
    if (!source) {
        return nullptr;
    }

    auto newLayout = new PhosphorZones::Layout(*source);
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

void LayoutRegistry::setActiveLayout(PhosphorZones::Layout* layout)
{
    if (m_activeLayout != layout && (layout == nullptr || m_layouts.contains(layout))) {
        // Capture current as previous before changing (on first run, use layout as both)
        m_previousLayout = m_activeLayout ? m_activeLayout : layout;
        qCInfo(lcZonesLib) << "setActiveLayout:"
                           << (m_previousLayout ? m_previousLayout->name() : QStringLiteral("null")) << "->"
                           << (layout ? layout->name() : QStringLiteral("null"));
        m_activeLayout = layout;
        Q_EMIT activeLayoutChanged(m_activeLayout);
    } else if (m_activeLayout == layout) {
        qCInfo(lcZonesLib) << "setActiveLayout: SKIPPED (already active):"
                           << (layout ? layout->name() : QStringLiteral("null"));
    }
}

void LayoutRegistry::setActiveLayoutById(const QUuid& id)
{
    setActiveLayout(layoutById(id));
}

PhosphorZones::Layout* LayoutRegistry::layoutForShortcut(int number) const
{
    if (m_quickLayoutShortcuts.contains(number)) {
        const QString& id = m_quickLayoutShortcuts[number];
        if (PhosphorLayout::LayoutId::isAutotile(id))
            return nullptr;
        return layoutById(QUuid::fromString(id));
    }
    return nullptr;
}

void LayoutRegistry::applyQuickLayout(int number, const QString& screenId)
{
    qCInfo(lcZonesLib) << "applyQuickLayout: number=" << number << "screen=" << screenId;

    // Quick-slot shortcuts are explicit bindings. If the user cleared the
    // slot (setQuickLayoutSlot(n, "")), pressing the shortcut must be a
    // no-op — falling back to m_layouts.at(number-1) silently resurrects
    // a layout the user deliberately unbound.
    auto layout = layoutForShortcut(number);
    if (!layout) {
        qCInfo(lcZonesLib) << "Quick slot" << number << "is unset — no-op";
        return;
    }

    qCDebug(lcZonesLib) << "Found layout for shortcut" << number << ":" << layout->name();
    applyLayoutToScreen(screenId, layout);
}

void LayoutRegistry::setQuickLayoutSlot(int number, const QString& layoutId)
{
    if (number < 1 || number > 9) {
        qCWarning(lcZonesLib) << "Invalid quick layout slot number:" << number << "(must be 1-9)";
        return;
    }

    if (layoutId.isEmpty()) {
        // Clear the slot
        m_quickLayoutShortcuts.remove(number);
        qCInfo(lcZonesLib) << "Cleared quick layout slot" << number;
    } else {
        // Verify layout exists (skip for autotile IDs — they don't have PhosphorZones::Layout*)
        if (!PhosphorLayout::LayoutId::isAutotile(layoutId) && !layoutById(QUuid::fromString(layoutId))) {
            qCWarning(lcZonesLib) << "Cannot assign non-existent layout to quick slot:" << layoutId;
            return;
        }
        m_quickLayoutShortcuts[number] = layoutId;
        qCInfo(lcZonesLib) << "Assigned layout" << layoutId << "to quick slot" << number;
    }

    // Save changes
    saveAssignments();
}

void LayoutRegistry::setAllQuickLayoutSlots(const QHash<int, QString>& slots)
{
    // Clear all existing slots first
    m_quickLayoutShortcuts.clear();

    // Set new slots (validate each one)
    for (auto it = slots.begin(); it != slots.end(); ++it) {
        int number = it.key();
        const QString& layoutId = it.value();

        if (number < 1 || number > 9) {
            qCWarning(lcZonesLib) << "Skipping invalid quick layout slot number:" << number;
            continue;
        }

        if (layoutId.isEmpty()) {
            // Empty means clear this slot (already cleared above)
            continue;
        }

        // Verify layout exists (skip for autotile IDs)
        if (!PhosphorLayout::LayoutId::isAutotile(layoutId) && !layoutById(QUuid::fromString(layoutId))) {
            qCWarning(lcZonesLib) << "Skipping non-existent layout for quick slot" << number << ":" << layoutId;
            continue;
        }

        m_quickLayoutShortcuts[number] = layoutId;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to quick slot" << number;
    }

    // Save once at the end
    saveAssignments();
    qCInfo(lcZonesLib) << "Batch set" << m_quickLayoutShortcuts.size() << "quick layout slots";
}
void LayoutRegistry::cycleToPreviousLayout(const QString& screenId)
{
    cycleLayoutImpl(screenId, -1);
}

void LayoutRegistry::cycleToNextLayout(const QString& screenId)
{
    cycleLayoutImpl(screenId, +1);
}

void LayoutRegistry::createBuiltInLayouts()
{
    // Don't duplicate if already created (check for system layouts)
    for (auto* layout : m_layouts) {
        if (layout->isSystemLayout()) {
            return;
        }
    }

    // Create standard templates
    addLayout(PhosphorZones::Layout::createColumnsLayout(2, this));
    addLayout(PhosphorZones::Layout::createColumnsLayout(3, this));
    addLayout(PhosphorZones::Layout::createRowsLayout(2, this));
    addLayout(PhosphorZones::Layout::createGridLayout(2, 2, this));
    addLayout(PhosphorZones::Layout::createGridLayout(3, 2, this));
    addLayout(PhosphorZones::Layout::createPriorityGridLayout(this));
    addLayout(PhosphorZones::Layout::createFocusLayout(this));
}

QVector<PhosphorZones::Layout*> LayoutRegistry::builtInLayouts() const
{
    QVector<PhosphorZones::Layout*> result;
    for (auto* layout : m_layouts) {
        if (layout->isSystemLayout()) {
            result.append(layout);
        }
    }
    return result;
}

} // namespace PhosphorZones
