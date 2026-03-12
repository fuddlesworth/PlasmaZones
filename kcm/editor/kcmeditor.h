// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <KConfigGroup>

namespace PlasmaZones {

/**
 * @brief Editor sub-KCM -- Layout editor keyboard shortcuts and snapping settings
 *
 * Settings are stored directly in the [Editor] group of plasmazonesrc
 * (not via the Settings class).
 */
class KCMEditor : public KQuickConfigModule
{
    Q_OBJECT

    // Editor shortcuts
    Q_PROPERTY(QString editorDuplicateShortcut READ editorDuplicateShortcut WRITE setEditorDuplicateShortcut NOTIFY
                   editorDuplicateShortcutChanged)
    Q_PROPERTY(QString editorSplitHorizontalShortcut READ editorSplitHorizontalShortcut WRITE
                   setEditorSplitHorizontalShortcut NOTIFY editorSplitHorizontalShortcutChanged)
    Q_PROPERTY(QString editorSplitVerticalShortcut READ editorSplitVerticalShortcut WRITE setEditorSplitVerticalShortcut
                   NOTIFY editorSplitVerticalShortcutChanged)
    Q_PROPERTY(
        QString editorFillShortcut READ editorFillShortcut WRITE setEditorFillShortcut NOTIFY editorFillShortcutChanged)

    // Editor snapping settings
    Q_PROPERTY(bool editorGridSnappingEnabled READ editorGridSnappingEnabled WRITE setEditorGridSnappingEnabled NOTIFY
                   editorGridSnappingEnabledChanged)
    Q_PROPERTY(bool editorEdgeSnappingEnabled READ editorEdgeSnappingEnabled WRITE setEditorEdgeSnappingEnabled NOTIFY
                   editorEdgeSnappingEnabledChanged)
    Q_PROPERTY(qreal editorSnapIntervalX READ editorSnapIntervalX WRITE setEditorSnapIntervalX NOTIFY
                   editorSnapIntervalXChanged)
    Q_PROPERTY(qreal editorSnapIntervalY READ editorSnapIntervalY WRITE setEditorSnapIntervalY NOTIFY
                   editorSnapIntervalYChanged)
    Q_PROPERTY(int editorSnapOverrideModifier READ editorSnapOverrideModifier WRITE setEditorSnapOverrideModifier NOTIFY
                   editorSnapOverrideModifierChanged)

    // Fill on drop
    Q_PROPERTY(bool fillOnDropEnabled READ fillOnDropEnabled WRITE setFillOnDropEnabled NOTIFY fillOnDropEnabledChanged)
    Q_PROPERTY(
        int fillOnDropModifier READ fillOnDropModifier WRITE setFillOnDropModifier NOTIFY fillOnDropModifierChanged)

    // Default values (CONSTANT — for reset buttons in UI)
    Q_PROPERTY(QString defaultEditorDuplicateShortcut READ defaultEditorDuplicateShortcut CONSTANT)
    Q_PROPERTY(QString defaultEditorSplitHorizontalShortcut READ defaultEditorSplitHorizontalShortcut CONSTANT)
    Q_PROPERTY(QString defaultEditorSplitVerticalShortcut READ defaultEditorSplitVerticalShortcut CONSTANT)
    Q_PROPERTY(QString defaultEditorFillShortcut READ defaultEditorFillShortcut CONSTANT)
    Q_PROPERTY(int defaultEditorSnapOverrideModifier READ defaultEditorSnapOverrideModifier CONSTANT)
    Q_PROPERTY(int defaultFillOnDropModifier READ defaultFillOnDropModifier CONSTANT)

public:
    KCMEditor(QObject* parent, const KPluginMetaData& data);
    ~KCMEditor() override = default;

    // Shortcuts
    QString editorDuplicateShortcut() const;
    void setEditorDuplicateShortcut(const QString& shortcut);
    QString editorSplitHorizontalShortcut() const;
    void setEditorSplitHorizontalShortcut(const QString& shortcut);
    QString editorSplitVerticalShortcut() const;
    void setEditorSplitVerticalShortcut(const QString& shortcut);
    QString editorFillShortcut() const;
    void setEditorFillShortcut(const QString& shortcut);

    // Snapping
    bool editorGridSnappingEnabled() const;
    void setEditorGridSnappingEnabled(bool enabled);
    bool editorEdgeSnappingEnabled() const;
    void setEditorEdgeSnappingEnabled(bool enabled);
    qreal editorSnapIntervalX() const;
    void setEditorSnapIntervalX(qreal interval);
    qreal editorSnapIntervalY() const;
    void setEditorSnapIntervalY(qreal interval);
    int editorSnapOverrideModifier() const;
    void setEditorSnapOverrideModifier(int modifier);

    // Fill on drop
    bool fillOnDropEnabled() const;
    void setFillOnDropEnabled(bool enabled);
    int fillOnDropModifier() const;
    void setFillOnDropModifier(int modifier);

    // Default values
    QString defaultEditorDuplicateShortcut() const;
    QString defaultEditorSplitHorizontalShortcut() const;
    QString defaultEditorSplitVerticalShortcut() const;
    QString defaultEditorFillShortcut() const;
    int defaultEditorSnapOverrideModifier() const;
    int defaultFillOnDropModifier() const;

    Q_INVOKABLE void resetEditorShortcuts();

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

Q_SIGNALS:
    void editorDuplicateShortcutChanged();
    void editorSplitHorizontalShortcutChanged();
    void editorSplitVerticalShortcutChanged();
    void editorFillShortcutChanged();
    void editorGridSnappingEnabledChanged();
    void editorEdgeSnappingEnabledChanged();
    void editorSnapIntervalXChanged();
    void editorSnapIntervalYChanged();
    void editorSnapOverrideModifierChanged();
    void fillOnDropEnabledChanged();
    void fillOnDropModifierChanged();

private:
    void emitAllChanged();
    static KConfigGroup editorConfigGroup();
};

} // namespace PlasmaZones
