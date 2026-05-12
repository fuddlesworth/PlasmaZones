// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/ForeignToplevel.h>
#include "qpa/layershellintegration.h"
#include "qpa/foreign_toplevel_protocol.h"

#include <QGuiApplication>
#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QScreen>
#include <QWindow>

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandinputdevice_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

Q_LOGGING_CATEGORY(lcForeignToplevel, "phosphorwayland.foreigntoplevel")

namespace PhosphorWayland {

// =====================================================================
// ForeignToplevel::Private
// =====================================================================

class ForeignToplevel::Private
{
public:
    ForeignToplevel* owner = nullptr;
    QPointer<ForeignToplevelManager> manager;
    struct zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    uint32_t version = 1;

    // Committed state — what callers see via the property accessors.
    QString title;
    QString appId;
    bool maximized = false;
    bool minimized = false;
    bool activated = false;
    bool fullscreen = false;
    QList<QPointer<QScreen>> outputs;
    QPointer<ForeignToplevel> parentToplevel;
    bool closed = false;

    // Pending state — what we're collecting between protocol events. The
    // protocol batches updates with a `done` event; we promote pending → live
    // and emit signals only at `done`. Mirrors xdg_toplevel's configure
    // semantics and matches what taskbars want (one repaint per logical
    // change, not per protocol event).
    QString pendingTitle;
    QString pendingAppId;
    bool pendingMaximized = false;
    bool pendingMinimized = false;
    bool pendingActivated = false;
    bool pendingFullscreen = false;
    QList<QPointer<QScreen>> pendingOutputs;
    QPointer<ForeignToplevel> pendingParent;
    bool titleDirty = false;
    bool appIdDirty = false;
    bool stateDirty = false;
    bool outputsDirty = false;
    bool parentDirty = false;

    static QScreen* screenForOutput(struct wl_output* output);
    static struct wl_output* outputForScreen(QScreen* screen);

    // ─── Protocol event handlers ────────────────────────────────────────
    //
    // Every handler must null-check `data`: ~ForeignToplevelManager nulls
    // the per-handle user_data via `wl_proxy_set_user_data(handle, nullptr)`
    // before destroying the handle, so any in-flight queued event for that
    // handle that hasn't dispatched yet lands here with data == nullptr.
    // Without the guard we'd dereference freed Private memory.

    static void handleTitle(void* data, struct zwlr_foreign_toplevel_handle_v1*, const char* title)
    {
        if (!data) {
            return;
        }
        auto* self = static_cast<Private*>(data);
        const QString s = QString::fromUtf8(title);
        if (self->pendingTitle != s) {
            self->pendingTitle = s;
            self->titleDirty = true;
        }
    }

    static void handleAppId(void* data, struct zwlr_foreign_toplevel_handle_v1*, const char* appId)
    {
        if (!data) {
            return;
        }
        auto* self = static_cast<Private*>(data);
        const QString s = QString::fromUtf8(appId);
        if (self->pendingAppId != s) {
            self->pendingAppId = s;
            self->appIdDirty = true;
        }
    }

    static void handleOutputEnter(void* data, struct zwlr_foreign_toplevel_handle_v1*, struct wl_output* output)
    {
        if (!data) {
            return;
        }
        auto* self = static_cast<Private*>(data);
        QScreen* screen = screenForOutput(output);
        if (!screen) {
            return;
        }
        // Sync pendingOutputs from current outputs on first dirty mutation
        // this batch — pending must reflect "current state + this batch's
        // changes" so a remove-only batch doesn't accidentally re-add.
        if (!self->outputsDirty) {
            self->pendingOutputs = self->outputs;
            self->outputsDirty = true;
        }
        for (const auto& s : self->pendingOutputs) {
            if (s.data() == screen) {
                return; // already present
            }
        }
        self->pendingOutputs.append(QPointer<QScreen>(screen));
    }

    static void handleOutputLeave(void* data, struct zwlr_foreign_toplevel_handle_v1*, struct wl_output* output)
    {
        if (!data) {
            return;
        }
        auto* self = static_cast<Private*>(data);
        QScreen* screen = screenForOutput(output);
        if (!screen) {
            return;
        }
        if (!self->outputsDirty) {
            self->pendingOutputs = self->outputs;
            self->outputsDirty = true;
        }
        self->pendingOutputs.removeIf([screen](const QPointer<QScreen>& s) {
            return s.data() == screen;
        });
    }

    static void handleState(void* data, struct zwlr_foreign_toplevel_handle_v1*, struct wl_array* state)
    {
        if (!data) {
            return;
        }
        auto* self = static_cast<Private*>(data);
        bool max = false, min = false, act = false, full = false;
        // Defensive: a misbehaving compositor can send a wl_array whose
        // size isn't a multiple of sizeof(uint32_t) — walk only complete
        // entries to avoid reading past the buffer end.
        if (state->size % sizeof(uint32_t) != 0) {
            qCWarning(lcForeignToplevel) << "Compositor sent malformed state array; ignoring";
            return;
        }
        const uint32_t* values = static_cast<const uint32_t*>(state->data);
        const size_t count = state->size / sizeof(uint32_t);
        for (size_t i = 0; i < count; ++i) {
            switch (values[i]) {
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
                max = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
                min = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
                act = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
                full = true;
                break;
            default:
                break;
            }
        }
        if (max != self->pendingMaximized || min != self->pendingMinimized || act != self->pendingActivated
            || full != self->pendingFullscreen) {
            self->pendingMaximized = max;
            self->pendingMinimized = min;
            self->pendingActivated = act;
            self->pendingFullscreen = full;
            self->stateDirty = true;
        }
    }

    static void handleParent(void* data, struct zwlr_foreign_toplevel_handle_v1*,
                             struct zwlr_foreign_toplevel_handle_v1* parent);

    static void handleClosed(void* data, struct zwlr_foreign_toplevel_handle_v1*);

    static void handleDone(void* data, struct zwlr_foreign_toplevel_handle_v1*)
    {
        if (!data) {
            return;
        }
        auto* self = static_cast<Private*>(data);
        if (self->titleDirty) {
            self->title = self->pendingTitle;
            self->titleDirty = false;
            Q_EMIT self->owner->titleChanged();
        }
        if (self->appIdDirty) {
            self->appId = self->pendingAppId;
            self->appIdDirty = false;
            Q_EMIT self->owner->appIdChanged();
        }
        if (self->stateDirty) {
            self->maximized = self->pendingMaximized;
            self->minimized = self->pendingMinimized;
            self->activated = self->pendingActivated;
            self->fullscreen = self->pendingFullscreen;
            self->stateDirty = false;
            Q_EMIT self->owner->stateChanged();
        }
        if (self->outputsDirty) {
            self->outputs = self->pendingOutputs;
            self->outputsDirty = false;
            Q_EMIT self->owner->outputsChanged();
        }
        if (self->parentDirty) {
            self->parentToplevel = self->pendingParent;
            self->parentDirty = false;
            Q_EMIT self->owner->parentToplevelChanged();
        }
    }
};

// =====================================================================
// ForeignToplevelManager::Private
// =====================================================================

class ForeignToplevelManager::Private
{
public:
    ForeignToplevelManager* owner = nullptr;
    bool stopped = false;
    QHash<struct zwlr_foreign_toplevel_handle_v1*, ForeignToplevel*> toplevels;

    static void handleToplevel(void* data, struct zwlr_foreign_toplevel_manager_v1*,
                               struct zwlr_foreign_toplevel_handle_v1* handle)
    {
        // Manager dtor sets user_data to nullptr before destroying its
        // Private — any event still in flight after that point lands here
        // with a null pointer. Destroy the orphan handle to avoid a leak,
        // then bail (no Private to dispatch into).
        if (!data) {
            zwlr_foreign_toplevel_handle_v1_destroy(handle);
            return;
        }
        auto* self = static_cast<Private*>(data);
        if (self->stopped) {
            // We told the compositor to stop, but in-flight events may still
            // arrive. Destroy the orphan handle so we don't leak.
            zwlr_foreign_toplevel_handle_v1_destroy(handle);
            return;
        }

        auto p = std::make_unique<ForeignToplevel::Private>();
        ForeignToplevel::Private* raw = p.get();
        auto* tl = new ForeignToplevel(std::move(p));
        raw->owner = tl;
        raw->manager = self->owner;
        raw->handle = handle;
        // Read the version from the per-HANDLE proxy, not from the
        // manager. The protocol allows handles to be bound at a
        // version different from the manager itself (though in
        // practice every known compositor binds them at the same
        // version). Using the handle's own version is the
        // spec-correct read.
        raw->version = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(handle));

        static const struct zwlr_foreign_toplevel_handle_v1_listener listener = {
            .title = ForeignToplevel::Private::handleTitle,
            .app_id = ForeignToplevel::Private::handleAppId,
            .output_enter = ForeignToplevel::Private::handleOutputEnter,
            .output_leave = ForeignToplevel::Private::handleOutputLeave,
            .state = ForeignToplevel::Private::handleState,
            .done = ForeignToplevel::Private::handleDone,
            .closed = ForeignToplevel::Private::handleClosed,
            .parent = ForeignToplevel::Private::handleParent,
        };
        zwlr_foreign_toplevel_handle_v1_add_listener(handle, &listener, raw);

        self->toplevels.insert(handle, tl);
        Q_EMIT self->owner->toplevelAdded(tl);
    }

    static void handleFinished(void* data, struct zwlr_foreign_toplevel_manager_v1*)
    {
        // `finished` is the protocol's destructor event — libwayland
        // will reclaim the proxy after this handler returns. Notify
        // the integration so its destructor doesn't try to stop() a
        // proxy that's about to be freed (CVE-class UAF if the
        // integration's dtor runs after this handler but before
        // wl_display teardown). The clear is idempotent — safe even
        // if the integration is already torn down.
        if (auto* integration = LayerShellIntegration::instance()) {
            integration->clearForeignToplevelManager();
        }
        // Same dangling-listener guard as handleToplevel: if the manager's
        // Private has already been destroyed, `data` was set to nullptr
        // and there's nothing left to mark stopped. The proxy itself is
        // owned by LayerShellIntegration and will be reclaimed by the
        // wl_display teardown.
        if (!data) {
            return;
        }
        auto* self = static_cast<Private*>(data);
        self->stopped = true;
    }
};

// =====================================================================
// Cross-class helpers
// =====================================================================

QScreen* ForeignToplevel::Private::screenForOutput(struct wl_output* output)
{
    if (!output) {
        return nullptr;
    }
    // Walk Qt's screens looking for one whose underlying wl_output matches.
    // QtWayland's QWaylandScreen exposes the wl_output pointer through its
    // private API.
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (!screen || !screen->handle()) {
            continue;
        }
        auto* wlScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(screen->handle());
        if (wlScreen && wlScreen->output() == output) {
            return screen;
        }
    }
    return nullptr;
}

struct wl_output* ForeignToplevel::Private::outputForScreen(QScreen* screen)
{
    if (!screen || !screen->handle()) {
        return nullptr;
    }
    auto* wlScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(screen->handle());
    return wlScreen ? wlScreen->output() : nullptr;
}

void ForeignToplevel::Private::handleParent(void* data, struct zwlr_foreign_toplevel_handle_v1*,
                                            struct zwlr_foreign_toplevel_handle_v1* parent)
{
    if (!data) {
        return;
    }
    auto* self = static_cast<Private*>(data);
    ForeignToplevel* parentObj = nullptr;
    if (parent && self->manager) {
        parentObj = self->manager->d->toplevels.value(parent, nullptr);
    }
    if (self->pendingParent.data() != parentObj) {
        self->pendingParent = parentObj;
        self->parentDirty = true;
    }
}

void ForeignToplevel::Private::handleClosed(void* data, struct zwlr_foreign_toplevel_handle_v1*)
{
    if (!data) {
        return;
    }
    auto* self = static_cast<Private*>(data);
    self->closed = true;
    // Detach from manager FIRST so any closedChanged listener iterating
    // manager->toplevels() does not still see this entry (consistent
    // observer state). Manager pointer is QPointer-guarded — it may have
    // been destroyed mid-event-dispatch during teardown.
    if (self->manager) {
        self->manager->d->toplevels.remove(self->handle);

        // Notify any sibling toplevel whose parentToplevel referenced us.
        // QPointer<ForeignToplevel> in pendingParent / parentToplevel will
        // auto-clear once the deleteLater() below actually runs, but QML
        // bindings can still see the stale pointer between now and that
        // event-loop pass — eagerly clearing + emitting parentToplevelChanged
        // closes the gap.
        //
        // Snapshot the toplevel set before the loop so emitting
        // parentToplevelChanged on a sibling can't synchronously mutate
        // the source hash mid-iteration (a slot might call manager->stop()
        // or destruct the manager from a Q_INVOKABLE chain). Wayland is
        // single-threaded by Qt's dispatcher so no concurrent mutation,
        // but synchronous re-entry from QML JS slots is allowed.
        //
        // Edge case worth noting but not fixing: pendingParent can point
        // to us if a reparent batch hasn't yet hit `done`. We clear
        // pendingParent and reset parentDirty, which DROPS that in-flight
        // batch. That's the correct outcome — the parent (us) is gone, so
        // promoting pendingParent at done would just bind the sibling to
        // a deleteLater'd object. The compositor is expected to issue
        // `done` after a coherent reparent, so a closed event arriving
        // mid-batch implies the batch was already invalidated.
        const auto siblings = self->manager->d->toplevels.values();
        for (ForeignToplevel* sibling : siblings) {
            if (!sibling) {
                continue;
            }
            auto& sd = *sibling->d;
            bool changed = false;
            if (sd.parentToplevel.data() == self->owner) {
                sd.parentToplevel.clear();
                changed = true;
            }
            if (sd.pendingParent.data() == self->owner) {
                sd.pendingParent.clear();
                sd.parentDirty = false;
            }
            if (changed) {
                Q_EMIT sibling->parentToplevelChanged();
            }
        }
    }
    if (self->handle) {
        zwlr_foreign_toplevel_handle_v1_destroy(self->handle);
        self->handle = nullptr;
    }
    Q_EMIT self->owner->closedChanged();
    if (self->manager) {
        Q_EMIT self->manager->toplevelRemoved(self->owner);
    }
    self->owner->deleteLater();
}

// =====================================================================
// ForeignToplevelManager
// =====================================================================

ForeignToplevelManager::ForeignToplevelManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;

    auto* integration = LayerShellIntegration::instance();
    if (!integration) {
        qCWarning(lcForeignToplevel) << "ForeignToplevelManager: PhosphorWayland integration not loaded —"
                                     << "auto-fit panel surfaces won't see toplevel events";
        return;
    }
    auto* manager = integration->foreignToplevelManager();
    if (!manager) {
        qCDebug(lcForeignToplevel) << "Compositor doesn't advertise zwlr_foreign_toplevel_manager_v1 — "
                                      "ForeignToplevelManager will be inert";
        return;
    }

    static const struct zwlr_foreign_toplevel_manager_v1_listener listener = {
        .toplevel = Private::handleToplevel,
        .finished = Private::handleFinished,
    };
    zwlr_foreign_toplevel_manager_v1_add_listener(manager, &listener, d.get());
}

ForeignToplevelManager::~ForeignToplevelManager()
{
    // Don't call stop() here — the protocol's manager object is owned by
    // LayerShellIntegration, not us. If we call stop() the compositor will
    // tear down the global, breaking subsequent ForeignToplevelManager
    // instances. The integration's destructor stops the manager instead.

    // Sever the listener's data pointer FIRST. The static handlers
    // (handleToplevel / handleFinished) read `data` and our null-checks
    // there bail out cleanly, so any event that arrives between now and
    // the proxy's eventual reclamation by wl_display teardown can't
    // dereference our about-to-be-freed Private. Without this, the
    // `finished` event the integration's stop() solicits could land on
    // dangling memory if it arrives after we go away.
    //
    // Use the raw-proxy accessor that bypasses the
    // availability gate: when the compositor has already removed the
    // global, `foreignToplevelManager()` returns nullptr and the
    // listener-data nulling would be skipped — leaving the dangling
    // pointer for any in-flight event to dereference.
    if (auto* integration = LayerShellIntegration::instance()) {
        if (auto* manager = integration->rawForeignToplevelManagerProxy()) {
            wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(manager), nullptr);
        }
    }

    // Destroy the per-toplevel handles synchronously. Use `delete` (not
    // deleteLater) — at process shutdown the QCoreApplication event loop
    // may already be gone, in which case deleteLater queues into a sink
    // and the objects leak with a Qt warning.
    //
    // QPointer snapshot defends against the case where a consumer slot
    // queued a `deleteLater()` on a toplevel before we got here: that
    // toplevel may already be in the QObject deferred-delete sink, and
    // `delete tl` would race with the pending DeferredDelete event.
    // Each QPointer auto-clears the moment the QObject is destroyed,
    // so we delete only entries that are still alive.
    QList<QPointer<ForeignToplevel>> snapshot;
    snapshot.reserve(d->toplevels.size());
    for (ForeignToplevel* tl : std::as_const(d->toplevels)) {
        snapshot.append(QPointer<ForeignToplevel>(tl));
    }
    d->toplevels.clear();
    for (auto& tlp : snapshot) {
        ForeignToplevel* tl = tlp.data();
        if (!tl) {
            continue;
        }
        if (tl->d->handle) {
            // Symmetric with the manager-proxy nulling above: clear the
            // per-handle listener data before destroying so any in-flight
            // event (queued before destroy round-trips) lands on null
            // and our handlers can null-check rather than dereference
            // a freed Private. Each handle's `add_listener` was set to
            // tl->d (a raw Private*) in handleToplevel.
            wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(tl->d->handle), nullptr);
            zwlr_foreign_toplevel_handle_v1_destroy(tl->d->handle);
            tl->d->handle = nullptr;
        }
        delete tl;
    }
}

bool ForeignToplevelManager::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->foreignToplevelManager();
}

QList<ForeignToplevel*> ForeignToplevelManager::toplevels() const
{
    return d->toplevels.values();
}

void ForeignToplevelManager::stop()
{
    if (d->stopped) {
        return;
    }
    d->stopped = true;
    auto* integration = LayerShellIntegration::instance();
    if (!integration) {
        return;
    }
    auto* manager = integration->foreignToplevelManager();
    if (manager) {
        zwlr_foreign_toplevel_manager_v1_stop(manager);
    }
}

// =====================================================================
// ForeignToplevel
// =====================================================================

ForeignToplevel::ForeignToplevel(std::unique_ptr<Private> p)
    : QObject(nullptr)
    , d(std::move(p))
{
}

ForeignToplevel::~ForeignToplevel()
{
    if (d->handle) {
        zwlr_foreign_toplevel_handle_v1_destroy(d->handle);
        d->handle = nullptr;
    }
}

QString ForeignToplevel::title() const
{
    return d->title;
}

QString ForeignToplevel::appId() const
{
    return d->appId;
}

bool ForeignToplevel::isMaximized() const
{
    return d->maximized;
}

bool ForeignToplevel::isMinimized() const
{
    return d->minimized;
}

bool ForeignToplevel::isActivated() const
{
    return d->activated;
}

bool ForeignToplevel::isFullscreen() const
{
    return d->fullscreen;
}

QList<QScreen*> ForeignToplevel::outputs() const
{
    QList<QScreen*> result;
    result.reserve(d->outputs.size());
    for (const auto& s : d->outputs) {
        if (s) {
            result.append(s.data());
        }
    }
    return result;
}

ForeignToplevel* ForeignToplevel::parentToplevel() const
{
    return d->parentToplevel.data();
}

bool ForeignToplevel::isClosed() const
{
    return d->closed;
}

void ForeignToplevel::activate()
{
    if (!d->handle) {
        return;
    }
    auto* integration = LayerShellIntegration::instance();
    if (!integration || !integration->display()) {
        return;
    }
    auto seats = integration->display()->inputDevices();
    if (seats.isEmpty()) {
        qCWarning(lcForeignToplevel) << "activate(): no seat available";
        return;
    }
    struct wl_seat* seat = seats.first()->wl_seat();
    if (!seat) {
        return;
    }
    zwlr_foreign_toplevel_handle_v1_activate(d->handle, seat);
}

void ForeignToplevel::close()
{
    if (d->handle) {
        zwlr_foreign_toplevel_handle_v1_close(d->handle);
    }
}

void ForeignToplevel::setMaximized(bool maximized)
{
    if (!d->handle) {
        return;
    }
    if (maximized) {
        zwlr_foreign_toplevel_handle_v1_set_maximized(d->handle);
    } else {
        zwlr_foreign_toplevel_handle_v1_unset_maximized(d->handle);
    }
}

void ForeignToplevel::setMinimized(bool minimized)
{
    if (!d->handle) {
        return;
    }
    if (minimized) {
        zwlr_foreign_toplevel_handle_v1_set_minimized(d->handle);
    } else {
        zwlr_foreign_toplevel_handle_v1_unset_minimized(d->handle);
    }
}

void ForeignToplevel::setFullscreen(bool fullscreen, QScreen* output)
{
    if (!d->handle) {
        return;
    }
    if (d->version < 2) {
        qCDebug(lcForeignToplevel) << "setFullscreen: protocol v1 — silently ignored";
        return;
    }
    if (fullscreen) {
        struct wl_output* wlOutput = output ? Private::outputForScreen(output) : nullptr;
        zwlr_foreign_toplevel_handle_v1_set_fullscreen(d->handle, wlOutput);
    } else {
        zwlr_foreign_toplevel_handle_v1_unset_fullscreen(d->handle);
    }
}

void ForeignToplevel::setRectangle(QWindow* surface, const QRect& rect)
{
    if (!d->handle || !surface) {
        return;
    }
    auto* platformWindow = surface->handle();
    if (!platformWindow) {
        qCDebug(lcForeignToplevel) << "setRectangle: window has no platform handle — ignored";
        return;
    }
    auto* wlWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(platformWindow);
    if (!wlWindow) {
        qCDebug(lcForeignToplevel) << "setRectangle: window is not a Wayland window — ignored";
        return;
    }
    struct wl_surface* wlSurface = wlWindow->wlSurface();
    if (!wlSurface) {
        return;
    }
    zwlr_foreign_toplevel_handle_v1_set_rectangle(d->handle, wlSurface, rect.x(), rect.y(), rect.width(),
                                                  rect.height());
}

} // namespace PhosphorWayland
