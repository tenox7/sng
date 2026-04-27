-include .env

CC = gcc
CFLAGS = -g
LDFLAGS =

# Platform detection
UNAME_S ?= $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -framework IOKit
    ifeq ($(UNIVERSAL),1)
        CFLAGS += -arch x86_64 -arch arm64
        LDFLAGS += -arch x86_64 -arch arm64
    endif
endif
ifeq ($(UNAME_S),Linux)
    CFLAGS += -DLINUX
    LDFLAGS += -lpthread -lm
endif
ifeq ($(UNAME_S),FreeBSD)
    LDFLAGS += -lpthread -lm
endif
ifeq ($(UNAME_S),NetBSD)
    LDFLAGS += -lpthread -lm
endif
ifeq ($(UNAME_S),OpenBSD)
    LDFLAGS += -lpthread -lm
endif
ifeq ($(UNAME_S),SunOS)
    LDFLAGS += -lpthread -lm -lsocket -lnsl -lrt -lkstat -lresolv
endif
ifeq ($(UNAME_S),HP-UX)
    LDFLAGS += -lpthread -lm -lnm
endif
ifeq ($(UNAME_S),AIX)
    CFLAGS += -pthread
    LDFLAGS += -pthread -lm
endif
ifeq ($(UNAME_S),UnixWare)
    CFLAGS += -DUNIXWARE
    LDFLAGS += -lthread -lm -lsocket -lnsl -lelf -lmas
endif
ifeq ($(UNAME_S),OSF1)
    CFLAGS += -pthread
    LDFLAGS += -pthread -lm -lmach
endif
ifneq ($(findstring IRIX,$(UNAME_S)),)
    CFLAGS += -isystem /usr/include -D__STDINT_H__
    LDFLAGS += -lpthread -lelf
endif

# Graphics driver selection
ifeq ($(GFX),SDL3)
    CFLAGS += -DGFX_SDL3 $(shell pkg-config --cflags sdl3 sdl3-ttf fontconfig)
    LDFLAGS += $(shell pkg-config --libs sdl3 sdl3-ttf fontconfig)
endif
ifeq ($(GFX),SDL2)
    CFLAGS += -DGFX_SDL2 $(shell pkg-config --cflags fontconfig)
    LDFLAGS += -lSDL2 -lSDL2_ttf $(shell pkg-config --libs fontconfig)
endif
ifeq ($(GFX),X11)
    CFLAGS += -DGFX_X11
    LDFLAGS += -lX11
endif
ifeq ($(GFX),GTK3)
    CFLAGS += -DGFX_GTK3 $(shell pkg-config --cflags gtk+-3.0)
    LDFLAGS += $(shell pkg-config --libs gtk+-3.0)
endif
ifeq ($(GFX),GLFW)
    CFLAGS += -DGFX_GLFW $(shell pkg-config --cflags glfw3 freetype2)
    ifeq ($(UNAME_S),Darwin)
        LDFLAGS += $(shell pkg-config --libs glfw3 freetype2) -framework Cocoa -framework OpenGL -framework IOKit
    else
        LDFLAGS += $(shell pkg-config --libs glfw3 freetype2) -lGL
    endif
endif
ifeq ($(GFX),COCOA)
    CFLAGS += -DGFX_COCOA
    LDFLAGS += -framework Cocoa
    OBJC_SOURCES = gfx/cocoa.m
endif

ifeq ($(GFX),)
    CFLAGS += -DGFX_X11
    LDFLAGS += -lX11
endif

ifeq ($(NO_SHELL),1)
    CFLAGS += -DNO_SHELL
    SHELL_SRC =
else
    SHELL_SRC = ds/shell.c
endif

SOURCES = main.c graphics.c config.c plot.c ringbuf.c threading.c ini_parser.c datasource.c ds/snmp_client.c ds/ping.c ds/cpu.c ds/memory.c ds/snmp.c ds/if_thr.c ds/loadavg.c $(SHELL_SRC) ds/clock.c os/os.c os/defgw.c
OBJECTS = $(SOURCES:.c=.o)
OBJC_OBJECTS = $(OBJC_SOURCES:.m=.o)
TARGET = sng

all: $(TARGET)

$(TARGET): $(OBJECTS) $(OBJC_OBJECTS)
	$(CC) $(OBJECTS) $(OBJC_OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.m
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o */*.o $(TARGET)
	rm -rf $(BUNDLE_DIR) $(APP_NAME).dmg sng-macos.dmg dmg_staging
	rm -rf macos/$(APP_NAME).iconset macos/icon_1024.png

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# macOS app bundle variables
APP_NAME = SNG
BUNDLE_DIR = $(APP_NAME).app
BUNDLE_CONTENTS = $(BUNDLE_DIR)/Contents
BUNDLE_MACOS = $(BUNDLE_CONTENTS)/MacOS
BUNDLE_RESOURCES = $(BUNDLE_CONTENTS)/Resources
DMG_PATH = sng-macos.dmg
STAGING = dmg_staging

define INFO_PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>$(APP_NAME)</string>
	<key>CFBundleIdentifier</key>
	<string>com.example.sng</string>
	<key>CFBundleName</key>
	<string>$(APP_NAME)</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>CFBundleIconFile</key>
	<string>$(APP_NAME)</string>
</dict>
</plist>
endef
export INFO_PLIST

# Create macOS .app bundle (ad-hoc signed for local use)
app: $(TARGET)
	rm -rf $(BUNDLE_DIR)
	mkdir -p $(BUNDLE_MACOS)
	mkdir -p $(BUNDLE_RESOURCES)
	cp $(TARGET) $(BUNDLE_MACOS)/$(APP_NAME)
	cp macos/$(APP_NAME).icns $(BUNDLE_RESOURCES)/$(APP_NAME).icns
	echo "$$INFO_PLIST" > $(BUNDLE_CONTENTS)/Info.plist
	codesign --force --sign - $(BUNDLE_DIR)

# Regenerate the .icns icon from macos/make_icon.swift (requires swift + iconutil)
icon:
	cd macos && swift make_icon.swift icon_1024.png
	rm -rf macos/$(APP_NAME).iconset && mkdir macos/$(APP_NAME).iconset
	for s in 16 32 128 256 512; do \
		sips -z $$s $$s macos/icon_1024.png --out macos/$(APP_NAME).iconset/icon_$${s}x$${s}.png >/dev/null; \
		s2=$$((s*2)); \
		sips -z $$s2 $$s2 macos/icon_1024.png --out macos/$(APP_NAME).iconset/icon_$${s}x$${s}@2x.png >/dev/null; \
	done
	iconutil -c icns macos/$(APP_NAME).iconset -o macos/$(APP_NAME).icns
	rm -rf macos/$(APP_NAME).iconset macos/icon_1024.png

define build_dmg
	rm -rf $(STAGING) $(1)
	mkdir -p $(STAGING)
	cp -R $(BUNDLE_DIR) $(STAGING)/
	ln -s /Applications $(STAGING)/Applications
	hdiutil create -volname "$(APP_NAME)" -srcfolder $(STAGING) -ov -format UDZO $(1)
	rm -rf $(STAGING)
endef

# Create .dmg from .app bundle
dmg: app
	$(call build_dmg,$(DMG_PATH))

# Universal Cocoa-backed .app for macOS (x86_64 + arm64)
macos:
	$(MAKE) clean
	$(MAKE) GFX=COCOA UNIVERSAL=1 app

# Signed + notarized release (requires DEV_ID + NOTARY_PROFILE in .env)
release:
	@test -n "$(DEV_ID)" || { echo "DEV_ID not set — copy .env.example to .env and fill in"; exit 1; }
	@test -n "$(NOTARY_PROFILE)" || { echo "NOTARY_PROFILE not set — copy .env.example to .env and fill in"; exit 1; }
	$(MAKE) clean
	$(MAKE) GFX=COCOA UNIVERSAL=1 $(TARGET)
	rm -rf $(BUNDLE_DIR)
	mkdir -p $(BUNDLE_MACOS) $(BUNDLE_RESOURCES)
	cp $(TARGET) $(BUNDLE_MACOS)/$(APP_NAME)
	cp macos/$(APP_NAME).icns $(BUNDLE_RESOURCES)/$(APP_NAME).icns
	echo "$$INFO_PLIST" > $(BUNDLE_CONTENTS)/Info.plist
	codesign --force --options runtime --timestamp --sign "$(DEV_ID)" $(BUNDLE_DIR)
	$(call build_dmg,$(DMG_PATH))
	codesign --force --timestamp --sign "$(DEV_ID)" $(DMG_PATH)
	xcrun notarytool submit $(DMG_PATH) --keychain-profile "$(NOTARY_PROFILE)" --wait
	xcrun stapler staple $(DMG_PATH)
	@echo "Signed + notarized: $(DMG_PATH)"

DOCKER ?= docker
APPIMAGE_IMAGE = sng-appimage-builder
FLATPAK_IMAGE  = sng-flatpak-builder
SNAP_IMAGE     = sng-snap-builder

# AppImage default = amd64 (broadest reach; QEMU emulation works fine here).
appimage: appimage-amd64
appimage-all: appimage-amd64 appimage-arm64

appimage-%:
	$(DOCKER) build --platform=linux/$* -t $(APPIMAGE_IMAGE)-$* packaging/appimage
	$(DOCKER) run --rm --platform=linux/$* -v $(CURDIR):/work -w /work $(APPIMAGE_IMAGE)-$* packaging/appimage/build.sh

# Flatpak/Snap default targets build for the host arch — cross-arch via QEMU
# emulation breaks bwrap (flatpak) and is wasteful for snap. Use *-amd64 /
# *-arm64 explicitly when you know the host can support it (real Linux x86_64,
# Apple Silicon for arm64).
flatpak:
	$(DOCKER) build -t $(FLATPAK_IMAGE) packaging/flatpak
	$(DOCKER) run --rm --privileged \
		--security-opt seccomp=unconfined \
		--security-opt apparmor=unconfined \
		-v $(CURDIR):/work -w /work \
		-v sng-flatpak-cache:/root/.local/share/flatpak \
		$(FLATPAK_IMAGE) packaging/flatpak/build.sh

flatpak-all: flatpak-amd64 flatpak-arm64

flatpak-%:
	$(DOCKER) build --platform=linux/$* -t $(FLATPAK_IMAGE)-$* packaging/flatpak
	$(DOCKER) run --rm --privileged --platform=linux/$* \
		--security-opt seccomp=unconfined \
		--security-opt apparmor=unconfined \
		-v $(CURDIR):/work -w /work \
		-v sng-flatpak-cache-$*:/root/.local/share/flatpak \
		$(FLATPAK_IMAGE)-$* packaging/flatpak/build.sh

snap:
	$(DOCKER) build -t $(SNAP_IMAGE) packaging/snap
	$(DOCKER) run --rm \
		-v $(CURDIR):/work -w /work \
		-v sng-snap-cache:/root/.cache/snapcraft \
		$(SNAP_IMAGE) packaging/snap/build.sh

snap-all: snap-amd64 snap-arm64

snap-%:
	$(DOCKER) build --platform=linux/$* -t $(SNAP_IMAGE)-$* packaging/snap
	$(DOCKER) run --rm --platform=linux/$* \
		-v $(CURDIR):/work -w /work \
		-v sng-snap-cache-$*:/root/.cache/snapcraft \
		$(SNAP_IMAGE)-$* packaging/snap/build.sh

linux-packages: appimage flatpak snap
linux-packages-all: appimage-all flatpak-all snap-all

.PHONY: all clean install app dmg icon macos release \
	appimage appimage-all \
	flatpak flatpak-all \
	snap snap-all \
	linux-packages linux-packages-all
