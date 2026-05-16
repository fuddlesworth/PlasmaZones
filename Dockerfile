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

FROM fedora:42

# --- Build tools (spec: Build tools section) ---
# --- Qt6 (spec: Fedora path) ---
# --- KDE Frameworks 6 (spec: Fedora path) ---
# --- Plasma 6.6 / KWin 6.6 (spec: Fedora path) ---
# Plus ccache for faster rebuilds
RUN dnf install -y --setopt=install_weak_deps=False \
        /usr/bin/wayland-scanner \
        cmake \
        extra-cmake-modules \
        gcc-c++ \
        ninja-build \
        ccache \
        qt6-qtbase-devel \
        qt6-qtbase-private-devel \
        qt6-qtdeclarative-devel \
        qt6-qttools-devel \
        qt6-qtshadertools-devel \
        qt6-qtsvg-devel \
        kf6-kcmutils-devel \
        kf6-kglobalaccel-devel \
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
    && dnf clean all

WORKDIR /build

ENTRYPOINT ["/bin/bash", "-c", "\
    cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    && cmake --build /build --parallel $(nproc) \
    && \"$@\"", "--"]

CMD ["ctest", "--output-on-failure"]
