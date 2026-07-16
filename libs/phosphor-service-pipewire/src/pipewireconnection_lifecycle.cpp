// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Lifecycle implementation for PipeWireConnection. Everything here is
// either:
//
//   - the loop-thread entry point (LoopThread::run),
//   - the connect / disconnect state machine called on the loop thread
//     (doConnect, doDisconnect) and their pw_loop_invoke shims
//     (dispatchConnect, dispatchDisconnect),
//   - the GUI-thread ctor/dtor that owns the loop thread,
//   - the public GUI-thread slot implementations
//     (connectToDaemon, disconnectFromDaemon) that post into the
//     loop via the dispatchers above.
//
// Every entry here is a member of PipeWireConnection or
// PipeWireConnection::Private; declarations live in
// pipewireconnection_p.h / PipeWireConnection.h. No new public
// surface. See the "Cross-thread invariants" comment block at the
// top of pipewireconnection.cpp for the lambda-lifetime contract
// referenced throughout.

#include "pipewireconnection_p.h"

#include <QCoreApplication>

namespace PhosphorServicePipeWire {

void PipeWireConnection::Private::LoopThread::run()
{
    pw_main_loop* createdLoop = pw_main_loop_new(nullptr);
    // Release-store so the GUI thread either sees null (the failure
    // path below) or the fully constructed loop pointer.
    owner->loop.store(createdLoop, std::memory_order_release);
    // Signal startup completion to the constructor whether or not the
    // loop was created. The constructor blocks on this semaphore so the
    // destructor never races against a worker that hasn't yet reached
    // pw_main_loop_new.
    owner->startupReady.release();
    if (!createdLoop) {
        qCWarning(lcPipeWire) << "pw_main_loop_new failed; PipeWire integration disabled";
        // Surface the failure to GUI observers via the error() signal
        // so a status indicator can render "PipeWire integration
        // disabled" instead of silently staying in the
        // disconnected-with-no-explanation baseline. Queue the emit on
        // `d->q` (GUI thread): the constructor blocks on
        // startupReady, which we release above, but the constructor
        // returns BEFORE the GUI event loop processes this queued
        // post — that's fine because QueuedConnection just enqueues
        // and observers will see the signal on the next event-loop
        // turn. Lifetime: the destructor's wait(5000) + sendPostedEvents
        // + removePostedEvents drain catches this post the same way it
        // catches every other loop-thread emit. See top-of-file
        // "Cross-thread invariants" block in pipewireconnection.cpp.
        Private* d = owner;
        QMetaObject::invokeMethod(
            d->q,
            [d]() {
                Q_EMIT d->q->error(QStringLiteral("PipeWire integration disabled: pw_main_loop_new returned null"));
            },
            Qt::QueuedConnection);
        return;
    }
    // Blocks until pw_main_loop_quit fires or the loop exits on a
    // libpipewire-internal error.
    pw_main_loop_run(createdLoop);
    // Tear down any remaining loop-owned state on the loop thread, in
    // the correct order, before exit. doDisconnect is idempotent.
    owner->doDisconnect();
    // Destroy + null-store happen under loopMutex so a destructor
    // racing a spontaneous loop exit (libpipewire-internal error,
    // daemon kill mid-run) cannot observe a non-null `loop` pointer
    // and then call pw_main_loop_quit on freed memory. The destructor
    // holds the same mutex across its load+quit pair, so either it
    // sees the loop alive and quits it before we destroy, or it sees
    // null and skips the quit entirely. Without the mutex, the
    // release-store-after-destroy below would also be a TOCTOU UAF
    // window even if we ordered store-before-destroy — the destructor
    // could still grab the pointer post-store-pre-destroy. The lock
    // is the only correct serialiser.
    {
        QMutexLocker locker(&owner->loopMutex);
        pw_main_loop_destroy(createdLoop);
        owner->loop.store(nullptr, std::memory_order_release);
    }
}

void PipeWireConnection::Private::doConnect()
{
    // Recovery path: an earlier onCoreError flagged the connection
    // wedged but left core/context/registry hanging. Tear them down
    // first so this reconnect starts from a clean slate. This is the
    // counterpart to the deliberate "don't teardown from the error
    // callback" choice in onCoreError — recovery happens here, on the
    // loop thread, exactly when the next caller asks to reconnect.
    //
    // Gate on the explicit `wedged` flag, NOT on inferred
    // `!connected.load()`: an in-flight handshake (core != nullptr,
    // connected == false because the sync hasn't acked yet) would
    // otherwise look identical to the post-error wedge and we'd tear
    // down a perfectly healthy mid-handshake state on a re-entrant
    // connectToDaemon() call.
    if (core && wedged)
        doDisconnect();
    if (core)
        return; // already connected (or mid-handshake)
    // Past the recovery gate; we're about to build a fresh core, so
    // any prior wedge is resolved. Clear the flag before any failure
    // path so a fresh wedge from this attempt isn't masked by the old
    // one.
    wedged = false;
    pw_main_loop* mainLoop = loop.load(std::memory_order_acquire);
    if (!mainLoop) {
        qCWarning(lcPipeWire) << "doConnect called before loop creation";
        return;
    }
    context = pw_context_new(pw_main_loop_get_loop(mainLoop), nullptr, 0);
    if (!context) {
        // Failure paths that leave `core` null must also reset every
        // piece of cached connection state. Flip the atomics
        // synchronously on the loop thread (both `connected` AND
        // `daemonAvailable`, since the GUI-side setters no longer
        // touch the atomic — see setConnected/setDaemonAvailable) so
        // isConnected() / isDaemonAvailable() report the truth
        // immediately, then queue the GUI-side snapshot reset
        // (setConnected/setDaemonAvailable handle their own dedupe via
        // shadow-equal-skip, mirroring the onCoreError L1-override
        // pattern) so observers see one round of NOTIFY signals on the
        // right thread. resetGuiSnapshot() also clears default
        // sink/source names — without that, a previous connection's
        // stale defaults would survive a failed reconnect and ghost
        // the UI.
        //
        // pendingSyncSeq defensively reset to the sentinel. Today the
        // prior doDisconnect already left it at kNoPendingSync, but
        // pinning it here makes this failure path self-contained: a
        // future refactor that reorders disconnect/connect won't
        // silently leave a stale seq behind that would gate
        // onCoreDone on a now-impossible handshake completion.
        connected.store(false, std::memory_order_release);
        daemonAvailable.store(false, std::memory_order_release);
        pendingSyncSeq = kNoPendingSync;
        QMetaObject::invokeMethod(
            q,
            [this]() {
                resetGuiSnapshot();
                Q_EMIT q->error(QStringLiteral("pw_context_new returned null"));
            },
            Qt::QueuedConnection);
        return;
    }
    core = pw_context_connect(context, nullptr, 0);
    if (!core) {
        pw_context_destroy(context);
        context = nullptr;
        connected.store(false, std::memory_order_release);
        daemonAvailable.store(false, std::memory_order_release);
        // Defensive: same rationale as the pw_context_new failure
        // path above.
        pendingSyncSeq = kNoPendingSync;
        QMetaObject::invokeMethod(
            q,
            [this]() {
                resetGuiSnapshot();
                Q_EMIT q->error(QStringLiteral("pw_context_connect returned null (no running daemon?)"));
            },
            Qt::QueuedConnection);
        return;
    }
    pw_core_add_listener(core, &coreListener, &kCoreEvents, this);

    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    if (registry) {
        pw_registry_add_listener(registry, &registryListener, &kRegistryEvents, this);
    } else {
        qCWarning(lcPipeWire) << "pw_core_get_registry returned null; registry walking disabled";
    }

    // Sync request: the matching `done` event signals "PipeWire has
    // processed every prior request". We use it as our handshake
    // completion marker. A negative return value is an errno code
    // from the loop layer (e.g. -EINVAL on a torn-down core), in
    // which case the handshake will never complete; surface it as a
    // hard error and leave pendingSyncSeq at the kNoPendingSync
    // sentinel so onCoreDone's equality check can never spuriously
    // match an unrelated daemon-side done event (PipeWire emits only
    // non-negative seq values, so the sentinel is permanently out of
    // band).
    const int syncRc = pw_core_sync(core, PW_ID_CORE, 0);
    if (syncRc < 0) {
        // Mirror the other doConnect failure paths: a sync request the
        // loop refused leaves the handshake unable to complete, so
        // tear down core/context/registry/listeners on the loop thread
        // and flip BOTH atomics. Without the teardown a subsequent
        // connectToDaemon() would short-circuit on `if (core) return;`
        // and the caller would be stuck with a wedged-but-not-wedged
        // core that never acks. resetGuiSnapshot mirrors the pw_context_connect
        // failure path so observers see one round of NOTIFY signals on
        // the GUI thread.
        if (registry) {
            spa_hook_remove(&registryListener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
            registry = nullptr;
        }
        spa_hook_remove(&coreListener);
        pw_core_disconnect(core);
        core = nullptr;
        pw_context_destroy(context);
        context = nullptr;
        pendingSyncSeq = kNoPendingSync;
        connected.store(false, std::memory_order_release);
        daemonAvailable.store(false, std::memory_order_release);
        QMetaObject::invokeMethod(
            q,
            [this, syncRc]() {
                resetGuiSnapshot();
                Q_EMIT q->error(QStringLiteral("pw_core_sync failed: %1").arg(syncRc));
            },
            Qt::QueuedConnection);
        return;
    }
    pendingSyncSeq = syncRc;
}

void PipeWireConnection::Private::doDisconnect()
{
    // Tear down per-node state before the registry / core so the
    // listener removals run while the proxy + core still exist. We
    // skip clearing entry->proxy because the loopNodes.clear() call
    // immediately below destroys the owning unique_ptr, freeing the
    // LoopNode storage — the write would be a dead store.
    for (auto& kv : loopNodes) {
        auto& entry = kv.second;
        if (entry && entry->proxy) {
            spa_hook_remove(&entry->nodeListener);
            pw_proxy_destroy(entry->proxy);
        }
    }
    loopNodes.clear();

    if (defaultMetadata) {
        spa_hook_remove(&defaultMetadataListener);
        pw_proxy_destroy(defaultMetadata);
        defaultMetadata = nullptr;
    }
    defaultMetadataId = SPA_ID_INVALID;

    if (registry) {
        spa_hook_remove(&registryListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
        registry = nullptr;
    }
    if (core) {
        spa_hook_remove(&coreListener);
        pw_core_disconnect(core);
        core = nullptr;
    }
    if (context) {
        pw_context_destroy(context);
        context = nullptr;
    }
    pendingSyncSeq = kNoPendingSync;
    // Clear the wedged flag: after a full teardown there is no stale
    // core/context to recover from, so the next doConnect must NOT
    // misinterpret a fresh in-flight handshake as a wedge.
    wedged = false;
    // Synchronously flip the atomics on the loop thread BEFORE queueing
    // the GUI-side reset. After this point both isConnected() and
    // isDaemonAvailable() report false the moment the next caller
    // looks, matching the real loop-side state. The queued
    // resetGuiSnapshot() only does shadow-dedup + NOTIFY emits; it no
    // longer writes the atomics (see setConnected /
    // setDaemonAvailable), so we have to flip them here.
    connected.store(false, std::memory_order_release);
    daemonAvailable.store(false, std::memory_order_release);
    QMetaObject::invokeMethod(
        q,
        [this]() {
            resetGuiSnapshot();
            guiNodesReset();
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

PipeWireConnection::PipeWireConnection(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
    detail::ensurePipeWireInit();
    d->thread.start();
    // Block until the worker has either created the loop or given up.
    // After this point `d->loop` is stable (either a valid pointer or
    // null with the thread already exiting) so the destructor's quit
    // path can't race against worker startup.
    d->startupReady.acquire();
}

PipeWireConnection::~PipeWireConnection()
{
    // Loud guard for accidental cross-thread destruction. The
    // destructor's QCoreApplication::sendPostedEvents / removePostedEvents
    // drain at the bottom only works correctly when invoked from the
    // GUI thread that the QObject lives on; calling either of those
    // from a non-owner thread is undefined per Qt's documented event-
    // dispatcher contract. Catch the misuse loudly in debug; in release
    // log the violation and skip the cross-thread-unsafe drain so we
    // don't induce UB silently. The loop quit + thread wait above are
    // already MT-safe and run unconditionally.
    const bool ownerThread = (thread() == QThread::currentThread());
    Q_ASSERT(ownerThread);
    // Short-circuit when the worker has already self-exited: with
    // isRunning() == false we know LoopThread::run has progressed past
    // its mutex-guarded destroy+null-store and there's no live loop to
    // quit. Avoids taking loopMutex in the common shutdown case where
    // the worker is already gone.
    if (d->thread.isRunning()) {
        // Hold loopMutex across BOTH the load AND the quit so the
        // worker thread cannot destroy the loop between those two
        // operations. Without the mutex, a spontaneous loop exit
        // (libpipewire-internal error, daemon kill mid-run) lets the
        // worker race through doDisconnect + pw_main_loop_destroy
        // after we load the pointer but before we call quit on it —
        // classic TOCTOU UAF. The matching lock in LoopThread::run
        // makes the load+quit pair atomic w.r.t. destroy+null-store.
        // pw_main_loop_quit itself is MT-safe (routes through
        // pw_loop_signal_event internally); quitting before the loop
        // has started running is also safe — pw_main_loop records
        // the request and exits on entry to pw_main_loop_run.
        QMutexLocker locker(&d->loopMutex);
        if (pw_main_loop* mainLoop = d->loop.load(std::memory_order_acquire)) {
            pw_main_loop_quit(mainLoop);
        }
    }
    if (!d->thread.wait(5000)) {
        // FATAL-BY-DESIGN: a stuck pw_main_loop_run means libpipewire
        // internal state cannot be reclaimed cleanly. We accept the
        // leak because the only path that gets here is process
        // shutdown — the OS reaps the address space immediately
        // after. A future reader should NOT try to soften this into
        // a recovery loop: the loop thread is wedged inside foreign
        // code, and forcibly resuming it would risk corrupting any
        // global libpipewire state still in use elsewhere.
        qCCritical(lcPipeWire) << "PipeWire loop thread did not exit within 5s; terminating "
                                  "(libpipewire state leaked; only acceptable at process shutdown)";
        d->thread.terminate();
        // Bound the post-terminate join too. terminate() is advisory on
        // some platforms (it can be a no-op), so an unbounded wait here
        // would undercut the 5s budget above and risk hanging process
        // exit on a truly wedged thread. Give it a short budget and move
        // on — the OS reaps the thread with the address space regardless.
        if (!d->thread.wait(2000)) {
            qCCritical(lcPipeWire) << "PipeWire loop thread still alive after terminate(); "
                                      "abandoning at shutdown (address space reaped by the OS)";
        }
    }
    // The loop thread's final doDisconnect — and every other loop-side
    // callback that ran before quit — posts QMetaObject::invokeMethod
    // lambdas targeting `this` via QueuedConnection. By the time the
    // destructor runs they are sitting in the GUI thread's event
    // queue, and observers wired to nodeAdded / nodeRemoved have not
    // yet seen them.
    //
    // First flush: deliver every queued cross-thread post targeting
    // `this` so observers (PwNodeModel, KCM widgets, ...) receive the
    // final round of add/remove signals and drop any pointers to
    // PwNode children that are about to be destroyed. Then drop
    // anything else still queued for `this` so the GUI event loop
    // doesn't dispatch into a half-destroyed object after we return.
    //
    // Subclassing caveat: by the time this drain runs, the subclass
    // destructor has already replaced the vtable with the base
    // PipeWireConnection one. A subclass that connected its OWN slots
    // to nodeAdded / nodeRemoved / error and relied on subclass-side
    // state in those slots will NOT see the final drain — the
    // subclass is gone by then. External observers (separate QObjects
    // wired via Qt::QueuedConnection) are the supported pattern. See
    // the matching note in pipewireconnection.cpp's "Cross-thread
    // invariants" block.
    if (ownerThread) {
        QCoreApplication::sendPostedEvents(this, 0);
        QCoreApplication::removePostedEvents(this);
    } else {
        qCCritical(lcPipeWire) << "PipeWireConnection destroyed from non-owner thread; "
                                  "skipping queued-event drain to avoid UB. Observers may miss the "
                                  "final nodeAdded/nodeRemoved batch.";
    }
}

// connectToDaemon / disconnectFromDaemon bypass submitLoopRequest because
// they carry no payload (no heap request, no ownership handoff), so the
// direct pw_loop_invoke call is clearer than constructing an empty
// request struct and routing it through the template. A negative rc
// means the loop refused the post; surface it as a warning so the
// failure isn't silently dropped, but keep the call shape simple. See
// submitLoopRequest in pipewireconnection_writes.cpp for the rc < 0
// rationale (it's the same: pw_loop_invoke returns a non-negative seq
// on success or a negative errno when the loop queue refuses the post).
//
// loopMutex scope: same TOCTOU window the destructor closes — a
// spontaneous loop exit (libpipewire-internal error, daemon kill
// mid-run) can race LoopThread::run's doDisconnect +
// pw_main_loop_destroy in between our load and our pw_loop_invoke, so
// we'd hand a destroyed loop pointer to pw_loop_invoke. Hold loopMutex
// across the load AND the invoke so the worker's destroy+null-store
// stays serialised with us. No deadlock: the worker only acquires
// loopMutex during post-pw_main_loop_run teardown, well outside this
// path.
void PipeWireConnection::connectToDaemon()
{
    if (!d->thread.isRunning())
        return;
    int rc;
    {
        QMutexLocker locker(&d->loopMutex);
        pw_main_loop* mainLoop = d->loop.load(std::memory_order_acquire);
        if (!mainLoop)
            return;
        rc = pw_loop_invoke(pw_main_loop_get_loop(mainLoop), &Private::dispatchConnect, 0, nullptr, 0, false, d.get());
    }
    if (rc < 0) {
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for connectToDaemon rc" << rc;
        // Surface the dispatch failure to GUI observers. A debug-only
        // log line lets a buggy host swallow the failure silently and
        // leaves a status indicator wedged in "connecting…" forever.
        // Queue on the GUI thread (`this` lives there) so this entry
        // point stays callable from either thread without violating
        // signal-thread affinity.
        Private* dd = d.get();
        const int rcCopy = rc;
        QMetaObject::invokeMethod(
            this,
            [dd, rcCopy]() {
                Q_EMIT dd->q->error(
                    QStringLiteral("dispatch: pw_loop_invoke failed for connectToDaemon: %1").arg(rcCopy));
            },
            Qt::QueuedConnection);
    }
}

void PipeWireConnection::disconnectFromDaemon()
{
    if (!d->thread.isRunning())
        return;
    int rc;
    {
        QMutexLocker locker(&d->loopMutex);
        pw_main_loop* mainLoop = d->loop.load(std::memory_order_acquire);
        if (!mainLoop)
            return;
        rc = pw_loop_invoke(pw_main_loop_get_loop(mainLoop), &Private::dispatchDisconnect, 0, nullptr, 0, false,
                            d.get());
    }
    if (rc < 0) {
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for disconnectFromDaemon rc" << rc;
        // Same rationale as connectToDaemon's dispatch-failure branch:
        // a silent debug log leaves observers unaware that the
        // disconnect they requested never landed.
        Private* dd = d.get();
        const int rcCopy = rc;
        QMetaObject::invokeMethod(
            this,
            [dd, rcCopy]() {
                Q_EMIT dd->q->error(
                    QStringLiteral("dispatch: pw_loop_invoke failed for disconnectFromDaemon: %1").arg(rcCopy));
            },
            Qt::QueuedConnection);
    }
}

} // namespace PhosphorServicePipeWire
