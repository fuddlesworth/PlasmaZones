// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QVariant>

namespace PlasmaZones {
namespace GpuDeviceList {

/// Enumerate the machine's GPUs via DRM render nodes
/// (/sys/class/drm/renderD*/device). Each entry is a {text, value} map for
/// combo consumption: value is the lowercase "vendor:device" hex PCI pair
/// matching the Rendering.Gpu setting, text a human-readable name resolved
/// from the hwdata pci.ids database (falling back to a vendor label + the
/// raw pair). Duplicate pairs (two identical cards) collapse to one entry
/// because the setting cannot distinguish them. The "auto" entry is NOT
/// included — the caller prepends it with a translated label.
QVariantList enumerate();

} // namespace GpuDeviceList
} // namespace PlasmaZones
