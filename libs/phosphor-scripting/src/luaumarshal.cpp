// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "luaumarshal.h"

#include <lua.h>

#include <QVariantList>
#include <QVariantMap>

#include <cmath>

namespace PhosphorScripting {
namespace Marshal {

void pushVariant(lua_State* L, const QVariant& v, int depth)
{
    if (depth > MaxDepth || !v.isValid() || v.isNull()) {
        lua_pushnil(L);
        return;
    }
    // Reserve stack for this level's pushes (table + key + value). Luau only
    // guarantees LUA_MINSTACK free slots on entry; a deep/wide nest marshalled
    // outside a protected call would otherwise risk an uncatchable overflow.
    if (!lua_checkstack(L, 4)) {
        lua_pushnil(L);
        return;
    }

    switch (v.typeId()) {
    case QMetaType::Bool:
        lua_pushboolean(L, v.toBool() ? 1 : 0);
        return;
    case QMetaType::Int:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::SChar:
    case QMetaType::UChar:
        // All fit in lua_Integer (32-bit int) without loss.
        lua_pushinteger(L, v.toInt());
        return;
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Double:
    case QMetaType::Float:
        // UInt and the 64-bit widths can exceed lua_Integer's range; push as a
        // double (Lua numbers are doubles) to avoid truncation.
        lua_pushnumber(L, v.toDouble());
        return;
    case QMetaType::QString: {
        const QByteArray utf8 = v.toString().toUtf8();
        lua_pushlstring(L, utf8.constData(), static_cast<size_t>(utf8.size()));
        return;
    }
    case QMetaType::QStringList: {
        const QStringList list = v.toStringList();
        lua_newtable(L);
        for (qsizetype i = 0; i < list.size(); ++i) {
            const QByteArray utf8 = list[i].toUtf8();
            lua_pushlstring(L, utf8.constData(), static_cast<size_t>(utf8.size()));
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
        return;
    }
    case QMetaType::QVariantList: {
        const QVariantList list = v.toList();
        lua_newtable(L);
        for (qsizetype i = 0; i < list.size(); ++i) {
            pushVariant(L, list[i], depth + 1);
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
        return;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap map = v.toMap();
        lua_newtable(L);
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QByteArray key = it.key().toUtf8();
            pushVariant(L, it.value(), depth + 1);
            lua_setfield(L, -2, key.constData());
        }
        return;
    }
    default:
        if (v.canConvert<double>()) {
            lua_pushnumber(L, v.toDouble());
        } else {
            lua_pushnil(L);
        }
        return;
    }
}

QVariant toVariant(lua_State* L, int idx, int depth)
{
    // Absolute index so stack growth during table reads doesn't shift it.
    const int abs = idx < 0 ? lua_gettop(L) + idx + 1 : idx;

    switch (lua_type(L, abs)) {
    case LUA_TNIL:
    case LUA_TNONE:
        return QVariant();
    case LUA_TBOOLEAN:
        return QVariant(lua_toboolean(L, abs) != 0);
    case LUA_TNUMBER: {
        const double n = lua_tonumber(L, abs);
        // Present whole numbers as integers so round-trips stay clean. The
        // cutoff is 2^53, the largest magnitude at which a double represents
        // every integer exactly; beyond it, keep the value as a double.
        if (std::isfinite(n) && std::floor(n) == n && std::fabs(n) <= 9007199254740992.0) {
            return QVariant(static_cast<qlonglong>(n));
        }
        return QVariant(n);
    }
    case LUA_TSTRING: {
        size_t len = 0;
        const char* s = lua_tolstring(L, abs, &len);
        return QVariant(QString::fromUtf8(s, static_cast<qsizetype>(len)));
    }
    case LUA_TTABLE: {
        if (depth > MaxDepth) {
            return QVariant();
        }
        // Reserve stack for the reads below (rawgeti / pushvalue / next). See
        // the pushVariant note: this runs outside any protected call.
        if (!lua_checkstack(L, 4)) {
            return QVariant();
        }
        // lua_objlen returns the array border as int; guard the (not normally
        // reachable) negative case so the loop bound is always sane.
        const int n = lua_objlen(L, abs);
        if (n < 0) {
            return QVariant();
        }
        if (n > 0) {
            QVariantList list;
            list.reserve(n);
            for (int i = 1; i <= n; ++i) {
                lua_rawgeti(L, abs, i);
                list.append(toVariant(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return list;
        }
        QVariantMap map;
        lua_pushnil(L);
        while (lua_next(L, abs) != 0) {
            // value at -1, key at -2. Stringify a COPY of the key so we never
            // call lua_tolstring on the live key (which can corrupt lua_next).
            lua_pushvalue(L, -2);
            size_t klen = 0;
            const char* k = lua_tolstring(L, -1, &klen);
            // Distinguish a genuinely empty-string key (kept) from a key that
            // failed coercion to a string — only the latter (k == nullptr,
            // e.g. a table/function/boolean key) is dropped.
            const bool haveKey = (k != nullptr);
            const QString key = haveKey ? QString::fromUtf8(k, static_cast<qsizetype>(klen)) : QString();
            lua_pop(L, 1); // key copy
            if (haveKey) {
                map.insert(key, toVariant(L, -1, depth + 1));
            }
            lua_pop(L, 1); // value, keep original key for lua_next
        }
        return map;
    }
    default:
        return QVariant();
    }
}

} // namespace Marshal
} // namespace PhosphorScripting
