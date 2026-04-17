// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PhosphorZones::Layout assignment management (per-screen, per-desktop, per-activity).
// Part of LayoutManager — split from layoutmanager.cpp for SRP.

#include "../layoutmanager.h"
#include "../constants.h"
#include "../logging.h"
#include "../utils.h"
#include "../virtualscreen.h"
#include <QFile>
#include <QJsonDocument>
#include <optional>

namespace PlasmaZones {

namespace {

// Walk the assignment cascade for a screen/desktop/activity context.
// Visitor: (const AssignmentEntry&) -> std::optional<T>
// Returns the first non-nullopt result from the visitor, or std::nullopt if
// no entry satisfied the visitor at any cascade level.
//
// Cascade order:
//   1. Exact match (screenId, virtualDesktop, activity)
//   2. Screen + desktop, any activity
//   3. Screen only (desktop=0, activity="") — the "display default"
//   4. Connector name fallback (recursive)
//   5. Virtual screen fallback — inherit from physical screen ID (recursive)
template<typename Visitor>
auto walkCascade(const QHash<LayoutAssignmentKey, AssignmentEntry>& assignments, const QString& screenId,
                 int virtualDesktop, const QString& activity, Visitor&& visitor)
    -> decltype(visitor(std::declval<const AssignmentEntry&>()))
{
    using Result = decltype(visitor(std::declval<const AssignmentEntry&>()));

    // 1. Exact match
    LayoutAssignmentKey exactKey{screenId, virtualDesktop, activity};
    if (auto it = assignments.constFind(exactKey); it != assignments.constEnd()) {
        if (auto r = visitor(it.value()))
            return r;
    }

    // 2. Screen + desktop (any activity)
    LayoutAssignmentKey desktopKey{screenId, virtualDesktop, QString()};
    if (auto it = assignments.constFind(desktopKey); it != assignments.constEnd()) {
        if (auto r = visitor(it.value()))
            return r;
    }

    // 3. Screen only (any desktop, any activity) — the "display default"
    LayoutAssignmentKey screenKey{screenId, 0, QString()};
    if (auto it = assignments.constFind(screenKey); it != assignments.constEnd()) {
        if (auto r = visitor(it.value()))
            return r;
    }

    // 4. Connector name fallback: if screenId looks like a connector name (no colons),
    // try resolving to a screen ID and looking up again.
    if (Utils::isConnectorName(screenId)) {
        QString resolved = Utils::screenIdForName(screenId);
        if (resolved != screenId) {
            return walkCascade(assignments, resolved, virtualDesktop, activity, std::forward<Visitor>(visitor));
        }
    }

    // 5. Virtual screen fallback: try physical screen ID if this is a virtual screen.
    // This lets virtual screens inherit the physical screen's snapping layout assignment
    // when no per-virtual-screen assignment exists.
    //
    // Design note: the fallback is intentionally layout-resolution-only from the caller's
    // perspective. layoutForScreen()'s visitor already guards against Autotile mode by
    // returning std::nullopt for Autotile entries — so a VS inheriting a physical screen's
    // Autotile assignment from this cascade correctly falls through to the global default
    // layout rather than activating autotile on the VS. assignmentEntryForScreen() does
    // not apply this guard, so callers that need mode isolation (e.g. AutotileEngine
    // checking whether a VS is in autotile mode) must treat an inherited Autotile entry
    // as "no explicit VS assignment" and use hasExplicitAssignment() to distinguish.
    if (VirtualScreenId::isVirtual(screenId)) {
        const QString physId = VirtualScreenId::extractPhysicalId(screenId);
        auto result = walkCascade(assignments, physId, virtualDesktop, activity, std::forward<Visitor>(visitor));
        if (result)
            return result;
    }

    return Result{};
}

} // anonymous namespace

void LayoutManager::assignLayout(const QString& screenId, int virtualDesktop, const QString& activity,
                                 PhosphorZones::Layout* layout)
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
    PhosphorZones::Layout* layout = nullptr;
    if (entry.mode == AssignmentEntry::Snapping && !entry.snappingLayout.isEmpty()) {
        layout = layoutById(QUuid::fromString(entry.snappingLayout));
    }
    Q_EMIT layoutAssigned(screenId, virtualDesktop, layout);
    saveAssignments();
}

// layoutForScreen, assignmentIdForScreen, and assignmentEntryForScreen share the
// same fallback cascade, implemented once in walkCascade() above. Each method
// supplies a visitor that decides whether to accept or cascade past each entry.

PhosphorZones::Layout* LayoutManager::layoutForScreen(const QString& screenId, int virtualDesktop,
                                                      const QString& activity) const
{
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [this](const AssignmentEntry& entry) -> std::optional<PhosphorZones::Layout*> {
                                  if (entry.mode == AssignmentEntry::Autotile)
                                      return std::nullopt;
                                  if (entry.snappingLayout.isEmpty())
                                      return std::nullopt;
                                  PhosphorZones::Layout* layout = layoutById(QUuid::fromString(entry.snappingLayout));
                                  return layout ? std::optional<PhosphorZones::Layout*>(layout) : std::nullopt;
                              });
    if (result)
        return *result;

    // No assignment: use defaultLayoutId from settings when set, else first layout (by defaultOrder)
    if (m_settings && !m_settings->defaultLayoutId().isEmpty()) {
        if (PhosphorZones::Layout* L = layoutById(QUuid::fromString(m_settings->defaultLayoutId()))) {
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
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [](const AssignmentEntry& entry) -> std::optional<QString> {
                                  QString id = entry.activeLayoutId();
                                  return id.isEmpty() ? std::nullopt : std::optional<QString>(id);
                              });
    return result.value_or(QString());
}

AssignmentEntry LayoutManager::assignmentEntryForScreen(const QString& screenId, int virtualDesktop,
                                                        const QString& activity) const
{
    auto result = walkCascade(m_assignments, screenId, virtualDesktop, activity,
                              [](const AssignmentEntry& entry) -> std::optional<AssignmentEntry> {
                                  if (entry.snappingLayout.isEmpty() && entry.tilingAlgorithm.isEmpty())
                                      return std::nullopt;
                                  return entry;
                              });
    return result.value_or(AssignmentEntry{});
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
