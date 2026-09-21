// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Undefines the macros configkeymacro.h defines, so a link in the ConfigKeys
// chain does not leak them to its includers. Paired with that header and
// DELIBERATELY NOT `#pragma once` for the same reason.

#undef P_CONFIG_KEY
#undef P_CONFIG_GROUP
