// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Central controller for the standalone settings application.
// Manages page navigation, layout CRUD (via D-Bus), screen info,
// editor config, and owns the shared Settings instance.

#pragma once

#include "../config/settings.h"
#include "../config/configbackend.h"
#include "../../kcm/common/daemoncontroller.h"
#include "../../kcm/common/screenhelper.h"
#include "../core/constants.h"

#include <QObject>
#include <QString>
#include <QDBusConnection>
#include <QVariantList>
#include <QTimer>
#include <memory>

namespace PlasmaZones {

class SettingsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString activePage READ activePage WRITE setActivePage NOTIFY activePageChanged)
    Q_PROPERTY(bool needsSave READ needsSave NOTIFY needsSaveChanged)
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonRunningChanged)
    Q_PROPERTY(Settings* settings READ settings CONSTANT)
    Q_PROPERTY(DaemonController* daemonController READ daemonController CONSTANT)

    // Layout management
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)

    // Screen management
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

    // Editor settings
    Q_PROPERTY(QString editorDuplicateShortcut READ editorDuplicateShortcut WRITE setEditorDuplicateShortcut NOTIFY
                   editorDuplicateShortcutChanged)
    Q_PROPERTY(QString editorSplitHorizontalShortcut READ editorSplitHorizontalShortcut WRITE
                   setEditorSplitHorizontalShortcut NOTIFY editorSplitHorizontalShortcutChanged)
    Q_PROPERTY(QString editorSplitVerticalShortcut READ editorSplitVerticalShortcut WRITE setEditorSplitVerticalShortcut
                   NOTIFY editorSplitVerticalShortcutChanged)
    Q_PROPERTY(
        QString editorFillShortcut READ editorFillShortcut WRITE setEditorFillShortcut NOTIFY editorFillShortcutChanged)
    Q_PROPERTY(bool editorGridSnappingEnabled READ editorGridSnappingEnabled WRITE setEditorGridSnappingEnabled NOTIFY
                   editorGridSnappingEnabledChanged)
    Q_PROPERTY(bool editorEdgeSnappingEnabled READ editorEdgeSnappingEnabled WRITE setEditorEdgeSnappingEnabled NOTIFY
                   editorEdgeSnappingEnabledChanged)
    Q_PROPERTY(qreal editorSnapIntervalX READ editorSnapIntervalX WRITE setEditorSnapIntervalX NOTIFY
                   editorSnapIntervalXChanged)
    Q_PROPERTY(qreal editorSnapIntervalY READ editorSnapIntervalY WRITE setEditorSnapIntervalY NOTIFY
                   editorSnapIntervalYChanged)

public:
    explicit SettingsController(QObject* parent = nullptr);
    ~SettingsController() override = default;

    QString activePage() const
    {
        return m_activePage;
    }
    void setActivePage(const QString& page);

    bool needsSave() const
    {
        return m_needsSave;
    }
    bool daemonRunning() const
    {
        return m_daemonController.isRunning();
    }

    Settings* settings()
    {
        return &m_settings;
    }
    DaemonController* daemonController()
    {
        return &m_daemonController;
    }

    // Layout accessors
    QVariantList layouts() const
    {
        return m_layouts;
    }

    // Screen accessors
    QVariantList screens() const
    {
        return m_screenHelper.screens();
    }

    // Layout CRUD (D-Bus to daemon)
    Q_INVOKABLE void createNewLayout();
    Q_INVOKABLE void deleteLayout(const QString& layoutId);
    Q_INVOKABLE void duplicateLayout(const QString& layoutId);
    Q_INVOKABLE void editLayout(const QString& layoutId);
    Q_INVOKABLE void openLayoutsFolder();

    // Screen helpers
    Q_INVOKABLE bool isMonitorDisabled(const QString& screenName) const;
    Q_INVOKABLE void setMonitorDisabled(const QString& screenName, bool disabled);

    // Editor settings accessors
    QString editorDuplicateShortcut() const;
    QString editorSplitHorizontalShortcut() const;
    QString editorSplitVerticalShortcut() const;
    QString editorFillShortcut() const;
    bool editorGridSnappingEnabled() const;
    bool editorEdgeSnappingEnabled() const;
    qreal editorSnapIntervalX() const;
    qreal editorSnapIntervalY() const;

    void setEditorDuplicateShortcut(const QString& shortcut);
    void setEditorSplitHorizontalShortcut(const QString& shortcut);
    void setEditorSplitVerticalShortcut(const QString& shortcut);
    void setEditorFillShortcut(const QString& shortcut);
    void setEditorGridSnappingEnabled(bool enabled);
    void setEditorEdgeSnappingEnabled(bool enabled);
    void setEditorSnapIntervalX(qreal interval);
    void setEditorSnapIntervalY(qreal interval);

    Q_INVOKABLE void resetEditorDefaults();

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void defaults();
    Q_INVOKABLE void launchEditor();

Q_SIGNALS:
    void activePageChanged();
    void needsSaveChanged();
    void daemonRunningChanged();
    void layoutsChanged();
    void screensChanged();

    // Editor signals
    void editorDuplicateShortcutChanged();
    void editorSplitHorizontalShortcutChanged();
    void editorSplitVerticalShortcutChanged();
    void editorFillShortcutChanged();
    void editorGridSnappingEnabledChanged();
    void editorEdgeSnappingEnabledChanged();
    void editorSnapIntervalXChanged();
    void editorSnapIntervalYChanged();

private Q_SLOTS:
    void onExternalSettingsChanged();
    void loadLayoutsAsync();

private:
    void setNeedsSave(bool needs);
    void scheduleLayoutLoad();
    void loadEditorSettings();
    void saveEditorSettings();

    Settings m_settings;
    DaemonController m_daemonController;
    ScreenHelper m_screenHelper;
    std::unique_ptr<IConfigBackend> m_editorConfig;
    QString m_activePage = QStringLiteral("general");
    bool m_needsSave = false;
    bool m_saving = false;

    // Layout state
    QVariantList m_layouts;
    QTimer m_layoutLoadTimer;

    // Editor settings cache
    QString m_editorDuplicateShortcut;
    QString m_editorSplitHorizontalShortcut;
    QString m_editorSplitVerticalShortcut;
    QString m_editorFillShortcut;
    bool m_editorGridSnappingEnabled = true;
    bool m_editorEdgeSnappingEnabled = true;
    qreal m_editorSnapIntervalX = 0.05;
    qreal m_editorSnapIntervalY = 0.05;
};

} // namespace PlasmaZones
