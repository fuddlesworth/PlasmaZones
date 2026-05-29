// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>

#include <QLoggingCategory>
#include <QSemaphore>
#include <QThread>

#include <pipewire/pipewire.h>

#include <atomic>
#include <mutex>

Q_LOGGING_CATEGORY(lcPipeWire, "phosphor.service.pipewire.connection")

namespace PhosphorServicePipeWire {

namespace {

/// `pw_init` / `pw_deinit` are process-global and not idempotent. Guard
/// them with a `call_once` so multiple `PipeWireConnection` instances
/// (rare but possible in test fixtures) don't double-initialise.
std::once_flag g_pwInitOnce;

void ensurePipeWireInit()
{
    std::call_once(g_pwInitOnce, [] {
        pw_init(nullptr, nullptr);
    });
}

} // namespace

class PipeWireConnection::Private
{
public:
    explicit Private(PipeWireConnection* qPtr)
        : q(qPtr)
    {
    }

    PipeWireConnection* q = nullptr;

    /// Worker thread that runs `pw_main_loop_run`. NB: Qt's QThread
    /// default `run()` starts a Qt event loop via `exec()`. We can't
    /// use that — `pw_main_loop_run` blocks for the lifetime of the
    /// connection and Qt's event loop would never get scheduling
    /// time. Subclass `QThread` and override `run()` to call
    /// `pw_main_loop_run` directly; the Qt event loop on this thread
    /// is intentionally never started.
    class LoopThread : public QThread
    {
    public:
        explicit LoopThread(Private* owner)
            : QThread(nullptr)
            , owner(owner)
        {
            setObjectName(QStringLiteral("PipeWireLoop"));
        }

    protected:
        void run() override
        {
            owner->loop = pw_main_loop_new(nullptr);
            // Signal startup completion to the constructor whether or
            // not the loop was created. The constructor blocks on this
            // semaphore so the destructor never races against a worker
            // that hasn't yet reached pw_main_loop_new.
            owner->startupReady.release();
            if (!owner->loop) {
                qCWarning(lcPipeWire) << "pw_main_loop_new failed; PipeWire integration disabled";
                return;
            }
            // Blocks until pw_main_loop_quit fires.
            pw_main_loop_run(owner->loop);
            // Tear down any remaining loop-owned state on the loop
            // thread, in the correct order, before exit. doDisconnect
            // is idempotent.
            owner->doDisconnect();
            pw_main_loop_destroy(owner->loop);
            owner->loop = nullptr;
        }

    private:
        Private* owner = nullptr;
    };

    LoopThread thread{this};

    /// Released by the worker thread once `pw_main_loop_new` has
    /// either succeeded or failed (so `loop` has its final value).
    /// The constructor acquires this before returning so the
    /// destructor never observes the worker mid-startup.
    QSemaphore startupReady{0};

    // Loop-side state. ONLY accessed from the loop thread.
    pw_main_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    spa_hook coreListener{};
    int pendingSyncSeq = 0;

    // Snapshot of state for GUI-thread getters. Updated by the GUI
    // thread itself via queued cross-thread signals; the atomics let
    // a property-system getter return the cached value without a
    // round-trip to the worker thread.
    std::atomic<bool> connected{false};
    std::atomic<bool> daemonAvailable{false};

    // Core listener callbacks. Each runs on the loop thread; bounce
    // state back to the GUI thread via QMetaObject::invokeMethod with
    // QueuedConnection (which targets the GUI thread, NOT the loop
    // thread, because q lives on the GUI thread).
    static void onCoreInfo(void* data, const struct pw_core_info* info);
    static void onCoreDone(void* data, uint32_t id, int seq);
    static void onCoreError(void* data, uint32_t id, int seq, int res, const char* message);
    static const pw_core_events kCoreEvents;

    /// Called on the loop thread. Creates `pw_context` + `pw_core` and
    /// kicks off the handshake. Idempotent: re-entry while already
    /// connected is a no-op.
    void doConnect();
    /// Called on the loop thread. Tears down `pw_core` + `pw_context`.
    /// Idempotent.
    void doDisconnect();

    // Cross-thread dispatch shims. pw_loop_invoke is the documented
    // mechanism for posting work onto the PipeWire loop thread from
    // outside; it queues `fn` to run on the loop thread the next time
    // the loop processes its event queue. The `data` parameter is
    // ignored here (we pass user_data = this and ignore the typed
    // payload).
    static int dispatchConnect(struct spa_loop* loop, bool async, uint32_t seq, const void* data, size_t size,
                               void* user_data);
    static int dispatchDisconnect(struct spa_loop* loop, bool async, uint32_t seq, const void* data, size_t size,
                                  void* user_data);

    // GUI-thread setters. Always invoked via queued cross-thread
    // signals so the NOTIFY emits and atomic writes both happen on
    // the GUI thread.
    void setConnected(bool value);
    void setDaemonAvailable(bool value);
};

const pw_core_events PipeWireConnection::Private::kCoreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .info = &PipeWireConnection::Private::onCoreInfo,
    .done = &PipeWireConnection::Private::onCoreDone,
    .ping = nullptr,
    .error = &PipeWireConnection::Private::onCoreError,
    .remove_id = nullptr,
    .bound_id = nullptr,
    .add_mem = nullptr,
    .remove_mem = nullptr,
    .bound_props = nullptr,
};

void PipeWireConnection::Private::onCoreInfo(void* data, const struct pw_core_info* info)
{
    auto* d = static_cast<Private*>(data);
    qCDebug(lcPipeWire) << "pw_core info: name" << (info && info->name ? info->name : "(null)") << "version"
                        << (info ? info->version : "(null)");
    // Daemon answered; flip daemonAvailable BEFORE the done event so a
    // fast-failing test can distinguish "no daemon" from "daemon
    // present, handshake mid-flight".
    QMetaObject::invokeMethod(
        d->q,
        [d]() {
            d->setDaemonAvailable(true);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::onCoreDone(void* data, uint32_t id, int seq)
{
    auto* d = static_cast<Private*>(data);
    if (id != PW_ID_CORE || seq != d->pendingSyncSeq)
        return;
    qCDebug(lcPipeWire) << "pw_core done seq" << seq << "— handshake complete";
    QMetaObject::invokeMethod(
        d->q,
        [d]() {
            d->setConnected(true);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::onCoreError(void* data, uint32_t id, int seq, int res, const char* message)
{
    Q_UNUSED(id);
    Q_UNUSED(seq);
    auto* d = static_cast<Private*>(data);
    const QString msg = QStringLiteral("PipeWire core error %1: %2")
                            .arg(res)
                            .arg(message ? QString::fromUtf8(message) : QStringLiteral("(no message)"));
    qCWarning(lcPipeWire) << msg;
    QMetaObject::invokeMethod(
        d->q,
        [d, msg]() {
            d->setConnected(false);
            Q_EMIT d->q->error(msg);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::doConnect()
{
    if (core)
        return; // already connected (or mid-handshake)
    if (!loop) {
        qCWarning(lcPipeWire) << "doConnect called before loop creation";
        return;
    }
    context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
    if (!context) {
        QMetaObject::invokeMethod(
            q,
            [this]() {
                setDaemonAvailable(false);
                Q_EMIT q->error(QStringLiteral("pw_context_new returned null"));
            },
            Qt::QueuedConnection);
        return;
    }
    core = pw_context_connect(context, nullptr, 0);
    if (!core) {
        pw_context_destroy(context);
        context = nullptr;
        QMetaObject::invokeMethod(
            q,
            [this]() {
                setDaemonAvailable(false);
                Q_EMIT q->error(QStringLiteral("pw_context_connect returned null (no running daemon?)"));
            },
            Qt::QueuedConnection);
        return;
    }
    pw_core_add_listener(core, &coreListener, &kCoreEvents, this);
    // Sync request: the matching `done` event signals "PipeWire has
    // processed every prior request". We use it as our handshake
    // completion marker.
    pendingSyncSeq = pw_core_sync(core, PW_ID_CORE, 0);
}

void PipeWireConnection::Private::doDisconnect()
{
    if (core) {
        spa_hook_remove(&coreListener);
        pw_core_disconnect(core);
        core = nullptr;
    }
    if (context) {
        pw_context_destroy(context);
        context = nullptr;
    }
    pendingSyncSeq = 0;
    QMetaObject::invokeMethod(
        q,
        [this]() {
            setConnected(false);
            setDaemonAvailable(false);
        },
        Qt::QueuedConnection);
}

int PipeWireConnection::Private::dispatchConnect(struct spa_loop* loop, bool async, uint32_t seq, const void* data,
                                                 size_t size, void* user_data)
{
    Q_UNUSED(loop);
    Q_UNUSED(async);
    Q_UNUSED(seq);
    Q_UNUSED(data);
    Q_UNUSED(size);
    static_cast<Private*>(user_data)->doConnect();
    return 0;
}

int PipeWireConnection::Private::dispatchDisconnect(struct spa_loop* loop, bool async, uint32_t seq, const void* data,
                                                    size_t size, void* user_data)
{
    Q_UNUSED(loop);
    Q_UNUSED(async);
    Q_UNUSED(seq);
    Q_UNUSED(data);
    Q_UNUSED(size);
    static_cast<Private*>(user_data)->doDisconnect();
    return 0;
}

void PipeWireConnection::Private::setConnected(bool value)
{
    if (connected.exchange(value) == value)
        return;
    Q_EMIT q->connectedChanged();
}

void PipeWireConnection::Private::setDaemonAvailable(bool value)
{
    if (daemonAvailable.exchange(value) == value)
        return;
    Q_EMIT q->daemonAvailableChanged();
}

PipeWireConnection::PipeWireConnection(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
    ensurePipeWireInit();
    d->thread.start();
    // Block until the worker has either created the loop or given up.
    // After this point `d->loop` is stable (either a valid pointer or
    // null with the thread already exiting) so the destructor's quit
    // path can't race against worker startup.
    d->startupReady.acquire();
}

PipeWireConnection::~PipeWireConnection()
{
    if (d->loop) {
        // pw_main_loop_quit is MT-safe (it routes through
        // pw_loop_signal_event internally) so we can call it from the
        // GUI thread directly. The loop thread's run() body picks up
        // the quit, tears down any remaining state via doDisconnect,
        // and exits.
        pw_main_loop_quit(d->loop);
    }
    if (!d->thread.wait(5000)) {
        qCWarning(lcPipeWire) << "PipeWire loop thread did not exit within 5s; terminating";
        d->thread.terminate();
        d->thread.wait();
    }
}

bool PipeWireConnection::isConnected() const
{
    return d->connected.load();
}

bool PipeWireConnection::isDaemonAvailable() const
{
    return d->daemonAvailable.load();
}

void PipeWireConnection::connect()
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchConnect, 0, nullptr, 0, false, d.get());
}

void PipeWireConnection::disconnect()
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchDisconnect, 0, nullptr, 0, false, d.get());
}

} // namespace PhosphorServicePipeWire
