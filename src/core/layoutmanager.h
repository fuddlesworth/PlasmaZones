// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "layout.h"
#include "interfaces.h"
#include <QHash>
#include <QUuid>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Key for layout assignment (screen + desktop + activity)
 */
struct LayoutAssignmentKey
{
    QString screenName; // Monitor/output name
    int virtualDesktop = 0; // 0 = all desktops
    QString activity; // Empty = all activities

    bool operator==(const LayoutAssignmentKey& other) const
    {
        return screenName == other.screenName && virtualDesktop == other.virtualDesktop && activity == other.activity;
    }
};

inline size_t qHash(const LayoutAssignmentKey& key, size_t seed = 0)
{
    return qHash(key.screenName, seed) ^ (static_cast<size_t>(key.virtualDesktop) << 16) ^ qHash(key.activity, seed);
}

/**
 * @brief Manages all layouts and their assignments to screens/desktops
 *
 * The LayoutManager is the central component for managing zone layouts.
 * It handles:
 * - Loading/saving layouts from disk
 * - Layout assignment to screens, virtual desktops, and activities
 * - Quick layout switching via keyboard shortcuts
 * - Built-in template layouts
 *
 * Note: This class does NOT use the singleton pattern. Create instances
 * where needed and pass via dependency injection.
 *
 * Architecture note: This class inherits from both QObject (for Qt signals/properties)
 * and ILayoutManager (for the interface contract). ILayoutManager is a pure abstract
 * interface without QObject to avoid signal shadowing issues that cause heap corruption.
 * Components needing to connect to signals should use LayoutManager* directly.
 */
class PLASMAZONES_EXPORT LayoutManager : public QObject, public ILayoutManager
{
    Q_OBJECT

    Q_PROPERTY(int layoutCount READ layoutCount NOTIFY layoutsChanged)
    Q_PROPERTY(Layout* activeLayout READ activeLayout NOTIFY activeLayoutChanged)
    Q_PROPERTY(QString layoutDirectory READ layoutDirectory WRITE setLayoutDirectory NOTIFY layoutDirectoryChanged)

public:
    explicit LayoutManager(QObject* parent = nullptr);
    ~LayoutManager();

    // No singleton - use dependency injection instead

    // ILayoutManager interface implementation
    QString layoutDirectory() const override
    {
        return m_layoutDirectory;
    }
    void setLayoutDirectory(const QString& directory) override;

    int layoutCount() const override
    {
        return m_layouts.size();
    }
    QVector<Layout*> layouts() const override
    {
        return m_layouts;
    }
    Layout* layout(int index) const override;
    Layout* layoutById(const QUuid& id) const override;
    Layout* layoutByName(const QString& name) const override;

    Q_INVOKABLE void addLayout(Layout* layout) override;
    Q_INVOKABLE void removeLayout(Layout* layout) override;
    Q_INVOKABLE void removeLayoutById(const QUuid& id) override;
    Q_INVOKABLE Layout* duplicateLayout(Layout* source) override;

    Layout* activeLayout() const override
    {
        return m_activeLayout;
    }
    Q_INVOKABLE void setActiveLayout(Layout* layout) override;
    Q_INVOKABLE void setActiveLayoutById(const QUuid& id) override;

    Q_INVOKABLE void assignLayout(const QString& screenName, int virtualDesktop, const QString& activity,
                                  Layout* layout) override;
    Q_INVOKABLE void assignLayoutById(const QString& screenName, int virtualDesktop, const QString& activity,
                                      const QUuid& layoutId) override;
    Q_INVOKABLE Layout* layoutForScreen(const QString& screenName, int virtualDesktop = 0,
                                        const QString& activity = QString()) const override;
    Q_INVOKABLE void clearAssignment(const QString& screenName, int virtualDesktop = 0,
                                     const QString& activity = QString()) override;

    /**
     * @brief Check if there's an explicit assignment (no fallback)
     * @param screenName Monitor name
     * @param virtualDesktop Desktop number (0 = all desktops)
     * @param activity Activity ID (empty = all activities)
     * @return true if explicit assignment exists for this exact key
     */
    Q_INVOKABLE bool hasExplicitAssignment(const QString& screenName, int virtualDesktop = 0,
                                           const QString& activity = QString()) const override;

    /**
     * @brief Batch set all screen assignments (base assignments, desktop=0, no activity)
     * @param assignments Map of screenName -> layoutId
     * Clears existing base assignments and sets new ones, saves once at end
     */
    void setAllScreenAssignments(const QHash<QString, QUuid>& assignments) override;

    /**
     * @brief Batch set all per-desktop assignments
     * @param assignments Map of (screenName, virtualDesktop) -> layoutId
     * Clears existing per-desktop assignments and sets new ones, saves once at end
     */
    void setAllDesktopAssignments(const QHash<QPair<QString, int>, QUuid>& assignments) override;

    /**
     * @brief Batch set all per-activity assignments
     * @param assignments Map of (screenName, activityId) -> layoutId
     * Clears existing per-activity assignments and sets new ones, saves once at end
     */
    void setAllActivityAssignments(const QHash<QPair<QString, QString>, QUuid>& assignments) override;

    Q_INVOKABLE Layout* layoutForShortcut(int number) const override;
    Q_INVOKABLE void applyQuickLayout(int number, const QString& screenName) override;
    void setQuickLayoutSlot(int number, const QUuid& layoutId) override;
    void setAllQuickLayoutSlots(const QHash<int, QUuid>& slots) override;  // Batch set - saves once
    QHash<int, QUuid> quickLayoutSlots() const override
    {
        return m_quickLayoutShortcuts;
    }

    // Layout cycling
    Q_INVOKABLE void cycleToPreviousLayout(const QString& screenName);
    Q_INVOKABLE void cycleToNextLayout(const QString& screenName);

    Q_INVOKABLE void createBuiltInLayouts() override;
    QVector<Layout*> builtInLayouts() const override;

    Q_INVOKABLE void loadLayouts(const QString& defaultLayoutId = QString()) override;
    void setSettings(ISettings* settings);
    Q_INVOKABLE void saveLayouts() override;
    Q_INVOKABLE void loadAssignments() override;
    Q_INVOKABLE void saveAssignments() override;
    Q_INVOKABLE void importLayout(const QString& filePath) override;
    Q_INVOKABLE void exportLayout(Layout* layout, const QString& filePath) override;

    /**
     * @brief Get all per-desktop assignments (virtualDesktop > 0, no activity)
     * @return Map of (screenName, virtualDesktop) -> layoutId
     */
    QHash<QPair<QString, int>, QUuid> desktopAssignments() const;

    /**
     * @brief Get all per-activity assignments (activity non-empty, any desktop)
     * @return Map of (screenName, activityId) -> layoutId
     */
    QHash<QPair<QString, QString>, QUuid> activityAssignments() const;

Q_SIGNALS:
    // Signals declared here only (ILayoutManager is a pure interface without signals)
    void layoutsChanged();
    void layoutAdded(Layout* layout);
    void layoutRemoved(Layout* layout);
    void activeLayoutChanged(Layout* layout);
    void layoutAssigned(const QString& screenName, Layout* layout);
    void layoutDirectoryChanged();
    void layoutsLoaded();
    void layoutsSaved();

private:
    void ensureLayoutDirectory();
    void loadLayoutsFromDirectory(const QString& directory);
    QString layoutFilePath(const QUuid& id) const;

    ISettings* m_settings = nullptr;
    QString m_layoutDirectory;
    QVector<Layout*> m_layouts;
    Layout* m_activeLayout = nullptr;
    QHash<LayoutAssignmentKey, QUuid> m_assignments;
    QHash<int, QUuid> m_quickLayoutShortcuts; // number -> layout ID
};

} // namespace PlasmaZones
