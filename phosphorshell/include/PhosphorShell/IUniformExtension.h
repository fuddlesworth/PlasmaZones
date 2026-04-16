// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorshell_export.h>

namespace PhosphorShell {

/// Interface for appending custom uniform data after the base UBO layout.
///
/// The RHI render node allocates sizeof(BaseUniforms) + extensionSize() bytes
/// for the UBO. During prepare(), if isDirty() returns true, write() is called
/// to fill the extension region.
///
/// All methods may be called on the render thread.
class PHOSPHORSHELL_EXPORT IUniformExtension
{
public:
    virtual ~IUniformExtension() = default;

    /// Size in bytes of the extension region (must respect std140 alignment).
    ///
    /// Must be stable for the lifetime of the extension instance: the render
    /// node sizes the UBO and its staging buffer once when the extension is
    /// installed (via setUniformExtension) and reuses both across frames. A
    /// changing size silently bypasses the resize path and risks UBO write
    /// overruns. To change the size, install a fresh IUniformExtension
    /// instance with the new size — that triggers UBO recreation.
    virtual int extensionSize() const = 0;

    /// Write extension data into @p buffer starting at @p offset.
    /// Called on the render thread during prepare().
    virtual void write(char* buffer, int offset) const = 0;

    /// Whether the extension data has changed since the last write.
    virtual bool isDirty() const = 0;

    /// Mark as clean after a successful write.
    virtual void clearDirty() = 0;
};

} // namespace PhosphorShell
