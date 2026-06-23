// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "matchexpressiontreemodel.h"

namespace PlasmaZones {

MatchExpressionTreeModel::MatchExpressionTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

MatchExpressionTreeModel::~MatchExpressionTreeModel() = default;

void MatchExpressionTreeModel::setMatchJson(const QVariantMap& json)
{
    if (m_json == json) {
        return;
    }
    // Full reset — the structural shape of the tree may have changed
    // arbitrarily (a composite kind flip / a child count change), so
    // emitting per-row signals here is more fragile than just rebuilding.
    // The host TreeView re-applies expandRecursively from `matchJson`'s
    // change handler so the read-only peek stays fully expanded after the
    // reset.
    beginResetModel();
    m_json = json;
    m_root = buildNode(json, nullptr, 0);
    endResetModel();
    Q_EMIT matchJsonChanged();
}

std::unique_ptr<MatchExpressionTreeModel::Node> MatchExpressionTreeModel::buildNode(const QVariantMap& json,
                                                                                    Node* parentNode, int row) const
{
    auto node = std::make_unique<Node>();
    node->parent = parentNode;
    node->row = row;

    if (json.contains(QLatin1String("field"))) {
        node->kind = QStringLiteral("leaf");
        node->fieldWire = json.value(QLatin1String("field")).toString();
        node->opWire = json.value(QLatin1String("op")).toString();
        node->value = json.value(QLatin1String("value"));
        return node;
    }

    // Composite — pick the first recognised group key so a hand-edited
    // JSON with multiple group keys (e.g. both `all` and `any`) folds onto
    // a single deterministic shape. The shipped writers only ever emit one
    // group key per node, so this is a defensive load-time normaliser.
    QString groupKind;
    QVariantList children;
    if (json.contains(QLatin1String("any"))) {
        groupKind = QStringLiteral("any");
        children = json.value(QLatin1String("any")).toList();
    } else if (json.contains(QLatin1String("none"))) {
        groupKind = QStringLiteral("none");
        children = json.value(QLatin1String("none")).toList();
    } else {
        // Default to `all` for an empty / unrecognised composite — same
        // catch-all shape MatchExpression() defaults to in the library.
        groupKind = QStringLiteral("all");
        children = json.value(QLatin1String("all")).toList();
    }
    node->kind = groupKind;
    node->children.reserve(children.size());
    for (int i = 0; i < children.size(); ++i) {
        node->children.push_back(buildNode(children.at(i).toMap(), node.get(), i));
    }
    return node;
}

QModelIndex MatchExpressionTreeModel::index(int row, int column, const QModelIndex& parentIndex) const
{
    if (!hasIndex(row, column, parentIndex)) {
        return {};
    }
    if (!parentIndex.isValid()) {
        // The model exposes a single top-level row pointing at m_root —
        // every other row sits under it. Returning a fresh index keyed off
        // the m_root pointer keeps the parent() walk straightforward (a
        // child's parent is its `parent` back-pointer, including the
        // top-level child whose parent is null).
        if (!m_root || row != 0) {
            return {};
        }
        return createIndex(row, column, m_root.get());
    }
    auto* parentNode = static_cast<Node*>(parentIndex.internalPointer());
    if (!parentNode || row < 0 || row >= static_cast<int>(parentNode->children.size())) {
        return {};
    }
    return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex MatchExpressionTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    auto* childNode = static_cast<Node*>(child.internalPointer());
    if (!childNode || !childNode->parent) {
        return {};
    }
    Node* parentNode = childNode->parent;
    return createIndex(parentNode->row, 0, parentNode);
}

int MatchExpressionTreeModel::rowCount(const QModelIndex& parentIndex) const
{
    if (!parentIndex.isValid()) {
        return m_root ? 1 : 0;
    }
    auto* node = static_cast<Node*>(parentIndex.internalPointer());
    return node ? static_cast<int>(node->children.size()) : 0;
}

int MatchExpressionTreeModel::columnCount(const QModelIndex& parentIndex) const
{
    Q_UNUSED(parentIndex);
    return 1;
}

QVariant MatchExpressionTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    auto* node = static_cast<Node*>(index.internalPointer());
    if (!node) {
        return {};
    }
    switch (role) {
    case KindRole:
        return node->kind;
    case FieldWireRole:
        return node->fieldWire;
    case OpWireRole:
        return node->opWire;
    case ValueRole:
        return node->value;
    case IsLastChildRole:
        // The synthetic top-level row (m_root) has no parent, so it is
        // trivially "the only child of nothing" — treat it as last so the
        // delegate doesn't draw a continuation line past it.
        return !node->parent || node->row == static_cast<int>(node->parent->children.size()) - 1;
    case DepthRole: {
        int depth = 0;
        for (const Node* p = node->parent; p; p = p->parent) {
            ++depth;
        }
        return depth;
    }
    case AncestorIsLastChildRole: {
        // Walk from this row up toward the root, collecting last-child
        // flags for each ancestor on the path; prepend so the result is
        // root-first (entry[0] = depth-1 ancestor's last-child status,
        // entry[depth-1] = current row's own last-child status).
        QVariantList flags;
        for (const Node* n = node; n && n->parent; n = n->parent) {
            const bool isLast = n->row == static_cast<int>(n->parent->children.size()) - 1;
            flags.prepend(isLast);
        }
        return flags;
    }
    case HasChildrenRole:
        return !node->children.empty();
    case Qt::DisplayRole:
        // The DisplayRole is provided as a courtesy for non-TreeView
        // inspectors (debug tools, future flat readouts). The settings
        // delegate keys off KindRole + the wire roles and ignores this.
        if (node->kind == QStringLiteral("leaf")) {
            return QStringLiteral("%1 %2 %3").arg(node->fieldWire, node->opWire, node->value.toString());
        }
        return node->kind;
    default:
        return {};
    }
}

QHash<int, QByteArray> MatchExpressionTreeModel::roleNames() const
{
    return {
        {KindRole, QByteArrayLiteral("kind")},
        {FieldWireRole, QByteArrayLiteral("fieldWire")},
        {OpWireRole, QByteArrayLiteral("opWire")},
        {ValueRole, QByteArrayLiteral("value")},
        {IsLastChildRole, QByteArrayLiteral("isLastChild")},
        {DepthRole, QByteArrayLiteral("depth")},
        {AncestorIsLastChildRole, QByteArrayLiteral("ancestorIsLastChild")},
        {HasChildrenRole, QByteArrayLiteral("hasChildrenRow")},
        {Qt::DisplayRole, QByteArrayLiteral("display")},
    };
}

} // namespace PlasmaZones
