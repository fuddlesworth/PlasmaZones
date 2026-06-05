// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/ClipboardDevice.h>

#include "qpa/data_control_protocol.h"
#include "qpa/layershellintegration.h"

#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QSocketNotifier>

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandinputdevice_p.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

Q_LOGGING_CATEGORY(lcClipboardDevice, "phosphorwayland.clipboarddevice")

namespace PhosphorWayland {

namespace {
// Upper bound on a single clipboard payload we buffer in memory. Clipboard
// content is untrusted external input: any application can own the selection, so
// without a ceiling a hostile or runaway producer could stream unbounded bytes
// down the pipe and exhaust memory. A selection larger than this is dropped and
// delivered as a failed (empty) read rather than truncated, so a partial image
// or document is never recorded.
constexpr qint64 kMaxReadBytes = 100 * 1024 * 1024; // 100 MiB

// One in-flight asynchronous read of an offer over a pipe.
struct ReadContext
{
    QString mimeType;
    int fd = -1;
    QSocketNotifier* notifier = nullptr;
    QByteArray buffer;
};

// One in-flight asynchronous write servicing a paste request.
struct WriteContext
{
    QByteArray data;
    qint64 offset = 0;
    int fd = -1;
    QSocketNotifier* notifier = nullptr;
};

// Release a context's notifier + fd. Closing the fd before deleteLater ensures
// the (disabled) notifier can never fire on a closed/reused descriptor.
void finalizeIo(int& fd, QSocketNotifier*& notifier)
{
    if (notifier) {
        notifier->setEnabled(false);
        notifier->deleteLater();
        notifier = nullptr;
    }
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}
} // namespace

class ClipboardDevice::Private
{
public:
    ClipboardDevice* owner = nullptr;

    struct zwlr_data_control_device_v1* device = nullptr;

    // The offer that is the current clipboard selection, plus its MIME types.
    struct zwlr_data_control_offer_v1* currentOffer = nullptr;
    QStringList currentMimes;

    // Offers introduced by `data_offer` but not yet promoted by `selection`;
    // their `offer` events accumulate the MIME types here.
    QHash<struct zwlr_data_control_offer_v1*, QStringList> pendingOffers;

    // Selection we own (set via setSelection), and the bytes we serve per type.
    struct zwlr_data_control_source_v1* source = nullptr;
    QMap<QString, QByteArray> sourceData;

    std::vector<std::unique_ptr<ReadContext>> reads;
    std::vector<std::unique_ptr<WriteContext>> writes;

    void ensureDevice();
    void teardownDevice();
    void destroyOffer(struct zwlr_data_control_offer_v1* offer);
    void clearPendingOffers(struct zwlr_data_control_offer_v1* except);
    void destroySource();
    void flush();

    void startRead(const QString& mimeType, int readFd);
    void finishRead(ReadContext* ctx);
    void startWrite(const QByteArray& data, int writeFd);
    void finishWrite(WriteContext* ctx);

    // ─── device listener ───────────────────────────────────────────────
    static void handleDataOffer(void* data, struct zwlr_data_control_device_v1*,
                                struct zwlr_data_control_offer_v1* offer);
    static void handleSelection(void* data, struct zwlr_data_control_device_v1*,
                                struct zwlr_data_control_offer_v1* offer);
    static void handleFinished(void* data, struct zwlr_data_control_device_v1*);
    static void handlePrimarySelection(void* data, struct zwlr_data_control_device_v1*,
                                       struct zwlr_data_control_offer_v1* offer);

    // ─── offer listener ────────────────────────────────────────────────
    static void handleOfferMime(void* data, struct zwlr_data_control_offer_v1* offer, const char* mimeType);

    // ─── source listener ───────────────────────────────────────────────
    static void handleSend(void* data, struct zwlr_data_control_source_v1*, const char* mimeType, int32_t fd);
    static void handleCancelled(void* data, struct zwlr_data_control_source_v1*);
};

void ClipboardDevice::Private::flush()
{
    auto* integration = LayerShellIntegration::instance();
    if (integration && integration->display())
        integration->display()->flushRequests();
}

void ClipboardDevice::Private::ensureDevice()
{
    if (device)
        return;
    auto* integration = LayerShellIntegration::instance();
    if (!integration)
        return;
    auto* manager = integration->dataControlManager();
    if (!manager)
        return;
    auto* display = integration->display();
    if (!display)
        return;
    const auto seats = display->inputDevices();
    if (seats.isEmpty()) {
        qCWarning(lcClipboardDevice) << "No input seat available for the clipboard device";
        return;
    }
    struct wl_seat* seat = seats.first()->wl_seat();
    if (!seat)
        return;
    device = zwlr_data_control_manager_v1_get_data_device(manager, seat);
    if (device) {
        static const struct zwlr_data_control_device_v1_listener listener = {
            .data_offer = handleDataOffer,
            .selection = handleSelection,
            .finished = handleFinished,
            .primary_selection = handlePrimarySelection,
        };
        zwlr_data_control_device_v1_add_listener(device, &listener, this);
    }
}

void ClipboardDevice::Private::destroyOffer(struct zwlr_data_control_offer_v1* offer)
{
    if (offer) {
        // Sever the listener's back-pointer before destroying, so any event still
        // queued for this offer dispatches with data == nullptr and is dropped.
        wl_proxy_set_user_data(reinterpret_cast<struct wl_proxy*>(offer), nullptr);
        zwlr_data_control_offer_v1_destroy(offer);
    }
}

void ClipboardDevice::Private::clearPendingOffers(struct zwlr_data_control_offer_v1* except)
{
    for (auto it = pendingOffers.begin(); it != pendingOffers.end();) {
        struct zwlr_data_control_offer_v1* offer = it.key();
        if (offer == except) {
            ++it;
            continue;
        }
        destroyOffer(offer);
        it = pendingOffers.erase(it);
    }
}

void ClipboardDevice::Private::destroySource()
{
    if (source) {
        wl_proxy_set_user_data(reinterpret_cast<struct wl_proxy*>(source), nullptr);
        zwlr_data_control_source_v1_destroy(source);
        source = nullptr;
    }
    sourceData.clear();
}

void ClipboardDevice::Private::teardownDevice()
{
    // Finalize every in-flight read/write, then clear the vectors in one shot.
    // We must NOT call finishRead/finishWrite here: those erase the context from
    // the vector being iterated, which would invalidate the loop's iterator.
    for (auto& ctx : reads)
        finalizeIo(ctx->fd, ctx->notifier);
    reads.clear();
    for (auto& ctx : writes)
        finalizeIo(ctx->fd, ctx->notifier);
    writes.clear();

    destroySource();
    clearPendingOffers(nullptr);
    destroyOffer(currentOffer);
    currentOffer = nullptr;
    currentMimes.clear();

    if (device) {
        wl_proxy_set_user_data(reinterpret_cast<struct wl_proxy*>(device), nullptr);
        zwlr_data_control_device_v1_destroy(device);
        device = nullptr;
    }
}

// ─── reads ──────────────────────────────────────────────────────────────

void ClipboardDevice::Private::startRead(const QString& mimeType, int readFd)
{
    auto ctx = std::make_unique<ReadContext>();
    ctx->mimeType = mimeType;
    ctx->fd = readFd;
    ctx->notifier = new QSocketNotifier(readFd, QSocketNotifier::Read, owner);
    ReadContext* raw = ctx.get();
    QObject::connect(ctx->notifier, &QSocketNotifier::activated, owner, [this, raw] {
        char chunk[4096];
        for (;;) {
            const ssize_t n = ::read(raw->fd, chunk, sizeof(chunk));
            if (n > 0) {
                if (raw->buffer.size() + n > kMaxReadBytes) {
                    qCWarning(lcClipboardDevice) << "clipboard payload for" << raw->mimeType << "exceeds"
                                                 << kMaxReadBytes << "bytes; dropping the selection";
                    Q_EMIT owner->dataReceived(raw->mimeType, QByteArray());
                    finishRead(raw);
                    return;
                }
                raw->buffer.append(chunk, static_cast<int>(n));
                continue;
            }
            if (n == 0) { // EOF: the producer closed its end
                Q_EMIT owner->dataReceived(raw->mimeType, raw->buffer);
                finishRead(raw);
                return;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN) // == EWOULDBLOCK on Linux
                return; // wait for the next readiness edge
            // Hard error: deliver what we have as a failure.
            qCWarning(lcClipboardDevice) << "clipboard read failed for" << raw->mimeType << ":" << strerror(errno);
            Q_EMIT owner->dataReceived(raw->mimeType, QByteArray());
            finishRead(raw);
            return;
        }
    });
    reads.push_back(std::move(ctx));
}

void ClipboardDevice::Private::finishRead(ReadContext* ctx)
{
    if (!ctx)
        return;
    finalizeIo(ctx->fd, ctx->notifier);
    for (auto it = reads.begin(); it != reads.end(); ++it) {
        if (it->get() == ctx) {
            reads.erase(it);
            return;
        }
    }
}

// ─── writes (servicing paste requests) ────────────────────────────────────

void ClipboardDevice::Private::startWrite(const QByteArray& data, int writeFd)
{
    auto ctx = std::make_unique<WriteContext>();
    ctx->data = data;
    ctx->fd = writeFd;
    ctx->notifier = new QSocketNotifier(writeFd, QSocketNotifier::Write, owner);
    WriteContext* raw = ctx.get();
    QObject::connect(ctx->notifier, &QSocketNotifier::activated, owner, [this, raw] {
        while (raw->offset < raw->data.size()) {
            const qint64 n = ::write(raw->fd, raw->data.constData() + raw->offset, raw->data.size() - raw->offset);
            if (n > 0) {
                raw->offset += n;
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && errno == EAGAIN) // == EWOULDBLOCK on Linux
                return; // pipe full: wait for the next writability edge
            // Reader went away (EPIPE) or hard error: stop.
            break;
        }
        finishWrite(raw);
    });
    writes.push_back(std::move(ctx));
}

void ClipboardDevice::Private::finishWrite(WriteContext* ctx)
{
    if (!ctx)
        return;
    finalizeIo(ctx->fd, ctx->notifier);
    for (auto it = writes.begin(); it != writes.end(); ++it) {
        if (it->get() == ctx) {
            writes.erase(it);
            return;
        }
    }
}

// ─── device events ────────────────────────────────────────────────────────

void ClipboardDevice::Private::handleDataOffer(void* data, struct zwlr_data_control_device_v1*,
                                               struct zwlr_data_control_offer_v1* offer)
{
    auto* self = static_cast<Private*>(data);
    if (!self || !offer)
        return;
    // The offer's MIME types arrive next, as `offer` events, before `selection`.
    self->pendingOffers.insert(offer, QStringList());
    static const struct zwlr_data_control_offer_v1_listener listener = {
        .offer = handleOfferMime,
    };
    zwlr_data_control_offer_v1_add_listener(offer, &listener, self);
}

void ClipboardDevice::Private::handleSelection(void* data, struct zwlr_data_control_device_v1*,
                                               struct zwlr_data_control_offer_v1* offer)
{
    auto* self = static_cast<Private*>(data);
    if (!self)
        return;

    // Replace the previous selection's offer (if any).
    if (self->currentOffer && self->currentOffer != offer) {
        self->destroyOffer(self->currentOffer);
        self->currentOffer = nullptr;
    }

    // A selection for an offer we never saw via `data_offer` violates the
    // protocol; do not adopt (and so never destroy) an offer we do not own.
    if (offer && !self->pendingOffers.contains(offer)) {
        qCWarning(lcClipboardDevice) << "selection for an unknown offer; ignoring";
        offer = nullptr;
    }

    if (!offer) {
        // Selection cleared (or the anomalous case above).
        self->clearPendingOffers(nullptr);
        self->currentOffer = nullptr;
        self->currentMimes.clear();
        Q_EMIT self->owner->selectionChanged(QStringList());
        return;
    }

    self->currentMimes = self->pendingOffers.take(offer); // claim its mimes + remove its pending slot
    self->clearPendingOffers(offer); // destroy any other still-pending offers
    self->currentOffer = offer;
    Q_EMIT self->owner->selectionChanged(self->currentMimes);
}

void ClipboardDevice::Private::handleFinished(void* data, struct zwlr_data_control_device_v1*)
{
    // The compositor invalidated this device (e.g. the seat went away). Tear it
    // down; a fresh selection event stream would require a new device.
    auto* self = static_cast<Private*>(data);
    if (!self)
        return;

    // Capture the MIME types of any reads still in flight before teardown drops
    // them. We must honour the receive() contract (exactly one dataReceived per
    // receive) even on invalidation, or a consumer that serializes reads (the
    // history model) would wait forever for a reply that never comes.
    QStringList unfinishedReads;
    unfinishedReads.reserve(static_cast<int>(self->reads.size()));
    for (const auto& ctx : self->reads)
        unfinishedReads.append(ctx->mimeType);

    // Teardown first, so these failure emits — and any read the consumer issues
    // in response — see a torn-down device (no current offer) and resolve
    // immediately rather than touching the proxy we just destroyed.
    self->teardownDevice();

    Q_EMIT self->owner->selectionChanged(QStringList());
    for (const QString& mimeType : unfinishedReads)
        Q_EMIT self->owner->dataReceived(mimeType, QByteArray());
}

void ClipboardDevice::Private::handlePrimarySelection(void*, struct zwlr_data_control_device_v1*,
                                                      struct zwlr_data_control_offer_v1*)
{
    // Bound at v1: primary-selection is never delivered. No-op for completeness.
}

void ClipboardDevice::Private::handleOfferMime(void* data, struct zwlr_data_control_offer_v1* offer,
                                               const char* mimeType)
{
    auto* self = static_cast<Private*>(data);
    if (!self || !mimeType)
        return;
    auto it = self->pendingOffers.find(offer);
    if (it != self->pendingOffers.end())
        it->append(QString::fromUtf8(mimeType));
}

// ─── source events (we own the selection) ─────────────────────────────────

void ClipboardDevice::Private::handleSend(void* data, struct zwlr_data_control_source_v1*, const char* mimeType,
                                          int32_t fd)
{
    auto* self = static_cast<Private*>(data);
    if (!self) {
        ::close(fd);
        return;
    }
    const QString mime = QString::fromUtf8(mimeType);
    const QByteArray bytes = self->sourceData.value(mime);
    // The async write path depends on a non-blocking fd; a blocking write on a
    // full pipe would stall the GUI thread. If we cannot make it non-blocking,
    // fail the paste cleanly rather than risk blocking the event loop.
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        qCWarning(lcClipboardDevice) << "could not set the paste fd non-blocking; dropping the transfer";
        ::close(fd);
        return;
    }
    self->startWrite(bytes, fd);
}

void ClipboardDevice::Private::handleCancelled(void* data, struct zwlr_data_control_source_v1*)
{
    // Another client took the selection; our source is dead.
    auto* self = static_cast<Private*>(data);
    if (!self)
        return;
    self->destroySource();
}

// ─── public surface ───────────────────────────────────────────────────────

ClipboardDevice::ClipboardDevice(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->ensureDevice();
}

ClipboardDevice::~ClipboardDevice()
{
    d->teardownDevice();
}

bool ClipboardDevice::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->dataControlManager();
}

QStringList ClipboardDevice::mimeTypes() const
{
    return d->currentMimes;
}

void ClipboardDevice::receive(const QString& mimeType)
{
    if (!d->currentOffer || !d->currentMimes.contains(mimeType)) {
        Q_EMIT dataReceived(mimeType, QByteArray());
        return;
    }
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        qCWarning(lcClipboardDevice) << "pipe2 failed for clipboard receive:" << strerror(errno);
        Q_EMIT dataReceived(mimeType, QByteArray());
        return;
    }
    zwlr_data_control_offer_v1_receive(d->currentOffer, mimeType.toUtf8().constData(), fds[1]);
    ::close(fds[1]); // the compositor keeps its dup of the write end
    d->flush();
    d->startRead(mimeType, fds[0]);
}

void ClipboardDevice::setSelection(const QMap<QString, QByteArray>& data)
{
    d->ensureDevice();
    if (!d->device)
        return;
    auto* integration = LayerShellIntegration::instance();
    auto* manager = integration ? integration->dataControlManager() : nullptr;
    if (!manager || data.isEmpty())
        return;

    d->destroySource();
    d->source = zwlr_data_control_manager_v1_create_data_source(manager);
    if (!d->source)
        return;
    d->sourceData = data;
    static const struct zwlr_data_control_source_v1_listener listener = {
        .send = Private::handleSend,
        .cancelled = Private::handleCancelled,
    };
    zwlr_data_control_source_v1_add_listener(d->source, &listener, d.get());
    for (auto it = data.constBegin(); it != data.constEnd(); ++it)
        zwlr_data_control_source_v1_offer(d->source, it.key().toUtf8().constData());
    zwlr_data_control_device_v1_set_selection(d->device, d->source);
    d->flush();
}

} // namespace PhosphorWayland
