// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OSDController.h"

#include "QmlComponentOSDFactory.h"

#include <QDebug>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStringList>
#include <QStringLiteral>

#include <memory>

namespace PhosphorOsdDemo {

namespace {
// All four built-ins live in the Phosphor.OSD module.
const QString kModule = QStringLiteral("Phosphor.OSD");
}

OSDController::OSDController(QObject* parent)
    : QObject(parent)
{
    // Register the four built-in OSDs as IOSDFactory instances. Each
    // wraps a delegate type from the Phosphor.OSD module. Capabilities
    // are advisory (Phase 5).
    const auto reg = [this](const QString& id, const QString& name, const QString& type) {
        m_registry.registerFactory(
            std::make_shared<QmlComponentOSDFactory>(id, name, kModule, type, QStringList{QStringLiteral("osd")}));
    };
    reg(QStringLiteral("volume"), QStringLiteral("Volume"), QStringLiteral("VolumeOSD"));
    reg(QStringLiteral("brightness"), QStringLiteral("Brightness"), QStringLiteral("BrightnessOSD"));
    reg(QStringLiteral("mic"), QStringLiteral("Microphone"), QStringLiteral("MicOSD"));
    reg(QStringLiteral("caps"), QStringLiteral("Caps Lock"), QStringLiteral("CapsLockOSD"));
}

OSDController::~OSDController() = default;

QQuickItem* OSDController::createOSD(const QString& kind, QQuickItem* parent)
{
    const auto factory = m_registry.factory(kind);
    if (!factory) {
        qWarning() << "OSDController: no OSD registered for kind" << kind;
        return nullptr;
    }
    QQmlEngine* engine = parent ? qmlEngine(parent) : nullptr;
    if (!engine) {
        qWarning() << "OSDController: no QML engine resolvable from parent for kind" << kind;
        return nullptr;
    }
    return factory->createOSD(engine, parent);
}

} // namespace PhosphorOsdDemo
