// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QHash>

namespace PlasmaZones {

class LayoutManager; // Concrete type needed for signal connections
class VirtualDesktopManager;
class Layout;

/**
 * @brief D-Bus adaptor for layout management operations
 *
 * Provides D-Bus interface: org.plasmazones.LayoutManager
 * Single responsibility: Layout CRUD and assignment operations
 */
class PLASMAZONES_EXPORT LayoutAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.LayoutManager")

public:
    explicit LayoutAdaptor(LayoutManager* manager, QObject* parent = nullptr);
    explicit LayoutAdaptor(LayoutManager* manager, VirtualDesktopManager* vdm, QObject* parent = nullptr);
    ~LayoutAdaptor() override = default;

    void setVirtualDesktopManager(VirtualDesktopManager* vdm);

public Q_SLOTS:
    // Layout queries
    QString getActiveLayout();
    QStringList getLayoutList();
    QString getLayout(const QString& id);

    // Layout management
    void setActiveLayout(const QString& id);
    void applyQuickLayout(int number, const QString& screenName);
    QString createLayout(const QString& name, const QString& type);
    void deleteLayout(const QString& id);
    QString duplicateLayout(const QString& id);

    // Import/Export
    QString importLayout(const QString& filePath);
    void exportLayout(const QString& layoutId, const QString& filePath);

    // Editor support
    bool updateLayout(const QString& layoutJson);
    QString createLayoutFromJson(const QString& layoutJson);

    // Editor launch
    void openEditor();
    void openEditorForScreen(const QString& screenName);
    void openEditorForLayout(const QString& layoutId);

    // Screen assignments (legacy: defaults to virtualDesktop=0 for all desktops)
    QString getLayoutForScreen(const QString& screenName);
    void assignLayoutToScreen(const QString& screenName, const QString& layoutId);
    void clearAssignment(const QString& screenName);

    // Per-virtual-desktop screen assignments
    QString getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop);
    void assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId);
    void clearAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop);
    bool hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop);

    // Virtual desktop information
    int getVirtualDesktopCount();
    QStringList getVirtualDesktopNames();
    QString getAllScreenAssignments();

    // Quick layout slots (1-9)
    QString getQuickLayoutSlot(int slotNumber);
    void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    QVariantMap getAllQuickLayoutSlots();

Q_SIGNALS:
    /**
     * @brief Emitted when the daemon has fully initialized and is ready
     * @note KCM should wait for this signal before querying layouts
     */
    void daemonReady();

    void layoutChanged(const QString& layoutJson);
    void layoutListChanged();
    void screenLayoutChanged(const QString& screenName, const QString& layoutId);
    void virtualDesktopCountChanged(int count);

    /**
     * @brief Emitted when the active layout changes (for KCM UI sync)
     * @param layoutId The ID of the newly active layout
     * @note This allows the settings panel to update its selection when
     *       the layout is changed externally (e.g., via quick layout hotkey)
     */
    void activeLayoutIdChanged(const QString& layoutId);

    /**
     * @brief Emitted when quick layout slots are modified
     * @note This allows the settings panel to refresh its quick layout slot assignments
     */
    void quickLayoutSlotsChanged();

private Q_SLOTS:
    // String-based connection slots for LayoutManager signals
    // (LayoutManager redeclares signals for Q_PROPERTY, so we use string-based connections)
    void onActiveLayoutChanged(Layout* layout);
    void onLayoutsChanged();
    void onLayoutAssigned(const QString& screen, Layout* layout);

private:
    void invalidateCache();
    void connectLayoutManagerSignals();
    void connectVirtualDesktopSignals();

    LayoutManager* m_layoutManager; // Concrete type for signal connections
    VirtualDesktopManager* m_virtualDesktopManager = nullptr;

    // JSON caching for performance
    QString m_cachedActiveLayoutJson;
    QUuid m_cachedActiveLayoutId;
    QHash<QUuid, QString> m_cachedLayoutJson; // Cache for individual layouts
};

} // namespace PlasmaZones
