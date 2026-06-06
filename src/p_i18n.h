// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Portable i18n for C++ sources.
//
// Provides PI18n::tr() backed by QCoreApplication::translate() with
// "plasmazones" as the translation context.  lupdate recognizes
// Q_DECLARE_TR_FUNCTIONS and tr() natively — no custom extraction
// scripts or function aliases needed.
//
// QML files continue to use i18n()/i18nc() via PLocalizedContext
// (see p_qml_i18n.h).

#pragma once

#include <QCoreApplication>
#include <QString>

class PI18n
{
    Q_DECLARE_TR_FUNCTIONS(plasmazones)
};
