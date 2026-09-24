// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractItemModel>
#include <QQmlEngine>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace PlasmaZones {

/**
 * @brief `QAbstractItemModel` adapter that exposes a Rule match
 *        expression to `QtQuick.Controls.TreeView` for read-only display.
 *
 * The match expression is a tagged-union JSON tree shared with the editor:
 * a leaf is `{ field, op, value }`, a composite is
 * `{ all | any | none: [...] }`. Each model node maps 1:1 to a tree row;
 * composites carry their child rows as model children, leaves are
 * terminal. `setMatchJson` rebuilds the internal tree and resets the model.
 *
 * Roles expose **raw wire** fields (`fieldWire` / `opWire` / `value` /
 * `kind`); the QML delegate resolves wire → user-facing labels via the
 * existing `RuleController::matchFields` / `operatorsForField`
 * tables. Keeping the model dumb (no controller dependency, no label
 * lookups) means a single C++ type with no plumbing — the existing
 * label-resolution logic stays in exactly one place in the QML layer.
 *
 * The class is instantiable from QML via the standard `QML_ELEMENT`
 * registration; the host TreeView sets `matchJson` declaratively and the
 * model rebuilds on every assignment.
 */
class MatchExpressionTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantMap matchJson READ matchJson WRITE setMatchJson NOTIFY matchJsonChanged)

public:
    enum Roles {
        /// "leaf" / "all" / "any" / "none" — drives the QML delegate's
        /// branch between leaf rendering and group-pill rendering.
        KindRole = Qt::UserRole + 1,
        /// Leaf only — the field's wire string (e.g. "appId"). The QML
        /// delegate calls into `controller.matchFields()` to resolve this
        /// to a user-facing label.
        FieldWireRole,
        /// Leaf only — the operator's wire string (e.g. "appIdMatches").
        OpWireRole,
        /// Leaf only — the raw match value, serialised as it was loaded.
        ValueRole,
        /// True iff this node is the last child of its parent. The QML
        /// delegate terminates the parent-column connector line at the
        /// row's vertical midpoint for last children (no dangling line
        /// below), so the tree connectors read as tree connectors instead
        /// of a column of unterminated verticals.
        IsLastChildRole,
        /// The node's depth in the tree — 0 for the root composite, 1 for
        /// its direct children, etc. Exposed as a role because
        /// `TreeView.depth` (attached) doesn't reliably resolve on plain
        /// `Item` delegates inside our QML, so without this role the
        /// read-only view collapsed every row to depth 0 (flat layout,
        /// no per-column tree-line geometry to drive the Canvas paint).
        DepthRole,
        /// `QVariantList<bool>` of length `depth`, root-first. Entry
        /// `i` is the last-child status of the ancestor at depth `i+1`
        /// on this row's path (i.e. `entry[0]` is the depth-1
        /// ancestor's flag, `entry[depth-1]` is the row's own
        /// `IsLastChildRole`). The QML delegate uses this to decide
        /// which ancestor-column verticals to continue through a row
        /// vs. blank out: column `a` (0 ≤ a < depth) continues iff
        /// `ancestorIsLastChild[a]` is false — i.e. the ancestor at
        /// depth `a+1` on this row's path still has siblings below,
        /// keeping that column's sub-tree alive past this row.
        AncestorIsLastChildRole,
        /// True iff this node has child rows. Composite rows with
        /// children render an extra half-vertical from row-midpoint to
        /// row-bottom at the children's-column position, so the parent
        /// row's L-stub visually connects down to the first child's
        /// immediate-parent vertical (which itself starts at the next
        /// row's top). Without this, every parent → first-child join
        /// has a half-row-height visual gap.
        HasChildrenRole,
    };

    explicit MatchExpressionTreeModel(QObject* parent = nullptr);
    ~MatchExpressionTreeModel() override;

    QVariantMap matchJson() const
    {
        return m_json;
    }
    void setMatchJson(const QVariantMap& json);

    // ── QAbstractItemModel ──────────────────────────────────────────────
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void matchJsonChanged();

private:
    struct Node
    {
        QString kind; ///< "leaf" / "all" / "any" / "none"
        QString fieldWire; ///< leaf only
        QString opWire; ///< leaf only
        QVariant value; ///< leaf only
        Node* parent = nullptr; ///< back-pointer used by parent()
        int row = 0; ///< index within parent->children
        std::vector<std::unique_ptr<Node>> children;
    };

    /// Recursive builder — turns a JSON sub-tree into a freshly-allocated
    /// Node graph rooted at the returned pointer. `parentNode` and `row`
    /// seed the parent / row back-pointers `parent()` and `index()` rely
    /// on so we don't have to traverse on every lookup.
    std::unique_ptr<Node> buildNode(const QVariantMap& json, Node* parentNode, int row) const;

    QVariantMap m_json;
    std::unique_ptr<Node> m_root;
};

} // namespace PlasmaZones
