// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Registry-event handlers and the per-global bind helpers for
// PipeWireConnection. All definitions are members of
// PipeWireConnection::Private and share the declarations in
// pipewireconnection_p.h; this file does not introduce any new public
// surface.

#include "pipewireconnection_p.h"

#include <PhosphorServicePipeWire/PwNode.h>

#include <QHash>
#include <QString>

#include <string_view>

namespace PhosphorServicePipeWire {

const pw_registry_events PipeWireConnection::Private::kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = &PipeWireConnection::Private::onRegistryGlobal,
    .global_remove = &PipeWireConnection::Private::onRegistryGlobalRemove,
};

void PipeWireConnection::Private::onRegistryGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                                                   uint32_t version, const struct spa_dict* props)
{
    Q_UNUSED(permissions);
    Q_UNUSED(version);
    auto* d = static_cast<Private*>(data);
    if (!type)
        return;
    const std::string_view typeView(type);
    // Two interfaces are relevant: Node (audio sinks / sources /
    // streams) and Metadata (WirePlumber's `default` for the runtime
    // default audio sink + source). Dispatch into a typed helper so
    // the C callback stays a thin router.
    if (typeView == PW_TYPE_INTERFACE_Metadata) {
        d->bindDefaultMetadata(id, type, detail::propsFromDict(props));
        return;
    }
    if (typeView != PW_TYPE_INTERFACE_Node)
        return;
    const auto p = detail::propsFromDict(props);
    const QString mediaClass = p.value(QStringLiteral("media.class"));
    if (!detail::isAudioNodeClass(mediaClass)) {
        qCDebug(lcPipeWire) << "skipping non-audio node" << id << "mediaClass" << mediaClass;
        return;
    }
    d->bindAudioNode(id, type, mediaClass, p);
}

void PipeWireConnection::Private::bindDefaultMetadata(uint32_t id, const char* type,
                                                      const QHash<QString, QString>& props)
{
    if (props.value(QStringLiteral("metadata.name")) != QLatin1String("default"))
        return;
    if (defaultMetadata) {
        qCDebug(lcPipeWire) << "duplicate default-metadata global; ignoring id" << id;
        return;
    }
    defaultMetadata = static_cast<pw_proxy*>(pw_registry_bind(registry, id, type, PW_VERSION_METADATA, 0));
    if (!defaultMetadata) {
        qCWarning(lcPipeWire) << "pw_registry_bind failed for default metadata global" << id;
        return;
    }
    // pw_metadata_add_listener is a void macro that wraps
    // spa_interface_call_method; failure cannot be observed at this
    // layer. defaultMetadataId is set after the call under the
    // invariant "id set implies fully-wired listener" — if a future
    // iface refactor makes add_listener observably fail, this
    // invariant breaks and the bind path will need a new
    // failure-handling branch (clear defaultMetadata, leave id at
    // SPA_ID_INVALID).
    pw_metadata_add_listener(reinterpret_cast<pw_metadata*>(defaultMetadata), &defaultMetadataListener,
                             &kDefaultMetadataEvents, this);
    // Cache the registry id LAST so the invariant "defaultMetadataId !=
    // SPA_ID_INVALID implies defaultMetadata is bound AND its listener
    // is attached" holds at every observation point. If a future
    // refactor inserts a failure path between the bind and the
    // listener attach, the id stays at SPA_ID_INVALID and
    // onRegistryGlobalRemove cannot misroute against a half-bound
    // proxy.
    defaultMetadataId = id;
    qCDebug(lcPipeWire) << "bound default metadata id" << id;
}

void PipeWireConnection::Private::bindAudioNode(uint32_t id, const char* type, const QString& mediaClass,
                                                const QHash<QString, QString>& props)
{
    qCDebug(lcPipeWire) << "audio node added: id" << id << "mediaClass" << mediaClass << "name"
                        << props.value(QStringLiteral("node.name"));

    // Bind the proxy on the loop thread so we can subscribe to its
    // info + param events. Track it in loopNodes for cleanup on
    // global_remove.
    // Defensive: PipeWire's protocol guarantees one global per id between
    // global / global_remove pairs, but a buggy daemon (or a future
    // protocol extension that fires `global` before `global_remove` on
    // an existing id) would have us emplace into a key that is already
    // occupied. std::unordered_map::emplace would destroy the moved-from
    // unique_ptr's allocation and leave entryPtr (captured below)
    // dangling — and we'd then wire a libpipewire listener pointing at
    // freed memory. Reject up-front before the bind so we don't leak a
    // proxy either.
    if (loopNodes.count(id) != 0) {
        qCWarning(lcPipeWire) << "duplicate node global; ignoring id" << id;
        return;
    }
    auto* proxy = static_cast<pw_proxy*>(pw_registry_bind(registry, id, type, PW_VERSION_NODE, 0));
    if (!proxy) {
        qCWarning(lcPipeWire) << "pw_registry_bind failed for node" << id;
        return;
    }
    auto entry = std::make_unique<LoopNode>();
    entry->owner = this;
    entry->id = id;
    entry->mediaClass = mediaClass;
    entry->proxy = proxy;
    // Move the entry into the map BEFORE wiring the C-side listener.
    // If the emplace throws (allocator failure), the unique_ptr cleans
    // up the entry on scope exit; wiring the spa_hook first would
    // leave it pointing at a freed entry.
    LoopNode* entryPtr = entry.get();
    loopNodes.emplace(id, std::move(entry));
    pw_node_add_listener(reinterpret_cast<pw_node*>(proxy), &entryPtr->nodeListener, &kNodeEvents, entryPtr);

    // Queue guiNodeAdded BEFORE pw_node_enum_params /
    // pw_node_subscribe_params. The libpipewire listener can
    // synchronously fire onNodeInfo / onNodeParam from inside the
    // enum/subscribe call for cached state on the proxy, and those
    // callbacks queue their own GUI-thread lambdas onto the same
    // target (`q`) via Qt::QueuedConnection. Qt's queued post
    // delivery on a single target is strictly FIFO, so a
    // guiNodeInfo / applyProps lambda queued before guiNodeAdded
    // would land first and silently early-return on
    // `guiNodes.find(id) == end()` — the node would appear in the
    // model without its initial info or props, and observers
    // wiring infoChanged via nodeAdded would never see the first
    // batch. Queue the create-event first to guarantee the GUI
    // node exists before any info/param drain.
    QMetaObject::invokeMethod(
        q,
        [this, id, mediaClass, props]() {
            guiNodeAdded(id, mediaClass, props);
        },
        Qt::QueuedConnection);

    // Pre-arm SPA_PARAM_Props: enumerate the current pod, then
    // subscribe so future updates (external volume changes from
    // pavucontrol, WirePlumber policy reconciles, hotkey-driven
    // adjustments) propagate as `param` events. Without the
    // subscription only the initial enumeration would land; every
    // out-of-band change would be invisible to the model.
    //
    // UINT32_MAX = "all params" per PipeWire convention. Trusted-
    // daemon assumption; a malicious daemon could ship unbounded param
    // events. Each event queues a GUI-thread lambda — flood vector if
    // the trust assumption breaks. If we ever need to defend against
    // an untrusted daemon, cap this via a sane upper bound and add
    // backpressure on the GUI-side handler.
    pw_node_enum_params(reinterpret_cast<pw_node*>(proxy), 0, SPA_PARAM_Props, 0, UINT32_MAX, nullptr);
    uint32_t subscribeIds[] = {SPA_PARAM_Props};
    pw_node_subscribe_params(reinterpret_cast<pw_node*>(proxy), subscribeIds, 1);
}

void PipeWireConnection::Private::onRegistryGlobalRemove(void* data, uint32_t id)
{
    auto* d = static_cast<Private*>(data);
    // Same id space covers nodes AND the metadata global. We can't use
    // pw_proxy_get_bound_id to identify the metadata proxy: it returns
    // SPA_ID_INVALID until the daemon's bound_id event arrives, and a
    // global_remove can plausibly fire before that ack (proxy bind
    // races a daemon-side teardown). Compare against the registry id
    // we captured at bind time instead — that one is always valid.
    // The `id != SPA_ID_INVALID` clause rejects the unbound sentinel
    // even if PipeWire ever surfaced one; the equality test against
    // `defaultMetadataId` already implies `defaultMetadata != nullptr`
    // via the invariant documented on `defaultMetadataId`, so no
    // separate `defaultMetadata` null-check is needed.
    if (d->defaultMetadataId == id && id != SPA_ID_INVALID) {
        spa_hook_remove(&d->defaultMetadataListener);
        pw_proxy_destroy(d->defaultMetadata);
        d->defaultMetadata = nullptr;
        d->defaultMetadataId = SPA_ID_INVALID;
        QMetaObject::invokeMethod(
            d->q,
            [d]() {
                d->setDefaultSinkName(QString());
                d->setDefaultSourceName(QString());
            },
            Qt::QueuedConnection);
        return;
    }
    auto it = d->loopNodes.find(id);
    if (it == d->loopNodes.end())
        return;
    auto entry = std::move(it->second);
    d->loopNodes.erase(it);
    if (entry->proxy) {
        spa_hook_remove(&entry->nodeListener);
        pw_proxy_destroy(entry->proxy);
    }
    QMetaObject::invokeMethod(
        d->q,
        [d, id]() {
            d->guiNodeRemoved(id);
        },
        Qt::QueuedConnection);
}

} // namespace PhosphorServicePipeWire
