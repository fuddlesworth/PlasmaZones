// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/phosphorprotocoltypes_export.h>

#include <QMetaType>
#include <QString>

namespace PhosphorProtocol {

/// D-Bus struct for bridge registration result: (sss)
struct PHOSPHORPROTOCOLTYPES_EXPORT BridgeRegistrationResult
{
    QString apiVersion;
    QString bridgeName;
    QString sessionId;

    /// Returns empty QString if valid, or a human-readable description of
    /// the invariant violation. "REJECTED" sessionId is a valid sentinel
    /// signaling version mismatch and is NOT flagged as invalid — callers
    /// must check for it separately before using the result.
    QString validationError() const;
};

} // namespace PhosphorProtocol

Q_DECLARE_METATYPE(PhosphorProtocol::BridgeRegistrationResult)
