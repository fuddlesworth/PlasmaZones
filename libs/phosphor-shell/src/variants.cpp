// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Variants.h>

#include <QAbstractListModel>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>

Q_LOGGING_CATEGORY(lcVariants, "phosphorshell.variants")

namespace PhosphorShell {

Variants::Variants(QQuickItem* parent)
    : QQuickItem(parent)
{
}

Variants::~Variants()
{
    clear();
}

QAbstractListModel* Variants::model() const
{
    return m_model;
}

void Variants::setModel(QAbstractListModel* model)
{
    if (m_model == model) {
        return;
    }

    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }

    m_model = model;

    if (m_model) {
        connect(m_model, &QAbstractListModel::rowsInserted, this, &Variants::onRowsInserted);
        connect(m_model, &QAbstractListModel::rowsRemoved, this, &Variants::onRowsRemoved);
        connect(m_model, &QAbstractListModel::modelReset, this, &Variants::onModelReset);
    }

    rebuild();
    Q_EMIT modelChanged();
}

QQmlComponent* Variants::delegate() const
{
    return m_delegate;
}

void Variants::setDelegate(QQmlComponent* delegate)
{
    if (m_delegate == delegate) {
        return;
    }

    m_delegate = delegate;
    rebuild();
    Q_EMIT delegateChanged();
}

void Variants::onRowsInserted(const QModelIndex& parent, int first, int last)
{
    Q_UNUSED(parent)

    if (!m_delegate || !m_model) {
        return;
    }

    auto* engine = qmlEngine(this);
    if (!engine) {
        return;
    }

    const auto roles = m_model->roleNames();

    for (int i = first; i <= last; ++i) {
        auto* context = new QQmlContext(qmlContext(this), this);
        const QModelIndex idx = m_model->index(i, 0);

        QVariantMap modelData;
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
            modelData.insert(QString::fromUtf8(it.value()), m_model->data(idx, it.key()));
        }
        context->setContextProperty(QStringLiteral("modelData"), modelData);

        auto* obj = m_delegate->create(context);
        if (!obj) {
            qCWarning(lcVariants) << "Failed to create instance for row" << i;
            delete context;
            continue;
        }

        context->setParent(obj);
        m_instances.insert(i, obj);
        qCDebug(lcVariants) << "Created instance for row" << i;
    }
}

void Variants::onRowsRemoved(const QModelIndex& parent, int first, int last)
{
    Q_UNUSED(parent)

    for (int i = last; i >= first; --i) {
        if (i < m_instances.size()) {
            delete m_instances.takeAt(i);
            qCDebug(lcVariants) << "Destroyed instance for row" << i;
        }
    }
}

void Variants::onModelReset()
{
    rebuild();
}

void Variants::rebuild()
{
    clear();

    if (!m_model || !m_delegate) {
        return;
    }

    auto* engine = qmlEngine(this);
    if (!engine) {
        return;
    }

    const int count = m_model->rowCount();
    const auto roles = m_model->roleNames();

    for (int i = 0; i < count; ++i) {
        auto* context = new QQmlContext(qmlContext(this), this);
        const QModelIndex idx = m_model->index(i, 0);

        QVariantMap modelData;
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
            modelData.insert(QString::fromUtf8(it.value()), m_model->data(idx, it.key()));
        }
        context->setContextProperty(QStringLiteral("modelData"), modelData);

        auto* obj = m_delegate->create(context);
        if (!obj) {
            qCWarning(lcVariants) << "Failed to create instance for row" << i;
            delete context;
            continue;
        }

        context->setParent(obj);
        m_instances.append(obj);
    }

    qCDebug(lcVariants) << "Rebuilt" << m_instances.size() << "instances";
}

void Variants::clear()
{
    qDeleteAll(m_instances);
    m_instances.clear();
}

} // namespace PhosphorShell
