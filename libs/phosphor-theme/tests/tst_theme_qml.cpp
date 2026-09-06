// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QtQuickTest runner for the QML-side singletons of Phosphor.Theme. The
// cases live in the tst_*.qml files alongside this translation unit; this
// is just the entry point QUICK_TEST_MAIN expands into. The source
// directory arrives through `-input` from add_test(), and the offscreen
// QPA platform (set in the test environment) keeps the run headless.

#include <QtQuickTest/quicktest.h>

QUICK_TEST_MAIN(phosphor_theme_qml)
