// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>
#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include "tileenginelogging.h"

namespace PhosphorTileEngine {

using NavigationContext = PhosphorEngine::NavigationContext;
using TilingStateKey = PhosphorEngine::TilingStateKey;
namespace PerScreenKeys = PhosphorEngine::PerScreenKeys;

template<typename T>
T* checkedCast(QObject* obj, const char* context)
{
    if (!obj)
        return nullptr;
    auto* concrete = qobject_cast<T*>(obj);
    if (!concrete) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << context << ": QObject is not the expected type — skipping";
    }
    return concrete;
}

} // namespace PhosphorTileEngine
