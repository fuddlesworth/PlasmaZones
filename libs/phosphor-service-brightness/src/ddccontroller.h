// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

namespace PhosphorServiceBrightness {

/**
 * @brief Off-thread libddcutil wrapper for external-monitor brightness (DDC/CI
 * VCP feature 0x10).
 *
 * Only built when PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL. Designed to live on a
 * dedicated worker QThread: every slot blocks on I2C (enumeration probes all
 * buses; get/set are ~50ms+ with retries), so it must never run on the GUI
 * thread. Results are reported via signals delivered back to the host thread
 * by Qt's queued connections.
 *
 * The libddcutil opaque handles are held as `void*` so this header stays free
 * of the ddcutil includes; the `.cpp` casts them.
 */
class DdcController : public QObject
{
    Q_OBJECT

public:
    explicit DdcController(QObject* parent = nullptr);
    ~DdcController() override;

public Q_SLOTS:
    /// Probe all DDC/CI displays; emit displayFound for each, with its current
    /// and max luminance. One-shot (called once after the worker starts).
    void enumerate();
    /// Set a display's luminance (raw VCP 0x10 value), then read it back and
    /// emit brightnessRead. @p id is the value passed to displayFound.
    void setBrightness(const QString& id, int value);

Q_SIGNALS:
    void displayFound(const QString& id, const QString& name, int brightness, int maxBrightness);
    void brightnessRead(const QString& id, int brightness);

private:
    bool ensureInitialized();
    // Read VCP 0x10 from an already-open handle into current/max; false on error.
    static bool readLuminance(void* handle, int& current, int& maxValue);

    // id -> DDCA_Display_Ref. The drefs are library-owned (valid until
    // ddca_redetect_displays), so we keep value-copies here and do not retain
    // the display-info list.
    QHash<QString, void*> m_refs;
    bool m_initialized = false;
};

} // namespace PhosphorServiceBrightness
