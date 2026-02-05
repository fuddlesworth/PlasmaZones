# PlasmaZones RPM Spec File
# FancyZones-style window tiling for KDE Plasma
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build: rpmbuild -ba plasmazones.spec
# Clean build: mock -r fedora-43-x86_64 plasmazones-1.2.0-1.fc43.src.rpm
#
# Version is set by CI from the git tag (e.g. v1.3.4 -> 1.3.4). Local builds use the value below.

Name:           plasmazones
Version:        0.0.0
Release:        1%{?dist}
Summary:        FancyZones-style window tiling for KDE Plasma

License:        GPL-3.0-or-later
URL:            https://github.com/fuddlesworth/PlasmaZones
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

# Plasma 6 / Wayland only
ExclusiveArch:  x86_64 aarch64

# Build tools
BuildRequires:  cmake >= 3.16
BuildRequires:  ninja-build
BuildRequires:  extra-cmake-modules >= 6.0.0
BuildRequires:  gcc-c++
BuildRequires:  gettext

# Qt6
BuildRequires:  qt6-qtbase-devel >= 6.6.0
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  qt6-qtshadertools-devel

# KDE Frameworks 6
BuildRequires:  kf6-kconfig-devel >= 6.0.0
BuildRequires:  kf6-kconfigwidgets-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kdbusaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcmutils-devel
BuildRequires:  kf6-kwindowsystem-devel
BuildRequires:  kf6-kglobalaccel-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  kf6-kcolorscheme-devel

# Plasma/KWin
BuildRequires:  kwin-devel
BuildRequires:  layer-shell-qt-devel

# Optional
BuildRequires:  plasma-activities-devel

# Systemd macros
BuildRequires:  systemd-rpm-macros
%{?systemd_requires}

# Runtime dependencies
Requires:       qt6-qtbase >= 6.6.0
Requires:       qt6-qtdeclarative
Requires:       qt6-qtshadertools
Requires:       kf6-kconfig
Requires:       kf6-kconfigwidgets
Requires:       kf6-kcoreaddons
Requires:       kf6-kdbusaddons
Requires:       kf6-ki18n
Requires:       kf6-kcmutils
Requires:       kf6-kwindowsystem
Requires:       kf6-kglobalaccel
Requires:       kf6-knotifications
Requires:       kf6-kcolorscheme
Requires:       layer-shell-qt
Requires:       kwin
Requires:       hicolor-icon-theme

%description
PlasmaZones brings FancyZones-style window tiling to KDE Plasma 6.

Features:
- Drag windows to predefined zones
- Custom zone layouts with visual editor
- Modifier key activation (Shift, Ctrl, etc.)
- Multi-monitor support
- Keyboard navigation between zones
- Activity-based layout switching
- Wayland-native using Layer Shell

%prep
%autosetup -n PlasmaZones-%{version}

%build
%cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF
%cmake_build

%install
%cmake_install

%post
# Refresh KDE service cache
/usr/bin/kbuildsycoca6 --noincremental 2>/dev/null || :
# Update icon cache
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
%systemd_user_post plasmazones.service

%preun
%systemd_user_preun plasmazones.service

%postun
/usr/bin/kbuildsycoca6 --noincremental 2>/dev/null || :
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
%systemd_user_postun_with_restart plasmazones.service

%files
%license LICENSE
%doc README.md

# Executables
%{_bindir}/plasmazonesd
%{_bindir}/plasmazones-editor

# Libraries
%{_libdir}/libplasmazones_core.so*

# KWin effect plugin
%{_libdir}/qt6/plugins/kwin/effects/plugins/kwin_effect_plasmazones.so

# KCM (System Settings module)
%{_libdir}/qt6/plugins/plasma/kcms/systemsettings/kcm_plasmazones.so

# QML modules (KDE KCM QML files)
%{_libdir}/qt6/qml/org/kde/kcms/plasmazones/

# D-Bus interfaces
%{_datadir}/dbus-1/interfaces/org.plasmazones.*.xml

# Desktop files
%{_datadir}/applications/org.plasmazones.editor.desktop
%{_datadir}/applications/org.plasmazones.daemon.desktop
%{_datadir}/applications/kcm_plasmazones.desktop

# Data files
%{_datadir}/plasmazones/

# Icons
%{_datadir}/icons/hicolor/scalable/apps/plasmazones.svg
%{_datadir}/icons/hicolor/scalable/apps/plasmazones-editor.svg
%{_datadir}/icons/hicolor-light/scalable/apps/plasmazones.svg
%{_datadir}/icons/hicolor-light/scalable/apps/plasmazones-editor.svg

# KConfig
%{_datadir}/config.kcfg/plasmazones.kcfg

# Systemd user service
%{_userunitdir}/plasmazones.service

# Translations (optional - may not exist yet)
%{_datadir}/locale/*/LC_MESSAGES/plasmazonesd.mo

%changelog
* Wed Feb  5 2026 fuddlesworth - 1.5.2-1
- Fix missing pacman hook files for sycoca cache refresh

* Wed Feb  5 2026 fuddlesworth - 1.5.1-1
- Multi-pass shader rendering with up to 4 buffer passes and texture channels
- Multi-channel shaders: buffer passes read outputs from previous passes
- Zone labels rendered as shader textures for custom number styling
- Fix buffer pass alpha blending in RHI renderer
- New Voronoi Stained Glass shader (3D raymarched with bloom)
- Mouse button zone activation as alternative to modifier-key drag (PR #80)
- Zone selector auto-scroll and screen-clamped positioning
- Update notification banner with GitHub release checking
- About tab with version info and links in KCM
- Pywal color import error feedback
- Fix duplicate zone IDs and use-after-free in editor undo system
- Redesign help dialog and fullscreen exit button
- Fix update check button layout shift
- Automatic AUR publishing on release

* Tue Feb  3 2026 fuddlesworth - 1.3.3-1
- Build/install only installs files; packaging handles sycoca and daemon enable
- Resnap-to-new-layout shortcut and shortcut consolidation (PR #75)
- Logging alignment and PR review fixes

* Sun Feb  1 2026 fuddlesworth - 1.2.0-1
- Initial RPM package
- Wayland-only release (X11 support removed)
- KDE Plasma 6 / KF6 / Qt6 required
- Window rotation zone ID matching fix
