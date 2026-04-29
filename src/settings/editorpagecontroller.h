// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

namespace PlasmaZones {

class Settings;

/// Q_PROPERTY surface for the "Editor" settings page.
///
/// Exposed to QML as a child of SettingsController via
/// `Q_PROPERTY(EditorPageController* editorPage ... CONSTANT)`, so bindings
/// read `settingsController.editorPage.duplicateShortcut` etc. All state
/// lives on the shared Settings instance — this class is a thin Q_PROPERTY
/// facade that delegates reads/writes and forwards NOTIFY signals.
///
/// Dirty tracking: each property emits a local NOTIFY (for QML) AND the
/// generic `changed()` signal, which SettingsController connects to
/// `onSettingsPropertyChanged()` so edits flip the needsSave flag on the
/// owning page. The Settings class does not expose Q_PROPERTY for the
/// [Editor] group, so SettingsController's meta-object-loop dirty wiring
/// skips it; this forwarder replaces the explicit connect() list that used
/// to sit in SettingsController's constructor.
class EditorPageController : public QObject
{
    Q_OBJECT

    // Editor keyboard shortcuts
    Q_PROPERTY(
        QString duplicateShortcut READ duplicateShortcut WRITE setDuplicateShortcut NOTIFY duplicateShortcutChanged)
    Q_PROPERTY(QString splitHorizontalShortcut READ splitHorizontalShortcut WRITE setSplitHorizontalShortcut NOTIFY
                   splitHorizontalShortcutChanged)
    Q_PROPERTY(QString splitVerticalShortcut READ splitVerticalShortcut WRITE setSplitVerticalShortcut NOTIFY
                   splitVerticalShortcutChanged)
    Q_PROPERTY(QString fillShortcut READ fillShortcut WRITE setFillShortcut NOTIFY fillShortcutChanged)

    // Editor canvas snapping
    Q_PROPERTY(bool gridSnappingEnabled READ gridSnappingEnabled WRITE setGridSnappingEnabled NOTIFY
                   gridSnappingEnabledChanged)
    Q_PROPERTY(bool edgeSnappingEnabled READ edgeSnappingEnabled WRITE setEdgeSnappingEnabled NOTIFY
                   edgeSnappingEnabledChanged)
    Q_PROPERTY(qreal snapIntervalX READ snapIntervalX WRITE setSnapIntervalX NOTIFY snapIntervalXChanged)
    Q_PROPERTY(qreal snapIntervalY READ snapIntervalY WRITE setSnapIntervalY NOTIFY snapIntervalYChanged)
    Q_PROPERTY(int snapOverrideModifier READ snapOverrideModifier WRITE setSnapOverrideModifier NOTIFY
                   snapOverrideModifierChanged)

    // Fill-on-drop (lives on the Editor page UI; stored in the shared [Editor] group)
    Q_PROPERTY(bool fillOnDropEnabled READ fillOnDropEnabled WRITE setFillOnDropEnabled NOTIFY fillOnDropEnabledChanged)
    Q_PROPERTY(
        int fillOnDropModifier READ fillOnDropModifier WRITE setFillOnDropModifier NOTIFY fillOnDropModifierChanged)

public:
    explicit EditorPageController(Settings* settings, QObject* parent = nullptr);

    QString duplicateShortcut() const;
    QString splitHorizontalShortcut() const;
    QString splitVerticalShortcut() const;
    QString fillShortcut() const;
    bool gridSnappingEnabled() const;
    bool edgeSnappingEnabled() const;
    qreal snapIntervalX() const;
    qreal snapIntervalY() const;
    int snapOverrideModifier() const;
    bool fillOnDropEnabled() const;
    int fillOnDropModifier() const;

    void setDuplicateShortcut(const QString& shortcut);
    void setSplitHorizontalShortcut(const QString& shortcut);
    void setSplitVerticalShortcut(const QString& shortcut);
    void setFillShortcut(const QString& shortcut);
    void setGridSnappingEnabled(bool enabled);
    void setEdgeSnappingEnabled(bool enabled);
    void setSnapIntervalX(qreal interval);
    void setSnapIntervalY(qreal interval);
    void setSnapOverrideModifier(int mod);
    void setFillOnDropEnabled(bool enabled);
    void setFillOnDropModifier(int mod);

    /// Restore every editor-page property to its ConfigDefaults value.
    Q_INVOKABLE void resetDefaults();

Q_SIGNALS:
    void duplicateShortcutChanged();
    void splitHorizontalShortcutChanged();
    void splitVerticalShortcutChanged();
    void fillShortcutChanged();
    void gridSnappingEnabledChanged();
    void edgeSnappingEnabledChanged();
    void snapIntervalXChanged();
    void snapIntervalYChanged();
    void snapOverrideModifierChanged();
    void fillOnDropEnabledChanged();
    void fillOnDropModifierChanged();

    /// Generic "something changed" — SettingsController hooks this to its
    /// dirty-tracking slot. Emitted alongside every per-property NOTIFY.
    void changed();

private:
    Settings* m_settings = nullptr;
};

} // namespace PlasmaZones
