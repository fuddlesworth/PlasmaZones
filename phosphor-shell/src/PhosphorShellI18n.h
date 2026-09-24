// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PhosphorI18n::tr() for the shell binary, the same spelling the rest of the
// tree uses, under the shell's own translation context. The plasmazones
// header of the same shape is out of reach by the tier rules, and its
// "plasmazones" context names a catalog this process never loads.
//
// Nothing extracts these strings yet: cmake/PhosphorTranslations.cmake scans
// the plasmazones tier only, so the shell ships untranslated until it gains a
// catalog of its own.

#pragma once

#include <QCoreApplication>
#include <QString>

class PhosphorI18n
{
    Q_DECLARE_TR_FUNCTIONS(phosphorshell)
};
