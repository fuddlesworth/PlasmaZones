// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Layout assignment management (per-screen, per-desktop, per-activity).
// Part of LayoutManager — split from layoutmanager.cpp for SRP.

#include "layoutmanager.h"
#include "constants.h"
#include "logging.h"
#include "utils.h"
#include <QFile>
#include <QJsonDocument>

namespace PlasmaZones {

void LayoutManager::assignLayout(const QString& screenId, int virtualDesktop, const QString& activity, Layout* layout)
{
    LayoutAssignmentKey key{screenId, virtualDesktop, activity};

    if (layout) {
        m_assignments[key] = layout->id().toString();
    } else {
        m_assignments.remove(key);
    }

    Q_EMIT layoutAssigned(screenId, layout);
    saveAssignments();
}

void LayoutManager::assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                     const QString& layoutId)
{
    if (LayoutId::isAutotile(layoutId)) {
        // Store autotile ID directly — no Layout* exists for autotile algorithms
        LayoutAssignmentKey key{screenId, virtualDesktop, activity};
        m_assignments[key] = layoutId;
        Q_EMIT layoutAssigned(screenId, nullptr);
        saveAssignments();
    } else {
        assignLayout(screenId, virtualDesktop, activity, layoutById(QUuid::fromString(layoutId)));
    }
}

Layout* LayoutManager::layoutForScreen(const QString& screenId, int virtualDesktop, const QString& activity) const
{
    // Helper: resolve stored assignment string to Layout* (returns nullptr for autotile IDs)
    auto resolveAssignment = [this](const QString& id) -> Layout* {
        if (id.isEmpty() || LayoutId::isAutotile(id)) return nullptr;
        return layoutById(QUuid::fromString(id));
    };

    // Try exact match first
    LayoutAssignmentKey exactKey{screenId, virtualDesktop, activity};
    if (m_assignments.contains(exactKey)) {
        return resolveAssignment(m_assignments[exactKey]);
    }

    // Try screen + desktop (any activity)
    LayoutAssignmentKey desktopKey{screenId, virtualDesktop, QString()};
    if (m_assignments.contains(desktopKey)) {
        return resolveAssignment(m_assignments[desktopKey]);
    }

    // Try screen only (any desktop, any activity)
    LayoutAssignmentKey screenKey{screenId, 0, QString()};
    if (m_assignments.contains(screenKey)) {
        return resolveAssignment(m_assignments[screenKey]);
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
        return m_assignments[exactKey];
    }

    // Try screen + desktop (any activity)
    LayoutAssignmentKey desktopKey{screenId, virtualDesktop, QString()};
    if (m_assignments.contains(desktopKey)) {
        return m_assignments[desktopKey];
    }

    // Try screen only (any desktop, any activity)
    LayoutAssignmentKey screenKey{screenId, 0, QString()};
    if (m_assignments.contains(screenKey)) {
        return m_assignments[screenKey];
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

void LayoutManager::clearAutotileAssignments()
{
    bool changed = false;
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (LayoutId::isAutotile(it.value())) {
            QString screenId = it.key().screenId;
            it = m_assignments.erase(it);
            Q_EMIT layoutAssigned(screenId, nullptr);
            changed = true;
        } else {
            ++it;
        }
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
    // Clear existing base screen assignments (desktop=0, activity=empty)
    // Keep per-desktop and per-activity assignments intact
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.key().virtualDesktop == 0 && it.key().activity.isEmpty()) {
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
        m_assignments[key] = layoutId;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to screen" << screenId;
    }

    saveAssignments();

    // Emit layoutAssigned for stored entries, deduplicated by screenId.
    // Use assignmentIdForScreen (cascading resolution) so the signal carries
    // the effective layout for the current desktop/activity context.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, assignmentIdForScreen(screenId, m_currentVirtualDesktop, m_currentActivity));
    }

    qCInfo(lcLayout) << "Batch set" << count << "screen assignments";
}

void LayoutManager::setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments)
{
    // Clear existing per-desktop assignments (desktop > 0, activity empty)
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (it.key().virtualDesktop > 0 && it.key().activity.isEmpty()) {
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
        m_assignments[key] = layoutId;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to" << screenId << "desktop" << virtualDesktop;
    }

    saveAssignments();

    // Emit once per screen (deduplicated) — use cascading resolution so the
    // signal carries the effective layout for the current context.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, assignmentIdForScreen(screenId, m_currentVirtualDesktop, m_currentActivity));
    }

    qCInfo(lcLayout) << "Batch set" << count << "desktop assignments";
}

void LayoutManager::setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments)
{
    // Clear existing per-activity assignments (activity non-empty, desktop=0)
    for (auto it = m_assignments.begin(); it != m_assignments.end();) {
        if (!it.key().activity.isEmpty() && it.key().virtualDesktop == 0) {
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
        m_assignments[key] = layoutId;
        storedScreens.insert(screenId);
        ++count;
        qCDebug(lcLayout) << "Batch: assigned layout" << layoutId << "to" << screenId << "activity" << activityId;
    }

    saveAssignments();

    // Emit once per screen (deduplicated) — use cascading resolution so the
    // signal carries the effective layout for the current context.
    for (const QString& screenId : storedScreens) {
        emitLayoutAssigned(screenId, assignmentIdForScreen(screenId, m_currentVirtualDesktop, m_currentActivity));
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
            result[qMakePair(key.screenId, key.virtualDesktop)] = it.value();
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
            result[qMakePair(key.screenId, key.activity)] = it.value();
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
