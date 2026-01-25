# Maintainer: fuddlesworth
pkgname=plasmazones-git
pkgver=1.0.0.r0.g0000000
pkgrel=1
pkgdesc="FancyZones-style window tiling for KDE Plasma"
arch=('x86_64')
url="https://github.com/fuddlesworth/PlasmaZones"
license=('GPL-3.0-or-later')
depends=(
    'qt6-base'
    'qt6-declarative'
    'kconfig'
    'kconfigwidgets'
    'kcoreaddons'
    'kdbusaddons'
    'ki18n'
    'kcmutils'
    'kwindowsystem'
    'kglobalaccel'
    'knotifications'
    'kcolorscheme'
)
makedepends=(
    'git'
    'cmake'
    'extra-cmake-modules'
)
optdepends=(
    'plasma-activities: activity-based layouts'
    'layer-shell-qt: better Wayland overlay support'
)
provides=('plasmazones')
conflicts=('plasmazones')
source=("${pkgname}::git+${url}.git")
sha256sums=('SKIP')

pkgver() {
    cd "$pkgname"
    git describe --long --tags 2>/dev/null | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g' ||
    printf "1.0.0.r%s.g%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cmake -B build -S "$pkgname" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DINSTALL_DAEMON_AUTOSTART=ON \
        -DBUILD_TESTING=OFF \
        -Wno-dev
    cmake --build build --parallel
}

package() {
    DESTDIR="$pkgdir" cmake --install build
}
