# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones build container — allows building and testing on macOS via Docker.
# Build deps copied from packaging/rpm/plasmazones.spec (Fedora path).
#
# Usage:
#   docker build -t plasmazones-build .
#   docker run --rm -v "$PWD":/src plasmazones-build
#   docker run --rm -v "$PWD":/src plasmazones-build ctest --output-on-failure

FROM fedora:44

# --- Build tools (spec: Build tools section) ---
# --- Qt6 (spec: Fedora path) ---
# --- KDE Frameworks 6 (spec: Fedora path) ---
# --- Plasma 6.7 / KWin 6.7 (spec: Fedora path) ---
# Plus the package providing /usr/bin/dbus-run-session,
# which cmake/PhosphorTestIsolation.cmake uses to give ctest a private session bus.
RUN dnf install -y --setopt=install_weak_deps=False \
        /usr/bin/wayland-scanner \
        cmake \
        extra-cmake-modules \
        gcc-c++ \
        ninja-build \
        qt6-qtbase-devel \
        qt6-qtbase-private-devel \
        qt6-qtdeclarative-devel \
        qt6-qttools-devel \
        qt6-qtshadertools-devel \
        qt6-qtsvg-devel \
        kf6-kcmutils-devel \
        kf6-kglobalaccel-devel \
        kf6-kcolorscheme-devel \
        kf6-kirigami-devel \
        kwin-devel \
        qt6-qtwayland-devel \
        libepoxy-devel \
        wayland-devel \
        libdrm-devel \
        libxkbcommon-devel \
        vulkan-loader-devel \
        vulkan-headers \
        plasma-activities-devel \
        systemd-rpm-macros \
        /usr/bin/dbus-run-session \
    && dnf clean all

WORKDIR /build

# The container has no session, so XDG_RUNTIME_DIR is unset and Qt emits a
# "QStandardPaths: XDG_RUNTIME_DIR not set" warning the first time
# QStandardPaths is queried. Tests that assert no warnings were emitted would
# pick that stray warning up and fail, so give it a private dir as a real
# session would (mirrors .github/workflows/ci.yml).
ENTRYPOINT ["/bin/bash", "-c", "\
    cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DBUILD_TESTING=ON \
    && cmake --build /build --parallel $(nproc) \
    && export XDG_RUNTIME_DIR=\"$(mktemp -d)\" \
    && \"$@\"", "--"]

CMD ["ctest", "--output-on-failure"]
