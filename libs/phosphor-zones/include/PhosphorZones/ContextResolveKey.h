// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QString>

namespace PhosphorZones {

/// Lookup key for the LayoutRegistry context-resolver caches. Mirrors the
/// parameters of LayoutRegistry::resolveAssignmentEntry — three independent
/// context dimensions the windowless cascade walks (screen id, virtual
/// desktop, activity). Field-equal + per-field hash composition is enough:
/// every dimension is part of the cascade identity.
struct ContextResolveKey
{
    QString screenId;
    int virtualDesktop = 0;
    QString activity;
    // A free-form fourth key dimension, used by the context resolvers that
    // share the ContextResolveKey type but never the same cache container. It
    // folds in every non-rule-set input the resolved value depends on, so a
    // change in one yields a fresh entry rather than a stale hit:
    //   - gap cascade: contextCacheKeyToken(mode, activeLayout, orientation) —
    //     the SAME (screen, desktop, activity) resolves DIFFERENT gaps per
    //     placement mode, active layout, and screen orientation.
    //   - lock / default-assignment / overlay: contextCacheKeyToken with an
    //     empty mode (they are mode-agnostic) plus activeLayout (except
    //     default-assignment, which omits it to avoid recursion) and orientation.
    //   - assignment resolver: "twc:N|or:<token>" — the tiled-window-count and
    //     the screen orientation (it does not read the active layout).
    // Each resolver owns its own cache hash, so the token vocabularies never
    // collide.
    QString mode;
    bool operator==(const ContextResolveKey& other) const noexcept
    {
        return virtualDesktop == other.virtualDesktop && screenId == other.screenId && activity == other.activity
            && mode == other.mode;
    }
};

inline size_t qHash(const ContextResolveKey& key, size_t seed) noexcept
{
    // ::qHash routes to the global Qt qHash overloads — without the leading
    // qualifier ADL would pick up qHash(LayoutAssignmentKey&) (declared in
    // AssignmentEntry.h alongside this header's consumer) and fail to
    // convert each field. Mirrors the same pattern LayoutAssignmentKey uses.
    size_t h = seed;
    h = ::qHash(key.screenId, h);
    h = ::qHash(key.virtualDesktop, h);
    h = ::qHash(key.activity, h);
    h = ::qHash(key.mode, h);
    return h;
}

} // namespace PhosphorZones
