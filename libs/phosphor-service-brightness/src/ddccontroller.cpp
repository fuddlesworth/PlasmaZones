// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "ddccontroller.h"

#include <QByteArray>
#include <QLoggingCategory>

#include <ddcutil_c_api.h>

#include <algorithm>
#include <cstdlib>

Q_LOGGING_CATEGORY(lcDdcController, "phosphor.service.brightness.ddc")

namespace {
constexpr DDCA_Vcp_Feature_Code kLuminanceFeature = 0x10;
} // namespace

namespace PhosphorServiceBrightness {

DdcController::DdcController(QObject* parent)
    : QObject(parent)
{
}

DdcController::~DdcController() = default;

bool DdcController::ensureInitialized()
{
    if (m_initialized)
        return true;

    // libddcutil writes startup chatter (e.g. the sleep-watch thread notice) to
    // its per-thread stdout/stderr by default; as an embedded library we never
    // want that on the consumer's streams. These redirections are thread-local
    // (ddcutil docs), and this controller owns its worker thread, so they only
    // silence our thread. The config file is still read (DDCA_INIT_OPTIONS_NONE),
    // preserving any per-monitor quirks the user has configured.
    ddca_set_fout(nullptr);
    ddca_set_ferr(nullptr);

    // ddca_init2 returns the init informational messages instead of printing
    // them (unlike the deprecated ddca_init); route them to our debug log.
    char** infoMsgs = nullptr;
    const DDCA_Status status = ddca_init2(nullptr, DDCA_SYSLOG_NEVER, DDCA_INIT_OPTIONS_NONE, &infoMsgs);
    if (infoMsgs) {
        for (char** msg = infoMsgs; *msg; ++msg) {
            qCDebug(lcDdcController) << "ddca_init:" << *msg;
            free(*msg);
        }
        free(infoMsgs);
    }
    // Init checks < 0 (hard error) rather than != 0: a positive status is a
    // non-fatal warning, unlike the per-call getters/setters below where any
    // non-zero status means the VCP operation did not complete.
    if (status < 0) {
        qCDebug(lcDdcController) << "ddca_init2 failed:" << status;
        return false;
    }
    m_initialized = true;
    return true;
}

bool DdcController::readLuminance(void* handle, int& current, int& maxValue)
{
    DDCA_Non_Table_Vcp_Value valrec{};
    const DDCA_Status status =
        ddca_get_non_table_vcp_value(static_cast<DDCA_Display_Handle>(handle), kLuminanceFeature, &valrec);
    if (status != 0)
        return false;
    current = (valrec.sh << 8) | valrec.sl;
    maxValue = (valrec.mh << 8) | valrec.ml;
    // A zero (or negative) max means the luminance feature is uncontrollable on
    // this monitor (a DDC/CI quirk): treat it as a failed read so enumeration
    // skips it rather than surfacing a permanently-inert display row.
    if (maxValue <= 0)
        return false;
    return true;
}

void DdcController::enumerate()
{
    if (!ensureInitialized())
        return;

    // Re-enumeration is not part of the current contract (the host calls this
    // once), but make it self-enforcing: drop any prior id->dref map before
    // re-probing so stale ids do not linger.
    m_refs.clear();

    DDCA_Display_Info_List* list = nullptr;
    const DDCA_Status status = ddca_get_display_info_list2(/*include_invalid=*/false, &list);
    if (status != 0 || !list) {
        qCDebug(lcDdcController) << "ddca_get_display_info_list2 failed:" << status;
        return;
    }

    for (int i = 0; i < list->ct; ++i) {
        const DDCA_Display_Info& info = list->info[i];
        // Address displays by their I2C bus (stable per physical port and what
        // `ddcutil --bus N` uses), not the display number (reassigned across
        // enumerations) or the model name (identical monitors collide).
        const QString id = info.path.io_mode == DDCA_IO_I2C ? QStringLiteral("i2c-%1").arg(info.path.path.i2c_busno)
                                                            : QStringLiteral("dispno-%1").arg(info.dispno);
        DDCA_Display_Handle handle = nullptr;
        if (ddca_open_display2(info.dref, /*wait=*/true, &handle) != 0 || !handle) {
            qCDebug(lcDdcController) << "open failed for display" << id;
            continue;
        }
        int current = 0;
        int maxValue = 0;
        const bool ok = readLuminance(handle, current, maxValue);
        ddca_close_display(handle);
        if (!ok) {
            qCDebug(lcDdcController) << "luminance read failed for display" << id;
            continue;
        }
        m_refs.insert(id, info.dref);
        // model_name is a fixed-size EDID field (13 chars max in a 14-byte
        // buffer); bound the read so a non-NUL-terminated field can never
        // over-read past the array.
        const QString name = QString::fromUtf8(info.model_name, qstrnlen(info.model_name, sizeof(info.model_name)));
        Q_EMIT displayFound(id, name, current, maxValue);
    }

    // The drefs stored in m_refs are library-owned and survive freeing the list
    // (ddca_free_display_info_list: the list is a pointer-free copy; only
    // ddca_redetect_displays invalidates drefs), so the list itself is no longer
    // needed once the map is built.
    ddca_free_display_info_list(list);
}

void DdcController::setBrightness(const QString& id, int value)
{
    const auto it = m_refs.constFind(id);
    if (it == m_refs.constEnd())
        return;
    // VCP non-table values are 16-bit; clamp at this I2C boundary so a stray
    // out-of-range request can never wrap into the packed bytes. The upstream
    // BrightnessDevice clamps to the per-display max; this guards the boundary
    // itself, since the slot is reachable by any queued caller.
    value = std::clamp(value, 0, 0xFFFF);
    DDCA_Display_Handle handle = nullptr;
    if (ddca_open_display2(it.value(), /*wait=*/true, &handle) != 0 || !handle) {
        qCDebug(lcDdcController) << "open failed for set on display" << id;
        return;
    }
    // ddca_set_non_table_vcp_value2 is the non-deprecated setter; it does not
    // verify, which is what we want since we do our own read-back below.
    const DDCA_Status status = ddca_set_non_table_vcp_value2(
        handle, kLuminanceFeature, static_cast<uint8_t>((value >> 8) & 0xff), static_cast<uint8_t>(value & 0xff));
    if (status != 0)
        qCDebug(lcDdcController) << "set luminance failed for display" << id << ":" << status;
    // Read back and report the value as authoritative even if the set above
    // failed: the read reflects the monitor's true state (DDC also clamps to its
    // own range), so a rejected set simply reports the unchanged value and QML
    // stays truthful rather than showing an optimistic value that never applied.
    int current = 0;
    int maxValue = 0;
    if (readLuminance(handle, current, maxValue))
        Q_EMIT brightnessRead(id, current);
    ddca_close_display(handle);
}

} // namespace PhosphorServiceBrightness
