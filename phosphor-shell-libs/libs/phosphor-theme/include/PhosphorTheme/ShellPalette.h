// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <PhosphorTheme/phosphortheme_export.h>
#include <QColor>
#include <QList>
#include <QVariantMap>

namespace PhosphorTheme {
// Shared by QML surfaces and the native window decoration.
struct PHOSPHORTHEME_EXPORT ShellPalette
{
    QColor surface;
    QColor card;
    QColor recess;
    QColor text;
    QColor muted;
    QColor outline;
    QList<QColor> stops;
    qreal opacity = 1;
    static ShellPalette fromSettings(const QVariantMap& settings);
    QVariantMap toVariant() const;
    QColor windowColor(int index) const;
};
}
