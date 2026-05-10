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
    return m_model.data();
}

void Variants::setModel(QAbstractListModel* model)
{
    if (m_model.data() == model) {
        return;
    }

    if (m_model) {
        disconnect(m_model.data(), nullptr, this, nullptr);
    }

    m_model = model;

    if (m_model) {
        connect(m_model.data(), &QAbstractListModel::rowsInserted, this, &Variants::onRowsInserted);
        connect(m_model.data(), &QAbstractListModel::rowsRemoved, this, &Variants::onRowsRemoved);
        connect(m_model.data(), &QAbstractListModel::modelReset, this, &Variants::onModelReset);
        connect(m_model.data(), &QAbstractListModel::dataChanged, this, &Variants::onDataChanged);
    }

    rebuild();
    Q_EMIT modelChanged();
}

QQmlComponent* Variants::delegate() const
{
    return m_delegate.data();
}

void Variants::setDelegate(QQmlComponent* delegate)
{
    if (m_delegate.data() == delegate) {
        return;
    }

    m_delegate = delegate;
    rebuild();
    Q_EMIT delegateChanged();
}

QVariantMap Variants::buildModelData(int row) const
{
    QVariantMap data;
    if (!m_model) {
        return data;
    }
    const auto roles = m_model->roleNames();
    const QModelIndex idx = m_model->index(row, 0);
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        data.insert(QString::fromUtf8(it.value()), m_model->data(idx, it.key()));
    }
    return data;
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

    for (int row = first; row <= last; ++row) {
        auto* context = new QQmlContext(qmlContext(this), this);
        context->setContextProperty(QStringLiteral("modelData"), buildModelData(row));

        auto* obj = m_delegate->create(context);
        if (!obj) {
            qCWarning(lcVariants) << "Failed to create instance for row" << row;
            delete context;
            continue;
        }

        context->setParent(obj);
        m_instances.insert(row, obj);
        qCDebug(lcVariants) << "Created instance for row" << row;
    }

    // After insertion, rows at indices >= last+1 in our list now correspond
    // to higher model row indices than they did before. Their context's
    // `modelData` was captured for the OLD row index — refresh each so it
    // reflects the new index. (For prepend/insert-in-middle this matters;
    // for append-at-end the loop is a no-op.)
    for (int row = last + 1; row < m_instances.size(); ++row) {
        refreshInstanceData(row);
    }
}

void Variants::onRowsRemoved(const QModelIndex& parent, int first, int last)
{
    Q_UNUSED(parent)

    for (int row = last; row >= first; --row) {
        if (row < m_instances.size()) {
            // deleteLater() is safer than delete inside QML signal stacks —
            // the model row may be referenced by in-flight QML expressions.
            m_instances.takeAt(row)->deleteLater();
            qCDebug(lcVariants) << "Destroyed instance for row" << row;
        }
    }

    // Surviving instances at indices >= first now correspond to lower
    // model indices — refresh their modelData.
    for (int row = first; row < m_instances.size(); ++row) {
        refreshInstanceData(row);
    }
}

void Variants::onModelReset()
{
    rebuild();
}

void Variants::onDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles)
{
    Q_UNUSED(roles)
    if (!m_model) {
        return;
    }
    const int first = topLeft.row();
    const int last = bottomRight.row();
    for (int row = first; row <= last && row < m_instances.size(); ++row) {
        refreshInstanceData(row);
    }
}

void Variants::refreshInstanceData(int row)
{
    if (row < 0 || row >= m_instances.size()) {
        return;
    }
    QObject* obj = m_instances.at(row);
    if (!obj) {
        return;
    }
    QQmlContext* context = QQmlEngine::contextForObject(obj);
    if (!context) {
        return;
    }
    context->setContextProperty(QStringLiteral("modelData"), buildModelData(row));
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

    for (int row = 0; row < count; ++row) {
        auto* context = new QQmlContext(qmlContext(this), this);
        context->setContextProperty(QStringLiteral("modelData"), buildModelData(row));

        auto* obj = m_delegate->create(context);
        if (!obj) {
            qCWarning(lcVariants) << "Failed to create instance for row" << row;
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
    for (QObject* obj : std::as_const(m_instances)) {
        if (obj) {
            obj->deleteLater();
        }
    }
    m_instances.clear();
}

} // namespace PhosphorShell
