# Module object(s)
modname := s5divert
obj-m   := $(modname).o

# Kernel version/build tree (override KVERSION if you want to target a different tree)
KVERSION ?= $(shell uname -r)
KDIR     ?= /lib/modules/$(KVERSION)/build
INSTALL_MOD_DIR ?= updates

PWD      := $(shell pwd)

# Optional: set INSTALL_MOD_PATH=/tmp/pkgroot for packaging, or use DESTDIR
# make install DESTDIR=/tmp/pkgroot
DESTDIR ?=
INSTALL_MOD_PATH ?= $(DESTDIR)

.PHONY: all modules clean install uninstall

all default: modules

update:
	-@env GIT_TERMINAL_PROMPT=0 sh -c '[ -d .git ] && git reset --hard -q && git -c http.lowSpeedLimit=1024 -c http.lowSpeedTime=15 pull -q || (curl --speed-limit 1024 --speed-time 15 -sL "https://github.com/rbm78bln/kmod_s5divert/archive/refs/heads/main.zip" | bsdtar --extract --strip-components=1 --file -)'

aur: kmod_s5divert-dkms.pkg.tar

kmod_s5divert-dkms.pkg.tar:
	makepkg
	mv -f kmod_s5divert-dkms-*.pkg.tar kmod_s5divert-dkms.pkg.tar
	rm -rf src pkg

dkms.conf: dkms_conf.template PKGBUILD
	@sh -c ' \
	  export $(shell grep -E "^_pkgbase=" PKGBUILD ); \
	  export $(shell grep -E "^pkgname=" PKGBUILD ); \
	  export pkgver=$$(grep MODULE_VERSION $${_pkgbase}.c | cut "-d\"" -f2); \
	  sed -e "s/@PKGBASE@/$${_pkgbase}/" -e "s/@PKGNAME@/$${pkgname}/" -e "s/@PKGVER@/$${pkgver}/" <dkms_conf.template >dkms.conf \
	'

modules-load.conf: dkms_conf.template PKGBUILD
	@sh -c ' \
	  export $(shell grep -E "^_pkgbase=" PKGBUILD ); \
	  echo "$${_pkgbase}" > modules-load.conf \
	'

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) LDFLAGS_MODULE=-Map=$(modname).map modules
	objdump -dS $(modname).ko > $(modname).asm
	objdump -t $(modname).ko > $(modname).symbols
	strip --strip-debug $(modname).ko
	modinfo ./$(modname).ko

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(modname).asm $(modname).map $(modname).symbols
	rm -rf src pkg kmod_s5divert-dkms*.pkg.tar*

install: modules
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install INSTALL_MOD_DIR=$(INSTALL_MOD_DIR) INSTALL_MOD_PATH=$(INSTALL_MOD_PATH)
ifeq ($(strip $(INSTALL_MOD_PATH)),)
	depmod -a $(KVERSION)
endif

uninstall:
	@set -e; \
	moddir="/lib/modules/$(KVERSION)/$(INSTALL_MOD_DIR)"; \
	for ext in ko ko.gz ko.xz ko.zst; do \
		f="$$moddir/$(modname).$$ext"; \
		if [ -e "$$f" ]; then echo "Removing $$f"; rm -f -- "$$f"; fi; \
	done; \
	# Remove empty directory if we created it and it is now empty
	[ -d "$$moddir" ] && rmdir --ignore-fail-on-non-empty "$$moddir" || true; \
	depmod -a $(KVERSION)

load:
	-@sudo rmmod $(modname) 2>/dev/null || true
	sudo insmod $(modname).ko

unload:
	sudo rmmod $(modname)
