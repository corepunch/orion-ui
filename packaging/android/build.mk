# Android: one native library (libmain.so) per ABI, linked statically like the
# iPad apps, then packaged with Gradle into a debug-signed APK. From the root:
#   make android APP=imageeditor ABIS="arm64-v8a x86_64"
# See packaging/android/README.md.
.DEFAULT_GOAL := apk
.DELETE_ON_ERROR:
include tools/app_sources.mk
ANDROID_NDK ?= $(or $(ANDROID_NDK_HOME),$(ANDROID_NDK_LATEST_HOME))
APP ?= imageeditor
ABI ?= arm64-v8a
ABIS ?= $(ABI)
API ?= 28
BUILD_DIR ?= build/android
APPLICATION_ID ?= org.orion.$(APP)
VERSION_NAME ?= $(shell cat VERSION 2>/dev/null || echo 0.0)
VERSION_CODE ?= 1
HOST_CC ?= cc
GRADLE ?= gradle
ifeq ($(ANDROID_NDK),)
$(error Set ANDROID_NDK_HOME to an Android NDK)
endif
ifeq ($(filter $(APP),imageeditor penciltest groove winamp),)
$(error APP must be imageeditor, penciltest, groove or winamp)
endif
ifeq ($(filter $(ABI),arm64-v8a x86_64),)
$(error ABI must be arm64-v8a or x86_64)
endif
TRIPLE := $(if $(filter arm64-v8a,$(ABI)),aarch64,x86_64)-linux-android
HOST := $(firstword $(wildcard $(ANDROID_NDK)/toolchains/llvm/prebuilt/*))
COMPILER := $(HOST)/bin/clang --target=$(TRIPLE)$(API)
BUILD_ROOT := $(abspath $(BUILD_DIR))/$(ABI)
APP_ROOT := $(BUILD_ROOT)/$(APP)
DEPS := $(BUILD_ROOT)/deps
GLUE := $(ANDROID_NDK)/sources/android/native_app_glue
STAGE := $(abspath $(BUILD_DIR))/gradle/$(APP)
FLAGS := -std=c11 -D_DEFAULT_SOURCE -fPIC -O2 -g -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function -MMD -MP \
	-I. -I$(DEPS)/include/libxml2 -I$(GLUE) -Ivendor/freetype/include -Iorion/user/image_deps_config -Ivendor/libpng -Ivendor/libjpeg \
	-Ivendor/libnsbmp/include -Ivendor/zlib -Ivendor/bmplib -Iorion/user/bmplib_config -I$(BUILD_ROOT)/bmplib-generated -DORION_ALLOW_HIGHDPI=1
ifeq ($(THEME_$(APP)),)
else ifeq ($(filter classic modern light navy amber porcelain porcelain_dark metro silver,$(THEME_$(APP))),)
$(error unknown THEME_$(APP)=$(THEME_$(APP)))
endif
APP_THEME_FLAGS := $(if $(filter-out modern,$(THEME_$(APP))),-DORION_THEME=$(THEME_$(APP)))
GROOVE_USE_SHINE ?= 0
APP_SOURCE := $(if $(filter penciltest,$(APP)),imageeditor,$(APP))
APP_DIR := apps/$(APP_SOURCE)
APP_FLAGS := $(if $(filter penciltest,$(APP)),-DIMAGEEDITOR_BW=1 -DIMAGEEDITOR_BW_RETINA) $(APP_THEME_FLAGS) -I$(APP_DIR) -I$(APP_DIR)/components \
	-DSHAREDIR='"../share/$(APP_SOURCE)"' -DORION_ANDROID_APP='"$(APP)"'
APP_FLAGS += $(if $(filter 1,$(GROOVE_USE_SHINE)),-DGROOVE_USE_SHINE)
USER_SRCS := $(filter-out orion/user/dialog.c orion/user/component_registry.c,$(wildcard orion/user/*.c))
KERNEL_SRCS := $(wildcard orion/kernel/*.c)
COMMCTL_SRCS := $(filter-out orion/commctl/tray.c,$(wildcard orion/commctl/*.c))
COMMDLG_SRCS := $(wildcard orion/commdlg/*.c)
COMPONENT_SRCS := $(wildcard $(APP_DIR)/components/*.c)
OBJECTS := $(addprefix $(BUILD_ROOT)/,user.o kernel.o commctl.o) $(addprefix $(BUILD_ROOT)/,$(COMMDLG_SRCS:.c=.o))
APP_OBJECTS := $(addprefix $(APP_ROOT)/,$(COMPONENT_SRCS:.c=.o)) $(APP_ROOT)/app.o $(APP_ROOT)/android_main.o $(BUILD_ROOT)/glue.o
APP_SRCS := $(shell $(call app_unity_sources,$(APP_DIR))) $(APP_DIR)/main.c
APP_HEADERS := $(shell $(call find_app_sources,$(APP_DIR),-name '*.h' ! -path '*/tests/*'))
PLATFORM_LIB := $(BUILD_ROOT)/platform/libplatform.a
FREETYPE_SRCS := base/ftbase base/ftinit base/ftsystem base/ftglyph base/ftbitmap base/ftdebug base/ftmm gzip/ftgzip autofit/autofit truetype/truetype cff/cff psaux/psaux psnames/psnames pshinter/pshinter sfnt/sfnt smooth/smooth sdf/sdf raster/raster
PNG_SRCS := png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil
JPEG_SRCS := jaricom jcomapi jutils jerror jmemmgr jmemnobs jcapimin jcapistd jcarith jctrans jcparam jdatadst jcinit jcmaster jcmarker jcmainct jcprepct jccoefct jccolor jcsample jchuff jcdctmgr jfdctfst jfdctflt jfdctint jdapimin jdapistd jdarith jdtrans jdatasrc jdmaster jdinput jdmarker jdhuff jdmainct jdcoefct jdpostct jddctmgr jidctfst jidctflt jidctint jdsample jdcolor jquant1 jquant2 jdmerge
ZLIB_SRCS := adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil
BMPLIB_SRCS := bmp-read bmp-write bmp-read-loadimage bmp-read-loadindexed bmp-read-icons bmp-common huffman logging
DEP_OBJECTS := $(addprefix $(BUILD_ROOT)/deps/freetype/,$(addsuffix .o,$(FREETYPE_SRCS))) $(addprefix $(BUILD_ROOT)/deps/png/,$(addsuffix .o,$(PNG_SRCS))) \
	$(addprefix $(BUILD_ROOT)/deps/jpeg/,$(addsuffix .o,$(JPEG_SRCS))) $(addprefix $(BUILD_ROOT)/deps/zlib/,$(addsuffix .o,$(ZLIB_SRCS))) \
	$(addprefix $(BUILD_ROOT)/deps/bmplib/,$(addsuffix .o,$(BMPLIB_SRCS))) $(BUILD_ROOT)/deps/bmp/libnsbmp.o
LIBXML2 := $(DEPS)/lib/libxml2.a
HOST_XML_CFLAGS := $(shell pkg-config --cflags libxml-2.0 2>/dev/null || xml2-config --cflags)
HOST_XML_LIBS := $(shell pkg-config --libs libxml-2.0 2>/dev/null || xml2-config --libs)
HOST_TOOL := $(abspath $(BUILD_DIR))/host/orionc
GENERATED := build/generated/$(APP_DIR)/$(APP_SOURCE).h
LIBS := $(LIBXML2) -lEGL -lGLESv3 -landroid -laaudio -llog -lm -ldl
LIBRARY := $(STAGE)/jniLibs/$(ABI)/libmain.so

.PHONY: apk library assets settings platform clean
settings:
	@mkdir -p "$(APP_ROOT)"
	@printf '%s\n' '$(COMPILER) $(FLAGS)' > "$(BUILD_ROOT)/settings.tmp"
	@cmp -s "$(BUILD_ROOT)/settings.tmp" "$(BUILD_ROOT)/settings" || cp "$(BUILD_ROOT)/settings.tmp" "$(BUILD_ROOT)/settings"
	@printf '%s\n' '$(COMPILER) $(FLAGS) $(APP_FLAGS)' > "$(APP_ROOT)/settings.tmp"
	@cmp -s "$(APP_ROOT)/settings.tmp" "$(APP_ROOT)/settings" || cp "$(APP_ROOT)/settings.tmp" "$(APP_ROOT)/settings"
$(BUILD_ROOT)/settings $(APP_ROOT)/settings: settings
platform:
	$(MAKE) -C platform SDK=android ANDROID_NDK="$(ANDROID_NDK)" ARCH=$(firstword $(subst -, ,$(TRIPLE))) ANDROID_API=$(API) OUTDIR="$(BUILD_ROOT)/platform"
$(PLATFORM_LIB): platform
$(LIBXML2):
	python3 tools/android/libxml2.py --ndk "$(ANDROID_NDK)" --abi $(ABI) --api $(API) --build-dir "$(BUILD_DIR)"
$(BUILD_ROOT)/deps/freetype/%.o: vendor/freetype/src/%.c orion/user/freetype_modules.h $(BUILD_ROOT)/settings
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H='"orion/user/freetype_modules.h"' -c "$<" -o "$@"
$(BUILD_ROOT)/deps/png/%.o: vendor/libpng/%.c orion/user/image_deps_config/pnglibconf.h $(BUILD_ROOT)/settings
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) -DPNG_ARM_NEON_OPT=0 -c "$<" -o "$@"
$(BUILD_ROOT)/deps/jpeg/%.o: vendor/libjpeg/%.c orion/user/image_deps_config/jconfig.h $(BUILD_ROOT)/settings
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) -c "$<" -o "$@"
$(BUILD_ROOT)/deps/zlib/%.o: vendor/zlib/%.c $(BUILD_ROOT)/settings
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) -c "$<" -o "$@"
$(BUILD_ROOT)/deps/bmp/%.o: vendor/libnsbmp/src/%.c $(BUILD_ROOT)/settings
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) -DNDEBUG -c "$<" -o "$@"
$(BUILD_ROOT)/bmplib-generated/huffman-codes.h: vendor/bmplib/gen-huffman.c vendor/bmplib/gen-huffman-codes.h
	@mkdir -p "$(@D)"
	$(HOST_CC) -std=c11 vendor/bmplib/gen-huffman.c -o "$(@D)/gen-huffman"
	"$(@D)/gen-huffman" "$@"
$(BUILD_ROOT)/deps/bmplib/%.o: vendor/bmplib/%.c orion/user/bmplib_config/config.h $(BUILD_ROOT)/bmplib-generated/huffman-codes.h $(BUILD_ROOT)/settings
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) -DNDEBUG -c "$<" -o "$@"
$(HOST_TOOL): tools/orionc.c $(wildcard orion/user/*.h)
	@mkdir -p "$(@D)"
	$(HOST_CC) -std=c11 -D_DEFAULT_SOURCE -Werror=implicit-function-declaration -O2 -I. $(HOST_XML_CFLAGS) $< $(HOST_XML_LIBS) -o "$@"
$(GENERATED): $(APP_DIR)/$(APP_SOURCE).orion $(HOST_TOOL)
	@mkdir -p "$(@D)"
	"$(HOST_TOOL)" --input $< --output $@ --prefix $(APP_SOURCE)
# One translation unit per framework library, as for the iPad.
define unity_core
$(BUILD_ROOT)/$(1).o: $(2) $(BUILD_ROOT)/settings $(GENERATED) $(LIBXML2) packaging/android/build.mk
	@printf '%s\n' $(2) | sed 's/.*/\#include "&"/' > "$(BUILD_ROOT)/$(1).c"
	$(COMPILER) $(FLAGS) -Icomponents -c "$(BUILD_ROOT)/$(1).c" -o "$$@"
endef
$(eval $(call unity_core,user,$(USER_SRCS)))
$(eval $(call unity_core,kernel,$(KERNEL_SRCS)))
$(eval $(call unity_core,commctl,$(COMMCTL_SRCS)))
$(BUILD_ROOT)/orion/%.o: orion/%.c $(BUILD_ROOT)/settings $(GENERATED) $(LIBXML2) packaging/android/build.mk
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) -c "$<" -o "$@"
$(APP_ROOT)/%.o: %.c $(APP_ROOT)/settings $(GENERATED) $(LIBXML2) packaging/android/build.mk
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) $(APP_FLAGS) -c "$<" -o "$@"
$(APP_ROOT)/app.o: $(APP_SRCS) $(APP_HEADERS) $(GENERATED) $(LIBXML2) $(APP_ROOT)/settings packaging/android/build.mk tools/app_sources.mk
	@printf '%s\n' $(APP_SRCS) | sed 's/.*/\#include "&"/' > "$(APP_ROOT)/app.c"
	$(COMPILER) $(FLAGS) $(APP_FLAGS) -c "$(APP_ROOT)/app.c" -o "$@"
$(APP_ROOT)/android_main.o: packaging/android/android_main.c $(APP_ROOT)/settings
	$(COMPILER) $(FLAGS) $(APP_FLAGS) -c "$<" -o "$@"
$(BUILD_ROOT)/glue.o: $(GLUE)/android_native_app_glue.c $(BUILD_ROOT)/settings
	$(COMPILER) -fPIC -O2 -g -I$(GLUE) -c "$<" -o "$@"

# NativeActivity finds ANativeActivity_onCreate in the glue; keep it exported.
$(LIBRARY): $(OBJECTS) $(APP_OBJECTS) $(PLATFORM_LIB) $(DEP_OBJECTS) $(LIBXML2)
	@mkdir -p "$(@D)"
	$(COMPILER) -shared -Wl,-soname,libmain.so -Wl,--no-undefined -Wl,-z,max-page-size=16384 -u ANativeActivity_onCreate \
		$(OBJECTS) $(APP_OBJECTS) $(PLATFORM_LIB) $(DEP_OBJECTS) $(LIBS) -o "$@"
library: $(LIBRARY)

assets:
	python3 tools/android/package.py --app $(APP) --output "$(STAGE)"

# Builds each ABI's library, then the APK under build/android/apk/.
apk:
	@for abi in $(ABIS); do $(MAKE) -f packaging/android/build.mk APP=$(APP) ABI=$$abi library || exit 1; done
	@for abi in arm64-v8a x86_64; do case " $(ABIS) " in *" $$abi "*) ;; *) rm -rf "$(STAGE)/jniLibs/$$abi";; esac; done
	$(MAKE) -f packaging/android/build.mk APP=$(APP) assets
	$(GRADLE) -p packaging/android/project --no-daemon -PorionBuild="$(STAGE)" -PorionApplicationId="$(APPLICATION_ID)" \
		-PorionVersionName="$(VERSION_NAME)" -PorionVersionCode="$(VERSION_CODE)" assembleDebug
	@mkdir -p "$(abspath $(BUILD_DIR))/apk"
	cp packaging/android/project/app/build/outputs/apk/debug/app-debug.apk "$(abspath $(BUILD_DIR))/apk/orion-$(APP).apk"
	@echo "APK: $(BUILD_DIR)/apk/orion-$(APP).apk"

clean:
	rm -rf "$(BUILD_DIR)" packaging/android/project/app/build packaging/android/project/.gradle

-include $(OBJECTS:.o=.d) $(APP_OBJECTS:.o=.d) $(DEP_OBJECTS:.o=.d)
