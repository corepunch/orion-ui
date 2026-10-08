DEFAULT_GOAL := deploy
APP ?=
APP_SOURCE ?= $(APP)
BUILD_DIR ?= build
UNAME_S ?= $(shell uname -s)
BUNDLE_ID ?= com.orion.$(APP)

ifeq ($(UNAME_S),Darwin)
else
$(error mac-deploy requires macOS (UNAME_S=$(UNAME_S)))
endif
ifeq ($(strip $(APP)),)
$(error APP is required; use make mac-deploy APP=<appname>)
endif
ifeq ($(wildcard apps/$(APP_SOURCE)/main.c),)
$(error Unknown app $(APP_SOURCE): expected apps/$(APP_SOURCE)/main.c)
endif
ifeq ($(wildcard apps/$(APP_SOURCE)/share/icon.png),)
$(error Missing app icon: apps/$(APP_SOURCE)/share/icon.png)
endif

APP_BINARY := $(BUILD_DIR)/bin/$(APP)
SHARE_DIR := $(BUILD_DIR)/share
BUNDLE := $(abspath $(BUILD_DIR)/macos/$(APP).app)

.PHONY: deploy
deploy:
	$(MAKE) --no-print-directory -f Makefile $(APP_BINARY) share
	python3 packaging/macos/bundle.py \
		--app "$(APP)" \
		--source "$(APP_SOURCE)" \
		--bundle "$(BUNDLE)" \
		--binary "$(APP_BINARY)" \
		--runtime-libs "$(BUILD_DIR)/lib" \
		--share "$(SHARE_DIR)" \
		--icon "apps/$(APP_SOURCE)/share/icon.png" \
		--bundle-id "$(BUNDLE_ID)"
