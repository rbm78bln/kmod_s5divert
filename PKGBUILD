# Maintainer: rbm78bln <rbm78bln(at)github(dot)com>
# Contributor: rbm78bln <rbm78bln(at)github(dot)com>

_pkgbase=s5divert
pkgname=kmod_s5divert-dkms
pkgver=$(grep MODULE_VERSION ${_pkgbase}.c | cut '-d"' -f2)
pkgrel=0
pkgdesc="A kernel module to divert system power off from ACPI S5 to S4, S3, or system reboot (DKMS)"
arch=('i686' 'x86_64')
url="https://github.com/rbm78bln/kmod_s5divert"
license=('GPL2')
depends=(
	'binutils'
	'coreutils'
	'curl'
	'dkms'
	'gawk'
	'gcc'
	'git'
	'grep'
	'kmod'
	'libarchive'
	'linux-headers'
	'make'
	'sed'
)
install=${_pkgbase}.install
source=(
	'.gitignore'
	'LICENSE'
	'Makefile'
	'PKGBUILD'
	'README.md'
    'dkms_conf.template'
    's5divert.c'
    's5divert.install'
)
sha256sums=(
	'SKIP'
	'SKIP'
	'SKIP'
	'SKIP'
	'SKIP'
	'SKIP'
	'SKIP'
	'SKIP'
)

pkgver () {
	cd "${srcdir}/${pkgname}"
	grep MODULE_VERSION ${_pkgbase}.c | cut '-d"' -f2
}

prepare() {
	cd "${srcdir}"
	mkdir "${pkgname}"
	for FILE in "${source[@]}"; do
	   cat "${FILE}" >"${pkgname}/${FILE}"
	done
}

build() {
	cd "${srcdir}/${pkgname}"
	make dkms.conf
	make modules-load.conf
}

package() {
	mkdir -p "${pkgdir}/usr/src/${pkgname}"
	[ -d "${srcdir}/../.git" ] && cp --archive "${srcdir}/../.git" "${pkgdir}"/usr/src/${pkgname}/.git
	for FILE in "${source[@]}"; do
	   install -Dm644 ${srcdir}/${pkgname}/${FILE} "${pkgdir}"/usr/src/${pkgname}/${FILE}
	done
	install -Dm644 ${srcdir}/${pkgname}/dkms.conf "${pkgdir}"/usr/src/${pkgname}/dkms.conf
	install -Dm644 ${srcdir}/${pkgname}/modules-load.conf "${pkgdir}"/etc/modules-load.d/${_pkgbase}.conf
}
