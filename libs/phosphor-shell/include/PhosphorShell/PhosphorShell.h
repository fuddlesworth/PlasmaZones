// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorShell.h
 * @brief Umbrella include for the PhosphorShell library — a Quickshell-
 *        style declarative QML framework for layer-shell desktop shells.
 *
 * Pulls in every public type (PanelWindow, PopupWindow, FloatingWindow,
 * ShellEngine, ShellLoader, Variants, LazyLoader, Process, FileView,
 * PersistentProperties, Environment, ShellGlobal, ScreenModel,
 * Toplevels). Consumers that only need a subset should include the
 * specific headers directly to keep compile times down.
 */

#include <PhosphorShell/Environment.h>
#include <PhosphorShell/FileView.h>
#include <PhosphorShell/FloatingWindow.h>
#include <PhosphorShell/LazyLoader.h>
#include <PhosphorShell/PanelWindow.h>
#include <PhosphorShell/PersistentProperties.h>
#include <PhosphorShell/PopupWindow.h>
#include <PhosphorShell/Process.h>
#include <PhosphorShell/ScreenModel.h>
#include <PhosphorShell/ShellEngine.h>
#include <PhosphorShell/ShellGlobal.h>
#include <PhosphorShell/ShellLoader.h>
#include <PhosphorShell/Toplevels.h>
#include <PhosphorShell/Variants.h>
#include <PhosphorShell/WallpaperService.h>
