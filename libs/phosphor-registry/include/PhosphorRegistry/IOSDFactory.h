// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QtCore/qtclasshelpermacros.h>

QT_BEGIN_NAMESPACE
class QObject;
class QQmlEngine;
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorRegistry {

// Factory for one on-screen display surface (volume, mic mute,
// brightness, caps-lock, num-lock). OSDs are short-lived: the host
// instantiates the QQuickItem when the OSD is triggered, fades it
// in, holds for a timeout, fades out, destroys.
//
// Phase 1.3 ships this as a documented header; the consuming OSD
// framework lands in Phase 3.3.
class PHOSPHORREGISTRY_EXPORT IOSDFactory : public IFactoryBase
{
public:
    IOSDFactory() = default;
    ~IOSDFactory() override = default;
    Q_DISABLE_COPY_MOVE(IOSDFactory)

    // Construct an OSD QQuickItem rooted at parent. engine MUST NOT
    // be null. The OSD host owns the item's lifetime and triggers
    // its fade-in / fade-out via the standard PopoutHost-style
    // open/dismissed contract that the OSD framework will define
    // in Phase 3.3.
    [[nodiscard]] virtual QQuickItem* createOSD(QQmlEngine* engine, QObject* parent) = 0;
};

} // namespace PhosphorRegistry
