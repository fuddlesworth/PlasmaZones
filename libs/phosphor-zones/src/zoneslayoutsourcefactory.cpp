// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ZonesLayoutSourceFactory.h>

#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/ZonesLayoutSource.h>

namespace PhosphorZones {

ZonesLayoutSourceFactory::ZonesLayoutSourceFactory(IZoneLayoutRegistry* registry)
    : m_registry(registry)
{
}

ZonesLayoutSourceFactory::~ZonesLayoutSourceFactory() = default;

QString ZonesLayoutSourceFactory::name() const
{
    return QStringLiteral("zones");
}

std::unique_ptr<PhosphorLayout::ILayoutSource> ZonesLayoutSourceFactory::create()
{
    return std::make_unique<ZonesLayoutSource>(m_registry);
}

namespace {
// Static-init self-registration. Composition roots that call
// LayoutSourceBundle::buildFromRegistered(ctx) pick this up
// automatically — no per-root addFactory line. Returns nullptr when
// the composition root didn't surface an IZoneLayoutRegistry, which
// is the "this composition root doesn't host the zones engine"
// signal.
//
// Priority 0 — manual zone layouts come first in the composite so
// their bare-UUID ids land before autotile's `autotile:` prefix in
// the composite's iteration order. Future families pick a priority
// to slot themselves in (autotile = 100 is the natural follow-on).
PhosphorLayout::LayoutSourceProviderRegistrar
    registrar(QStringLiteral("zones"), /*priority=*/0,
              [](const PhosphorLayout::FactoryContext& ctx) -> std::unique_ptr<PhosphorLayout::ILayoutSourceFactory> {
                  auto* registry = ctx.get<IZoneLayoutRegistry>();
                  if (!registry) {
                      return nullptr;
                  }
                  return std::make_unique<ZonesLayoutSourceFactory>(registry);
              });
} // anonymous namespace

} // namespace PhosphorZones
