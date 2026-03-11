// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QHash>
#include <QObject>
#include <QVariantList>

class QTimer;

namespace PlasmaZones {

class KCMPlasmaZones;
class Settings;

/**
 * @brief Manages layout CRUD, async loading, filtering, and pending states
 *
 * Owns the layout list, load timer, and pending hidden/auto-assign state.
 * Uses the back-pointer pattern to access KCM's Settings and currentScreenName().
 */
class LayoutManager : public QObject
{
    Q_OBJECT

public:
    explicit LayoutManager(KCMPlasmaZones* kcm, Settings* settings, QObject* parent = nullptr);

    // Layout list accessors
    const QVariantList& layouts() const
    {
        return m_layouts;
    }
    QString layoutToSelect() const
    {
        return m_layoutToSelect;
    }

    // CRUD operations (D-Bus to daemon)
    void createNewLayout();
    void deleteLayout(const QString& layoutId);
    void duplicateLayout(const QString& layoutId);
    void importLayout(const QString& filePath);
    void exportLayout(const QString& layoutId, const QString& filePath);
    void editLayout(const QString& layoutId);
    void openEditor();

    // Pending state staging (applied on save)
    void setLayoutHidden(const QString& layoutId, bool hidden);
    void setLayoutAutoAssign(const QString& layoutId, bool enabled);

    // Load operations
    void scheduleLoad();
    void loadAsync();
    void loadSync();
    void applyLayoutFilter();

    // Save/defaults integration
    void savePendingStates(QStringList& failedOperations);
    void resetAllToDefaults();
    void clearPendingStates();

    // Prevent reloads during save
    void setSaveInProgress(bool inProgress)
    {
        m_saveInProgress = inProgress;
    }

Q_SIGNALS:
    void layoutsChanged();
    void layoutToSelectChanged();
    void needsSave();

private:
    QDBusMessage callDaemon(const QString& interface, const QString& method, const QVariantList& args = {}) const;
    void watchAsyncDbusCall(QDBusPendingCall call, const QString& operation);

    KCMPlasmaZones* m_kcm = nullptr;
    Settings* m_settings = nullptr;

    QVariantList m_layouts;
    QVariantList m_unfilteredLayouts;
    QString m_layoutToSelect;
    bool m_initialLayoutLoadDone = false;
    QTimer* m_loadTimer = nullptr;
    int m_loadGeneration = 0;
    bool m_saveInProgress = false;

    QHash<QString, bool> m_pendingHiddenStates;
    QHash<QString, bool> m_pendingAutoAssignStates;
};

} // namespace PlasmaZones
