CC = gcc
CFLAGS = -g
LDFLAGS =

# Platform detection
UNAME_S ?= $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -framework IOKit
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

ifeq ($(GFX),)
    CFLAGS += -DGFX_X11
    LDFLAGS += -lX11
endif

SOURCES = main.c graphics.c config.c plot.c ringbuf.c threading.c ini_parser.c datasource.c ds/snmp_client.c ds/ping.c ds/cpu.c ds/memory.c ds/snmp.c ds/if_thr.c ds/loadavg.c ds/shell.c ds/clock.c os/os.c os/defgw.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = sng

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o */*.o $(TARGET)
	rm -rf $(BUNDLE_DIR) $(APP_NAME).dmg sng-macos.dmg
	rm -rf macos/$(APP_NAME).iconset macos/icon_1024.png

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# macOS app bundle variables
APP_NAME = SNG
BUNDLE_DIR = $(APP_NAME).app
BUNDLE_CONTENTS = $(BUNDLE_DIR)/Contents
BUNDLE_MACOS = $(BUNDLE_CONTENTS)/MacOS
BUNDLE_RESOURCES = $(BUNDLE_CONTENTS)/Resources

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

# Create macOS .app bundle
app: $(TARGET)
	mkdir -p $(BUNDLE_MACOS)
	mkdir -p $(BUNDLE_RESOURCES)
	cp $(TARGET) $(BUNDLE_MACOS)/$(APP_NAME)
	cp macos/$(APP_NAME).icns $(BUNDLE_RESOURCES)/$(APP_NAME).icns
	echo "$$INFO_PLIST" > $(BUNDLE_CONTENTS)/Info.plist

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

# Create .dmg from .app bundle
dmg: app
	rm -f $(APP_NAME).dmg
	hdiutil create -srcfolder $(BUNDLE_DIR) -volname "$(APP_NAME)" -format UDZO sng-macos.dmg

.PHONY: all clean install app dmg icon
