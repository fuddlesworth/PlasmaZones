# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# packaging/nix/devShell.nix — the `nix develop` environment. It is
# callPackage'd from a pkgs set that already has the PlasmaZones overlay
# applied, so `plasmazones` resolves to the real package and the shell inherits
# its complete build environment via `inputsFrom`.
{
  mkShell,
  plasmazones,
  gdb,
  clang-tools,
  cmake-language-server,
  qt6,
  glslang,
}:

mkShell {
  # Inherit every nativeBuildInput / buildInput from the package derivation, so
  # the shell can build PlasmaZones exactly as the package does.
  inputsFrom = [ plasmazones ];

  # Developer tools not needed for the build itself:
  packages = [
    gdb                   # step through the daemon / KWin effect plugin
    clang-tools           # clangd + clang-format
    cmake-language-server # CMake LSP
    qt6.qttools           # qdbus6, qmlformat, lupdate / lrelease
    # Needed by the shader_validate_animations test, which shells out to it to
    # compile the compositor-only animation packs and FAILS rather than skips
    # when it is absent. It comes from the devShell rather than the package
    # because the package builds with -DBUILD_TESTING=OFF, while the shellHook
    # below tells developers to configure with it ON.
    glslang
  ];

  shellHook = ''
    echo "PlasmaZones dev shell ready."
    echo "Build:  cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON && cmake --build build"
    echo "Test:   cmake --build build --target test  (requires running Wayland compositor)"
  '';
}
