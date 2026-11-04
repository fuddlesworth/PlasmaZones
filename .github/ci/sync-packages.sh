#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bring a CI container's packages in line with .github/ci/arch-packages.txt.
#
# In the CI image (.github/ci/Dockerfile) the list it was built from sits at
# /etc/plasmazones-ci/arch-packages.txt. When the checkout's list matches it,
# which is every run that does not edit the list, there is nothing to install
# and this returns at once. That is the point of the image: the toolchain and
# the Qt/KF6 headers do not move between runs, so the compiler cache stays warm
# for the whole week the image is current.
#
# When the lists differ (a pull request that adds a dependency) or there is no
# baked list at all (the bare archlinux:latest fallback ci.yml uses when the
# image cannot be pulled), it does a full upgrade with the checkout's list.
# A full -Syu rather than -S, because installing against a stale sync database
# risks Arch's unsupported partial-upgrade state.
set -euo pipefail

list="${1:-.github/ci/arch-packages.txt}"
baked=/etc/plasmazones-ci/arch-packages.txt

if [ -f "$baked" ] && cmp -s "$list" "$baked"; then
    echo "Packages match the CI image $(cat /etc/plasmazones-ci/id); nothing to install."
    exit 0
fi

if [ -f "$baked" ]; then
    echo "::notice::$list differs from the list the CI image was built from; upgrading in place."
    diff "$baked" "$list" || true
else
    echo "Not running in the CI image; installing every package in $list."
fi

mapfile -t packages < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' -e '/^$/d' "$list")
# Retried, so one flaky mirror does not fail the job.
for try in 1 2 3; do
    pacman -Syu --noconfirm --needed "${packages[@]}" && exit 0
    [ "$try" = 3 ] && exit 1
    sleep 10
done
