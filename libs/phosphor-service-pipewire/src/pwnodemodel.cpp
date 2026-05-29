// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PwNodeModel.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

namespace PhosphorServicePipeWire {

class PwNodeModel::Private
{
public:
    QPointer<PipeWireConnection> connection;
    QStringList mediaClasses;
    QList<PwNode*> nodes;
    // Track our QMetaObject::Connection handles so we can disconnect
    // cleanly when the source connection or filter set changes. Each
    // PwNode adds two connections (info + props); a single connection
    // tracks nodeAdded / nodeRemoved on the PipeWireConnection itself.
    QList<QMetaObject::Connection> connectionWires;
    QHash<PwNode*, QList<QMetaObject::Connection>> nodeWires;
};

namespace {

/// Wire a single PwNode to the model: subscribe to info / props
/// changes so the model emits dataChanged on the matching roles.
/// Extracted to one helper so the nodeAdded path and the snapshot-
/// seed path stay in sync — the previous code duplicated 14 lines of
/// wire-up across both call sites.
void wireNode(PwNodeModel* model, PwNode* node, QList<PwNode*>& nodes,
              QHash<PwNode*, QList<QMetaObject::Connection>>& nodeWires)
{
    nodeWires[node].append(QObject::connect(node, &PwNode::infoChanged, model, [model, node, &nodes]() {
        const int r = nodes.indexOf(node);
        if (r < 0)
            return;
        const QModelIndex idx = model->index(r);
        Q_EMIT model->dataChanged(
            idx, idx, {Qt::DisplayRole, PwNodeModel::NameRole, PwNodeModel::NickRole, PwNodeModel::DescriptionRole});
    }));
    nodeWires[node].append(QObject::connect(node, &PwNode::propsChanged, model, [model, node, &nodes]() {
        const int r = nodes.indexOf(node);
        if (r < 0)
            return;
        const QModelIndex idx = model->index(r);
        Q_EMIT model->dataChanged(idx, idx,
                                  {PwNodeModel::ChannelCountRole, PwNodeModel::VolumesRole, PwNodeModel::MutedRole});
    }));
}

} // namespace

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
    setConnection(nullptr);
}

PipeWireConnection* PwNodeModel::connection() const
{
    return d->connection.data();
}

void PwNodeModel::setConnection(PipeWireConnection* connection)
{
    if (d->connection == connection)
        return;
    rebuildFromConnection(connection);
    Q_EMIT connectionChanged();
}

void PwNodeModel::rebuildFromConnection(PipeWireConnection* connection)
{
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
        endRemoveRows();
        Q_EMIT countChanged();
    }

    d->connection = connection;

    if (d->connection) {
        // Hook the new connection. Use a captured raw `this`; we
        // disconnect on teardown and the model is the receiver, so Qt
        // takes care of automatic disconnect on `this`'s destruction.
        d->connectionWires.append(
            QObject::connect(d->connection.data(), &PipeWireConnection::nodeAdded, this, [this](PwNode* node) {
                if (!node || !d->mediaClasses.contains(node->mediaClass()))
                    return;
                const int row = d->nodes.size();
                beginInsertRows(QModelIndex(), row, row);
                d->nodes.append(node);
                wireNode(this, node, d->nodes, d->nodeWires);
                endInsertRows();
                Q_EMIT countChanged();
            }));
        d->connectionWires.append(
            QObject::connect(d->connection.data(), &PipeWireConnection::nodeRemoved, this, [this](PwNode* node) {
                const int row = d->nodes.indexOf(node);
                if (row < 0)
                    return;
                for (const auto& wire : d->nodeWires.value(node)) {
                    QObject::disconnect(wire);
                }
                d->nodeWires.remove(node);
                beginRemoveRows(QModelIndex(), row, row);
                d->nodes.removeAt(row);
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
            for (auto* node : matching) {
                wireNode(this, node, d->nodes, d->nodeWires);
            }
            endInsertRows();
            Q_EMIT countChanged();
        }
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
    if (!index.isValid() || index.row() < 0 || index.row() >= d->nodes.size())
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
