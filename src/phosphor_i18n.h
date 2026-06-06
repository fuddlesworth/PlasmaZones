// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Portable i18n for C++ sources.
//
// Provides PhosphorI18n::tr() backed by QCoreApplication::translate() with
// "plasmazones" as the translation context.  lupdate recognizes
// Q_DECLARE_TR_FUNCTIONS and tr() natively — no custom extraction
// scripts or function aliases needed.
//
// QML files continue to use i18n()/i18nc() via PhosphorLocalizedContext
// (see phosphor_qml_i18n.h).

#pragma once

#include <QCoreApplication>
#include <QString>

class PhosphorI18n
{
    Q_DECLARE_TR_FUNCTIONS(plasmazones)
};
