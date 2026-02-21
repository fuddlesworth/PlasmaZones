// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QUndoCommand>

#include "CommandId.h"

namespace PlasmaZones {

class EditorController;

/**
 * @brief Command for updating per-layout gap overrides (zone padding / edge gap)
 *
 * Enables undo/redo for zone padding and edge gap changes.
 * Supports merging consecutive changes to the same gap type
 * (e.g., SpinBox value drags) into a single undo step.
 *
 * Value semantics: -1 = no override (use global default), >= 0 = per-layout override.
 */
class UpdateGapOverrideCommand : public QUndoCommand
{
public:
    enum class GapType { ZonePadding, OuterGap, OuterGapTop, OuterGapBottom, OuterGapLeft, OuterGapRight, UsePerSideOuterGap };

    explicit UpdateGapOverrideCommand(QPointer<EditorController> editorController,
                                      GapType type, int oldValue, int newValue,
                                      const QString& text = QString(),
                                      QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override { return CommandId::UpdateGapOverride; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    void applyValue(int value);

    QPointer<EditorController> m_editorController;
    GapType m_type;
    int m_oldValue;
    int m_newValue;
};

} // namespace PlasmaZones
