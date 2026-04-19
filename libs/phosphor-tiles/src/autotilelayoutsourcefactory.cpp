// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/AutotileLayoutSourceFactory.h>

#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>
#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>

namespace PhosphorTiles {

AutotileLayoutSourceFactory::AutotileLayoutSourceFactory(ITileAlgorithmRegistry* registry)
    : m_registry(registry)
{
}

AutotileLayoutSourceFactory::~AutotileLayoutSourceFactory() = default;

QString AutotileLayoutSourceFactory::name() const
{
    return QStringLiteral("autotile");
}

std::unique_ptr<PhosphorLayout::ILayoutSource> AutotileLayoutSourceFactory::create()
{
    return std::make_unique<AutotileLayoutSource>(m_registry);
}

namespace {
// Static-init self-registration. Composition roots that call
// LayoutSourceBundle::buildFromRegistered(ctx) pick this up
// automatically — no per-root addFactory line. Returns nullptr when
// the composition root didn't surface an ITileAlgorithmRegistry,
// which is the "this composition root doesn't host the autotile
// engine" signal.
//
// Priority 100 — autotile entries follow manual zone entries in the
// composite's iteration order (zones registers at priority 0).
PhosphorLayout::LayoutSourceProviderRegistrar
    registrar(QStringLiteral("autotile"), /*priority=*/100,
              [](const PhosphorLayout::FactoryContext& ctx) -> std::unique_ptr<PhosphorLayout::ILayoutSourceFactory> {
                  auto* registry = ctx.get<ITileAlgorithmRegistry>();
                  if (!registry) {
                      return nullptr;
                  }
                  return std::make_unique<AutotileLayoutSourceFactory>(registry);
              });
} // anonymous namespace

} // namespace PhosphorTiles
