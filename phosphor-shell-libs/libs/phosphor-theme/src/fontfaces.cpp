// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "PhosphorTheme/FontFaces.h"

#include "phosphortheme_logging.h"

#include <QFontDatabase>
#include <QGuiApplication>

namespace PhosphorTheme {

FontFaces::FontFaces(QObject* parent)
    : QObject(parent)
{
    refresh();
}

QString FontFaces::ui() const
{
    return m_ui;
}

QString FontFaces::mono() const
{
    return m_mono;
}

bool FontFaces::uiPreferred() const
{
    return m_uiPreferred;
}

bool FontFaces::monoPreferred() const
{
    return m_monoPreferred;
}

QStringList FontFaces::uiCandidates()
{
    return {QStringLiteral("Manrope"), QStringLiteral("Inter"), QStringLiteral("Noto Sans")};
}

QStringList FontFaces::monoCandidates()
{
    return {QStringLiteral("JetBrains Mono"), QStringLiteral("Fira Code"), QStringLiteral("Noto Sans Mono"),
            QStringLiteral("DejaVu Sans Mono")};
}

QString FontFaces::resolve(const QStringList& candidates, const QString& fallback)
{
    for (const QString& family : candidates) {
        if (QFontDatabase::hasFamily(family)) {
            return family;
        }
    }
    return fallback;
}

void FontFaces::refresh()
{
    // The system faces close each list: the user's UI font for text, the
    // platform's fixed face for values. Both exist by definition.
    const QString systemUi = QGuiApplication::font().family();
    const QString systemMono = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();

    const QStringList uiList = uiCandidates();
    const QStringList monoList = monoCandidates();
    const QString ui = resolve(uiList, systemUi);
    const QString mono = resolve(monoList, systemMono);
    const bool uiPreferred = ui == uiList.first();
    const bool monoPreferred = mono == monoList.first();

    if (ui == m_ui && mono == m_mono && uiPreferred == m_uiPreferred && monoPreferred == m_monoPreferred) {
        return;
    }
    m_ui = ui;
    m_mono = mono;
    m_uiPreferred = uiPreferred;
    m_monoPreferred = monoPreferred;
    if (!uiPreferred || !monoPreferred) {
        qCInfo(lcPhosphorTheme) << "shell faces: ui" << m_ui << "mono" << m_mono
                                << "(install Manrope and JetBrains Mono for the identity's faces)";
    }
    Q_EMIT facesChanged();
}

} // namespace PhosphorTheme
