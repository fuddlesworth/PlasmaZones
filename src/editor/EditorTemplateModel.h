// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorZones {
class ScrollingTemplate;
}

namespace PlasmaZones {

class EditorController;

/**
 * @brief Scrolling-template edit state sub-model
 *
 * The template sibling of EditorGapsModel: owns everything a template
 * carries besides its id and name (which ride the controller's layoutId /
 * layoutName so the TopBar and save gating work unchanged). QML reads it as
 * `editorController.scrollingTemplate`. Every mutation pushes an
 * UpdateTemplateCommand through the controller's undo stack and marks the
 * controller dirty; the whole edit state round-trips as one snapshot map
 * because a template is a handful of scalars plus a list capped at 16
 * entries.
 *
 * Columns are maps with "width" (work-area fraction 0..1) and "display"
 * (0 Stacked, 1 Tabbed) — the ScrollingTemplateColumn wire spellings. The
 * defaults mirror the engine's DefaultWidthKind vocabulary (0 Proportion,
 * 1 Fixed px, 2 ClientDecides, 3 Preset), exported via scrollingConstants().
 */
class EditorTemplateModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(int defaultWidthKind READ defaultWidthKind WRITE setDefaultWidthKind NOTIFY defaultsChanged)
    Q_PROPERTY(qreal defaultWidthValue READ defaultWidthValue WRITE setDefaultWidthValue NOTIFY defaultsChanged)
    Q_PROPERTY(int defaultPresetIndex READ defaultPresetIndex WRITE setDefaultPresetIndex NOTIFY defaultsChanged)
    Q_PROPERTY(int defaultDisplay READ defaultDisplay WRITE setDefaultDisplay NOTIFY defaultsChanged)
    Q_PROPERTY(QVariantList presetWidths READ presetWidths WRITE setPresetWidths NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList presetHeights READ presetHeights WRITE setPresetHeights NOTIFY presetsChanged)
    /// True when the loaded template came from a read-only system location.
    /// Saving stores a shadowing user copy (the store's contract), so the
    /// editor stays fully editable — the UI only surfaces a note.
    Q_PROPERTY(bool isSystem READ isSystem NOTIFY isSystemChanged)

public:
    EditorTemplateModel(EditorController* controller, QObject* parent);

    QString description() const
    {
        return m_description;
    }
    QVariantList columns() const
    {
        return m_columns;
    }
    int defaultWidthKind() const
    {
        return m_defaultWidthKind;
    }
    qreal defaultWidthValue() const
    {
        return m_defaultWidthValue;
    }
    int defaultPresetIndex() const
    {
        return m_defaultPresetIndex;
    }
    int defaultDisplay() const
    {
        return m_defaultDisplay;
    }
    QVariantList presetWidths() const
    {
        return m_presetWidths;
    }
    QVariantList presetHeights() const
    {
        return m_presetHeights;
    }
    bool isSystem() const
    {
        return m_isSystem;
    }

    // Setters (each pushes an undo command when the value changes)
    void setDescription(const QString& description);
    void setDefaultWidthKind(int kind);
    void setDefaultWidthValue(qreal value);
    void setDefaultPresetIndex(int index);
    void setDefaultDisplay(int display);
    void setPresetWidths(const QVariantList& widths);
    void setPresetHeights(const QVariantList& heights);

    // Blueprint column operations (QML-invokable, all undoable)
    Q_INVOKABLE void addColumn();
    Q_INVOKABLE void removeColumn(int index);
    Q_INVOKABLE void moveColumn(int from, int to);
    /**
     * @brief Set one blueprint column's width fraction.
     * @param interactive true during a drag: consecutive updates within the
     * same gesture (see beginWidthDrag) merge into one undo entry.
     */
    Q_INVOKABLE void setColumnWidth(int index, qreal width, bool interactive = false);
    Q_INVOKABLE void setColumnDisplay(int index, int display);
    /// Start a new width-drag gesture. Bumps the merge token so the next
    /// interactive width update opens a fresh undo entry instead of merging
    /// into the previous drag's.
    Q_INVOKABLE void beginWidthDrag();

    /**
     * @brief The template-authoring bounds and vocabularies the QML binds.
     *
     * The editor-process sibling of SettingsController::scrollingConstants(),
     * trimmed to the keys the template canvas and panel read. Same key
     * spellings so the settings dialog's form logic ported across unchanged.
     */
    Q_INVOKABLE QVariantMap scrollingConstants() const;

    /// Apply a full edit state (columns, description, defaults, presets)
    /// without creating a command. Undo/redo entry point.
    void applyState(const QVariantMap& state);
    /// Snapshot of the same state applyState consumes.
    QVariantMap snapshot() const;
    /// Load/create entry point: set every field and emit unconditionally so
    /// QML rebuilds against the new template, mirroring loadLayout's zones.
    void resetState(const QVariantMap& state, bool isSystem);
    /// Flip isSystem after a save turned the loaded system template into a
    /// shadowing user copy.
    void clearSystemFlag();
    /// The wire payload the daemon's saveScrollingTemplate parses, in the
    /// ScrollingTemplate::fromJson shape.
    QJsonObject toWirePayload(const QString& id, const QString& name) const;
    /// Convert a ScrollingTemplate's field values into the snapshot shape
    /// resetState/applyState consume. Static so the load path can build a
    /// state without touching a live model; the snapshot key spellings stay
    /// private to this class's translation unit.
    static QVariantMap stateFromTemplate(const PhosphorZones::ScrollingTemplate& templ);

Q_SIGNALS:
    void descriptionChanged();
    void columnsChanged();
    /// Shared notify for the four later-columns default properties.
    void defaultsChanged();
    /// Shared notify for the two preset vocabularies.
    void presetsChanged();
    void isSystemChanged();

private:
    /// Push an UpdateTemplateCommand transitioning to @p newState.
    void pushCommand(const QString& label, const QVariantMap& newState, const QString& mergeKey = QString());

    EditorController* m_controller; ///< Non-owning parent; undo stack + dirty flag.
    QString m_description;
    QVariantList m_columns; // maps: {"width": qreal, "display": int}
    int m_defaultWidthKind = 3; // DefaultWidthKindPreset
    qreal m_defaultWidthValue = 0.5;
    int m_defaultPresetIndex = 1;
    int m_defaultDisplay = 0;
    QVariantList m_presetWidths;
    QVariantList m_presetHeights;
    bool m_isSystem = false;
    /// Merge token for interactive width drags: bumped by beginWidthDrag so
    /// each drag gesture coalesces into one undo entry without bleeding into
    /// the next gesture's.
    int m_gesture = 0;
};

} // namespace PlasmaZones
