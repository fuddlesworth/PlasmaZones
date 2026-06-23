# SPDX-License-Identifier: GPL-3.0-or-later
#
# packaging/nix/formatter.nix — `nix fmt`. Formats Nix, C++, and QML in-tree,
# reusing the repo's own .clang-format config so there is no second source of
# truth for C++ style. Wired to flake `formatter.<system>`.
{
  writeShellApplication,
  nixfmt-rfc-style,
  clang-tools,
  qt6,
  findutils,
}:

writeShellApplication {
  name = "plasmazones-fmt";
  runtimeInputs = [
    nixfmt-rfc-style # nixfmt (RFC 166 style)
    clang-tools      # clang-format (honours the in-tree .clang-format)
    qt6.qttools      # qmlformat
    findutils        # find
  ];
  text = ''
    # Nix files.
    find . -type f -name '*.nix' \
      -not -path './build/*' -not -path './.git/*' \
      -exec nixfmt {} +

    # C / C++ — uses the in-tree .clang-format.
    find . -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
      -not -path './build/*' -not -path './.git/*' \
      -exec clang-format -i {} +

    # QML.
    find . -type f -name '*.qml' \
      -not -path './build/*' -not -path './.git/*' \
      -exec qmlformat -i {} +
  '';
}
