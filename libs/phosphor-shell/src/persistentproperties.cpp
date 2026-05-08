// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PersistentProperties.h>

#include <QMetaObject>
#include <QMetaProperty>

namespace PhosphorShell {

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

    for (int i = offset; i < meta->propertyCount(); ++i) {
        const QMetaProperty prop = meta->property(i);
        if (qstrcmp(prop.name(), "reloadId") == 0) {
            continue;
        }
        state.insert(QString::fromUtf8(prop.name()), prop.read(this));
    }

    return state;
}

void PersistentProperties::restoreState(const QVariantMap& state)
{
    for (auto it = state.cbegin(); it != state.cend(); ++it) {
        setProperty(it.key().toUtf8().constData(), it.value());
    }
}

} // namespace PhosphorShell
