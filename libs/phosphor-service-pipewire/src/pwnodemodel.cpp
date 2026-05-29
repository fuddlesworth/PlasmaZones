// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PwNodeModel.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QPointer>

namespace PhosphorServicePipeWire {

class PwNodeModel::Private
{
public:
    explicit Private(PwNodeModel* qq)
        : q(qq)
    {
    }

    // Back-pointer to the owning model. Needed because
    // `rebuildFromConnection` and the connect-lambdas drive
    // QAbstractListModel's protected row-event machinery
    // (`beginInsertRows` / `endInsertRows`, `beginRemoveRows` /
    // `endRemoveRows`, `index()`) and emit `countChanged`. Private is
    // a friend of PwNodeModel (declared in the header) so those
    // protected/signal accesses through `q` resolve correctly.
    PwNodeModel* q = nullptr;

    QPointer<PipeWireConnection> connection;
    QStringList mediaClasses;
    QList<PwNode*> nodes;
    // Row-index hash maintained in lock-step with `nodes`: lets the
    // per-node info/props handlers find their row in O(1) instead of
    // O(n) via indexOf. Burst startup with many nodes (sinks + sources
    // + every stream) was O(n^2) without this — each node's first
    // info/props event walked the full list to find itself.
    QHash<PwNode*, int> rowIndex;
    // Track our QMetaObject::Connection handles so we can disconnect
    // cleanly when the source connection or filter set changes. Each
    // PwNode adds two connections (info + props); a single connection
    // tracks nodeAdded / nodeRemoved on the PipeWireConnection itself.
    QList<QMetaObject::Connection> connectionWires;
    QHash<PwNode*, QList<QMetaObject::Connection>> nodeWires;

    // Tear down all wires + rows, attach to `newConnection`, and
    // re-seed from its current node snapshot. Used by both
    // `setConnection` (which then emits `connectionChanged`) and
    // `setMediaClasses` (which only emits `mediaClassesChanged`, since
    // the connection pointer didn't actually change). Lives on
    // Private rather than PwNodeModel so the header doesn't have to
    // declare an implementation helper as a private member function —
    // every caller goes through the public slot signatures.
    void rebuildFromConnection(PipeWireConnection* newConnection);
};

PwNodeModel::PwNodeModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Private>(this))
{
}

PwNodeModel::~PwNodeModel()
{
    // QMetaObject::Connection handles auto-release when the source or
    // receiver dies, so no explicit teardown needed beyond clearing
    // the lists.
    //
    // Route through rebuildFromConnection directly rather than
    // setConnection: emitting connectionChanged() from the destructor
    // would invite slots to read connection() after `d` has begun
    // unwinding. rebuildFromConnection(nullptr) drops every wire +
    // row without firing the property-NOTIFY signal.
    d->rebuildFromConnection(nullptr);
}

PipeWireConnection* PwNodeModel::connection() const
{
    return d->connection.data();
}

void PwNodeModel::setConnection(PipeWireConnection* connection)
{
    if (d->connection == connection)
        return;
    // Assign the new connection FIRST so connection() reports the
    // post-mutation pointer for the rebuild duration, then run
    // rebuildFromConnection so every row event (beginRemoveRows /
    // beginInsertRows + the coalesced countChanged) fires against the
    // new connection, and only THEN emit connectionChanged. Qt's
    // convention for NOTIFY signals is to fire after the property
    // mutation completes, meaning any binding awakened by
    // connectionChanged observes a consistent state: connection()
    // returns the new pointer AND the model's rows already describe
    // it. Emitting before the rebuild would invert that — bindings
    // would wake to "new connection() but stale rows", breaking the
    // documented invariant that the model's rows always describe its
    // current `connection` property.
    d->connection = connection;
    d->rebuildFromConnection(connection);
    Q_EMIT connectionChanged();
}

void PwNodeModel::Private::rebuildFromConnection(PipeWireConnection* newConnection)
{
    // Cache the ORIGINAL row count (before any removes) so we only
    // emit countChanged once at the end if the count actually moved
    // net-net. Reading `nodes.size()` later would yield the
    // intermediate zero after the clear()/endRemoveRows() pair, so a
    // rebuild that lands on the same count would still fire a
    // spurious countChanged. Previously the rebuild fired
    // countChanged up to four times per call (one per remove +
    // insert phase) which forced every count-bound QML view to
    // re-evaluate four times for a single logical state change.
    const int oldCount = nodes.size();

    // Drop wires + nodes from any previous connection.
    for (const auto& wire : connectionWires) {
        QObject::disconnect(wire);
    }
    connectionWires.clear();
    for (auto* node : nodes) {
        // Catch wire-up / teardown asymmetry: every node in `nodes`
        // must have a corresponding entry in `nodeWires` (the inserts
        // are paired inside wireNode + the snapshot/append paths).
        // If this fires we've leaked the wire pair somewhere.
        Q_ASSERT(nodeWires.contains(node));
        for (const auto& wire : nodeWires.value(node)) {
            QObject::disconnect(wire);
        }
    }
    nodeWires.clear();
    if (!nodes.isEmpty()) {
        // `nodes.clear()` and `rowIndex.clear()` MUST sit between
        // beginRemoveRows() and endRemoveRows() for the
        // QAbstractItemModel invariant to hold: rowCount() reflects
        // the post-removal state when views query it from
        // endRemoveRows(), and any view-side cache rebuilt inside
        // that callback would see stale rows if we cleared after.
        q->beginRemoveRows(QModelIndex(), 0, nodes.size() - 1);
        nodes.clear();
        rowIndex.clear();
        q->endRemoveRows();
    }

    // setConnection has already assigned d->connection and emitted
    // connectionChanged when it called us; the dtor path assigns
    // nullptr here directly. Either way, mirror the parameter so the
    // rest of this function uses the same handle the caller passed.
    connection = newConnection;

    // Local wire-up helper: subscribe one PwNode to info / props
    // changes so the model emits dataChanged on the matching roles.
    // Lives inside the member function (not as a free helper) so it
    // can access Private's members directly without the privacy
    // escape hatch a separate function would need. The connect-
    // lambdas capture `this` (the Private instance) and `node` by
    // value because the connect-lambda outlives the
    // rebuildFromConnection stack frame: it stays registered until
    // model teardown or the next rebuild. Private outlives both via
    // the model's `std::unique_ptr<Private> d`, so the captured
    // `this` stays valid for the full lifetime of any connection the
    // lambda is wired to. Row lookups go through `this->rowIndex`
    // inside the lambdas rather than through references to specific
    // containers — keeps the wiring robust against Private's layout.
    auto wireNode = [this](PwNode* node) {
        auto& wires = nodeWires[node];
        wires.append(QObject::connect(node, &PwNode::infoChanged, q, [this, node]() {
            const int r = rowIndex.value(node, -1);
            if (r < 0)
                return;
            const QModelIndex idx = q->index(r);
            Q_EMIT q->dataChanged(idx, idx, {Qt::DisplayRole, NameRole, NickRole, DescriptionRole});
        }));
        wires.append(QObject::connect(node, &PwNode::propsChanged, q, [this, node]() {
            const int r = rowIndex.value(node, -1);
            if (r < 0)
                return;
            const QModelIndex idx = q->index(r);
            Q_EMIT q->dataChanged(idx, idx, {ChannelCountRole, VolumesRole, MutedRole});
        }));
    };

    if (connection) {
        // Hook the new connection. The receiver is `q` so Qt
        // auto-disconnects on the model's destruction; we also
        // disconnect explicitly on the next rebuild. Capture wireNode
        // by value into the connect-lambda: a copy of the wire-up
        // closure outlives the enclosing rebuildFromConnection stack
        // frame and stays valid for as long as the connect-lambda is
        // registered (until model teardown or the next rebuild).
        connectionWires.append(
            QObject::connect(connection.data(), &PipeWireConnection::nodeAdded, q, [this, wireNode](PwNode* node) {
                if (!node || !mediaClasses.contains(node->mediaClass()))
                    return;
                // Guard against a race window between connect() and
                // the nodes() snapshot below: if nodeAdded fires for
                // a node that's already in the snapshot we seed from,
                // we'd double-insert without this check. rowIndex is
                // the O(1) hash, so the lookup is cheap on the hot
                // path even when the race doesn't happen.
                if (rowIndex.contains(node))
                    return;
                const int row = nodes.size();
                q->beginInsertRows(QModelIndex(), row, row);
                nodes.append(node);
                rowIndex.insert(node, row);
                wireNode(node);
                q->endInsertRows();
                Q_EMIT q->countChanged();
            }));
        connectionWires.append(
            QObject::connect(connection.data(), &PipeWireConnection::nodeRemoved, q, [this](PwNode* node) {
                const int row = rowIndex.value(node, -1);
                if (row < 0)
                    return;
                for (const auto& wire : nodeWires.value(node)) {
                    QObject::disconnect(wire);
                }
                nodeWires.remove(node);
                q->beginRemoveRows(QModelIndex(), row, row);
                nodes.removeAt(row);
                rowIndex.remove(node);
                // Every row after the removed one shifts down by one;
                // patch the hash in-place so subsequent O(1) lookups
                // stay accurate. Removal is O(n) in row count due to
                // this row-shift fix-up; acceptable for low-cardinality
                // audio-node sets.
                //
                // Views handle row-index shifts on remove via
                // beginRemoveRows / endRemoveRows; no dataChanged
                // needed for the shifted rows.
                for (auto it = rowIndex.begin(); it != rowIndex.end(); ++it) {
                    if (it.value() > row)
                        --it.value();
                }
                q->endRemoveRows();
                Q_EMIT q->countChanged();
            }));
        // Seed from current snapshot: any nodes already surfaced
        // before this model attached.
        const auto snapshot = connection->nodes();
        QList<PwNode*> matching;
        matching.reserve(snapshot.size());
        for (auto* node : snapshot) {
            if (node && mediaClasses.contains(node->mediaClass()))
                matching.append(node);
        }
        if (!matching.isEmpty()) {
            q->beginInsertRows(QModelIndex(), 0, matching.size() - 1);
            nodes = matching;
            for (int i = 0; i < matching.size(); ++i) {
                rowIndex.insert(matching.at(i), i);
                wireNode(matching.at(i));
            }
            q->endInsertRows();
        }
    }

    // Single coalesced emit at the end; rows are notified via the
    // abstract-model row signals (beginInsertRows/endInsertRows +
    // beginRemoveRows/endRemoveRows) — countChanged is auxiliary for
    // QML count bindings. countChanged is the cheapest change-signal
    // for bindings, but firing it four times per rebuild was wasteful
    // and observable (an intermediate-state glimpse during the
    // remove → insert seam).
    if (nodes.size() != oldCount) {
        Q_EMIT q->countChanged();
    }
}

QStringList PwNodeModel::mediaClasses() const
{
    return d->mediaClasses;
}

void PwNodeModel::setMediaClasses(const QStringList& classes)
{
    if (d->mediaClasses == classes)
        return;
    // Assign the new filter FIRST so mediaClasses() reports the
    // post-mutation list for the rebuild, then re-seed from the
    // current connection so the filter change takes effect, and
    // finally emit mediaClassesChanged so any binding awakened by the
    // signal sees consistent state: mediaClasses() returns the new
    // list AND the model's rows already reflect it. Emitting before
    // rebuild would briefly expose "new mediaClasses() but stale
    // rows", breaking the documented invariant that the model's rows
    // always describe its current filter. Route through
    // rebuildFromConnection directly (not setConnection) so consumers
    // don't see a spurious connectionChanged when only the filter
    // moved.
    d->mediaClasses = classes;
    auto* current = d->connection.data();
    if (current) {
        d->rebuildFromConnection(current);
    } else {
        // No connection means no rows; rebuildFromConnection has
        // nothing to re-seed from. Lock the invariant: whoever last
        // detached this model (setConnection(nullptr) or the initial
        // empty state) must have drained the row list, so a filter
        // change with no connection cannot leave stragglers behind.
        Q_ASSERT(d->nodes.isEmpty());
    }
    Q_EMIT mediaClassesChanged();
}

int PwNodeModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : d->nodes.size();
}

QVariant PwNodeModel::data(const QModelIndex& index, int role) const
{
    // List models only have column 0. Guard explicitly so a caller
    // passing a wider QModelIndex (e.g. forwarded from a proxy that
    // doesn't normalise) gets a sensible empty QVariant instead of an
    // index into the wrong column.
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= d->nodes.size())
        return {};
    auto* node = d->nodes.at(index.row());
    if (!node)
        return {};
    switch (role) {
    case NodeRole:
        return QVariant::fromValue(node);
    case IdRole:
        return node->id();
    case NameRole:
        return node->name();
    case NickRole:
        return node->nick();
    case DescriptionRole:
        return node->description();
    case MediaClassRole:
        return node->mediaClass();
    case ChannelCountRole:
        return node->channelCount();
    case VolumesRole:
        return QVariant::fromValue(node->volumes());
    case MutedRole:
        return node->muted();
    case Qt::DisplayRole: {
        // Fall back through nick → description → name so plain
        // `model.display` always has something usable. Nick is the
        // PipeWire-curated short label and is preferred over the full
        // description for menu entries.
        const QString nick = node->nick();
        if (!nick.isEmpty())
            return nick;
        const QString desc = node->description();
        if (!desc.isEmpty())
            return desc;
        return node->name();
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> PwNodeModel::roleNames() const
{
    return {
        {NodeRole, QByteArrayLiteral("node")},
        {IdRole, QByteArrayLiteral("id")},
        {NameRole, QByteArrayLiteral("name")},
        {NickRole, QByteArrayLiteral("nick")},
        {DescriptionRole, QByteArrayLiteral("description")},
        {MediaClassRole, QByteArrayLiteral("mediaClass")},
        {ChannelCountRole, QByteArrayLiteral("channelCount")},
        {VolumesRole, QByteArrayLiteral("volumes")},
        {MutedRole, QByteArrayLiteral("muted")},
        {Qt::DisplayRole, QByteArrayLiteral("display")},
    };
}

// The pinned subclasses seed `mediaClasses` directly into Private
// rather than calling `setMediaClasses`. At construction the model has
// no connection and no rows, so the rebuildFromConnection path inside
// `setMediaClasses` would be a no-op anyway — and emitting
// `mediaClassesChanged` from a constructor would notify observers
// before the subclass is fully constructed. The friend declarations
// in PwNodeModel.h authorise this direct reach into `d`.

PwSinkModel::PwSinkModel(QObject* parent)
    : PwNodeModel(parent)
{
    d->mediaClasses = {QStringLiteral("Audio/Sink")};
}

PwSourceModel::PwSourceModel(QObject* parent)
    : PwNodeModel(parent)
{
    d->mediaClasses = {QStringLiteral("Audio/Source")};
}

PwStreamModel::PwStreamModel(QObject* parent)
    : PwNodeModel(parent)
{
    d->mediaClasses = {QStringLiteral("Stream/Output/Audio"), QStringLiteral("Stream/Input/Audio")};
}

} // namespace PhosphorServicePipeWire
