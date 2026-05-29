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
};

PwNodeModel::PwNodeModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Private>())
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
    rebuildFromConnection(nullptr);
}

PipeWireConnection* PwNodeModel::connection() const
{
    return d->connection.data();
}

void PwNodeModel::setConnection(PipeWireConnection* connection)
{
    if (d->connection == connection)
        return;
    // Emit connectionChanged BEFORE rebuildFromConnection so the
    // connection-coherence invariant holds: any QML binding watching
    // `connection` sees the new pointer before any `countChanged` /
    // `dataChanged` row events fire for the new connection's nodes.
    // If we emitted after rebuild, bindings that observe both signals
    // would briefly see "rows populated for the NEW connection but
    // `connection` still reports the OLD one", which breaks the
    // documented invariant that the model's rows always describe its
    // current `connection` property.
    d->connection = connection;
    Q_EMIT connectionChanged();
    rebuildFromConnection(connection);
}

void PwNodeModel::rebuildFromConnection(PipeWireConnection* connection)
{
    // Cache the row count up-front so we only emit countChanged once
    // at the end if the count actually moved. Previously we fired
    // countChanged up to four times per rebuild (one per remove +
    // insert phase) which forced every count-bound QML view to
    // re-evaluate four times for a single logical state change.
    const int oldCount = d->nodes.size();

    // Drop wires + nodes from any previous connection.
    for (const auto& wire : d->connectionWires) {
        QObject::disconnect(wire);
    }
    d->connectionWires.clear();
    for (auto* node : d->nodes) {
        for (const auto& wire : d->nodeWires.value(node)) {
            QObject::disconnect(wire);
        }
    }
    d->nodeWires.clear();
    if (!d->nodes.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, d->nodes.size() - 1);
        d->nodes.clear();
        d->rowIndex.clear();
        endRemoveRows();
    }

    // setConnection has already assigned d->connection and emitted
    // connectionChanged when it called us; the dtor path assigns
    // nullptr here directly. Either way, mirror the parameter so the
    // rest of this function uses the same handle the caller passed.
    d->connection = connection;

    // Local wire-up helper: subscribe one PwNode to info / props
    // changes so the model emits dataChanged on the matching roles.
    // Lives inside the member function (not as a free helper) so it
    // can access `this->d` directly without the privacy escape hatch
    // a separate function would need. The lambda captures only `this`
    // and `node`; row lookups go through `this->d->rowIndex` inside
    // the connect-lambdas, not via captured references to Private's
    // containers (which would be fragile if Private's layout changed).
    auto wireNode = [this](PwNode* node) {
        auto& wires = d->nodeWires[node];
        wires.append(QObject::connect(node, &PwNode::infoChanged, this, [this, node]() {
            const int r = d->rowIndex.value(node, -1);
            if (r < 0)
                return;
            const QModelIndex idx = index(r);
            Q_EMIT dataChanged(idx, idx, {Qt::DisplayRole, NameRole, NickRole, DescriptionRole});
        }));
        wires.append(QObject::connect(node, &PwNode::propsChanged, this, [this, node]() {
            const int r = d->rowIndex.value(node, -1);
            if (r < 0)
                return;
            const QModelIndex idx = index(r);
            Q_EMIT dataChanged(idx, idx, {ChannelCountRole, VolumesRole, MutedRole});
        }));
    };

    if (d->connection) {
        // Hook the new connection. Use a captured raw `this`; we
        // disconnect on teardown and the model is the receiver, so Qt
        // takes care of automatic disconnect on `this`'s destruction.
        // Capture wireNode by value into the connect-lambda: a copy of
        // the wire-up closure outlives the enclosing rebuildFromConnection
        // stack frame and stays valid for as long as the connect-lambda
        // is registered (until model teardown or the next rebuild).
        d->connectionWires.append(QObject::connect(d->connection.data(), &PipeWireConnection::nodeAdded, this,
                                                   [this, wireNode](PwNode* node) {
                                                       if (!node || !d->mediaClasses.contains(node->mediaClass()))
                                                           return;
                                                       const int row = d->nodes.size();
                                                       beginInsertRows(QModelIndex(), row, row);
                                                       d->nodes.append(node);
                                                       d->rowIndex.insert(node, row);
                                                       wireNode(node);
                                                       endInsertRows();
                                                       Q_EMIT countChanged();
                                                   }));
        d->connectionWires.append(
            QObject::connect(d->connection.data(), &PipeWireConnection::nodeRemoved, this, [this](PwNode* node) {
                const int row = d->rowIndex.value(node, -1);
                if (row < 0)
                    return;
                for (const auto& wire : d->nodeWires.value(node)) {
                    QObject::disconnect(wire);
                }
                d->nodeWires.remove(node);
                beginRemoveRows(QModelIndex(), row, row);
                d->nodes.removeAt(row);
                d->rowIndex.remove(node);
                // Every row after the removed one shifts down by one;
                // patch the hash in-place so subsequent O(1) lookups
                // stay accurate.
                for (auto it = d->rowIndex.begin(); it != d->rowIndex.end(); ++it) {
                    if (it.value() > row)
                        --it.value();
                }
                endRemoveRows();
                Q_EMIT countChanged();
            }));
        // Seed from current snapshot: any nodes already surfaced
        // before this model attached.
        const auto snapshot = d->connection->nodes();
        QList<PwNode*> matching;
        matching.reserve(snapshot.size());
        for (auto* node : snapshot) {
            if (node && d->mediaClasses.contains(node->mediaClass()))
                matching.append(node);
        }
        if (!matching.isEmpty()) {
            beginInsertRows(QModelIndex(), 0, matching.size() - 1);
            d->nodes = matching;
            for (int i = 0; i < matching.size(); ++i) {
                d->rowIndex.insert(matching.at(i), i);
                wireNode(matching.at(i));
            }
            endInsertRows();
        }
    }

    // Single coalesced emit at the end. countChanged is the cheapest
    // change-signal for bindings, but firing it four times per
    // rebuild was wasteful and observable (an intermediate-state
    // glimpse during the remove → insert seam).
    if (d->nodes.size() != oldCount) {
        Q_EMIT countChanged();
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
    d->mediaClasses = classes;
    // Re-seed from the current connection so the filter change takes
    // effect immediately. Route through rebuildFromConnection
    // directly so consumers don't see a spurious connectionChanged
    // (which would re-evaluate every QML binding on `connection`)
    // when only the filter moved.
    auto* current = d->connection.data();
    if (current) {
        rebuildFromConnection(current);
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

PwSinkModel::PwSinkModel(QObject* parent)
    : PwNodeModel(parent)
{
    setMediaClasses({QStringLiteral("Audio/Sink")});
}

PwSourceModel::PwSourceModel(QObject* parent)
    : PwNodeModel(parent)
{
    setMediaClasses({QStringLiteral("Audio/Source")});
}

PwStreamModel::PwStreamModel(QObject* parent)
    : PwNodeModel(parent)
{
    setMediaClasses({QStringLiteral("Stream/Output/Audio"), QStringLiteral("Stream/Input/Audio")});
}

} // namespace PhosphorServicePipeWire
