// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QtPlugin>

namespace PhosphorControl {

class FacetContext;

/// The root object of a Facet module.
///
/// A Facet module is a Qt plugin in the `facet` seam: its root QObject
/// implements this interface and declares
/// `Q_PLUGIN_METADATA(IID PhosphorFacetModule_iid FILE "metadata.json")`.
/// The metadata describes the module and its pages (FacetModuleMetadata), so
/// Facet can show them in the sidebar and search them before the module
/// loads. The module loads the first time one of its pages is opened.
class IFacetModule
{
public:
    virtual ~IFacetModule() = default;

    /// Register this module's page controllers and headless staging domains.
    /// Every page in the metadata must be registered, and nothing else. Called
    /// once, on the GUI thread, when the module loads. Must not throw.
    virtual void registerInto(FacetContext& context) = 0;
};

} // namespace PhosphorControl

#define PhosphorFacetModule_iid "org.phosphor_works.Facet.Module/1"
Q_DECLARE_INTERFACE(PhosphorControl::IFacetModule, PhosphorFacetModule_iid)
