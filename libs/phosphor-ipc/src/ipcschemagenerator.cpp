// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcSchemaGenerator.h>

#include "ipcrouterdetail.h"

#include <PhosphorIpc/IpcTarget.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaType>
#include <QObject>

namespace PhosphorIpc::IpcSchemaGenerator {

namespace {

// JSON Schema fragment for an unknown / unregistered QMetaType. No
// "type" constraint (the CLI accepts any value); only a description
// surfaces the C++ type name so callers can correlate the schema
// to the source. Shared between the parameter / return-type paths
// so both branches emit the identical shape if a future tweak
// (e.g. adding a "x-meta-type" extension) lands.
QJsonObject customTypeDescription(const char* typeName)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("description"),
               QStringLiteral("custom QMetaType: %1").arg(QLatin1String(typeName ? typeName : "<unknown>")));
    return obj;
}

// Map a QMetaType ID to a JSON Schema fragment. Per IpcSchemaGenerator.h's
// documented table. Unknown / custom types degrade to a
// {"description": "<typename>"} fragment without a "type" constraint;
// the CLI will accept any value and let the server decide.
QJsonObject typeToSchema(int metaTypeId)
{
    QJsonObject obj;
    switch (metaTypeId) {
    case QMetaType::Bool:
        obj.insert(QStringLiteral("type"), QStringLiteral("boolean"));
        return obj;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        obj.insert(QStringLiteral("type"), QStringLiteral("integer"));
        return obj;
    case QMetaType::Double:
    case QMetaType::Float:
        obj.insert(QStringLiteral("type"), QStringLiteral("number"));
        return obj;
    case QMetaType::QString:
        obj.insert(QStringLiteral("type"), QStringLiteral("string"));
        return obj;
    case QMetaType::QStringList: {
        obj.insert(QStringLiteral("type"), QStringLiteral("array"));
        QJsonObject items;
        items.insert(QStringLiteral("type"), QStringLiteral("string"));
        obj.insert(QStringLiteral("items"), items);
        return obj;
    }
    case QMetaType::QVariantList:
    case QMetaType::QJsonArray:
        obj.insert(QStringLiteral("type"), QStringLiteral("array"));
        return obj;
    case QMetaType::QVariantMap:
    case QMetaType::QJsonObject:
        obj.insert(QStringLiteral("type"), QStringLiteral("object"));
        return obj;
    case QMetaType::QVariant:
    case QMetaType::QJsonValue:
        // No type constraint, accept any JSON shape.
        return obj;
    case QMetaType::Void:
        // Caller (return-type path) checks this and omits the
        // "returns" field entirely.
        return obj;
    default:
        return customTypeDescription(QMetaType(metaTypeId).name());
    }
}

QJsonObject describeMethod(const QMetaMethod& m)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("name"), QString::fromUtf8(m.name()));

    QJsonArray params;
    const QList<QByteArray> paramNames = m.parameterNames();
    for (int i = 0; i < m.parameterCount(); ++i) {
        QJsonObject p = typeToSchema(m.parameterType(i));
        if (i < paramNames.size() && !paramNames.at(i).isEmpty()) {
            p.insert(QStringLiteral("name"), QString::fromUtf8(paramNames.at(i)));
        }
        params.append(p);
    }
    entry.insert(QStringLiteral("params"), params);
    return entry;
}

} // namespace

QJsonObject schemaFor(const QString& targetName, const QObject* object)
{
    QJsonObject root;
    root.insert(QStringLiteral("target"), targetName);
    QJsonArray functions;
    QJsonArray signals_;
    if (!object) {
        root.insert(QStringLiteral("functions"), functions);
        root.insert(QStringLiteral("signals"), signals_);
        return root;
    }
    const QMetaObject* meta = object->metaObject();
    // Single source of truth for the introspection floor: the
    // detail::firstUserMethodIndex helper is the same one the
    // router uses in findInvokableMethod / findSignal. Keeping the
    // schema and dispatcher in lockstep prevents the wire-leak
    // where a remote client could invoke or subscribe to a method
    // that wasn't advertised in the schema (notably
    // IpcTarget::emitEvent and IpcTarget::targetChanged, which are
    // wrapper-internal and not part of the user-declared surface).
    const int firstUserMethod = detail::firstUserMethodIndex(object);
    for (int i = firstUserMethod; i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        // Filter to Public-access methods/signals: protected/private
        // Q_INVOKABLE methods are subclass-only by convention;
        // exposing them on the wire defeats that. Matches the
        // findInvokableMethod / findSignal filter so the schema
        // never advertises what the router would refuse to dispatch.
        if (m.access() != QMetaMethod::Public) {
            continue;
        }
        if (m.methodType() == QMetaMethod::Signal) {
            signals_.append(describeMethod(m));
            continue;
        }
        // Q_INVOKABLE methods + public slots both surface here.
        if (m.methodType() == QMetaMethod::Method || m.methodType() == QMetaMethod::Slot) {
            QJsonObject entry = describeMethod(m);
            const int retType = m.returnMetaType().id();
            if (retType == QMetaType::Void) {
                // Pure void return: omit "returns" entirely.
            } else if (retType == QMetaType::UnknownType) {
                // Unregistered metatype: emit the same custom-type
                // description shape the param path uses, surfacing
                // the typeName so the CLI knows the method returns
                // *something* even if the type is opaque on the wire.
                // Omitting "returns" would mislead callers into
                // thinking the method is void.
                const QByteArray typeName = m.typeName();
                entry.insert(QStringLiteral("returns"),
                             customTypeDescription(typeName.isEmpty() ? nullptr : typeName.constData()));
            } else {
                entry.insert(QStringLiteral("returns"), typeToSchema(retType));
            }
            functions.append(entry);
        }
    }
    root.insert(QStringLiteral("functions"), functions);
    root.insert(QStringLiteral("signals"), signals_);
    return root;
}

} // namespace PhosphorIpc::IpcSchemaGenerator
