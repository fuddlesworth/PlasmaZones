# SPDX-FileCopyrightText: 2026 fuddlesworth
# COPR SRPM build instructions
# Called by COPR build system: make -f .copr/Makefile srpm outdir=<path>
# Also usable locally: make -f .copr/Makefile srpm outdir=./results
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Note: the COPR SRPM mock chroot is minimal (no git). Use tar, not git archive.

SPEC := packaging/rpm/plasmazones.spec
NAME := plasmazones

# Extract version from CMakeLists.txt (spec has placeholder 0.0.0; CI stamps it)
VERSION := $(shell sed -n 's/^project(PlasmaZones VERSION \([^ )]*\).*/\1/p' CMakeLists.txt)
# An empty parse would write "Version:" with no value into the spec and tar to
# plasmazones-.tar.gz. Both other consumers of this parse guard it the same way.
ifeq ($(strip $(VERSION)),)
$(error Could not parse VERSION from CMakeLists.txt)
endif

srpm: $(SPEC)
	@echo "=== Building SRPM for $(NAME)-$(VERSION) ==="

	# Stamp version into spec
	sed -i 's/^Version:.*/Version:        $(VERSION)/' $(SPEC)

	# Generate changelog from CHANGELOG.md if the script exists
	if [ -x packaging/generate-changelog.sh ]; then \
		bash packaging/generate-changelog.sh rpm; \
	fi

	# Create source tarball matching Source0 pattern in the spec
	# (no git in mock chroot — use tar with --transform to set prefix)
	mkdir -p /tmp/$(NAME)-srpm
	tar czf /tmp/$(NAME)-srpm/$(NAME)-$(VERSION).tar.gz \
		--transform 's,^\.,PlasmaZones-$(VERSION),' \
		--exclude=.git --exclude='build*' --exclude=.moon/cache --exclude=.claude .

	# Build SRPM
	mkdir -p /tmp/$(NAME)-rpmbuild
	rpmbuild -bs $(SPEC) \
		--define "_sourcedir /tmp/$(NAME)-srpm" \
		--define "_srcrpmdir $(outdir)" \
		--define "_topdir /tmp/$(NAME)-rpmbuild"

	@echo "=== SRPM written to $(outdir) ==="

.PHONY: srpm
