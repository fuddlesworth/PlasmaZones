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
    if (offer)
        zwlr_data_control_offer_v1_destroy(offer);
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
        zwlr_data_control_source_v1_destroy(source);
        source = nullptr;
    }
    sourceData.clear();
}

void ClipboardDevice::Private::teardownDevice()
{
    for (auto& ctx : reads)
        finishRead(ctx.get());
    reads.clear();
    for (auto& ctx : writes)
        finishWrite(ctx.get());
    writes.clear();

    destroySource();
    clearPendingOffers(nullptr);
    destroyOffer(currentOffer);
    currentOffer = nullptr;
    currentMimes.clear();

    if (device) {
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
    if (ctx->notifier) {
        ctx->notifier->setEnabled(false);
        ctx->notifier->deleteLater();
        ctx->notifier = nullptr;
    }
    if (ctx->fd >= 0) {
        ::close(ctx->fd);
        ctx->fd = -1;
    }
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
    if (ctx->notifier) {
        ctx->notifier->setEnabled(false);
        ctx->notifier->deleteLater();
        ctx->notifier = nullptr;
    }
    if (ctx->fd >= 0) {
        ::close(ctx->fd);
        ctx->fd = -1;
    }
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
    if (!offer)
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

    // Replace the previous selection's offer (if any).
    if (self->currentOffer && self->currentOffer != offer) {
        self->destroyOffer(self->currentOffer);
        self->currentOffer = nullptr;
    }

    if (!offer) {
        // Selection cleared.
        self->clearPendingOffers(nullptr);
        self->currentOffer = nullptr;
        if (!self->currentMimes.isEmpty()) {
            self->currentMimes.clear();
        }
        Q_EMIT self->owner->selectionChanged(QStringList());
        return;
    }

    self->currentOffer = offer;
    self->currentMimes = self->pendingOffers.value(offer);
    self->clearPendingOffers(offer); // drop any stale pending offers; keep this one's slot gone too
    self->pendingOffers.remove(offer);
    Q_EMIT self->owner->selectionChanged(self->currentMimes);
}

void ClipboardDevice::Private::handleFinished(void* data, struct zwlr_data_control_device_v1*)
{
    // The compositor invalidated this device (e.g. the seat went away). Tear it
    // down; a fresh selection event stream would require a new device.
    auto* self = static_cast<Private*>(data);
    self->teardownDevice();
    Q_EMIT self->owner->selectionChanged(QStringList());
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
    if (!mimeType)
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
    const QString mime = QString::fromUtf8(mimeType);
    const QByteArray bytes = self->sourceData.value(mime);
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    self->startWrite(bytes, fd);
}

void ClipboardDevice::Private::handleCancelled(void* data, struct zwlr_data_control_source_v1*)
{
    // Another client took the selection; our source is dead.
    auto* self = static_cast<Private*>(data);
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
