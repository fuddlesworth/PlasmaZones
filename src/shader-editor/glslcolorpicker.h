// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KTextEditor/InlineNoteProvider>
#include <QHash>
#include <QRegularExpression>

namespace KTextEditor {
class Document;
class View;
}

namespace PlasmaZones {

/**
 * Inline color swatch provider for GLSL shaders.
 *
 * Detects vec3(r,g,b), vec4(r,g,b,a), and #hexcolor patterns in shader code
 * and renders a small color preview square inline. Click to open a color picker.
 */
class GlslColorPicker : public KTextEditor::InlineNoteProvider
{
    Q_OBJECT

public:
    explicit GlslColorPicker(KTextEditor::Document* doc, KTextEditor::View* view);

    QList<int> inlineNotes(int line) const override;
    QSize inlineNoteSize(const KTextEditor::InlineNote& note) const override;
    void paintInlineNote(const KTextEditor::InlineNote& note, QPainter& painter,
                         Qt::LayoutDirection direction) const override;
    void inlineNoteActivated(const KTextEditor::InlineNote& note, Qt::MouseButtons buttons,
                             const QPoint& globalPos) override;

private:
    struct ColorMatch {
        int column;     // position in line (end of the color expression)
        QColor color;
        int startCol;   // start of the color expression
        int endCol;     // end of the color expression
    };

    QList<ColorMatch> findColors(int line) const;

    KTextEditor::Document* m_doc;
    KTextEditor::View* m_view;
    static const QRegularExpression s_vec3Pattern;
    static const QRegularExpression s_vec4Pattern;
    static const QRegularExpression s_hexPattern;
};

} // namespace PlasmaZones
