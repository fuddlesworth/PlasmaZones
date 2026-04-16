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
#include <QQuickWindow>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include <atomic>

#include <PhosphorShell/LayerSurface.h>
using PhosphorShell::LayerSurface;
namespace LayerSurfaceProps = PhosphorShell::LayerSurfaceProps;
#include "../../core/logging.h"
#include "../../core/enums.h"

namespace PlasmaZones {

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
inline QVariantList patchZonesWithHighlight(const QVariantList& zones, QQuickWindow* window)
{
    if (!window) {
        return zones;
    }
    const QString hid = window->property("highlightedZoneId").toString();
    const QVariantList hids = window->property("highlightedZoneIds").toList();

    QVariantList out;
    for (const QVariant& z : zones) {
        QVariantMap m = z.toMap();
        const QString id = m.value(QLatin1String("id")).toString();
        bool hi = (!id.isEmpty() && id == hid);
        if (!hi) {
            for (const QVariant& v : hids) {
                if (v.toString() == id) {
                    hi = true;
                    break;
                }
            }
        }
        m[QLatin1String("isHighlighted")] = hi;
        out.append(m);
    }
    return out;
}

// Parse zones from JSON array. Returns empty list on parse error or invalid format.
// Shared by overlayservice_shader.cpp, overlayservice_snapassist.cpp, and tests.
inline QVariantList parseZonesJson(const QString& json, const char* context)
{
    QVariantList zones;
    if (json.isEmpty()) {
        return zones;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcOverlay) << context << "invalid zones JSON:" << parseError.errorString();
        return zones;
    }
    if (!doc.isArray()) {
        qCWarning(lcOverlay) << context << "zones JSON is not an array";
        return zones;
    }
    for (const QJsonValue& v : doc.array()) {
        if (v.isObject()) {
            QVariantMap m;
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it) {
                m.insert(it.key(), it.value().toVariant());
            }
            zones.append(m);
        }
    }
    return zones;
}

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
