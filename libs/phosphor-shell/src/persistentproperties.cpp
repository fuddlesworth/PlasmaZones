// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PersistentProperties.h>

#include <QLoggingCategory>
#include <QMetaObject>
#include <QMetaProperty>

Q_LOGGING_CATEGORY(lcPersist, "phosphorshell.persist")

namespace PhosphorShell {

namespace {

// Whitelist QMetaTypes that survive a round-trip through QVariantMap →
// JSON (the persistence backend). Anything else is silently dropped at
// save time so we never persist unserialisable handles, QObject*, etc.
bool isJsonSerializable(int typeId)
{
    switch (typeId) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::QString:
    case QMetaType::QStringList:
    case QMetaType::QVariantList:
    case QMetaType::QVariantMap:
    case QMetaType::QDate:
    case QMetaType::QTime:
    case QMetaType::QDateTime:
        return true;
    default:
        return false;
    }
}

} // namespace

PersistentProperties::PersistentProperties(QQuickItem* parent)
    : QQuickItem(parent)
{
    setVisible(false);
}

PersistentProperties::~PersistentProperties() = default;

QString PersistentProperties::reloadId() const
{
    return m_reloadId;
}

void PersistentProperties::setReloadId(const QString& id)
{
    if (m_reloadId == id) {
        return;
    }
    m_reloadId = id;
    Q_EMIT reloadIdChanged();
}

QVariantMap PersistentProperties::saveState() const
{
    QVariantMap state;
    const QMetaObject* meta = metaObject();
    const int offset = QQuickItem::staticMetaObject.propertyCount();
    // Resolve the C++ `reloadId` Q_PROPERTY by index (not by name) so a QML
    // author who shadows the name with their own `property string reloadId`
    // — perfectly legal, lands at a different metaobject index — still
    // gets persisted. Skipping by name would silently drop the user's
    // property in addition to ours. Cached as `static const` because the
    // C++ class's metaobject is fixed at compile time; resolving on every
    // save is wasted work in tight save loops.
    static const int reloadIdIndex = PersistentProperties::staticMetaObject.indexOfProperty("reloadId");

    for (int i = offset; i < meta->propertyCount(); ++i) {
        if (i == reloadIdIndex) {
            continue;
        }
        const QMetaProperty prop = meta->property(i);
        const QVariant value = prop.read(this);
        if (!isJsonSerializable(value.metaType().id())) {
            qCDebug(lcPersist) << "Skipping non-JSON-serialisable property" << prop.name() << "of type"
                               << value.typeName();
            continue;
        }
        state.insert(QString::fromUtf8(prop.name()), value);
    }

    return state;
}

void PersistentProperties::restoreState(const QVariantMap& state)
{
    // Validate every key against the running metaObject before writing —
    // refusing to set unknown properties prevents a corrupt or hostile
    // state file from creating arbitrary dynamic properties on this
    // QQuickItem (some of which collide with Qt internals).
    const QMetaObject* meta = metaObject();
    for (auto it = state.cbegin(); it != state.cend(); ++it) {
        const QByteArray name = it.key().toUtf8();
        const int index = meta->indexOfProperty(name.constData());
        if (index < 0) {
            qCDebug(lcPersist) << "Ignoring unknown property in saved state:" << it.key();
            continue;
        }
        const QMetaProperty prop = meta->property(index);
        // Only restore if the saved value's type is convertible to the
        // declared property type — silently dropping mismatches avoids
        // setting QString("...") on a `property int` etc.
        QVariant value = it.value();
        if (!value.convert(prop.metaType())) {
            qCDebug(lcPersist) << "Type mismatch restoring" << it.key() << "—"
                               << "saved" << it.value().typeName() << "vs declared" << prop.typeName();
            continue;
        }
        prop.write(this, value);
    }
}

} // namespace PhosphorShell
