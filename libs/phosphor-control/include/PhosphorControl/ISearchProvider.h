// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "PhosphorControl/SearchEntry.h"

#include <QVector>

namespace PhosphorControl {

/**
 * @brief Source of dynamic (content) search entries — rules, shaders,
 *        layouts, virtual screens, etc.
 *
 * Implemented by the consuming app and registered with SearchController. The
 * controller calls searchEntries() lazily while (re)building its index (after
 * an invalidate()), so implementations should cheaply snapshot their current
 * model rather than hold the controller's index live. Static page / anchor
 * entries do NOT use this — they go through SearchController directly.
 */
class ISearchProvider
{
public:
    virtual ~ISearchProvider() = default;

    /// Current searchable entries from this source (snapshot).
    virtual QVector<SearchEntry> searchEntries() const = 0;
};

} // namespace PhosphorControl
