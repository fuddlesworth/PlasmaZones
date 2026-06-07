// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Umbrella header. Pulls in the public surface of the library so
// consumers can `#include <PhosphorRegistry/PhosphorRegistry.h>`
// without enumerating every individual header.

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/IControlCenterTileFactory.h>
#include <PhosphorRegistry/IDesktopWidgetFactory.h>
#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/ILauncherProviderFactory.h>
#include <PhosphorRegistry/IOSDFactory.h>
#include <PhosphorRegistry/Manifest.h>
#include <PhosphorRegistry/MetadataPackLoader.h>
#include <PhosphorRegistry/PluginLoader.h>
#include <PhosphorRegistry/Registry.h>
#include <PhosphorRegistry/RegistryNotifier.h>
