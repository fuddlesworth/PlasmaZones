// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_known_desktops.cpp
 * @brief The virtual desktop ids the session last saw, kept in session state
 *        through the WindowTrackingAdaptor.
 *
 * The daemon hands the list to VirtualDesktopManager::setBaselineDesktopIds
 * at the next start, so desktops removed, added or moved while it was down
 * renumber the stores keyed on the old positions (F809). Pinned here: the
 * list survives a save and a fresh adaptor's load in order, and setting the
 * list the adaptor already holds marks nothing to save. The monitors' last
 * reported pixel sizes ride the same session state and are pinned the same
 * way, plus their merge and their identifier flip.
 */

#include <QHash>
#include <QSize>
#include <QTest>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using WTS = PhosphorPlacement::WindowTrackingService;

namespace {
QStringList sampleIds()
{
    return {QStringLiteral("3f2a6d1e-0b7c-4c1d-9a51-1d2e3f4a5b6c"),
            QStringLiteral("8c1b2a3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d"),
            QStringLiteral("0d9e8f7a-6b5c-4d3e-8f2a-1b0c9d8e7f6a")};
}

QString ultrawideId()
{
    return QStringLiteral("SAM:Odyssey G9:H1AK500000");
}

QHash<QString, QSize> sampleSizes()
{
    return {{ultrawideId(), QSize(5120, 1440)}, {QStringLiteral("DEL:U2720Q:ABC123"), QSize(2160, 3840)}};
}
} // namespace

class TestWtaKnownDesktops : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
    }

    void cleanup()
    {
        destroyAdaptor();
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_guard.reset();
    }

    void testKnownDesktopIdsRoundTripThroughTheSession()
    {
        makeAdaptor();
        QVERIFY(m_wta->knownDesktopIds().isEmpty());
        m_wta->setKnownDesktopIds(sampleIds());
        QVERIFY(m_wta->service()->peekDirty() & WTS::DirtyKnownDesktops);
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        makeAdaptor();
        QCOMPARE(m_wta->knownDesktopIds(), sampleIds());
    }

    void testSettingTheSameListMarksNothing()
    {
        makeAdaptor();
        m_wta->setKnownDesktopIds(sampleIds());
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        // The load leaves nothing dirty, and the list it read is the one the
        // daemon sets again on every start that changed nothing.
        makeAdaptor();
        QCOMPARE(m_wta->service()->peekDirty() & WTS::DirtyKnownDesktops, 0u);
        m_wta->setKnownDesktopIds(sampleIds());
        QCOMPARE(m_wta->service()->peekDirty() & WTS::DirtyKnownDesktops, 0u);

        // Discriminator: a different order is a different list.
        QStringList reordered = sampleIds();
        reordered.swapItemsAt(0, 2);
        m_wta->setKnownDesktopIds(reordered);
        QVERIFY(m_wta->service()->peekDirty() & WTS::DirtyKnownDesktops);
    }

    // Each monitor's last reported pixel size rides the same session state, and
    // the daemon seeds the screen manager with it at the next start (F3), so a
    // screen-shape rule resolves against the real size before the compositor
    // reports it again.
    void testScreenPixelSizesRoundTripThroughTheSession()
    {
        makeAdaptor();
        QVERIFY(m_wta->screenPixelSizes().isEmpty());
        m_wta->recordScreenPixelSizes(sampleSizes());
        QVERIFY(m_wta->service()->peekDirty() & WTS::DirtyScreenPixelSizes);
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        makeAdaptor();
        QCOMPARE(m_wta->screenPixelSizes(), sampleSizes());
    }

    // The record merges: a monitor missing from a later report (unplugged
    // then) keeps the size it last had, which is what a replug reads. Setting
    // what is already held marks nothing.
    void testScreenPixelSizesMergeAndSkipTheUnchanged()
    {
        makeAdaptor();
        m_wta->recordScreenPixelSizes(sampleSizes());
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        makeAdaptor();
        QCOMPARE(m_wta->service()->peekDirty() & WTS::DirtyScreenPixelSizes, 0u);
        m_wta->recordScreenPixelSizes(sampleSizes());
        QCOMPARE(m_wta->service()->peekDirty() & WTS::DirtyScreenPixelSizes, 0u);

        // Only the ultrawide is connected now, in its half-width mode.
        m_wta->recordScreenPixelSizes({{ultrawideId(), QSize(2560, 1440)}});
        QVERIFY(m_wta->service()->peekDirty() & WTS::DirtyScreenPixelSizes);
        QHash<QString, QSize> expected = sampleSizes();
        expected.insert(ultrawideId(), QSize(2560, 1440));
        QCOMPARE(m_wta->screenPixelSizes(), expected);
    }

    // An identifier flip (a second same-model monitor joined) moves the entry
    // the way the screen manager moves its seed.
    void testScreenPixelSizeFollowsAnIdentifierFlip()
    {
        makeAdaptor();
        m_wta->recordScreenPixelSizes(sampleSizes());
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        makeAdaptor();
        const QString flipped = ultrawideId() + QStringLiteral("/DP-2");
        m_wta->renameScreenPixelSize(ultrawideId(), flipped);
        QVERIFY(m_wta->service()->peekDirty() & WTS::DirtyScreenPixelSizes);
        QVERIFY(!m_wta->screenPixelSizes().contains(ultrawideId()));
        QCOMPARE(m_wta->screenPixelSizes().value(flipped), QSize(5120, 1440));
    }

private:
    void makeAdaptor()
    {
        m_parent = new QObject(nullptr);
        m_wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, m_parent);
    }

    void destroyAdaptor()
    {
        if (!m_parent) {
            return;
        }
        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
};

QTEST_MAIN(TestWtaKnownDesktops)
#include "test_wta_known_desktops.moc"
