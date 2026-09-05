// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QHash>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <optional>

namespace PlasmaZones {

/// One drawn zone of a registered drop proxy: the miniature cell's rect and
/// the real zone it stands for.
struct DropProxyCell
{
    QString zoneId;
    QRect rect;
};

/// A shell-registered drop proxy (WindowDrag.registerDropProxy): the
/// miniature's bounding rect and its cells, all in the registering screen's
/// own pixels with the screen's top-left as origin.
struct DropProxy
{
    QRect rect;
    QVector<DropProxyCell> cells;
};

/**
 * @brief The drop proxies registered on the drag adaptor, one per screen.
 *
 * Pure bookkeeping plus a hit-test, kept apart from WindowDragAdaptor so the
 * wire shape (PhosphorProtocol::Service::DropProxyKey) and the cell lookup
 * are testable without a drag session. The adaptor consults resolve() on
 * every snap-path cursor tick and treats a Cell answer as the cursor being
 * over that real zone.
 */
class PLASMAZONES_EXPORT DropProxyRegistry
{
public:
    enum class Hit {
        Outside, ///< no proxy on that screen, or the cursor is outside its rect
        InsideNoCell, ///< inside the miniature but over no cell: no target
        Cell ///< over a cell; zoneId names the real zone
    };
    struct Resolution
    {
        Hit hit = Hit::Outside;
        QString zoneId;
    };

    /// Parse the registerDropProxy JSON. nullopt for anything malformed: a
    /// missing or non-positive rect, a cell with an empty id or a
    /// non-positive rect. Strict on purpose, this is a wire boundary and a
    /// half-parsed proxy would silently drop cells the shell drew.
    static std::optional<DropProxy> parse(const QString& json);

    /// Register (or replace) the proxy for @p screenId. Returns false and
    /// leaves any earlier registration standing when @p json fails to parse.
    /// Screen ids are matched through ScreenIdentity::screensMatch, so a
    /// connector-name and EDID spelling of one output share a slot.
    bool registerProxy(const QString& screenId, const QString& json);
    void unregisterProxy(const QString& screenId);
    bool hasProxy(const QString& screenId) const;
    int count() const
    {
        return m_proxies.size();
    }

    /// Hit-test @p screenLocal (pixels from the screen's top-left) against
    /// the proxy registered for @p screenId. The first cell containing the
    /// point wins when cells overlap, in registration order.
    Resolution resolve(const QString& screenId, const QPoint& screenLocal) const;

private:
    QHash<QString, DropProxy>::const_iterator find(const QString& screenId) const;
    QHash<QString, DropProxy> m_proxies;
};

} // namespace PlasmaZones
