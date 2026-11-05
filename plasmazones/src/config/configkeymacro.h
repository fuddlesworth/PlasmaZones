// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The one definition of the config-key accessor macros, shared by every link
// in the ConfigKeys chain (configkeys.h and the per-domain headers split out
// of it).
//
// DELIBERATELY NOT `#pragma once`. Each of those headers undefines the macros
// again at its end, via configkeymacro_undef.h, so they never leak to the code
// that includes them. A guarded header would define the macros for whichever
// link happened to be compiled first and leave every later one with nothing,
// which is why each link used to carry its own hand-copied duplicate. Include
// this at the top of a link and configkeymacro_undef.h at the bottom.

#include <QString>

// Defines a static config key accessor returning a QStringLiteral.
// Usage: P_CONFIG_KEY(snappingEnabledKey, "SnappingEnabled")
// Expands to: static QString snappingEnabledKey() { return QStringLiteral("SnappingEnabled"); }
#define P_CONFIG_KEY(name, str)                                                                                        \
    static QString name()                                                                                              \
    {                                                                                                                  \
        return QStringLiteral(str);                                                                                    \
    }

// Alias for group-name accessors — same body as P_CONFIG_KEY, single
// definition so a future tweak to P_CONFIG_KEY (e.g. attribute annotation)
// automatically applies to groups too. Separate macro name preserved for
// readability at the call sites.
#define P_CONFIG_GROUP(name, str) P_CONFIG_KEY(name, str)
