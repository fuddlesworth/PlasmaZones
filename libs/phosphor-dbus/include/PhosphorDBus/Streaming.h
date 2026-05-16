// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QDBusArgument>

#include <type_traits>
#include <utility>

namespace PhosphorDBus {

/**
 * @brief Compile-time check that a type has QDBusArgument streaming operators.
 *
 * Use in adaptor / marshalling headers to catch a missing
 * `operator<<` / `operator>>` definition at build time rather than hitting a
 * runtime "demarshalling function failed" crash.
 *
 * @code
 *   static_assert(PhosphorDBus::HasDBusStreaming<MyEntry>::value,
 *       "MyEntry needs QDBusArgument operator<< and operator>>");
 * @endcode
 */
template<typename T, typename = void>
struct HasDBusStreaming : std::false_type
{
};

template<typename T>
struct HasDBusStreaming<T,
                        std::void_t<decltype(std::declval<QDBusArgument&>() << std::declval<const T&>()),
                                    decltype(std::declval<const QDBusArgument&>() >> std::declval<T&>())>>
    : std::true_type
{
};

} // namespace PhosphorDBus
