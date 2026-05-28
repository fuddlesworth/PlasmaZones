// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcSchemaGenerator.h>

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
    default: {
        const char* name = QMetaType(metaTypeId).name();
        obj.insert(QStringLiteral("description"),
                   QStringLiteral("custom QMetaType: %1").arg(QLatin1String(name ? name : "<unknown>")));
        return obj;
    }
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
    // Start above the introspection floor that matches what
    // IpcRouter::findInvokableMethod / findSignal expose on the
    // wire. For a plain QObject-derived target, the floor is
    // QObject's own method count (filters destroyed,
    // objectNameChanged, deleteLater, etc.). For QML-instantiated
    // IpcTarget wrappers (the common case), the floor is
    // IpcTarget's method count so the wrapper's own emitEvent
    // Q_INVOKABLE and targetChanged signal don't leak into the
    // wire-visible schema; only the user's QML-declared functions
    // and signals on the IpcTarget item surface.
    const int firstUserMethod = qobject_cast<const IpcTarget*>(object) ? IpcTarget::staticMetaObject.methodCount()
                                                                       : QObject::staticMetaObject.methodCount();
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
                // description shape the param path uses (line 71),
                // surfacing the typeName so the CLI knows the
                // method returns *something* even if the type is
                // opaque on the wire. Omitting "returns" would
                // mislead callers into thinking the method is void.
                QJsonObject ret;
                const QByteArray typeName = m.typeName();
                ret.insert(QStringLiteral("description"),
                           QStringLiteral("custom QMetaType: %1")
                               .arg(QLatin1String(typeName.isEmpty() ? "<unknown>" : typeName.constData())));
                entry.insert(QStringLiteral("returns"), ret);
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
