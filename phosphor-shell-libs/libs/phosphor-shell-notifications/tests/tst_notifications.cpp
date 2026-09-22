// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QtQuickTest runner for the Phosphor.Notifications toast framework.
// Cases live in tst_*.qml; the source dir is passed via -input from
// add_test, and the offscreen QPA platform keeps the run headless.

#include <QtQuickTest/quicktest.h>

QUICK_TEST_MAIN(phosphor_shell_notifications)
