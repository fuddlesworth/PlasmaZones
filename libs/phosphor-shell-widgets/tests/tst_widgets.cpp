// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QtQuickTest runner for the Phosphor.Widgets atoms. The actual cases
// live in the tst_*.qml files alongside this translation unit; this is
// just the entry point that QUICK_TEST_MAIN expands into. The source
// directory is passed via `-input` from the add_test() command, and the
// offscreen QPA platform (set in the test environment) keeps the run
// headless so it works on CI without a display server.

#include <QtQuickTest/quicktest.h>

QUICK_TEST_MAIN(phosphor_shell_widgets)
