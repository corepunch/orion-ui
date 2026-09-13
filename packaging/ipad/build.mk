.DEFAULT_GOAL := app
.DELETE_ON_ERROR:
BUILD_DIR ?= build/ipad
APP ?= imageeditor
SDK ?= iphoneos
ARCH ?= $(if $(filter iphoneos,$(SDK)),arm64,$(shell uname -m))
IOS_MIN ?= 16.0
BUNDLE_ID ?= com.orion.$(APP)
TEAM ?=
PROFILE ?=
DEVICE ?=
ifeq ($(filter $(APP),imageeditor penciltest),)
$(error APP must be imageeditor or penciltest)
endif
ifeq ($(filter $(SDK),iphoneos iphonesimulator),)
$(error SDK must be iphoneos or iphonesimulator)
endif
SDK_PATH := $(shell xcrun --sdk $(SDK) --show-sdk-path)
SDK_VERSION := $(shell xcrun --sdk $(SDK) --show-sdk-version)
BUILD_ROOT := $(abspath $(BUILD_DIR))/$(SDK)-$(ARCH)
APP_ROOT := $(BUILD_ROOT)/$(APP)
BUNDLE := $(APP_ROOT)/$(APP).app
COMPILER := xcrun --sdk $(SDK) clang
MIN_FLAG := $(if $(filter iphoneos,$(SDK)),-miphoneos-version-min,-mios-simulator-version-min)=$(IOS_MIN)
FLAGS := -isysroot "$(SDK_PATH)" -arch $(ARCH) $(MIN_FLAG) -std=c11 -O2 -g -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function -Wno-deprecated-declarations -MMD -MP -I. -I"$(SDK_PATH)/usr/include/libxml2" -DORION_ALLOW_HIGHDPI=1
APP_FLAGS := -DSTBTT_STATIC $(if $(filter penciltest,$(APP)),-DIMAGEEDITOR_BW=1 -DIMAGEEDITOR_BW_RETINA) -Iapps/imageeditor -Iapps/imageeditor/components -DSHAREDIR='"../share/imageeditor"'
USER_SRCS := $(filter-out orion/user/dialog.c orion/user/component_registry.c,$(wildcard orion/user/*.c))
KERNEL_SRCS := $(wildcard orion/kernel/*.c)
COMMCTL_SRCS := $(filter-out orion/commctl/tray.c,$(wildcard orion/commctl/*.c))
COMMDLG_SRCS := $(wildcard orion/commdlg/*.c)
COMPONENT_SRCS := $(wildcard apps/imageeditor/components/*.c)
OBJECTS := $(addprefix $(BUILD_ROOT)/,user.o kernel.o commctl.o) $(addprefix $(BUILD_ROOT)/,$(COMMDLG_SRCS:.c=.o) $(COMPONENT_SRCS:.c=.o))
APP_SRCS := $(shell find apps/imageeditor -name '*.c' ! -path '*/components/*' ! -path '*/tests/*' ! -name main.c | sort) apps/imageeditor/main.c
PLATFORM_LIB := $(BUILD_ROOT)/platform/libplatform.a
HOST_TOOL := $(abspath $(BUILD_DIR))/host/orionc
GENERATED := build/generated/apps/imageeditor/imageeditor.h
LIBS := -lxml2 -lm -framework UIKit -framework CoreGraphics -framework Foundation -framework OpenGLES -framework QuartzCore -framework Security -framework UniformTypeIdentifiers

.PHONY: app run deploy mac settings platform
settings:
	@mkdir -p "$(APP_ROOT)"
	@printf '%s\n' '$(COMPILER) $(FLAGS) $(SDK_VERSION)' > "$(BUILD_ROOT)/settings.tmp"
	@cmp -s "$(BUILD_ROOT)/settings.tmp" "$(BUILD_ROOT)/settings" || cp "$(BUILD_ROOT)/settings.tmp" "$(BUILD_ROOT)/settings"
	@printf '%s\n' '$(COMPILER) $(FLAGS) $(APP_FLAGS) $(SDK_VERSION)' > "$(APP_ROOT)/settings.tmp"
	@cmp -s "$(APP_ROOT)/settings.tmp" "$(APP_ROOT)/settings" || cp "$(APP_ROOT)/settings.tmp" "$(APP_ROOT)/settings"
$(BUILD_ROOT)/settings $(APP_ROOT)/settings: settings
platform:
	$(MAKE) -C platform SDK=$(SDK) ARCH=$(ARCH) IOS_MIN=$(IOS_MIN) OUTDIR="$(BUILD_ROOT)/platform"
$(PLATFORM_LIB): platform
$(HOST_TOOL): tools/orionc.c $(wildcard orion/user/*.h)
	@mkdir -p "$(@D)"
	xcrun --sdk macosx clang -std=c11 -O2 -I. -I"$(shell xcrun --sdk macosx --show-sdk-path)/usr/include/libxml2" $< -lxml2 -o "$@"
$(GENERATED): apps/imageeditor/imageeditor.orion $(HOST_TOOL)
	@mkdir -p "$(@D)"
	"$(HOST_TOOL)" --input $< --output $@ --prefix imageeditor
define unity_core
$(BUILD_ROOT)/$(1).o: $(2) $(BUILD_ROOT)/settings $(GENERATED) packaging/ipad/build.mk
	@printf '%s\n' $(2) | sed 's/.*/\#include "&"/' > "$(BUILD_ROOT)/$(1).c"
	$(COMPILER) $(FLAGS) -Icomponents -c "$(BUILD_ROOT)/$(1).c" -o "$$@"
endef
$(eval $(call unity_core,user,$(USER_SRCS)))
$(eval $(call unity_core,kernel,$(KERNEL_SRCS)))
$(eval $(call unity_core,commctl,$(COMMCTL_SRCS)))
$(BUILD_ROOT)/%.o: %.c $(BUILD_ROOT)/settings $(GENERATED) packaging/ipad/build.mk
	@mkdir -p "$(@D)"
	$(COMPILER) $(FLAGS) -Iapps/imageeditor -Iapps/imageeditor/components -c "$<" -o "$@"
$(APP_ROOT)/app.o: $(APP_SRCS) $(GENERATED) $(APP_ROOT)/settings packaging/ipad/build.mk
	@printf '%s\n' $(APP_SRCS) | sed 's/.*/\#include "&"/' > "$(APP_ROOT)/app.c"
	$(COMPILER) $(FLAGS) $(APP_FLAGS) -c "$(APP_ROOT)/app.c" -o "$@"
$(APP_ROOT)/$(APP): $(OBJECTS) $(APP_ROOT)/app.o $(PLATFORM_LIB)
	$(COMPILER) -isysroot "$(SDK_PATH)" -arch $(ARCH) $(MIN_FLAG) $(OBJECTS) "$(APP_ROOT)/app.o" $(PLATFORM_LIB) $(LIBS) -o "$@"
app: $(APP_ROOT)/$(APP)
	python3 tools/ipad/bundle.py --root "$(CURDIR)" --target "$(BUNDLE)" --binary "$<" --app $(APP) --bundle-id "$(BUNDLE_ID)" --sdk $(SDK) --sdk-version $(SDK_VERSION) --minimum $(IOS_MIN)
ifeq ($(SDK),iphonesimulator)
	codesign --force --sign - "$(BUNDLE)"
endif
run: app
	@test "$(SDK)" = iphonesimulator || { echo 'Use SDK=iphonesimulator'; exit 1; }
	python3 tools/ipad/run_simulator.py "$(BUNDLE)" $(if $(DEVICE),--device "$(DEVICE)")
deploy: app
	@test "$(SDK)" = iphoneos -a "$(ARCH)" = arm64 || { echo 'Deploy requires SDK=iphoneos ARCH=arm64'; exit 1; }
	@test -n "$(DEVICE)" || { echo 'Set DEVICE="iPad name or UDID"; use make list-devices'; exit 1; }
	python3 tools/ipad/sign.py "$(BUNDLE)" $(if $(TEAM),--team "$(TEAM)") $(if $(PROFILE),--profile "$(PROFILE)")
	xcrun devicectl device install app --device "$(DEVICE)" "$(BUNDLE)"
	xcrun devicectl device process launch --device "$(DEVICE)" --terminate-existing "$(BUNDLE_ID)"
mac: app
	@test "$(SDK)" = iphoneos -a "$(ARCH)" = arm64 || { echo 'Mac launch requires SDK=iphoneos ARCH=arm64'; exit 1; }
	python3 tools/ipad/sign.py "$(BUNDLE)" $(if $(TEAM),--team "$(TEAM)") $(if $(PROFILE),--profile "$(PROFILE)")
	python3 tools/ipad/wrap_mac.py "$(BUNDLE)" "$(abspath $(BUILD_DIR))/$(APP).app"
	open "$(abspath $(BUILD_DIR))/$(APP).app"
-include $(OBJECTS:.o=.d) $(APP_ROOT)/app.d
