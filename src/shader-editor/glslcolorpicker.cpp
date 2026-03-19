// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "glslcolorpicker.h"

#include <QColorDialog>
#include <QPainter>

#include <KTextEditor/Document>
#include <KTextEditor/InlineNote>
#include <KTextEditor/View>

namespace PlasmaZones {

// Match vec3(0.5, 0.2, 0.8) or vec3(1.0) (single-component shorthand)
const QRegularExpression GlslColorPicker::s_vec3Pattern(
    QStringLiteral("\\bvec3\\s*\\(\\s*"
                   "([0-9]*\\.?[0-9]+)\\s*"                    // r
                   "(?:,\\s*([0-9]*\\.?[0-9]+)\\s*"            // g (optional)
                   ",\\s*([0-9]*\\.?[0-9]+)\\s*)?"             // b (optional)
                   "\\)"));

// Match vec4(0.5, 0.2, 0.8, 1.0)
const QRegularExpression GlslColorPicker::s_vec4Pattern(
    QStringLiteral("\\bvec4\\s*\\(\\s*"
                   "([0-9]*\\.?[0-9]+)\\s*,\\s*"               // r
                   "([0-9]*\\.?[0-9]+)\\s*,\\s*"               // g
                   "([0-9]*\\.?[0-9]+)\\s*,\\s*"               // b
                   "([0-9]*\\.?[0-9]+)\\s*"                    // a
                   "\\)"));

// Match #rrggbb or #rrggbbaa in strings/comments
const QRegularExpression GlslColorPicker::s_hexPattern(
    QStringLiteral("(#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?)"));

GlslColorPicker::GlslColorPicker(KTextEditor::Document* doc, KTextEditor::View* view)
    : KTextEditor::InlineNoteProvider()
    , m_doc(doc)
    , m_view(view)
{
    // Re-scan when text changes
    connect(doc, &KTextEditor::Document::textChanged, this, &InlineNoteProvider::inlineNotesReset);
}

QList<GlslColorPicker::ColorMatch> GlslColorPicker::findColors(int line) const
{
    QList<ColorMatch> results;
    if (!m_doc || line < 0 || line >= m_doc->lines()) return results;

    const QString text = m_doc->line(line);

    // Helper: check if all values are in 0.0-1.0 range (color-plausible)
    auto isColorRange = [](float v) { return v >= 0.0f && v <= 1.0f; };

    // vec3(r, g, b) or vec3(v) (grayscale) — only if all components are 0.0-1.0
    auto it3 = s_vec3Pattern.globalMatch(text);
    while (it3.hasNext()) {
        const auto m = it3.next();
        const float r = m.captured(1).toFloat();
        const float g = m.captured(2).isEmpty() ? r : m.captured(2).toFloat();
        const float b = m.captured(3).isEmpty() ? r : m.captured(3).toFloat();
        if (!isColorRange(r) || !isColorRange(g) || !isColorRange(b)) continue;
        results.append({
            static_cast<int>(m.capturedEnd()),
            QColor::fromRgbF(r, g, b),
            static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedEnd())
        });
    }

    // vec4(r, g, b, a) — only if all components are 0.0-1.0
    auto it4 = s_vec4Pattern.globalMatch(text);
    while (it4.hasNext()) {
        const auto m = it4.next();
        const float r = m.captured(1).toFloat();
        const float g = m.captured(2).toFloat();
        const float b = m.captured(3).toFloat();
        const float a = m.captured(4).toFloat();
        if (!isColorRange(r) || !isColorRange(g) || !isColorRange(b) || !isColorRange(a)) continue;
        results.append({
            static_cast<int>(m.capturedEnd()),
            QColor::fromRgbF(r, g, b, a),
            static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedEnd())
        });
    }

    // #rrggbb / #rrggbbaa
    auto itH = s_hexPattern.globalMatch(text);
    while (itH.hasNext()) {
        const auto m = itH.next();
        QColor color(m.captured(1));
        if (color.isValid()) {
            results.append({static_cast<int>(m.capturedEnd()), color,
                            static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedEnd())});
        }
    }

    return results;
}

QList<int> GlslColorPicker::inlineNotes(int line) const
{
    QList<int> columns;
    const auto matches = findColors(line);
    for (const auto& m : matches) {
        columns.append(m.column);
    }
    return columns;
}

QSize GlslColorPicker::inlineNoteSize(const KTextEditor::InlineNote& note) const
{
    const int h = note.lineHeight();
    return QSize(h, h); // square swatch
}

void GlslColorPicker::paintInlineNote(const KTextEditor::InlineNote& note, QPainter& painter,
                                       Qt::LayoutDirection) const
{
    const auto matches = findColors(note.position().line());
    const int idx = note.index();
    if (idx < 0 || idx >= matches.size()) return;

    const QColor color = matches[idx].color;
    const int h = note.lineHeight();
    const int margin = 2;
    const int size = h - 2 * margin;

    // Checkerboard background for transparency
    if (color.alpha() < 255) {
        const int sq = size / 4;
        for (int y = 0; y < size; y += sq) {
            for (int x = 0; x < size; x += sq) {
                painter.fillRect(margin + x, margin + y, sq, sq,
                    ((x / sq + y / sq) % 2 == 0) ? QColor(200, 200, 200) : QColor(255, 255, 255));
            }
        }
    }

    // Color swatch
    painter.fillRect(margin, margin, size, size, color);

    // Border
    painter.setPen(QPen(QColor(128, 128, 128, 180), 1));
    painter.drawRect(margin, margin, size - 1, size - 1);
}

void GlslColorPicker::inlineNoteActivated(const KTextEditor::InlineNote& note,
                                            Qt::MouseButtons buttons, const QPoint&)
{
    if (!(buttons & Qt::LeftButton)) return;

    const auto matches = findColors(note.position().line());
    const int idx = note.index();
    if (idx < 0 || idx >= matches.size()) return;

    const ColorMatch& match = matches[idx];
    const QColor chosen = QColorDialog::getColor(match.color, m_view, QString(),
                                                  QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) return;

    // Determine replacement text based on the original pattern
    const int line = note.position().line();
    const QString text = m_doc->line(line);
    const QString original = text.mid(match.startCol, match.endCol - match.startCol);

    QString replacement;
    if (original.startsWith(QLatin1Char('#'))) {
        // Hex color
        replacement = chosen.alpha() < 255 ? chosen.name(QColor::HexArgb) : chosen.name(QColor::HexRgb);
    } else if (original.startsWith(QLatin1String("vec4"))) {
        replacement = QStringLiteral("vec4(%1, %2, %3, %4)")
            .arg(chosen.redF(), 0, 'f', 3)
            .arg(chosen.greenF(), 0, 'f', 3)
            .arg(chosen.blueF(), 0, 'f', 3)
            .arg(chosen.alphaF(), 0, 'f', 3);
    } else {
        replacement = QStringLiteral("vec3(%1, %2, %3)")
            .arg(chosen.redF(), 0, 'f', 3)
            .arg(chosen.greenF(), 0, 'f', 3)
            .arg(chosen.blueF(), 0, 'f', 3);
    }

    m_doc->replaceText(KTextEditor::Range(line, match.startCol, line, match.endCol), replacement);
}

} // namespace PlasmaZones
