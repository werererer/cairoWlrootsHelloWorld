WAYLAND_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER=$(shell pkg-config --variable=wayland_scanner wayland-scanner)

PKGS = wlroots wayland-server xkbcommon cairo
CFLAGS += $(foreach p,$(PKGS),$(shell pkg-config --cflags $(p)))
LDLIBS += $(foreach p,$(PKGS),$(shell pkg-config --libs $(p)))
start: test.c
	gcc -o test -g3 -fPIC -I/usr/include/libdrm -I/usr/include/pixman-1 -lwlroots -DWLR_USE_UNSTABLE $(CFLAGS) $(LDLIBS) -lcairo test.c
