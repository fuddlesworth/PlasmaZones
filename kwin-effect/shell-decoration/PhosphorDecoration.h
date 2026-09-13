// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <KDecoration3/Decoration>
#include <PhosphorTheme/ShellPalette.h>
#include <QVariantMap>

namespace KDecoration3 {
class DecorationButtonGroup;
}
namespace PhosphorWindow {
class Style;
class Decoration final : public KDecoration3::Decoration
{
    Q_OBJECT
public:
    Decoration(QObject* parent, const QVariantList& args);
    bool init() override;
    void paint(QPainter* painter, const QRectF& repaintArea) override;
    bool event(QEvent* event) override;
    QColor accent() const;
    bool focused() const;
    const PhosphorTheme::ShellPalette& palette() const;
    QString application() const;
    QString applicationId() const;
    void paintApplicationIcon(QPainter* painter, const QRectF& rect) const;

private:
    void refresh();
    void layoutButtons();
    Style* m_style = nullptr;
    KDecoration3::DecorationButtonGroup* m_left = nullptr;
    KDecoration3::DecorationButtonGroup* m_right = nullptr;
};
}
