// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file bench_dbus_adaptors.cpp
 * @brief Micro-benchmarks for D-Bus adaptor hot paths.
 *
 * These benchmarks drive the D-Bus performance refactor on branch
 * refactor/dbus-performance. Each benchmark targets a specific phase
 * of the refactor so before/after numbers can be captured:
 *
 *   - SettingsAdaptor::setSetting unchanged-value writes (Phase 1.1)
 *   - SettingsAdaptor::getSettings batch vs N×getSetting (baseline)
 *   - LayoutAdaptor::setLayoutHidden signal emission path (Phase 1.2/4)
 *
 * Results are captured in docs/perf/dbus-baseline.md. Run with:
 *
 *   ctest --test-dir build -R bench_dbus_adaptors --output-on-failure
 *
 * or directly:
 *
 *   ./build/tests/unit/bench_dbus_adaptors -callgrind
 *   ./build/tests/unit/bench_dbus_adaptors -tickcounter
 *
 * Stored in tests/unit/dbus/ (not a separate bench/ dir) so the
 * existing headless-QPA fixture and IsolatedConfigGuard can be reused.
 */

#include <QTest>
#include <QSignalSpy>
#include <QVariant>
#include <QVariantMap>
#include <QDBusVariant>
#include <QStringList>

#include "dbus/settingsadaptor.h"
#include "dbus/layoutadaptor.h"
#include <PhosphorZones/Layout.h>
#include "core/layoutmanager.h"
#include <PhosphorZones/Zone.h>
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class BenchDBusAdaptors : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        // m_settings is deliberately parented to nullptr so it outlives the
        // adaptor's destructor, which calls m_settings->save() during
        // teardown. See test_settings_adaptor_batch.cpp for the same pattern.
        m_settings = new StubSettings(nullptr);
        m_parent = new QObject(nullptr);
        m_settingsAdaptor = new SettingsAdaptor(m_settings, /*shaderRegistry=*/nullptr, m_parent);

        m_layoutManager = makePzLayoutManager(m_parent).release();
        auto* layout = new PhosphorZones::Layout(QStringLiteral("BenchLayout"));
        for (int i = 0; i < 4; ++i) {
            auto* zone = new PhosphorZones::Zone(QRectF(0.25 * i, 0.0, 0.25, 1.0));
            zone->setZoneNumber(i + 1);
            layout->addZone(zone);
        }
        m_layoutManager->addLayout(layout);
        m_benchLayoutId = layout->id().toString();
        m_layoutAdaptor = new LayoutAdaptor(m_layoutManager, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_settingsAdaptor = nullptr;
        m_layoutManager = nullptr;
        m_layoutAdaptor = nullptr;
        delete m_settings;
        m_settings = nullptr;
        m_guard.reset();
    }

    // ─────────────────────────────────────────────────────────────────
    // Settings: setSetting with a value that is already current.
    // Before Phase 1.1 this call still reaches the setter lambda and
    // schedules a debounced save; after Phase 1.1 it returns early
    // because the getter returns the same value.
    //
    // Must use a value that StubSettings actually returns from its
    // getter — the stub ignores setters, so priming has no effect and
    // the guard only engages if we supply the stub's default.
    // See StubSettings::zonePadding() which returns 8.
    // ─────────────────────────────────────────────────────────────────
    void benchSetSetting_unchanged()
    {
        const QString key = QStringLiteral("zonePadding");
        const QDBusVariant value(QVariant(8));

        QBENCHMARK {
            m_settingsAdaptor->setSetting(key, value);
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // Settings: setSetting with a value that actually changes.
    // Baseline for "unchanged path must not be slower than changed path".
    //
    // Seed v outside the StubSettings::zonePadding() default (8) so no
    // iteration accidentally short-circuits through the Phase 1.1
    // equality guard. Without this seed, one in every N iterations
    // collides with the stub default and skews the bench.
    // ─────────────────────────────────────────────────────────────────
    void benchSetSetting_changing()
    {
        const QString key = QStringLiteral("zonePadding");
        int v = 100;

        QBENCHMARK {
            ++v;
            m_settingsAdaptor->setSetting(key, QDBusVariant(QVariant(v)));
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // Settings: one batched read for the editor startup key set vs the
    // old per-key chain. The batched form is already in place — this
    // bench pins the win so a future refactor can't regress it.
    // ─────────────────────────────────────────────────────────────────
    void benchGetSettings_batch()
    {
        const QStringList keys{
            QStringLiteral("zonePadding"),   QStringLiteral("outerGap"),           QStringLiteral("usePerSideOuterGap"),
            QStringLiteral("outerGapTop"),   QStringLiteral("outerGapBottom"),     QStringLiteral("outerGapLeft"),
            QStringLiteral("outerGapRight"), QStringLiteral("overlayDisplayMode"),
        };

        QBENCHMARK {
            (void)m_settingsAdaptor->getSettings(keys);
        }
    }

    void benchGetSetting_individual()
    {
        const QStringList keys{
            QStringLiteral("zonePadding"),   QStringLiteral("outerGap"),           QStringLiteral("usePerSideOuterGap"),
            QStringLiteral("outerGapTop"),   QStringLiteral("outerGapBottom"),     QStringLiteral("outerGapLeft"),
            QStringLiteral("outerGapRight"), QStringLiteral("overlayDisplayMode"),
        };

        QBENCHMARK {
            for (const QString& k : keys) {
                (void)m_settingsAdaptor->getSetting(k);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // PhosphorZones::Layout: setLayoutHidden full emission path.
    //
    // Before Phase 1.2 this emitted layoutChanged (full JSON, 5–20KB in
    // prod) AND layoutListChanged; after Phase 1.2 only layoutChanged
    // fires. After Phase 4 the full-JSON signal will be replaced by a
    // compact layoutPropertyChanged. The bench captures the cost of the
    // signal path including JSON serialization.
    // ─────────────────────────────────────────────────────────────────
    void benchSetLayoutHidden_toggle()
    {
        QSignalSpy changedSpy(m_layoutAdaptor, &LayoutAdaptor::layoutChanged);
        bool hidden = false;

        QBENCHMARK {
            hidden = !hidden;
            m_layoutAdaptor->setLayoutHidden(m_benchLayoutId, hidden);
        }
        // Keep the spy alive so Qt compiles in the signal delivery cost.
        Q_UNUSED(changedSpy);
    }

    // Repeated setLayoutHidden with the same value. After Phase 4's
    // layoutPropertyChanged split we can add a value-equality guard in
    // the setter; today this exercises the redundant-emit path.
    void benchSetLayoutHidden_sameValue()
    {
        m_layoutAdaptor->setLayoutHidden(m_benchLayoutId, true);
        QBENCHMARK {
            m_layoutAdaptor->setLayoutHidden(m_benchLayoutId, true);
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // PhosphorZones::Layout: getLayout for a known id. Before Phase 1.3 the JSON
    // cache is populated but never invalidated after property mutations,
    // so stale JSON can be served. This bench pins the cached-path cost.
    // ─────────────────────────────────────────────────────────────────
    void benchGetLayout_cached()
    {
        // Prime the cache.
        (void)m_layoutAdaptor->getLayout(m_benchLayoutId);

        QBENCHMARK {
            (void)m_layoutAdaptor->getLayout(m_benchLayoutId);
        }
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    StubSettings* m_settings = nullptr;
    SettingsAdaptor* m_settingsAdaptor = nullptr;
    LayoutManager* m_layoutManager = nullptr;
    LayoutAdaptor* m_layoutAdaptor = nullptr;
    QString m_benchLayoutId;
};

QTEST_MAIN(BenchDBusAdaptors)
#include "bench_dbus_adaptors.moc"
