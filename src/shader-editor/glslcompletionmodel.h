// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KTextEditor/CodeCompletionModel>
#include <KTextEditor/CodeCompletionModelControllerInterface>
#include <QIcon>

namespace PlasmaZones {

/**
 * Code completion model for PlasmaZones GLSL shaders.
 *
 * Provides completions for:
 * - UBO fields (customParams, customColors, zoneRects, etc.)
 * - Audio functions (getBass, audioBarSmooth, etc.)
 * - Common helpers (noise2D, sdRoundedBox, compositeLabels, etc.)
 * - Multipass helpers (channelUv, iChannel0-3)
 * - Wallpaper/texture uniforms
 */
class GlslCompletionModel : public KTextEditor::CodeCompletionModel,
                             public KTextEditor::CodeCompletionModelControllerInterface
{
    Q_OBJECT
    Q_INTERFACES(KTextEditor::CodeCompletionModelControllerInterface)

public:
    explicit GlslCompletionModel(QObject* parent = nullptr);

    void completionInvoked(KTextEditor::View* view, const KTextEditor::Range& range,
                           InvocationType invocationType) override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    KTextEditor::Range completionRange(KTextEditor::View* view, const KTextEditor::Cursor& position) override;
    bool shouldAbortCompletion(KTextEditor::View* view, const KTextEditor::Range& range,
                               const QString& currentCompletion) override;

private:
    struct CompletionItem {
        QString name;
        QString detail;     // type/signature shown after name
        QString prefix;     // return type or category
        int properties = 0; // CompletionProperties flags
    };

    void buildItems();

    QList<CompletionItem> m_allItems;
    QList<const CompletionItem*> m_filtered; // items matching current prefix
    QIcon m_funcIcon;
    QIcon m_varIcon;
    QIcon m_macroIcon;
};

} // namespace PlasmaZones
