// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IOSDFactory.h>
#include <PhosphorRegistry/Registry.h>

#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorOsdDemo {

// QML-exposed provider for OSDHost. Owns a Registry<IOSDFactory>,
// registers the four built-in OSDs as IOSDFactory instances at
// construction, and exposes createOSD(kind, parent) so OSDHost can be
// wired as `provider: osdController`. This is the registry-backed
// provider the framework's README describes; the host stays registry-
// agnostic.
class OSDController : public QObject
{
    Q_OBJECT

public:
    explicit OSDController(QObject* parent = nullptr);
    ~OSDController() override;

    // OSDHost provider contract: build the OSD delegate for `kind`,
    // parented into `parent`. Returns null for an unknown kind. The
    // engine is resolved from `parent` so QML need not pass it.
    [[nodiscard]] Q_INVOKABLE QQuickItem* createOSD(const QString& kind, QQuickItem* parent);

    // Registered OSD kinds in registration order, for the demo's button row.
    [[nodiscard]] Q_INVOKABLE QStringList kinds() const;

private:
    PhosphorRegistry::Registry<PhosphorRegistry::IOSDFactory> m_registry;
};

} // namespace PhosphorOsdDemo
