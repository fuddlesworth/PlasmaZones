// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_narrow_stubs.cpp
 * @brief Demonstration that StubScrollSettings / StubAutotileSettings can be
 *        used standalone, without the full ISettings abstract-base requirement.
 *
 * The split of ISettings into per-mode interfaces (IScrollSettings,
 * IAutotileSettings) is only useful if a NEW consumer that needs only a
 * scroll-mode or autotile-mode setting can take that narrow interface and a
 * test can stub it independently. Before the split, every test had to either
 * inherit StubSettings (which dragged in 100+ snap/animation/persistence
 * methods) or implement the full ISettings abstract base by hand.
 *
 * This test compiles and exercises the narrow stubs on their own — the win
 * the reviewer asked for in PR #493: stubs compose, scroll-only tests don't
 * pull the snap surface, and the abstract-base requirement is a slice not
 * the whole interface.
 */

#include <QTest>
#include <QObject>

#include "../helpers/StubAutotileSettings.h"
#include "../helpers/StubScrollSettings.h"

using namespace PlasmaZones;

namespace {

// A made-up consumer that only needs IScrollSettings — no snap, no autotile,
// no animations. This is the shape a future contributor would write for a
// scroll-mode-only feature.
class ScrollOnlyConsumer
{
public:
    explicit ScrollOnlyConsumer(IScrollSettings* settings)
        : m_settings(settings)
    {
    }
    bool shouldDecorate() const
    {
        return m_settings && m_settings->scrollingEnabled() && m_settings->scrollShowBorder();
    }
    int decorationWidth() const
    {
        return m_settings ? m_settings->scrollBorderWidth() : 0;
    }

private:
    IScrollSettings* m_settings = nullptr;
};

// Same shape, autotile side.
class AutotileOnlyConsumer
{
public:
    explicit AutotileOnlyConsumer(IAutotileSettings* settings)
        : m_settings(settings)
    {
    }
    bool shouldDecorate() const
    {
        return m_settings && m_settings->autotileEnabled() && m_settings->autotileShowBorder();
    }

private:
    IAutotileSettings* m_settings = nullptr;
};

} // namespace

class TestSettingsNarrowStubs : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The narrow scroll stub satisfies IScrollSettings on its own — no need
    // to instantiate the full StubSettings. This is the whole point of the
    // split: a scroll-only consumer takes a scroll-only stub.
    void scrollStubStandsAlone()
    {
        StubScrollSettings stub;
        IScrollSettings* iface = &stub;
        QVERIFY(iface != nullptr);
        // Defaults route through ConfigDefaults, same as the unified stub.
        QCOMPARE(iface->scrollBorderWidth(), ConfigDefaults::scrollBorderWidth());
    }

    void autotileStubStandsAlone()
    {
        StubAutotileSettings stub;
        IAutotileSettings* iface = &stub;
        QVERIFY(iface != nullptr);
        QCOMPARE(iface->autotileEnabled(), ConfigDefaults::autotileEnabled());
    }

    // A consumer designed to only need IScrollSettings can be exercised
    // with StubScrollSettings alone — not StubSettings. This is the win
    // for new feature work: narrow contract → narrow stub → narrow test.
    void scrollOnlyConsumerWithNarrowStub()
    {
        StubScrollSettings stub;
        ScrollOnlyConsumer consumer(&stub);
        // shouldDecorate() defaults: scrollingEnabled=default, scrollShowBorder=default.
        // The exact value isn't the point — the point is that the test
        // compiles and links without dragging in snap/animation/persistence stubs.
        Q_UNUSED(consumer.shouldDecorate());
        QCOMPARE(consumer.decorationWidth(), ConfigDefaults::scrollBorderWidth());
    }

    void autotileOnlyConsumerWithNarrowStub()
    {
        StubAutotileSettings stub;
        AutotileOnlyConsumer consumer(&stub);
        Q_UNUSED(consumer.shouldDecorate());
    }
};

QTEST_GUILESS_MAIN(TestSettingsNarrowStubs)
#include "test_settings_narrow_stubs.moc"
