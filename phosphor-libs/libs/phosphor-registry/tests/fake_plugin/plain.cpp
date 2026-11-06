// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// A shared library that is not a Qt plugin: no Q_PLUGIN_METADATA, so it
// carries no plugin metadata. The catalog must refuse it from its metadata
// alone, without mapping it.

extern "C" int phosphor_registry_test_plain_library()
{
    return 0;
}
