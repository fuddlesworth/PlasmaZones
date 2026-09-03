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
    // PwNode adds two connections (info + props) in nodeWires. The
    // PipeWireConnection itself adds two more in connectionWires, for
    // nodeAdded and nodeRemoved.
    QList<QMetaObject::Connection> connectionWires;
    QHash<PwNode*, QList<QMetaObject::Connection>> nodeWires;

    // Last value published through `firstNodeChanged`. Row 0's identity is
    // its own observable: a rebuild can swap every row for a different set
    // of the same size, which moves row 0 while leaving the count alone, so
    // `countChanged` is not a usable proxy for it.
    //
    // QPointer, not a raw pointer, even though this is only ever COMPARED.
    // A comparison against a freed address is exactly where ABA bites: if a
    // connection is destroyed externally (taking its PwNodes with it) and a
    // later rebuild happens to allocate the new row 0 at the same address,
    // a raw compare reports "unchanged" and suppresses firstNodeChanged —
    // while QML's binding has already been nulled by the old object's
    // destruction. The widget would then stay dead for the session, which
    // is the exact failure this property exists to prevent. QPointer
    // auto-nulls on destruction, so the next refresh always sees a real
    // difference and emits.
    QPointer<PwNode> publishedFirstNode;

    // Re-read row 0 and emit only on an actual identity change. Call after
    // EVERY mutation of `nodes`; it is a pointer compare, so the cost of
    // calling it redundantly is nil next to the cost of missing a path.
    void refreshFirstNode()
    {
        PwNode* const current = nodes.isEmpty() ? nullptr : nodes.constFirst();
        if (current == publishedFirstNode.data()) {
            return;
        }
        publishedFirstNode = current;
        Q_EMIT q->firstNodeChanged();
    }

    // Tear down all wires + rows and re-seed from the current
    // `connection` member's node snapshot. Used by both
    // `setConnection` (which then emits `connectionChanged`) and
    // `setMediaClasses` (which only emits `mediaClassesChanged`, since
    // the connection pointer didn't actually change). Lives on
    // Private rather than PwNodeModel so the header doesn't have to
    // declare an implementation helper as a private member function —
    // every caller goes through the public slot signatures.
    //
    // No `newConnection` parameter: the caller is responsible for
    // assigning `connection` before calling. This avoids a stale-arg
    // bug class where the parameter and the member could diverge.
    void rebuildFromConnection();
};

PwNodeModel::PwNodeModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Private>(this))
{
}

PwNodeModel::~PwNodeModel()
{
    // No row signals during destruction. By the time ~PwNodeModel
    // runs the subclass destructor has already completed, so the
    // vtable has been sliced back to PwNodeModel's. Emitting
    // beginRemoveRows / endRemoveRows here would route through that
    // sliced vtable into any view still attached via
    // rowsAboutToBeRemoved — a view that touches the model's data()
    // from inside that callback would see the half-destroyed state.
    // Views handle model death via QObject::destroyed instead; the
    // standard QAbstractItemModel teardown path doesn't require row
    // signals to fire on destruction.
    //
    // QMetaObject::Connection handles auto-release when the source or
    // receiver dies, so the explicit disconnect calls below are
    // defensive — they keep the bookkeeping symmetric with the
    // wire-up path inside rebuildFromConnection() rather than relying
    // solely on Qt's auto-cleanup, and they ensure no callback can
    // fire between this point and the actual object teardown.
    d->connection = nullptr;
    // QObject::disconnect(wire) returns bool — intentionally ignored
    // here and in every disconnect loop below. A false return simply
    // means the connection was already torn down (Qt auto-releases the
    // QMetaObject::Connection when its sender or receiver is destroyed),
    // and a true return means we proactively dropped a live wire.
    // Either outcome leaves us in the desired post-state: no callback
    // can fire from this wire after the loop exits.
    for (const auto& wire : d->connectionWires) {
        QObject::disconnect(wire);
    }
    d->connectionWires.clear();
    // Iterate nodeWires itself, not d->nodes. This is the container being
    // cleared, so walking it needs no contains() guard and cannot miss an
    // entry whose node has already left d->nodes — which the old shape did,
    // contradicting the symmetry the comment above claims.
    //
    // Same disconnect-bool-ignored contract as the connectionWires loop:
    // false = sender/receiver already gone (auto-cleanup), true = wire
    // proactively dropped. Both outcomes are the desired post-state.
    for (auto it = d->nodeWires.cbegin(); it != d->nodeWires.cend(); ++it) {
        for (const auto& wire : it.value()) {
            QObject::disconnect(wire);
        }
    }
    d->nodeWires.clear();
    d->nodes.clear();
    d->rowIndex.clear();
}

PipeWireConnection* PwNodeModel::connection() const
{
    return d->connection.data();
}

void PwNodeModel::setConnection(PipeWireConnection* connection)
{
    // QPointer-cleared recovery FIRST: when the prior connection was
    // destroyed externally, d->connection.data() reads as null but our
    // row caches (d->nodes, d->rowIndex, d->nodeWires) still hold the
    // dangling PwNode* pointers from that connection. The equality
    // check below fires `nullptr == nullptr → true` in this scenario,
    // so an unconditional early-return would skip cleanup. Detect the
    // cleared-QPointer case BEFORE the equality short-circuit and reset
    // to a consistent empty state. Mirrors setMediaClasses's
    // dangling-pointer-detection path (which uses beginResetModel for
    // the same reason).
    // Evidence-of-prior-connection check: nodes OR connectionWires.
    // `connectionWires` is populated whenever a non-null connection
    // was attached, regardless of whether any node matched the
    // mediaClasses filter. Checking nodes alone misses the case where
    // the prior connection had no matching rows AND was then
    // externally destroyed — the QPointer auto-null would leave both
    // bindings stale (the auto-null itself never fires NOTIFY).
    // The flush runs for ANY replacement of an externally-destroyed
    // connection, not only a null one: handing a NEW connection to a model
    // whose caches still hold the dead connection's PwNode* would otherwise
    // fall through to rebuildFromConnection, whose remove phase runs
    // beginRemoveRows over those dangling pointers — and a view that
    // queries data() from rowsAboutToBeRemoved dereferences freed memory.
    // beginResetModel forbids exactly those per-row queries.
    if (d->connection.isNull() && (!d->nodes.isEmpty() || !d->connectionWires.isEmpty())) {
        const bool hadRows = !d->nodes.isEmpty();
        if (hadRows) {
            beginResetModel();
        }
        // Drop wire bookkeeping without disconnect (wires point at
        // already-destroyed sender QObjects; QMetaObject::Connection
        // releases internally as the sender's destructor unhooks
        // signal targets).
        d->connectionWires.clear();
        d->nodeWires.clear();
        d->nodes.clear();
        d->rowIndex.clear();
        if (hadRows) {
            endResetModel();
            Q_EMIT countChanged();
            // NOT refreshFirstNode(): by this point the connection and its
            // PwNodes are already destroyed, so the published QPointer has
            // silently auto-nulled and the change gate would compare
            // nullptr against nullptr and stay silent — swallowing what is
            // observably a row 0 → nothing transition. Publish it directly.
            d->publishedFirstNode = nullptr;
            Q_EMIT firstNodeChanged();
        }
        // Fire connectionChanged at the acknowledgement boundary so
        // bindings observing the property get a NOTIFY for the implicit
        // non-null → null transition that happened when the QPointer
        // auto-nulled. Without this, any binding wired solely through
        // connectionChanged stays pinned to the prior non-null value
        // in its dependency graph.
        if (connection == nullptr) {
            Q_EMIT connectionChanged();
            return;
        }
        // A NEW connection follows the flush: fall through to the normal
        // assignment + rebuild below, which now runs against clean caches
        // and fires connectionChanged once the rows describe it.
    }
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
    d->rebuildFromConnection();
    Q_EMIT connectionChanged();
}

void PwNodeModel::Private::rebuildFromConnection()
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
    //
    // QObject::disconnect(wire) returns bool — intentionally ignored
    // in both loops below. A false return simply means the wire was
    // already disconnected (Qt auto-releases the
    // QMetaObject::Connection when its sender or receiver dies, which
    // can happen between the previous rebuild and this one if the
    // upstream PipeWireConnection or a PwNode was destroyed). A true
    // return means we proactively dropped a still-live wire. Either
    // outcome leaves us in the desired post-state: the wire cannot
    // fire after this loop exits.
    for (const auto& wire : connectionWires) {
        QObject::disconnect(wire);
    }
    connectionWires.clear();
    for (auto* node : nodes) {
        // Catch wire-up / teardown asymmetry: every node in `nodes`
        // must have a corresponding entry in `nodeWires` (the inserts
        // are paired inside wireNode + the snapshot/append paths).
        // If this fires we've leaked the wire pair somewhere.
        // Release-build behaviour of this assert is benign by construction:
        // nodeWires.value() on a missing key yields a default-constructed empty
        // QList and the loop below is a no-op, so there is no separate runtime
        // guard to pair it with.
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

    // setConnection has already assigned d->connection (the
    // connectionChanged emit happens AFTER we return); setMediaClasses
    // calls us with d->connection already in its post-mutation state.
    // The rebuild reads only the `connection` member from here on —
    // there is no separate parameter to keep in sync, which removes
    // the prior assert-only invariant that an out-of-sync caller could
    // violate in release builds.

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
                // Defensive guard: nodeAdded is fired on the GUI
                // thread, but if a future refactor moves the
                // connection/snapshot read across a yielding boundary,
                // this prevents double-insertion. rowIndex is the O(1)
                // hash, so the lookup is cheap on the hot path even
                // when no race window exists.
                if (rowIndex.contains(node))
                    return;
                const int row = nodes.size();
                q->beginInsertRows(QModelIndex(), row, row);
                nodes.append(node);
                rowIndex.insert(node, row);
                wireNode(node);
                q->endInsertRows();
                Q_EMIT q->countChanged();
                refreshFirstNode();
            }));
        connectionWires.append(
            QObject::connect(connection.data(), &PipeWireConnection::nodeRemoved, q, [this](PwNode* node) {
                const int row = rowIndex.value(node, -1);
                if (row < 0)
                    return;
                // QObject::disconnect(wire) bool return intentionally
                // ignored — see ~PwNodeModel for the full rationale.
                // false = wire auto-released when sender/receiver died;
                // true = wire proactively dropped. Either outcome
                // leaves the wire unable to fire after this loop.
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
                refreshFirstNode();
            }));
        // Seed from current snapshot: any nodes already surfaced
        // before this model attached. The `rowIndex.contains(node)`
        // guard mirrors the defensive check inside the nodeAdded
        // lambda above — although `nodes` and `rowIndex` were both
        // cleared at the top of this function, keeping the asymmetry
        // out of the codebase removes a class of future regression
        // where a refactor reuses one path's invariants in the other.
        const auto snapshot = connection->nodes();
        QList<PwNode*> matching;
        matching.reserve(snapshot.size());
        for (auto* node : snapshot) {
            if (node && mediaClasses.contains(node->mediaClass()) && !rowIndex.contains(node))
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
    // A rebuild can replace every row with a different set of the SAME
    // size, so this is the one path where row 0 moves with no countChanged.
    refreshFirstNode();
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
        d->rebuildFromConnection();
    } else if (!d->nodes.isEmpty() || !d->connectionWires.isEmpty()) {
        // QPointer guards against an external PipeWireConnection
        // destruction that bypassed setConnection(nullptr): the
        // tracked pointer has gone null but our rows / wires still
        // hold the (now-dangling) state the connection owned. Drop
        // them with a model reset (if we had rows) and clear the
        // wire bookkeeping — every QMetaObject::Connection has
        // already been auto-released by Qt when its source died, but
        // the QHash entries themselves are stale and must be
        // flushed. The connectionWires-but-no-rows case (the prior
        // connection's nodes didn't match this model's filter) is
        // included so the catch-up connectionChanged below fires
        // even when there are no rows to reset.
        const bool hadRows = !d->nodes.isEmpty();
        if (hadRows) {
            beginResetModel();
        }
        d->nodes.clear();
        d->rowIndex.clear();
        d->nodeWires.clear();
        d->connectionWires.clear();
        if (hadRows) {
            endResetModel();
            Q_EMIT countChanged();
        }
        // Same as setConnection's recovery branch: the published QPointer
        // has already auto-nulled along with the destroyed nodes, so the
        // change gate cannot see the transition. Publish directly when
        // there were rows; with none, nothing moved.
        if (hadRows) {
            d->publishedFirstNode = nullptr;
            Q_EMIT firstNodeChanged();
        }
        // Fire connectionChanged at the acknowledgement boundary so
        // bindings see the implicit non-null → null transition that
        // happened when the QPointer auto-nulled (no prior NOTIFY
        // would have been emitted for the external destruction).
        Q_EMIT connectionChanged();
    }
    Q_EMIT mediaClassesChanged();
}

int PwNodeModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : d->nodes.size();
}

PwNode* PwNodeModel::firstNode() const
{
    return d->nodes.isEmpty() ? nullptr : d->nodes.constFirst();
}

PwNode* PwNodeModel::nodeAt(int row) const
{
    // No null check on the element: both insert paths reject null before it
    // reaches `nodes`, so a row is never null. data()'s extra check is
    // belt-and-braces at the QAbstractItemModel boundary, not evidence that
    // this one is missing something.
    if (row < 0 || row >= d->nodes.size())
        return nullptr;
    return d->nodes.at(row);
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
    // Cache the map in a function-local static: the role table is
    // immutable per-class, but QAbstractItemModel may call roleNames()
    // repeatedly during view setup and dataChanged-driven refresh, and
    // every call previously reallocated a fresh QHash. Function-local
    // statics in C++11+ are thread-safe via magic-statics.
    static const QHash<int, QByteArray> kRoles = {
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
    return kRoles;
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
