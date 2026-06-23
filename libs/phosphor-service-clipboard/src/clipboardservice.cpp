// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceClipboard/ClipboardService.h>

#include "clipboardhistorymodel.h"
#include "clipboardstore.h"
#include "waylandclipboardsource.h"

#include <QByteArray>
#include <QMap>

namespace PhosphorServiceClipboard {

class ClipboardService::Private
{
public:
    ClipboardHistoryModel model;
    ClipboardStore store{ClipboardStore::defaultDirectory()};
    WaylandClipboardSource source;
};

ClipboardService::ClipboardService(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // Seed from disk first (setEntries does not emit historyChanged, so this does
    // not trigger an immediate re-save), then watch the live clipboard.
    d->model.setEntries(d->store.load());
    d->model.setSource(&d->source);

    connect(&d->model, &ClipboardHistoryModel::countChanged, this, &ClipboardService::countChanged);
    connect(&d->model, &ClipboardHistoryModel::historyChanged, this, [this] {
        d->store.save(d->model.entries());
    });
}

ClipboardService::~ClipboardService() = default;

bool ClipboardService::isSupported() const
{
    return PhosphorWayland::ClipboardDevice::isSupported();
}

QAbstractItemModel* ClipboardService::history() const
{
    return &d->model;
}

int ClipboardService::count() const
{
    return d->model.rowCount();
}

void ClipboardService::copy(int index)
{
    const ClipboardEntry entry = d->model.entryAt(index);
    if (entry.content.isEmpty() || entry.mimeType.isEmpty())
        return;
    // Re-offer the materialized type. A loopback selection event re-reads it, but
    // dedup just moves the entry to the front rather than duplicating it.
    d->source.setSelection({{entry.mimeType, entry.content}});
}

void ClipboardService::remove(int index)
{
    d->model.removeAt(index);
}

void ClipboardService::clear()
{
    d->model.clear();
}

} // namespace PhosphorServiceClipboard
