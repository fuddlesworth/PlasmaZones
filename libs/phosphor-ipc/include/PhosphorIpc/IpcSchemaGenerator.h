// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/phosphoripc_export.h>

#include <QJsonObject>
#include <QString>
#include <QtCore/qtmetamacros.h>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace PhosphorIpc {

// QMetaObject → JSON Schema. Walks the target QObject's metaobject
// and emits a per-target schema document:
//
//   {
//     "target": "<name>",
//     "functions": [
//       {"name": "fn", "params": [{"name":"x","type":"integer"}], "returns": {"type":"string"}}
//     ],
//     "signals": [
//       {"name": "sigName", "params": [{"name":"y","type":"number"}]}
//     ]
//   }
//
// Type mapping (QMetaType ID → JSON Schema):
//   - Int / Long / LongLong / Short / UInt / etc.      → {"type":"integer"}
//   - Double / Float                                   → {"type":"number"}
//   - Bool                                             → {"type":"boolean"}
//   - QString                                          → {"type":"string"}
//   - QStringList                                      → {"type":"array","items":{"type":"string"}}
//   - QVariantList                                     → {"type":"array"}
//   - QVariantMap / QJsonObject                        → {"type":"object"}
//   - Void (return only)                               → "returns" omitted
//   - Unknown / custom                                 → {"description":"custom QMetaType: <name>"}
//
// The CLI (phosphorctl) uses this schema for argument validation
// before sending a call request. Server side, the schema lets
// `phosphorctl schema <target>` describe the surface without
// requiring documentation maintained out of band.
namespace IpcSchemaGenerator {

[[nodiscard]] PHOSPHORIPC_EXPORT QJsonObject schemaFor(const QString& targetName, const QObject* object);

} // namespace IpcSchemaGenerator

} // namespace PhosphorIpc
