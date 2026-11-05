// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Choice-declaration helpers for the settings schema. Internal to the schema
// builders (settingsschema.cpp) — nothing else declares choices.

#include <PhosphorConfig/Schema.h>

#include <QLatin1StringView>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <initializer_list>
#include <utility>

namespace PlasmaZones {

/// Declare the legal values of an int-valued enum key.
///
/// The token is a STABLE identifier, never user-facing text: the words live
/// app-side (settingsvaluelabels.cpp) keyed off these tokens, which is what
/// keeps translated strings out of the config library. Renaming a token breaks
/// that lookup, so treat them as shipped API.
///
/// Declaring choices does not validate anything — the validator beside it stays
/// the single coercion path. This exists so a caller can READ BACK what a key
/// accepts, which a clamp lambda cannot answer.
inline QVector<PhosphorConfig::ChoiceDef> intChoices(std::initializer_list<std::pair<int, QLatin1StringView>> entries)
{
    QVector<PhosphorConfig::ChoiceDef> out;
    out.reserve(static_cast<int>(entries.size()));
    for (const auto& [value, token] : entries) {
        out.append({QVariant(value), QString(token)});
    }
    return out;
}

/// Declare the legal values of a key whose stored value IS its token (the
/// string-valued settings: rendering backend, audio mode, appearance scope).
inline QVector<PhosphorConfig::ChoiceDef> tokenChoices(std::initializer_list<QLatin1StringView> tokens)
{
    QVector<PhosphorConfig::ChoiceDef> out;
    out.reserve(static_cast<int>(tokens.size()));
    for (const QLatin1StringView token : tokens) {
        out.append({QVariant(QString(token)), QString(token)});
    }
    return out;
}

/// Same, for a key whose token set already exists as a ConfigDefaults accessor
/// (rendering backend, audio channel mode, audio input method). Reusing the
/// accessor keeps one list rather than a schema copy that can drift from the
/// validator that normalizes against it.
inline QVector<PhosphorConfig::ChoiceDef> tokenChoices(const QStringList& tokens)
{
    QVector<PhosphorConfig::ChoiceDef> out;
    out.reserve(tokens.size());
    for (const QString& token : tokens) {
        out.append({QVariant(token), token});
    }
    return out;
}

} // namespace PlasmaZones
