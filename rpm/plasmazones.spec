# PlasmaZones RPM Spec File
# FancyZones-style window tiling for KDE Plasma
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build: rpmbuild -ba plasmazones.spec
# Clean build: mock -r fedora-41-x86_64 plasmazones-1.2.0-1.fc41.src.rpm

Name:           plasmazones
Version:        1.2.0
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
    -DCMAKE_BUILD_TYPE=Release \
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
%license COPYING
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

# QML modules
%{_libdir}/qt6/qml/org/kde/kcms/plasmazones/
%{_libdir}/qt6/qml/org/plasmazones/

# D-Bus interfaces
%{_datadir}/dbus-1/interfaces/org.plasmazones.*.xml

# Desktop files
%{_datadir}/applications/org.kde.plasmazones-editor.desktop
%{_datadir}/kservices6/kcm_plasmazones.desktop

# Data files
%{_datadir}/plasmazones/

# Icons
%{_datadir}/icons/hicolor/scalable/apps/plasmazones.svg
%{_datadir}/icons/hicolor/scalable/apps/plasmazones-editor.svg
%{_datadir}/icons/hicolor-light/scalable/apps/plasmazones.svg
%{_datadir}/icons/hicolor-light/scalable/apps/plasmazones-editor.svg

# KConfig
%{_datadir}/config.kcfg/plasmazones.kcfg

# Notifications
%{_datadir}/knotifications6/plasmazones.notifyrc

# Systemd user service
%{_userunitdir}/plasmazones.service

# Translations
%{_datadir}/locale/*/LC_MESSAGES/plasmazonesd.mo
%{_datadir}/locale/*/LC_MESSAGES/plasmazones-editor.mo
%{_datadir}/locale/*/LC_MESSAGES/kcm_plasmazones.mo

%changelog
* Sat Feb 01 2026 fuddlesworth - 1.2.0-1
- Initial RPM package
- Wayland-only release (X11 support removed)
- KDE Plasma 6 / KF6 / Qt6 required
- Window rotation zone ID matching fix
