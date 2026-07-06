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

LayoutRegistry::LayoutRegistry(PhosphorRules::RuleStore* ruleStore, QString layoutSubdirectory, QObject* parent)
    : IZoneLayoutRegistry(parent)
    , m_ruleStore(ruleStore)
    , m_layoutSubdirectory(std::move(layoutSubdirectory))
{
    // qFatal, not Q_ASSERT: initCommon() unconditionally dereferences
    // m_ruleStore (->ruleSet()), so a null store is a developer composition-root
    // error that would otherwise segfault in release where Q_ASSERT compiles
    // out. Matches the layoutSubdirectory invariants below.
    if (m_ruleStore == nullptr) {
        qFatal("LayoutRegistry: ruleStore is required — assignment resolution dereferences it");
    }
    Q_ASSERT_X(!m_layoutSubdirectory.isEmpty(), "LayoutRegistry", "layoutSubdirectory is required");
    initCommon();
}

void LayoutRegistry::initCommon()
{
    // The subdirectory is appended to XDG data roots, so it must be a
    // relative path without traversal segments. An absolute path would
    // ignore the XDG root entirely; ".." would escape the user-writable
    // area. Reject both — this is a developer error (composition-root
    // configuration), not user input, so a fatal is the right signal:
    // Q_ASSERT compiles out in release, but a bad subdirectory would silently
    // proceed to QStandardPaths concatenation and produce a non-existent
    // (absolute) or escaping (..) directory in EVERY build that ships.
    if (m_layoutSubdirectory.startsWith(QLatin1Char('/'))) {
        qFatal("LayoutRegistry: layoutSubdirectory must be a relative XDG path, not absolute: %s",
               qPrintable(m_layoutSubdirectory));
    }
    if (m_layoutSubdirectory.contains(QLatin1String(".."))) {
        qFatal("LayoutRegistry: layoutSubdirectory must not contain '..' traversal: %s",
               qPrintable(m_layoutSubdirectory));
    }

    m_layoutDirectory =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + m_layoutSubdirectory;
    ensureLayoutDirectory();

    // One evaluation model — bound to the store's live rule set. The store
    // owns the set's lifetime; the evaluator holds a reference to it.
    m_evaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());

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

void LayoutRegistry::setDefaultAutotileAlgorithmProvider(std::function<QString()> provider)
{
    m_defaultAutotileAlgorithmProvider = std::move(provider);
}

void LayoutRegistry::setTiledWindowCountProvider(
    std::function<std::optional<int>(const QString& screenId, int virtualDesktop, const QString& activity)> provider)
{
    m_tiledWindowCountProvider = std::move(provider);
}

void LayoutRegistry::setScreenOrientationProvider(
    std::function<std::optional<QString>(const QString& screenId)> provider)
{
    m_screenOrientationProvider = std::move(provider);
}

void LayoutRegistry::setSnappingPreferredProvider(std::function<bool()> provider)
{
    m_snappingPreferredProvider = std::move(provider);
}

void LayoutRegistry::setDefaultAssignmentSuppressedProvider(std::function<bool()> provider)
{
    m_defaultAssignmentSuppressedProvider = std::move(provider);
}

bool LayoutRegistry::snappingPreferred() const
{
    return m_snappingPreferredProvider && m_snappingPreferredProvider();
}

AssignmentEntry LayoutRegistry::resolveDefaultAssignmentEntry() const
{
    // Global suppress gate. When the user has opted to suppress the synthesized
    // default assignment, no unassigned context gets a level-1 default — the
    // result is a default-constructed (invalid) entry whose activeLayoutId() is
    // empty, the SAME effective state as a system with no default providers
    // configured. The daemon's existing "empty entry ⇒ no default, no active
    // engine" handling therefore covers it with no further change. The
    // per-context "allow" override bypasses this by calling the raw sibling
    // directly (see resolveDefaultAssignmentEntryForContext).
    if (m_defaultAssignmentSuppressedProvider && m_defaultAssignmentSuppressedProvider()) {
        return AssignmentEntry{};
    }
    return resolveDefaultAssignmentEntryRaw();
}

AssignmentEntry LayoutRegistry::resolveDefaultAssignmentEntryRaw() const
{
    // Level-1 global default. Resolution order:
    //
    //   (a) If the explicit "snapping is preferred" provider says yes,
    //       return Snapping with whatever the snap default-id provider
    //       has (possibly empty). This stops the cascade from silently
    //       falling through to autotile when the user has snapping
    //       enabled but no global default snap layout configured —
    //       a common state where the user expects per-screen snap
    //       assignments to drive everything.
    //
    //   (b) Else if the snap-id provider has a non-empty default,
    //       return Snapping with that id. Legacy behaviour for
    //       composition roots that haven't wired the snapping-
    //       preferred provider.
    //
    //   (c) Else if the autotile-algorithm provider has a non-empty
    //       value, return Autotile with that algorithm.
    //
    //   (d) Else return a default-constructed (invalid) entry.
    //
    // Provider returns are surfaced raw — neither the snap UUID nor
    // the autotile algorithm id is validated against the registry /
    // algorithm registry here. A stale UUID (settings still references
    // a deleted layout) or an unknown algorithm id (settings predates
    // a renamed/removed algorithm) falls through to the caller, where
    // the KCM/UI is expected to surface "stale default" UX. Adding
    // membership validation here would silently swallow that signal —
    // see testLevel1Default_snapWithUnknownUuid_layoutForScreenFallsThrough.
    if (snappingPreferred()) {
        AssignmentEntry e;
        e.mode = AssignmentEntry::Snapping;
        if (m_defaultLayoutIdProvider) {
            e.snappingLayout = m_defaultLayoutIdProvider();
        }
        qCDebug(lcZonesLib) << "resolveDefaultAssignmentEntry: snapping-preferred branch — snap id ="
                            << (e.snappingLayout.isEmpty() ? QStringLiteral("(empty)") : e.snappingLayout);
        return e;
    }
    if (m_defaultLayoutIdProvider) {
        const QString id = m_defaultLayoutIdProvider();
        if (!id.isEmpty()) {
            AssignmentEntry e;
            e.mode = AssignmentEntry::Snapping;
            e.snappingLayout = id;
            return e;
        }
    }
    if (m_defaultAutotileAlgorithmProvider) {
        const QString algo = m_defaultAutotileAlgorithmProvider();
        if (!algo.isEmpty()) {
            AssignmentEntry e;
            e.mode = AssignmentEntry::Autotile;
            e.tilingAlgorithm = algo;
            return e;
        }
    }
    return AssignmentEntry{};
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
            if (PhosphorZones::Layout* layout = layoutById(QUuid::fromString(configuredId))) {
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
        resolvedScreenId = PhosphorScreens::ScreenIdentity::isConnectorName(screenId)
            ? PhosphorScreens::ScreenIdentity::idForName(screenId)
            : screenId;
    }

    // Per-output virtual desktops (#648): cycle relative to the target screen's
    // OWN desktop, not the global active one, so the visibility filter and the
    // reference-layout lookup below resolve against the same desktop the screen
    // is actually showing. Falls back to the global desktop when no screen is
    // known (empty screenId) or no per-output value is on record.
    const int desktop =
        resolvedScreenId.isEmpty() ? m_currentVirtualDesktop : currentVirtualDesktopForScreen(resolvedScreenId);

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
        if (desktop > 0 && !l->allowedDesktops().isEmpty()) {
            if (!l->allowedDesktops().contains(desktop)) {
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
        currentLayout = layoutForScreen(resolvedScreenId, desktop, m_currentActivity);
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
        // Per-output virtual desktops (#648): write to the screen's OWN desktop,
        // not the global active one. Both callers (cycleLayoutImpl and the D-Bus
        // applyQuickLayout via LayoutAdaptor) pass an already idForName-resolved
        // screenId, matching the per-output map's key.
        const int desktop = currentVirtualDesktopForScreen(screenId);
        // Write per-desktop assignment with empty activity so it applies
        // regardless of which activity is active. Activity-specific
        // overrides are a separate KCM-only feature. Clear any stale
        // activity-keyed entry that would shadow this one in the cascade.
        if (!m_currentActivity.isEmpty()) {
            clearAssignment(screenId, desktop, m_currentActivity);
        }
        assignLayout(screenId, desktop, QString(), layout);
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

    // Drop the layout's settings sidecar entry. For a user override being
    // deleted to restore the system original, this is also what we want — the
    // restored system layout should not inherit the user's custom settings.
    m_layoutSettings.removeLayout(layoutIdStr);
    if (!m_layoutSettings.saveToFile(layoutSettingsFilePath())) {
        qCWarning(lcZonesLib) << "Failed to persist layout settings sidecar after removing" << layoutIdStr;
    }

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

    disconnect(layout, &PhosphorZones::Layout::layoutModified, this, nullptr);
    layout->deleteLater();
    Q_EMIT layoutRemoved(layout);

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
        // Truly deleted — clean up rules and shortcuts referencing this layout.
        // A context rule references the layout iff its SetSnappingLayout action
        // carries this layout's UUID string. The rule is NOT blanket-deleted:
        // an Autotile-mode context rule can carry a stale SetSnappingLayout
        // (the mode-toggle losslessness invariant), so dropping the whole rule
        // would lose its SetEngineMode + SetTilingAlgorithm autotile intent.
        // purgeSnappingLayoutFromAssignments rebuilds each affected rule with
        // only the snapping layout cleared, dropping a rule only when nothing
        // meaningful remains.
        purgeSnappingLayoutFromAssignments(layoutIdStr);

        // A deleted manual layout's UUID only ever lives in the Snapping
        // slots, but prune both modes defensively so a stale binding can
        // never resurrect a deleted layout.
        bool shortcutRemoved = false;
        for (auto& slots : m_quickLayoutSlots) {
            for (auto it = slots.begin(); it != slots.end();) {
                if (it.value() == layoutIdStr) {
                    it = slots.erase(it);
                    shortcutRemoved = true;
                } else {
                    ++it;
                }
            }
        }
        if (shortcutRemoved) {
            writeQuickLayouts();
        }

        if (wasActive) {
            setActiveLayout(defaultLayout());
        }
    }

    Q_EMIT layoutsChanged();
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

PhosphorZones::Layout* LayoutRegistry::layoutForShortcut(AssignmentEntry::Mode mode, int number) const
{
    const auto& slots = m_quickLayoutSlots[modeIndex(mode)];
    if (slots.contains(number)) {
        const QString& id = slots[number];
        if (PhosphorLayout::LayoutId::isAutotile(id))
            return nullptr;
        return layoutById(QUuid::fromString(id));
    }
    return nullptr;
}

void LayoutRegistry::applyQuickLayout(AssignmentEntry::Mode mode, int number, const QString& screenId)
{
    qCInfo(lcZonesLib) << "applyQuickLayout: mode=" << mode << "number=" << number << "screen=" << screenId;

    // Quick-slot shortcuts are explicit bindings. If the user cleared the
    // slot (setQuickLayoutSlot(mode, n, "")), pressing the shortcut must be a
    // no-op — falling back to m_layouts.at(number-1) silently resurrects
    // a layout the user deliberately unbound.
    //
    // Only manual (Snapping) slots can be applied here: an autotile slot
    // resolves to an algorithm ID with no Layout*, and switching algorithms
    // needs the autotile engine, which lives in the daemon. The daemon's
    // shortcut handler applies autotile slots directly; see daemon/start.cpp.
    auto layout = layoutForShortcut(mode, number);
    if (!layout) {
        qCInfo(lcZonesLib) << "Quick slot" << number << "is unset or not directly applicable — no-op";
        return;
    }

    qCDebug(lcZonesLib) << "Found layout for shortcut" << number << ":" << layout->name();
    applyLayoutToScreen(screenId, layout);
}

void LayoutRegistry::setQuickLayoutSlot(AssignmentEntry::Mode mode, int number, const QString& layoutId)
{
    if (number < 1 || number > 9) {
        qCWarning(lcZonesLib) << "Invalid quick layout slot number:" << number << "(must be 1-9)";
        return;
    }

    auto& slots = m_quickLayoutSlots[modeIndex(mode)];

    if (layoutId.isEmpty()) {
        // Clear the slot
        slots.remove(number);
        qCInfo(lcZonesLib) << "Cleared quick layout slot" << number << "mode=" << mode;
    } else if (PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        // Autotile IDs have no corresponding Layout* — accept as-is.
        slots[number] = layoutId;
        qCInfo(lcZonesLib) << "Assigned autotile layout" << layoutId << "to quick slot" << number;
    } else {
        // Reject non-UUID garbage up front so a bogus string doesn't
        // silently slip past layoutById (which would just return null
        // for a null-parsed UUID and surface as "non-existent layout"
        // — technically correct but misleading about the real cause).
        const QUuid parsed = QUuid::fromString(layoutId);
        if (parsed.isNull()) {
            qCWarning(lcZonesLib) << "Rejecting malformed layout id for quick slot:" << layoutId;
            return;
        }
        if (!layoutById(parsed)) {
            qCWarning(lcZonesLib) << "Cannot assign non-existent layout to quick slot:" << layoutId;
            return;
        }
        slots[number] = layoutId;
        qCInfo(lcZonesLib) << "Assigned layout" << layoutId << "to quick slot" << number;
    }

    // Save changes
    writeQuickLayouts();
}

void LayoutRegistry::setAllQuickLayoutSlots(AssignmentEntry::Mode mode, const QHash<int, QString>& slots)
{
    auto& target = m_quickLayoutSlots[modeIndex(mode)];

    // Clear all existing slots for this mode first
    target.clear();

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

        if (!PhosphorLayout::LayoutId::isAutotile(layoutId)) {
            // See setQuickLayoutSlot for the two-step parse/lookup
            // rationale — catches malformed UUID strings separately
            // from lookup-miss for clearer diagnostics.
            const QUuid parsed = QUuid::fromString(layoutId);
            if (parsed.isNull()) {
                qCWarning(lcZonesLib) << "Skipping malformed layout id for quick slot" << number << ":" << layoutId;
                continue;
            }
            if (!layoutById(parsed)) {
                qCWarning(lcZonesLib) << "Skipping non-existent layout for quick slot" << number << ":" << layoutId;
                continue;
            }
        }

        target[number] = layoutId;
        qCDebug(lcZonesLib) << "Batch: assigned layout" << layoutId << "to quick slot" << number << "mode=" << mode;
    }

    // Save once at the end
    writeQuickLayouts();
    qCInfo(lcZonesLib) << "Batch set" << target.size() << "quick layout slots for mode=" << mode;
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
