// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Pure/inline helper functions shared between overlayservice TUs and tests.
// This header has NO dependency on ConfigDefaults, ShaderRegistry, or
// settings_interfaces, so it can be included directly from test TUs.

#include <QObject>
#include <QVariant>
#include <QString>
#include <QQmlProperty>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
// No QQuickWindow, no QJson* and no logging.h here. The first six were orphaned by the
// removal of parseZonesJson and the shader-preview helpers; logging.h outlived them by one
// round, because the edit that dropped the six moved the load-bearing copy into internal.h
// (which does call qCWarning) and left this one standing. This header's own body uses no
// qC macro and no category.
//
// Verified before removal, not after, and twice over: each pass scanned every TU that
// reaches this header (directly or through internal.h) plus every TU in the two unity
// targets, with comments AND string literals stripped, and resolved the provision graph —
// exactly one TU uses a QJson type without its own include and it lands in a target that
// compiles nothing reaching this header. A unity build cannot tell an orphan from a
// load-bearing transitive include, so each removal was checked against a NON-UNITY build
// with the shell both on and off.
#include <QQuickItem>
#include <QRectF>
#include <QVector>
#include <atomic>

#include <PhosphorWayland/LayerSurface.h>
#include <PhosphorZones/ZoneJsonKeys.h>
#include "qml_property_names.h"
#include "core/types/enums.h"

namespace PlasmaZones {
using PhosphorWayland::LayerSurface;
namespace LayerSurfaceProps = PhosphorWayland::LayerSurfaceProps;
} // namespace PlasmaZones

namespace PlasmaZones {

/// Recursive QML item search by objectName, returning the first match.
inline QQuickItem* findQmlItemByName(QQuickItem* item, const QString& objectName)
{
    if (!item) {
        return nullptr;
    }

    if (item->objectName() == objectName) {
        return item;
    }

    const auto children = item->childItems();
    for (auto* child : children) {
        if (auto* found = findQmlItemByName(child, objectName)) {
            return found;
        }
    }

    return nullptr;
}

/// Recursive QML item search collecting every match, for repeated delegates
/// where findQmlItemByName's first-hit contract is not enough.
inline void collectQmlItemsByName(QQuickItem* item, const QString& objectName, QVector<QQuickItem*>& out)
{
    if (!item) {
        return;
    }

    if (item->objectName() == objectName) {
        out.append(item);
    }

    const auto children = item->childItems();
    for (auto* child : children) {
        collectQmlItemsByName(child, objectName, out);
    }
}

/// Map @p item's bounding rect into @p target's coordinates, intersected with
/// every clipping ancestor along the way. Returns an empty rect when @p item is
/// entirely clipped away.
///
/// Cursor hit-testing must use this rather than a bare mapRectToItem: an item
/// scrolled outside a clipping ancestor (the zone selector's ScrollView clips
/// once the layout list overflows) still holds a position in the scene graph, so
/// its unclipped rect maps to coordinates where the item paints nothing. Hit
/// testing that rect matches the cursor against something the user cannot see.
inline QRectF mapVisibleRectToItem(QQuickItem* item, QQuickItem* target)
{
    if (!item || !target) {
        return QRectF();
    }

    QRectF visible = item->mapRectToItem(target, QRectF(0, 0, item->width(), item->height()));
    for (QQuickItem* ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (ancestor->clip()) {
            visible &= ancestor->mapRectToItem(target, QRectF(0, 0, ancestor->width(), ancestor->height()));
        }
        if (ancestor == target) {
            break;
        }
    }
    return visible;
}

inline void writeQmlProperty(QObject* object, const QString& name, const QVariant& value)
{
    if (!object) {
        return;
    }

    QQmlProperty prop(object, name);
    if (prop.isValid()) {
        prop.write(value);
    } else {
        object->setProperty(name.toUtf8().constData(), value);
    }
}

// Patch zone data with current highlight state from QML window properties.
// The zone model doesn't know about overlay highlights (keyboard/hover),
// so we patch isHighlighted here before passing to shaders.
inline QVariantList patchZonesWithHighlight(const QVariantList& zones, QObject* propertyHost)
{
    if (!propertyHost) {
        return zones;
    }
    const QString hid = propertyHost->property(OverlayQmlPropertyNames::HighlightedZoneId.data()).toString();
    const QVariantList hids = propertyHost->property(OverlayQmlPropertyNames::HighlightedZoneIds.data()).toList();

    QVariantList out;
    for (const QVariant& z : zones) {
        QVariantMap m = z.toMap();
        const QString id = m.value(PhosphorZones::ZoneJsonKeys::Id).toString();
        bool hi = (!id.isEmpty() && id == hid);
        if (!hi) {
            for (const QVariant& v : hids) {
                if (v.toString() == id) {
                    hi = true;
                    break;
                }
            }
        }
        m[PhosphorZones::ZoneJsonKeys::IsHighlighted] = hi;
        out.append(m);
    }
    return out;
}

// parseZonesJson was removed: the zone payload travels as a QVariantList, not as a
// JSON string, so it had no production caller left.

// Initialize shader timer if not already running. Prevents large iTimeDelta jumps
// by only starting if invalid. Replaces 3 occurrences of mutex-guarded timer init.
inline void ensureShaderTimerStarted(QElapsedTimer& timer, QMutex& mutex, std::atomic<qint64>& lastFrame,
                                     std::atomic<int>& frameCount)
{
    QMutexLocker locker(&mutex);
    if (!timer.isValid()) {
        timer.start();
        lastFrame.store(0);
        frameCount.store(0);
    }
}

// Convert ZoneSelectorPosition to layer surface anchors
inline LayerSurface::Anchors getAnchorsForPosition(ZoneSelectorPosition pos)
{
    switch (pos) {
    case ZoneSelectorPosition::TopLeft:
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorLeft);
    case ZoneSelectorPosition::Top:
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorLeft | LayerSurface::AnchorRight);
    case ZoneSelectorPosition::TopRight:
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorRight);
    case ZoneSelectorPosition::Left:
        return LayerSurface::Anchors(LayerSurface::AnchorLeft | LayerSurface::AnchorTop | LayerSurface::AnchorBottom);
    case ZoneSelectorPosition::Center:
        // Anchor to all edges so the window fills the screen; the QML "center" state
        // positions the container in the middle of the full-screen transparent window.
        return LayerSurface::AnchorAll;
    case ZoneSelectorPosition::Right:
        return LayerSurface::Anchors(LayerSurface::AnchorRight | LayerSurface::AnchorTop | LayerSurface::AnchorBottom);
    case ZoneSelectorPosition::BottomLeft:
        return LayerSurface::Anchors(LayerSurface::AnchorBottom | LayerSurface::AnchorLeft);
    case ZoneSelectorPosition::Bottom:
        return LayerSurface::Anchors(LayerSurface::AnchorBottom | LayerSurface::AnchorLeft | LayerSurface::AnchorRight);
    case ZoneSelectorPosition::BottomRight:
        return LayerSurface::Anchors(LayerSurface::AnchorBottom | LayerSurface::AnchorRight);
    default:
        // Default to top anchors
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorLeft | LayerSurface::AnchorRight);
    }
}

} // namespace PlasmaZones
