// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QUndoStack>

namespace PlasmaZones {

/**
 * @brief Manages QUndoStack and exposes undo/redo state to QML
 *
 * Provides undo/redo functionality for the layout editor.
 * Manages command history and exposes state to QML for UI updates.
 */
class UndoController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoTextChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY redoTextChanged)
    Q_PROPERTY(int undoStackDepth READ undoStackDepth NOTIFY undoStackDepthChanged)
    Q_PROPERTY(int maxUndoStackDepth READ maxUndoStackDepth WRITE setMaxUndoStackDepth NOTIFY maxUndoStackDepthChanged)

public:
    explicit UndoController(QObject* parent = nullptr);
    ~UndoController() override = default;

    // Property getters
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    int undoStackDepth() const;
    int maxUndoStackDepth() const;

    // Property setters
    void setMaxUndoStackDepth(int depth);

public Q_SLOTS:
    /**
     * @brief Undo last operation
     */
    void undo();

    /**
     * @brief Redo last undone operation
     */
    void redo();

    /**
     * @brief Clear undo stack
     */
    void clear();

    /**
     * @brief Mark stack as clean (after save)
     */
    void setClean();

    /**
     * @brief Check if stack is clean
     */
    bool isClean() const;

    /**
     * @brief Push command onto undo stack
     * @param command Command to push (QUndoStack takes ownership)
     */
    void push(QUndoCommand* command);

    /**
     * @brief Begin a command macro (groups multiple commands as single undo step)
     * @param text Description of the macro for undo menu
     */
    void beginMacro(const QString& text);

    /**
     * @brief End the current command macro
     */
    void endMacro();

Q_SIGNALS:
    void canUndoChanged();
    void canRedoChanged();
    void undoTextChanged();
    void redoTextChanged();
    void undoStackDepthChanged();
    void maxUndoStackDepthChanged();

private Q_SLOTS:
    /**
     * @brief Handle undo stack index changes
     */
    void onIndexChanged(int idx);

    /**
     * @brief Handle undo stack clean state changes
     */
    void onCleanChanged(bool clean);

private:
    /**
     * @brief Update property values and emit signals
     */
    void updateProperties();

    QUndoStack* m_undoStack = nullptr; // Owned by this (parent-based ownership)
    int m_maxUndoStackDepth = 50; // Default depth

    // Cached property values
    bool m_canUndo = false;
    bool m_canRedo = false;
    QString m_undoText;
    QString m_redoText;
    int m_undoStackDepth = 0;
};

} // namespace PlasmaZones
