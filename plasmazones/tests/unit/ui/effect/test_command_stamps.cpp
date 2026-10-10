// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_command_stamps.cpp
 * @brief WindowCommandStamps (plasmazoneseffect/effect_state.h): a daemon loss
 *        supersedes every deferred apply at once (F405), while a zero stamp
 *        stays current as before.
 */

#include "plasmazoneseffect/effect_state.h"

#include <QTest>

using PlasmaZones::WindowCommandStamps;

namespace {
// The stamps key on the window pointer and never dereference it.
const KWin::EffectWindow* fakeWindow(quintptr value)
{
    return reinterpret_cast<const KWin::EffectWindow*>(value);
}
} // namespace

class TestCommandStamps : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void supersedeAll_retiresEveryCapturedStamp()
    {
        WindowCommandStamps stamps;
        const auto* w1 = fakeWindow(0x10);
        const auto* w2 = fakeWindow(0x20);
        const quint64 s1 = stamps.bump(w1);
        const quint64 s2 = stamps.bump(w2);
        QVERIFY(stamps.isCurrent(w1, s1));
        QVERIFY(stamps.isCurrent(w2, s2));
        stamps.supersedeAll();
        QVERIFY(!stamps.isCurrent(w1, s1));
        QVERIFY(!stamps.isCurrent(w2, s2));
    }

    // An entry scheduled with no live window carries a zero stamp, which is
    // never superseded.
    void supersedeAll_zeroStampStaysCurrent()
    {
        WindowCommandStamps stamps;
        const auto* w1 = fakeWindow(0x10);
        stamps.bump(w1);
        stamps.supersedeAll();
        QVERIFY(stamps.isCurrent(w1, 0));
    }

    void supersedeAll_dropsStaleAcks()
    {
        WindowCommandStamps stamps;
        const auto* w1 = fakeWindow(0x10);
        stamps.staleAcks.insert(w1, WindowCommandStamps::StaleAck{QRect(0, 0, 100, 100), stamps.bump(w1)});
        stamps.supersedeAll();
        QVERIFY(stamps.staleAcks.isEmpty());
    }
};

QTEST_MAIN(TestCommandStamps)
#include "test_command_stamps.moc"
