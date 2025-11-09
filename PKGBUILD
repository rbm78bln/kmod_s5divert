# Maintainer: foo <foo(at)example(dot)org>
# Contributor: bar <bar(at)example(dot)org>

_pkgbase=s5divert
pkgname=kmod_s5divert-dkms
pkgver=0.1
pkgrel=1
pkgdesc="A kernel module to divert system power off from ACPI S5 to S4, S3, or system reboot (DKMS)"
arch=('i686' 'x86_64')
# url="https://www.example.org/"
license=('GPL2')
depends=('dkms' 'linux-headers' 'git')
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
	for FILE in 'Makefile' 'PKGBUILD' 'dkms_conf.template' 's5divert.c' 's5divert.install'; do
	   cat "${FILE}" >"${pkgname}/${FILE}"
	done
}

build() {
	cd "${srcdir}/${pkgname}"
	cat dkms_conf.template | \
	sed \
	    -e "s/@PKGBASE@/${_pkgbase}/" \
	    -e "s/@PKGNAME@/${pkgname}/" \
	    -e "s/@PKGVER@/${pkgver}/" |
	tee dkms.conf >/dev/null
	echo "${_pkgbase}" > modules-load.conf
}

package() {
	install -Dm644 ${srcdir}/${pkgname}/.gitignore "${pkgdir}"/usr/src/${pkgname}/.gitignore
	install -Dm644 ${srcdir}/${pkgname}/LICENSE "${pkgdir}"/usr/src/${pkgname}/LICENSE
	install -Dm644 ${srcdir}/${pkgname}/Makefile "${pkgdir}"/usr/src/${pkgname}/Makefile
	install -Dm644 ${srcdir}/${pkgname}/PKGBUILD "${pkgdir}"/usr/src/${pkgname}/PKGBUILD
	install -Dm644 ${srcdir}/${pkgname}/README.md "${pkgdir}"/usr/src/${pkgname}/README.md
	install -Dm644 ${srcdir}/${pkgname}/dkms.conf "${pkgdir}"/usr/src/${pkgname}/dkms.conf
	install -Dm644 ${srcdir}/${pkgname}/dkms_conf.template "${pkgdir}"/usr/src/${pkgname}/dkms_conf.template
	install -Dm644 ${srcdir}/${pkgname}/s5divert.c "${pkgdir}"/usr/src/${pkgname}/s5divert.c
	install -Dm644 ${srcdir}/${pkgname}/s5divert.install "${pkgdir}"/usr/src/${pkgname}/s5divert.install
	install -Dm644 ${srcdir}/${pkgname}/modules-load.conf "${pkgdir}"/etc/modules-load.d/${_pkgbase}.conf
}
