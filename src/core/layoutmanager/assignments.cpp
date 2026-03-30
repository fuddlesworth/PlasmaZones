// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Layout assignment management (per-screen, per-desktop, per-activity).
// Part of LayoutManager — split from layoutmanager.cpp for SRP.

#include "../layoutmanager.h"
#include "../constants.h"
#include "../logging.h"
#include "../utils.h"
#include <QFile>
#include <QJsonDocument>

namespace PlasmaZones {

void LayoutManager::assignLayout(const QString& screenId, int virtualDesktop, const QString& activity, Layout* layout)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};

    if (layout) {
        AssignmentEntry& entry = m_assignments[key];
        entry.mode = AssignmentEntry::Snapping;
        entry.snappingLayout = layout->id().toString();
        // Preserve existing tilingAlgorithm — only mode + snappingLayout change
        qCDebug(lcLayout) << "assignLayout: screen=" << screenId << "desktop=" << virtualDesktop
                          << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity)
                          << "layout=" << layout->name();
    } else {
        // Clearing: remove the entry entirely.
        // Skip save/signal when there's nothing to remove — avoids a redundant
        // disk write and layoutAssigned emission (e.g. clearAssignment for an
        // activity-keyed entry that doesn't exist).
        bool hadAssignment = m_assignments.remove(key);
        if (!hadAssignment) {
            return;
        }
        qCDebug(lcLayout) << "assignLayout: removed screen=" << screenId << "desktop=" << virtualDesktop
                          << "activity=" << (activity.isEmpty() ? QStringLiteral("(all)") : activity);
    }

    Q_EMIT layoutAssigned(screenId, key.virtualDesktop, layout);
    saveAssignments();
}

void LayoutManager::assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                     const QString& layoutId)
{
    if (LayoutId::isAutotile(layoutId)) {
        // Store autotile assignment — set mode to Autotile, preserve snappingLayout
        LayoutAssignmentKey key{screenId, virtualDesktop, activity};
        AssignmentEntry& entry = m_assignments[key];
        entry.mode = AssignmentEntry::Autotile;
        entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
        // Preserve existing snappingLayout — only mode + tilingAlgorithm change
        Q_EMIT layoutAssigned(screenId, virtualDesktop, nullptr);
        saveAssignments();
    } else {
        assignLayout(screenId, virtualDesktop, activity, layoutById(QUuid::fromString(layoutId)));
    }
}

void LayoutManager::setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                             const AssignmentEntry& entry)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};

    // Store the entry unconditionally — mode-only entries (empty snapping + empty tiling)
    // are valid when explicitly set by the KCM to preserve mode at a context level.
    m_assignments[key] = entry;

    qCDebug(lcLayout) << "setAssignmentEntryDirect: screen=" << screenId << "desktop=" << virtualDesktop
                      << "activity=" << activity << "mode=" << entry.mode << "snapping=" << entry.snappingLayout
                      << "tiling=" << entry.tilingAlgorithm;

    // Resolve layout for signal emission
    Layout* layout = nullptr;
    if (entry.mode == AssignmentEntry::Snapping && !entry.snappingLayout.isEmpty()) {
        layout = layoutById(QUuid::fromString(entry.snappingLayout));
    }
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
    saveAssignments();
}

Layout* LayoutManager::layoutForScreen(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Helper: resolve stored assignment to Layout* (returns nullptr for autotile mode)
    auto resolveEntry = [this](const AssignmentEntry& entry) -> Layout* {
        if (entry.mode == AssignmentEntry::Autotile)
            return nullptr;
        if (entry.snappingLayout.isEmpty())
            return nullptr;
        return layoutById(QUuid::fromString(entry.snappingLayout));
    };

    // Try exact match first — continue cascading if resolveEntry returns nullptr
    // (mode-only entries have empty snapping, so resolveEntry returns nullptr;
    // the mode is preserved but the layout cascades to the parent scope)
    LayoutAssignmentKey exactKey{screenId, virtualDesktop, activity};
    if (m_assignments.contains(exactKey)) {
        Layout* layout = resolveEntry(m_assignments[exactKey]);
        if (layout)
            return layout;
    }

    // Try screen + desktop (any activity)
    LayoutAssignmentKey desktopKey{screenId, virtualDesktop, QString()};
    if (m_assignments.contains(desktopKey)) {
        Layout* layout = resolveEntry(m_assignments[desktopKey]);
        if (layout)
            return layout;
    }

    // Try screen only (any desktop, any activity) — the "display default"
    LayoutAssignmentKey screenKey{screenId, 0, QString()};
    if (m_assignments.contains(screenKey)) {
        Layout* layout = resolveEntry(m_assignments[screenKey]);
        if (layout)
            return layout;
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

QString LayoutManager::assignmentIdForScreen(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Same fallback cascade as layoutForScreen() but returns the raw assignment string

    // Try exact match first
    LayoutAssignmentKey exactKey{screenId, virtualDesktop, activity};
    if (m_assignments.contains(exactKey)) {
        return m_assignments[exactKey].activeLayoutId();
    }

    // Try screen + desktop (any activity)
    LayoutAssignmentKey desktopKey{screenId, virtualDesktop, QString()};
    if (m_assignments.contains(desktopKey)) {
        return m_assignments[desktopKey].activeLayoutId();
    }

    // Try screen only (any desktop, any activity) — the "display default".
    // Both manual and autotile base entries inherit to desktops without
    // explicit assignments, so fresh desktops match the display default.
    LayoutAssignmentKey screenKey{screenId, 0, QString()};
    if (m_assignments.contains(screenKey)) {
        return m_assignments[screenKey].activeLayoutId();
    }

    // Fallback: if screenId looks like a connector name, try resolving to screen ID
    if (Utils::isConnectorName(screenId)) {
        QString resolved = Utils::screenIdForName(screenId);
        if (resolved != screenId) {
            return assignmentIdForScreen(resolved, virtualDesktop, activity);
        }
    }

    return QString();
}

AssignmentEntry LayoutManager::assignmentEntryForScreen(const QString& screenId, int virtualDesktop,
                                                        const QString& activity) const
{
    // Same fallback cascade as layoutForScreen()

    LayoutAssignmentKey exactKey{screenId, virtualDesktop, activity};
    if (m_assignments.contains(exactKey)) {
        return m_assignments[exactKey];
    }

    LayoutAssignmentKey desktopKey{screenId, virtualDesktop, QString()};
    if (m_assignments.contains(desktopKey)) {
        return m_assignments[desktopKey];
    }

    LayoutAssignmentKey screenKey{screenId, 0, QString()};
    if (m_assignments.contains(screenKey)) {
        return m_assignments[screenKey];
    }

    if (Utils::isConnectorName(screenId)) {
        QString resolved = Utils::screenIdForName(screenId);
        if (resolved != screenId) {
            return assignmentEntryForScreen(resolved, virtualDesktop, activity);
        }
    }

    return AssignmentEntry{};
}

AssignmentEntry::Mode LayoutManager::modeForScreen(const QString& screenId, int virtualDesktop,
                                                   const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).mode;
}

QString LayoutManager::snappingLayoutForScreen(const QString& screenId, int virtualDesktop,
                                               const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).snappingLayout;
}

QString LayoutManager::tilingAlgorithmForScreen(const QString& screenId, int virtualDesktop,
                                                const QString& activity) const
{
    return assignmentEntryForScreen(screenId, virtualDesktop, activity).tilingAlgorithm;
}

void LayoutManager::clearAutotileAssignments()
{
    // Collect autotile keys first, then modify in a second pass.
    QList<LayoutAssignmentKey> autotileKeys;
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        if (it.value().mode == AssignmentEntry::Autotile) {
            autotileKeys.append(it.key());
        }
    }

    bool changed = !autotileKeys.isEmpty();
    for (const LayoutAssignmentKey& key : autotileKeys) {
        AssignmentEntry& entry = m_assignments[key];
        // Flip mode to Snapping — preserve both snappingLayout and tilingAlgorithm
        // so re-enabling autotile can restore the previous algorithm.
        entry.mode = AssignmentEntry::Snapping;
        qCDebug(lcLayout) << "clearAutotileAssignments: flipped to Snapping for screen=" << key.screenId
                          << "desktop=" << key.virtualDesktop;
        Q_EMIT layoutAssigned(key.screenId, key.virtualDesktop, nullptr);
    }

    // Also clear autotile quick layout slots
    for (auto it = m_quickLayoutShortcuts.begin(); it != m_quickLayoutShortcuts.end();) {
        if (LayoutId::isAutotile(it.value())) {
            it = m_quickLayoutShortcuts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        saveAssignments();
        qCInfo(lcLayout) << "Cleared all autotile assignments";
    }
}

void LayoutManager::setAllScreenAssignments(const QHash<QString, QString>& assignments)
{
    // Snapshot existing base entries so fromLayoutId can preserve the "other" field
    // (e.g. batch-setting snapping layout preserves tilingAlgorithm)
    QHash<LayoutAssignmentKey, AssignmentEntry> oldBase;
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.key().virtualDesktop == 0 && it.key().activity.isEmpty()) {
            oldBase[it.key()] = it.value();
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments, tracking which screens were successfully stored
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key();
        const QString& layoutId = it.value();

        if (screenId.isEmpty()) {
            qCWarning(lcLayout) << "Skipping assignment with empty screen ID";
            continue;
        }
        if (shouldSkipLayoutAssignment(layoutId, QStringLiteral("screen ") + screenId)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, 0, QString()};
        AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldBase.value(key));
        m_assignments[key] = entry;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to screen" << screenId;
    }

    saveAssignments();

    // Emit layoutAssigned for stored entries, deduplicated by screenId.
    // Resolve at desktop=0 (the base level we just wrote) so the KCM
    // receives the actual display default — NOT the current desktop's
    // effective layout, which would corrupt its m_screenAssignments cache.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, 0, assignmentIdForScreen(screenId, 0, QString()));
    }

    qCInfo(lcLayout) << "Batch set" << count << "screen assignments";
}

void LayoutManager::setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments)
{
    // Snapshot existing per-desktop entries so fromLayoutId can preserve the "other" field
    QHash<LayoutAssignmentKey, AssignmentEntry> oldDesktop;
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.key().virtualDesktop > 0 && it.key().activity.isEmpty()) {
            oldDesktop[it.key()] = it.value();
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments, tracking which screens were successfully stored
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        int virtualDesktop = it.key().second;
        const QString& layoutId = it.value();

        if (screenId.isEmpty() || virtualDesktop < 1) {
            qCWarning(lcLayout) << "Skipping invalid desktop assignment:" << screenId << virtualDesktop;
            continue;
        }
        QString context = QStringLiteral("%1 desktop %2").arg(screenId).arg(virtualDesktop);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, virtualDesktop, QString()};
        AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldDesktop.value(key));
        m_assignments[key] = entry;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to" << screenId << "desktop" << virtualDesktop;
    }

    saveAssignments();

    // Emit once per screen (deduplicated) — use cascading resolution so the
    // signal carries the effective layout for the current context.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, m_currentVirtualDesktop,
                           assignmentIdForScreen(screenId, m_currentVirtualDesktop, m_currentActivity));
    }

    qCInfo(lcLayout) << "Batch set" << count << "desktop assignments";
}

void LayoutManager::setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments)
{
    // Snapshot existing per-activity entries so fromLayoutId can preserve the "other" field
    QHash<LayoutAssignmentKey, AssignmentEntry> oldActivity;
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (!it.key().activity.isEmpty() && it.key().virtualDesktop == 0) {
            oldActivity[it.key()] = it.value();
            it = m_assignments.erase(it);
        } else {
            ++it;
        }
    }

    // Set new assignments, tracking which screens were successfully stored
    int count = 0;
    QSet<QString> storedScreens;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenId = it.key().first;
        const QString& activityId = it.key().second;
        const QString& layoutId = it.value();

        if (screenId.isEmpty() || activityId.isEmpty()) {
            qCWarning(lcLayout) << "Skipping invalid activity assignment:" << screenId << activityId;
            continue;
        }
        QString context = QStringLiteral("%1 activity %2").arg(screenId, activityId);
        if (shouldSkipLayoutAssignment(layoutId, context)) {
            continue;
        }

        LayoutAssignmentKey key{screenId, 0, activityId};
        AssignmentEntry entry = AssignmentEntry::fromLayoutId(layoutId, oldActivity.value(key));
        m_assignments[key] = entry;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to" << screenId << "activity" << activityId;
    }

    saveAssignments();

    // Emit once per screen (deduplicated) — resolve at the base level
    // (desktop=0) so the KCM receives the display default, not the
    // current desktop's effective layout which would corrupt its cache.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, 0, assignmentIdForScreen(screenId, 0, QString()));
    }

    qCInfo(lcLayout) << "Batch set" << count << "activity assignments";
}

QHash<QPair<QString, int>, QString> LayoutManager::desktopAssignments() const
{
    QHash<QPair<QString, int>, QString> result;

    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        // Per-desktop: virtualDesktop > 0 and activity is empty
        if (key.virtualDesktop > 0 && key.activity.isEmpty()) {
            result[qMakePair(key.screenId, key.virtualDesktop)] = it.value().activeLayoutId();
        }
    }

    return result;
}

QHash<QPair<QString, QString>, QString> LayoutManager::activityAssignments() const
{
    QHash<QPair<QString, QString>, QString> result;

    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        const LayoutAssignmentKey& key = it.key();
        // Per-activity: activity is non-empty (for any desktop value)
        if (!key.activity.isEmpty()) {
            result[qMakePair(key.screenId, key.activity)] = it.value().activeLayoutId();
        }
    }

    return result;
}

// Autotile layout overrides

QJsonObject LayoutManager::loadAllAutotileOverrides() const
{
    QFile file(m_layoutDirectory + QStringLiteral("/autotile-overrides.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void LayoutManager::saveAllAutotileOverrides(const QJsonObject& all)
{
    ensureLayoutDirectory();
    QFile file(m_layoutDirectory + QStringLiteral("/autotile-overrides.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcLayout) << "Failed to save autotile overrides:" << file.errorString();
        return;
    }
    file.write(QJsonDocument(all).toJson());
}

QJsonObject LayoutManager::loadAutotileOverrides(const QString& algorithmId) const
{
    return loadAllAutotileOverrides().value(algorithmId).toObject();
}

void LayoutManager::saveAutotileOverrides(const QString& algorithmId, const QJsonObject& overrides)
{
    QJsonObject all = loadAllAutotileOverrides();
    if (overrides.isEmpty()) {
        all.remove(algorithmId);
    } else {
        all[algorithmId] = overrides;
    }
    saveAllAutotileOverrides(all);
}

} // namespace PlasmaZones
