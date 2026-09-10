// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// ControlAdaptor — shortcut catalog read
//
// The Phosphor shell's keybind cheatsheet draws each chord on the rect it
// acts on, so it needs the daemon's bound chords per action, uncompressed.
// ShortcutManager owns that catalog but lives in the daemon binary, not in
// the core library this adaptor ships in, so the daemon hands the read in
// as a callable and relays the manager's change signal through
// notifyShortcutsChanged. Nothing here touches placement state.
// ═══════════════════════════════════════════════════════════════════════════════

#include "controladaptor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

void ControlAdaptor::setShortcutCatalogProvider(std::function<QVariantList()> provider)
{
    m_shortcutCatalog = std::move(provider);
}

void ControlAdaptor::notifyShortcutsChanged()
{
    Q_EMIT shortcutsChanged();
}

QString ControlAdaptor::getShortcutsJson()
{
    QJsonArray rows;
    if (m_shortcutCatalog) {
        const QVariantList catalog = m_shortcutCatalog();
        for (const QVariant& row : catalog) {
            rows.append(QJsonObject::fromVariantMap(row.toMap()));
        }
    }
    return QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact));
}

} // namespace PlasmaZones
