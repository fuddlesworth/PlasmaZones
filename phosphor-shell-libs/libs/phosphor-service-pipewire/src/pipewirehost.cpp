// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PipeWireHost.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>

#include <QLatin1String>
#include <QPointer>

namespace PhosphorServicePipeWire {

namespace {

// The media classes a PipeWire default can name.
//
// These strings also appear in pwnodemodel.cpp (PwSinkModel /
// PwSourceModel filters) and pipewireconnection.cpp (the audio-node
// admission test). Nothing checks the three copies agree, and a typo in
// any one degrades silently — to "the default never resolves" here, or
// "the model is always empty" there. Kept local rather than hoisted
// because the sibling uses sit behind different concerns; if a fourth
// copy ever appears, hoist all of them to one internal header instead.
constexpr QLatin1String SinkMediaClass{"Audio/Sink"};
constexpr QLatin1String SourceMediaClass{"Audio/Source"};

// True for the two endpoint classes a PipeWire default can name. Streams
// (`Stream/Output/Audio` and friends) also live in the node set but can
// never be a default, so they are excluded from both the join and the
// per-node subscriptions.
bool isDefaultableEndpoint(const PwNode* node)
{
    if (!node) {
        return false;
    }
    const QString mediaClass = node->mediaClass();
    return mediaClass == SinkMediaClass || mediaClass == SourceMediaClass;
}

// The name -> node join. An empty name never matches: PipeWire publishes
// an empty default when the metadata module has not spoken yet (and on
// disconnect, which resets both names), and a node whose own `name` has
// not arrived is also empty, so matching those would pair "no default"
// with "no name yet" and resolve to an arbitrary node.
//
// Lowest id wins rather than first match. `nodes()` is a QHash's values()
// and its own docs say the order is unspecified, so on the (unusual but
// unenforced) case of two nodes sharing a name and class, first-match
// would let an unrelated insert rehash the container and silently flip
// which node is "the default" — emitting a change signal for a state that
// did not actually move. The id is stable and totally ordered.
PwNode* nodeNamed(const QList<PwNode*>& nodes, QLatin1String mediaClass, const QString& name)
{
    if (name.isEmpty()) {
        return nullptr;
    }
    PwNode* best = nullptr;
    for (PwNode* node : nodes) {
        if (!node || node->mediaClass() != mediaClass || node->name() != name) {
            continue;
        }
        if (!best || node->id() < best->id()) {
            best = node;
        }
    }
    return best;
}

} // namespace

class PipeWireHost::Private
{
public:
    std::unique_ptr<PipeWireConnection> connection;
    // QPointer, not raw: PwNode is deleteLater'd after `nodeRemoved`, so
    // between that emission and the actual delete a raw member could be
    // handed to QML as a dangling pointer. resolveDefaults() clears it on
    // the same turn in practice, but the pointer is also read by the
    // getters from arbitrary binding evaluations.
    QPointer<PwNode> defaultSink;
    QPointer<PwNode> defaultSource;
};

PipeWireHost::PipeWireHost(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // PipeWireConnection ownership is unique_ptr-only (no Qt parent).
    // Passing `this` as the QObject parent would double up ownership:
    // the unique_ptr inside Private would delete it on host teardown
    // AND Qt would have the host's child-deletion sweep target the
    // same pointer. Per CLAUDE.md the rule is "Parent-based ownership
    // for QObjects; unique_ptr/QPointer otherwise; never manual
    // delete" — pick one model. The connection isn't surfaced via
    // QObject::children() anywhere (we hand it out by raw pointer
    // through `connection()`), so dropping the parent has no
    // observable effect for consumers.
    d->connection = std::make_unique<PipeWireConnection>(nullptr);

    // Resolve the defaults BEFORE the plain forwards below, so that by the
    // time a QML observer is told a node arrived or a default name moved,
    // `defaultSink` / `defaultSource` already agree with it. Connection
    // order is emission order for direct connections, so wiring these
    // second would let an observer read a stale default from inside its
    // own nodeAdded handler.
    //
    // Four inputs can move the answer, and all four are needed:
    //   - either default NAME changing (metadata module speaks, or a
    //     disconnect resets both to empty);
    //   - a node arriving, which is when a name may first become
    //     resolvable;
    //   - a node leaving, which can strand a resolved default;
    //   - a node's INFO batch, because `PwNode::name` is empty at
    //     nodeAdded and lands in a later batch. Registry ordering
    //     guarantees guiNodeAdded is delivered before that batch, so
    //     subscribing here catches every name a node will ever have.
    const auto resolve = [this] {
        resolveDefaults();
    };
    QObject::connect(d->connection.get(), &PipeWireConnection::defaultSinkNameChanged, this, resolve);
    QObject::connect(d->connection.get(), &PipeWireConnection::defaultSourceNameChanged, this, resolve);
    QObject::connect(d->connection.get(), &PipeWireConnection::nodeAdded, this, [this, resolve](PwNode* node) {
        // Endpoints only. The node set also carries one entry per
        // application audio STREAM, none of which can ever be a default, so
        // subscribing to all of them would make every stream's info batch
        // (and there is a burst of them at startup on a busy desktop) run a
        // resolve that cannot change the answer.
        if (isDefaultableEndpoint(node)) {
            // `this` as context object: the connection dies with either
            // party, so a node that is destroyed takes its subscription
            // with it and no manual disconnect is needed.
            QObject::connect(node, &PwNode::infoChanged, this, resolve);
        }
        resolve();
    });
    QObject::connect(d->connection.get(), &PipeWireConnection::nodeRemoved, this, resolve);

    // Forward every observable signal — including `error` — so QML can
    // bind through PipeWireHost without reaching for `.connection.foo`
    // everywhere. The forwarding is symmetric (no transformation) — the
    // host is a facade, not a controller.
    QObject::connect(d->connection.get(), &PipeWireConnection::connectedChanged, this, &PipeWireHost::connectedChanged);
    QObject::connect(d->connection.get(), &PipeWireConnection::daemonAvailableChanged, this,
                     &PipeWireHost::daemonAvailableChanged);
    QObject::connect(d->connection.get(), &PipeWireConnection::defaultSinkNameChanged, this,
                     &PipeWireHost::defaultSinkNameChanged);
    QObject::connect(d->connection.get(), &PipeWireConnection::defaultSourceNameChanged, this,
                     &PipeWireHost::defaultSourceNameChanged);
    QObject::connect(d->connection.get(), &PipeWireConnection::error, this, &PipeWireHost::error);
    QObject::connect(d->connection.get(), &PipeWireConnection::nodeAdded, this, &PipeWireHost::nodeAdded);
    QObject::connect(d->connection.get(), &PipeWireConnection::nodeRemoved, this, &PipeWireHost::nodeRemoved);
    // Kick off the handshake immediately — typical QML usage binds to
    // `PipeWireHost.connection.nodes` and expects state to be live as
    // soon as the singleton is instantiated.
    d->connection->connectToDaemon();
}

// ~PipeWireConnection (invoked when Private unwinds via the unique_ptr)
// handles the disconnect contract — it calls pw_main_loop_quit and the
// loop thread's run() invokes doDisconnect on exit. Adding an explicit
// disconnectFromDaemon() here would only double up that teardown.
//
// What this dtor DOES have to do is sever every subscription first.
// Tearing the connection down runs guiNodesReset, which emits
// nodeRemoved for each surviving node before deleting it. By that point
// ~PipeWireHost's body has already run, so the object is a bare QObject
// with a half-destroyed `d`: a resolveDefaults() reached through one of
// those emissions would dereference the very unique_ptr being unwound.
// Qt only auto-disconnects in ~QObject, which runs after every member,
// so it cannot save us here — the disconnect has to be explicit and it
// has to happen before `d` is touched.
PipeWireHost::~PipeWireHost()
{
    // Drops the forwards AND the resolve subscriptions in one go: this
    // severs every connection whose receiver is `this`. The connection is
    // constructed in the ctor and never reassigned, so it is non-null for
    // the host's lifetime.
    d->connection->disconnect(this);
    // The per-node infoChanged subscriptions have the nodes as senders, so
    // they are not covered by the line above. Sweeping every node rather
    // than only the endpoints: disconnect on a node that was never
    // subscribed is a no-op, and that costs less than depending on this
    // loop's filter staying in step with the one at the subscription site.
    const QList<PwNode*> nodes = d->connection->nodes();
    for (PwNode* node : nodes) {
        if (node) {
            node->disconnect(this);
        }
    }
}

PipeWireConnection* PipeWireHost::connection() const
{
    return d->connection.get();
}

bool PipeWireHost::isConnected() const
{
    return d->connection->isConnected();
}

bool PipeWireHost::isDaemonAvailable() const
{
    return d->connection->isDaemonAvailable();
}

QString PipeWireHost::defaultSinkName() const
{
    return d->connection->defaultSinkName();
}

QString PipeWireHost::defaultSourceName() const
{
    return d->connection->defaultSourceName();
}

PwNode* PipeWireHost::defaultSink() const
{
    return d->defaultSink.data();
}

PwNode* PipeWireHost::defaultSource() const
{
    return d->defaultSource.data();
}

void PipeWireHost::resolveDefaults()
{
    const QList<PwNode*> nodes = d->connection->nodes();

    // Compare identity before emitting: most of the inputs that call this
    // (any node's info batch, any node arriving) cannot move the answer,
    // and an unconditional emit would re-evaluate every bound volume
    // readout in the shell on each one.
    PwNode* const sink = nodeNamed(nodes, SinkMediaClass, d->connection->defaultSinkName());
    if (d->defaultSink.data() != sink) {
        d->defaultSink = sink;
        Q_EMIT defaultSinkChanged();
    }

    PwNode* const source = nodeNamed(nodes, SourceMediaClass, d->connection->defaultSourceName());
    if (d->defaultSource.data() != source) {
        d->defaultSource = source;
        Q_EMIT defaultSourceChanged();
    }
}

void PipeWireHost::reconnect()
{
    // Queues a disconnect+connect pair back-to-back. The PipeWire loop
    // thread services the two requests in FIFO order, so the disconnect
    // is observed before the follow-up connect kicks off — that
    // ordering is the entire reason this method exists rather than
    // letting callers chain the two pieces themselves.
    //
    // No coalescing or idempotency guard: calling reconnect() while a
    // previous reconnect is still in-flight enqueues another
    // disconnect+connect cycle. Acceptable for the "force fresh
    // connection" intent (each call gets you a fresh handshake); if a
    // future caller needs single-in-flight semantics, add a
    // m_reconnectPending guard that clears in the connect-completion
    // handler.
    d->connection->disconnectFromDaemon();
    d->connection->connectToDaemon();
}

void PipeWireHost::connectToDaemon()
{
    d->connection->connectToDaemon();
}

void PipeWireHost::disconnectFromDaemon()
{
    d->connection->disconnectFromDaemon();
}

} // namespace PhosphorServicePipeWire
