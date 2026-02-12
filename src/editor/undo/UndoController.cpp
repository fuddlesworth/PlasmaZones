// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UndoController.h"
#include <KLocalizedString>

namespace PlasmaZones {

UndoController::UndoController(QObject* parent)
    : QObject(parent)
    , m_undoStack(new QUndoStack(this))
{
    // Connect to QUndoStack signals to update properties
    connect(m_undoStack, &QUndoStack::indexChanged, this, &UndoController::onIndexChanged);
    connect(m_undoStack, &QUndoStack::cleanChanged, this, &UndoController::onCleanChanged);

    // Initial property update
    updateProperties();
}

bool UndoController::canUndo() const
{
    return m_canUndo;
}

bool UndoController::canRedo() const
{
    return m_canRedo;
}

QString UndoController::undoText() const
{
    return m_undoText;
}

QString UndoController::redoText() const
{
    return m_redoText;
}

void UndoController::undo()
{
    if (m_undoStack && m_undoStack->canUndo()) {
        m_undoStack->undo();
    }
}

void UndoController::redo()
{
    if (m_undoStack && m_undoStack->canRedo()) {
        m_undoStack->redo();
    }
}

void UndoController::clear()
{
    if (m_undoStack) {
        m_undoStack->clear();
    }
}

void UndoController::setClean()
{
    if (m_undoStack) {
        m_undoStack->setClean();
    }
}

bool UndoController::isClean() const
{
    return m_undoStack ? m_undoStack->isClean() : true;
}

void UndoController::push(QUndoCommand* command)
{
    if (m_undoStack && command) {
        m_undoStack->push(command);
    }
}

void UndoController::beginMacro(const QString& text)
{
    if (m_undoStack) {
        m_undoStack->beginMacro(text);
    }
}

void UndoController::endMacro()
{
    if (m_undoStack) {
        m_undoStack->endMacro();
    }
}

void UndoController::onIndexChanged(int idx)
{
    Q_UNUSED(idx);
    updateProperties();
}

void UndoController::onCleanChanged(bool clean)
{
    Q_UNUSED(clean);
    updateProperties();
}

void UndoController::updateProperties()
{
    if (!m_undoStack) {
        return;
    }

    // Update cached values
    bool newCanUndo = m_undoStack->canUndo();
    bool newCanRedo = m_undoStack->canRedo();
    QString newUndoText = m_undoStack->undoText();
    QString newRedoText = m_undoStack->redoText();

    // Emit signals only when values change
    if (m_canUndo != newCanUndo) {
        m_canUndo = newCanUndo;
        Q_EMIT canUndoChanged();
    }

    if (m_canRedo != newCanRedo) {
        m_canRedo = newCanRedo;
        Q_EMIT canRedoChanged();
    }

    if (m_undoText != newUndoText) {
        m_undoText = newUndoText;
        Q_EMIT undoTextChanged();
    }

    if (m_redoText != newRedoText) {
        m_redoText = newRedoText;
        Q_EMIT redoTextChanged();
    }
}

} // namespace PlasmaZones
