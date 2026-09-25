// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1Char>
#include <QString>

namespace PlasmaZones::ThumbnailUrl {

/// Normalise a compositor handle to the unbraced UUID form used for cache
/// keys and URL paths. Accepts both `{xxxxxxxx-...}` and `xxxxxxxx-...`.
/// Non-UUID input is returned unchanged; the providers additionally REJECT
/// (at insert) any handle that is empty after normalisation or contains a
/// URL-reserved '/' — QQuickImageProvider ids are parsed with
/// `id.section('/', 0, 0)`, so a '/'-bearing handle would key the store on
/// the full string but look up only its first segment: a silent, permanent
/// cache miss. The D-Bus boundary already restricts handles to QUuid
/// spellings; the insert-side reject keeps direct C++ callers honest too.
inline QString normaliseHandle(const QString& handle)
{
    if (handle.size() >= 2 && handle.startsWith(QLatin1Char('{')) && handle.endsWith(QLatin1Char('}'))) {
        return handle.mid(1, handle.size() - 2);
    }
    return handle;
}

/// True when @p normalisedHandle may be used as a provider cache key / URL
/// path segment. See @ref normaliseHandle for why '/' is rejected.
inline bool isValidHandleKey(const QString& normalisedHandle)
{
    return !normalisedHandle.isEmpty() && !normalisedHandle.contains(QLatin1Char('/'));
}

/// Compose `image://<providerId>/<handle>/<generation>`. Single multi-arg
/// .arg() substitutes all placeholders in one pass, so a handle containing a
/// literal "%N" can't be re-substituted by a later chained .arg() (chaining
/// is a known QString footgun on dynamic middle args). Handles are KWin
/// window UUIDs, but build the URL defensively anyway.
inline QString makeUrl(const char* providerId, const QString& handle, quint32 generation)
{
    return QStringLiteral("image://%1/%2/%3").arg(QString::fromLatin1(providerId), handle, QString::number(generation));
}

} // namespace PlasmaZones::ThumbnailUrl
