// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The scrollEffectBehaviour value parser, extracted from
// TilingHandler::applyScrollEffectBehaviour so its three-way contract is unit
// testable (tests/unit/compositor-common/test_scroll_behaviour_parse.cpp):
// the map crosses D-Bus from another process, and every half of it decides
// compositor behaviour, so the parse failing SILENTLY on screen is exactly
// what the tests exist to prevent. Header-only plain Qt types, no KWin — the
// test_strip_motion_sampler shape.

#include <QDBusArgument>
#include <QDBusVariant>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

namespace PlasmaZones::ScrollBehaviourParse {

/// One screen-id list out of the scrollEffectBehaviour map.
///
/// The three-way contract, load-bearing at the caller:
///  - ABSENT (invalid QVariant): a legitimate publish with that key
///    unresolved — an EMPTY set, the same safe direction bring-up takes.
///    Engaged optional, no warning.
///  - MALFORMED (present but not convertible to a string list): DISENGAGED
///    optional, plus a warning line in @p warnings — the caller decides per
///    key whether empty or keep-current is the safe fallback (the axis keeps
///    its membership; the two behaviour toggles fall to empty).
///  - VALID: the de-duplicated set, with empty ids dropped (each adds
///    a warning — no window resolves to one, and they only defeat the
///    caller's change gate).
///
/// The map's one WINDOW-id list (focusScrollBlockedWindows) parses through
/// here too: the shape and every failure mode are identical, and only the
/// warning text would have differed.
///
/// An `as` value arrives either already demarshalled (the property Get path)
/// or still wrapped in a QDBusVariant (a signal delivered without a
/// registered argument type) — one level is unwrapped before the type test.
/// A container Qt's demarshaller did not special-case arrives as a raw
/// QDBusArgument, which converts to NOTHING and would read as "off
/// everywhere" — it is demarshalled explicitly instead.
inline std::optional<QSet<QString>> parseScreenIdList(const QVariant& raw, QLatin1StringView key, QStringList& warnings)
{
    QVariant v = raw;
    if (v.typeId() == QMetaType::fromType<QDBusVariant>().id()) {
        v = qvariant_cast<QDBusVariant>(v).variant();
    }
    if (v.typeId() == QMetaType::fromType<QDBusArgument>().id()) {
        v = QVariant::fromValue(qdbus_cast<QStringList>(v));
    }
    QSet<QString> out;
    if (!v.isValid()) {
        return out;
    }
    if (!v.canConvert<QStringList>()) {
        warnings.append(QStringLiteral("scrollEffectBehaviour: dropping non-list value for %1 type %2")
                            .arg(QString(key), QString::fromLatin1(v.typeName())));
        return std::nullopt;
    }
    const QStringList list = v.toStringList();
    out.reserve(list.size());
    for (const QString& screenId : list) {
        if (screenId.isEmpty()) {
            warnings.append(QStringLiteral("scrollEffectBehaviour: dropping empty id from %1").arg(key));
            continue;
        }
        out.insert(screenId);
    }
    return out;
}

} // namespace PlasmaZones::ScrollBehaviourParse
