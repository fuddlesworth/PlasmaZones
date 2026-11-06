# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The CI image: Arch with every package .github/ci/arch-packages.txt lists.
# Built and pushed to ghcr.io/<owner>/plasmazones-ci by
# .github/workflows/ci-image.yml once a week and whenever this directory
# changes on main. Not the root Dockerfile, which is the Fedora build image for
# developers on macOS.
#
# Why an image at all: each container job used to start from archlinux:latest
# and run pacman -Syu, so the compiler and every Qt/KF6 header moved whenever
# Arch published an update. ccache keys on both, and one such update turned a
# 3 minute warm build into a 30 to 45 minute cold one. A built image pins them
# for its week and skips about two minutes of package installs per job.

FROM archlinux:latest

ARG IMAGE_ID=local

SHELL ["/bin/bash", "-c"]

COPY arch-packages.txt /etc/plasmazones-ci/arch-packages.txt

RUN set -euo pipefail; \
    mapfile -t packages < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' -e '/^$/d' /etc/plasmazones-ci/arch-packages.txt); \
    pacman -Syu --noconfirm --needed "${packages[@]}"; \
    rm -rf /var/cache/pacman/pkg/*; \
    echo "$IMAGE_ID" > /etc/plasmazones-ci/id

LABEL org.opencontainers.image.source="https://github.com/phosphor-works/PlasmaZones" \
      org.opencontainers.image.description="PlasmaZones CI build environment (Arch)" \
      org.opencontainers.image.licenses="GPL-3.0-or-later"
