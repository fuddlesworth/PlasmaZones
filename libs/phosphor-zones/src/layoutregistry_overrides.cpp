// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Per-algorithm autotile layout overrides — JSON sidecar I/O.
// Part of LayoutRegistry — split from layoutregistry_assignments.cpp to keep
// each translation unit under the 800-line limit and to isolate the
// autotile-overrides persistence concern (a single autotile-overrides.json
// keyed by algorithm id) from the per-context assignment cascade.

#include <PhosphorZones/LayoutRegistry.h>

#include "zoneslogging.h"

#include <QFile>
#include <QJsonDocument>

namespace PhosphorZones {

namespace {
// Filename of the per-algorithm autotile-overrides JSON sidecar, relative to
// the layout directory. Hoisted so the load and save paths share one literal.
const QString kAutotileOverridesFile = QStringLiteral("/autotile-overrides.json");
} // namespace

QJsonObject LayoutRegistry::loadAllAutotileOverrides() const
{
    QFile file(m_layoutDirectory + kAutotileOverridesFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void LayoutRegistry::saveAllAutotileOverrides(const QJsonObject& all)
{
    ensureLayoutDirectory();
    QFile file(m_layoutDirectory + kAutotileOverridesFile);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "Failed to save autotile overrides:" << file.errorString();
        return;
    }
    file.write(QJsonDocument(all).toJson());
}

QJsonObject LayoutRegistry::loadAutotileOverrides(const QString& algorithmId) const
{
    return loadAllAutotileOverrides().value(algorithmId).toObject();
}

void LayoutRegistry::saveAutotileOverrides(const QString& algorithmId, const QJsonObject& overrides)
{
    QJsonObject all = loadAllAutotileOverrides();
    if (overrides.isEmpty()) {
        all.remove(algorithmId);
    } else {
        all[algorithmId] = overrides;
    }
    saveAllAutotileOverrides(all);
}

} // namespace PhosphorZones
