# Thin convenience wrapper around the CMake build. Run `make help` for usage.

CMAKE ?= cmake
GENERATOR ?= Ninja
CONFIG ?= Release
ARCH ?= native
LTO ?= 0
THIRDPARTY_WARNINGS ?= 0
FX ?= 0
JOBS ?=
TARGETS ?=
ANDROID_ABI ?= arm64-v8a

enabled = $(filter 1,$(strip $(1)))
cmake_bool = $(if $(call enabled,$(1)),ON,OFF)

HOST_OS ?= $(if $(filter Windows_NT,$(OS)),Windows_NT,$(shell uname -s))
HOST_ARCH ?= $(if $(filter Windows_NT,$(OS)),$(PROCESSOR_ARCHITECTURE),$(shell uname -m))
WINDOWS_HOST := $(filter Windows_NT MINGW% MSYS% CYGWIN%,$(HOST_OS))

VCVARS := C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat

# Initialize MSVC so Windows builds work from regular shells or Git Bash/MSYS
# without requiring a Visual Studio developer console. Use Windows-native temp
# storage to keep MSVC tooling reliable across shell environments.
ifneq ($(WINDOWS_HOST),)
VCVARS_CMD := $(shell cygpath -m -s "$(VCVARS)")
WINDOWS_CURDIR := $(shell cygpath -m "$(CURDIR)")
MSVC_TEMP_DIR := $(WINDOWS_CURDIR)/temp/msvc
define run_with_vcvars
	cmd //D //S //C set TEMP=$(MSVC_TEMP_DIR)\&\& set TMP=$(MSVC_TEMP_DIR)\&\& call $(VCVARS_CMD) \&\& $(1)
endef
check-shell:
	@mkdir -p "$(MSVC_TEMP_DIR)"
else
define run_with_vcvars
	$(1)
endef
check-shell:
endif

# Intel and Apple Silicon Macs use the same system plug-in folders, including
# for universal builds. Standalone apps and other platforms default to user-local paths.
ifeq ($(HOST_OS),Darwin)
  INSTALL_PLATFORM := macOS ($(HOST_ARCH))
  VST3_INSTALL_DIR ?= /Library/Audio/Plug-Ins/VST3
  VST2_INSTALL_DIR ?= /Library/Audio/Plug-Ins/VST
  AU_INSTALL_DIR ?= /Library/Audio/Plug-Ins/Components
  APP_INSTALL_DIR ?= $(HOME)/Applications
  VST2_EXTENSION := .vst
  APP_EXTENSION := .app
else ifneq ($(WINDOWS_HOST),)
  # This wrapper requires GNU Make with a POSIX shell, e.g. MSYS2/Git Bash.
  SHELL := sh
  INSTALL_PLATFORM := Windows ($(HOST_ARCH))
  WINDOWS_USER_DIR := $(subst \,/,$(or $(USERPROFILE),$(HOME)))
  WINDOWS_LOCAL_DATA := $(subst \,/,$(or $(LOCALAPPDATA),$(WINDOWS_USER_DIR)/AppData/Local))
  VST3_INSTALL_DIR ?= $(WINDOWS_LOCAL_DATA)/Programs/Common/VST3
  VST2_INSTALL_DIR ?= $(WINDOWS_LOCAL_DATA)/Programs/Common/VST2
  AU_INSTALL_DIR ?=
  APP_INSTALL_DIR ?= $(WINDOWS_LOCAL_DATA)/Programs/OsTIrus
  VST2_EXTENSION := .dll
  APP_EXTENSION := .exe
else ifeq ($(HOST_OS),Linux)
  INSTALL_PLATFORM := Linux ($(HOST_ARCH))
  VST3_INSTALL_DIR ?= $(HOME)/.vst3
  VST2_INSTALL_DIR ?= $(HOME)/.vst
  AU_INSTALL_DIR ?=
  APP_INSTALL_DIR ?= $(HOME)/.local/bin
  VST2_EXTENSION := .so
  APP_EXTENSION :=
else
  INSTALL_PLATFORM := unsupported ($(HOST_OS), $(HOST_ARCH))
  VST3_INSTALL_DIR ?=
  VST2_INSTALL_DIR ?=
  AU_INSTALL_DIR ?=
  APP_INSTALL_DIR ?=
endif

# Preserve spaces, apostrophes, and shell metacharacters in paths.
shell_quote = '$(subst ','"'"',$(1))'

ifeq ($(ARCH),universal)
  CMAKE_ARCHS := arm64;x86_64
else
  CMAKE_ARCHS := $(HOST_ARCH)
endif

LTO_CMAKE := $(call cmake_bool,$(LTO))
THIRDPARTY_WARNINGS_CMAKE := $(call cmake_bool,$(THIRDPARTY_WARNINGS))
CMAKE_FX := $(call cmake_bool,$(FX))

# Product and format selectors are independent.
PRODUCT_VARS := OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086
FORMAT_VARS := VST2 VST3 AU CLAP LV2 STANDALONE
SELECTED_PRODUCTS := \
	$(if $(call enabled,$(OSIRUS)),osirusJucePlugin) \
	$(if $(call enabled,$(OSTIRUS)),osTIrusJucePlugin) \
	$(if $(call enabled,$(VAVRA)),mqJucePlugin) \
	$(if $(call enabled,$(XENIA)),xtJucePlugin) \
	$(if $(call enabled,$(NODALRED2X)),n2xJucePlugin) \
	$(if $(call enabled,$(JE8086)),jeJucePlugin)

SELECTED_FX_PRODUCTS := $(if $(call enabled,$(FX)),\
	$(if $(call enabled,$(OSIRUS)),osirusJucePlugin_FX) \
	$(if $(call enabled,$(OSTIRUS)),osTIrusJucePlugin_FX) \
	$(if $(call enabled,$(VAVRA)),mqJucePlugin_FX) \
	$(if $(call enabled,$(XENIA)),xtJucePlugin_FX))

SELECTED_SUFFIXES := \
	$(if $(call enabled,$(VST2)),_VST) \
	$(if $(call enabled,$(VST3)),_VST3) \
	$(if $(call enabled,$(AU)),_AU) \
	$(if $(call enabled,$(CLAP)),_CLAP) \
	$(if $(call enabled,$(LV2)),_LV2) \
	$(if $(call enabled,$(STANDALONE)),_Standalone)

PRODUCT_TARGETS := $(foreach product,$(SELECTED_PRODUCTS) $(SELECTED_FX_PRODUCTS),$(foreach suffix,$(SELECTED_SUFFIXES),$(product)$(suffix)))
BUILD_TARGETS := $(strip $(PRODUCT_TARGETS) $(TARGETS))

CMAKE_OSIRUS := $(call cmake_bool,$(call enabled,$(OSIRUS)))
CMAKE_OSTIRUS := $(call cmake_bool,$(call enabled,$(OSTIRUS)))
CMAKE_VAVRA := $(call cmake_bool,$(call enabled,$(VAVRA)))
CMAKE_XENIA := $(call cmake_bool,$(call enabled,$(XENIA)))
CMAKE_NODALRED2X := $(call cmake_bool,$(call enabled,$(NODALRED2X)))
CMAKE_JE8086 := $(call cmake_bool,$(call enabled,$(JE8086)))
CMAKE_VST2 := $(call cmake_bool,$(call enabled,$(VST2)))
CMAKE_VST3 := $(call cmake_bool,$(call enabled,$(VST3)))
CMAKE_AU := $(call cmake_bool,$(call enabled,$(AU)))
CMAKE_CLAP := $(call cmake_bool,$(call enabled,$(CLAP)))
CMAKE_LV2 := $(call cmake_bool,$(call enabled,$(LV2)))
CMAKE_STANDALONE := $(call cmake_bool,$(call enabled,$(STANDALONE)))

# Cache each configured combination, so switching back does not reconfigure.
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
OPTION_PROFILE := $(subst $(SPACE),+,$(strip \
	$(if $(filter ON,$(CMAKE_OSIRUS)),osirus) \
	$(if $(filter ON,$(CMAKE_OSTIRUS)),ostirus) \
	$(if $(filter ON,$(CMAKE_VAVRA)),vavra) \
	$(if $(filter ON,$(CMAKE_XENIA)),xenia) \
	$(if $(filter ON,$(CMAKE_NODALRED2X)),n2x) \
	$(if $(filter ON,$(CMAKE_JE8086)),je8086) \
	$(if $(filter ON,$(CMAKE_VST2)),vst2) \
	$(if $(filter ON,$(CMAKE_VST3)),vst3) \
	$(if $(filter ON,$(CMAKE_AU)),au) \
	$(if $(filter ON,$(CMAKE_CLAP)),clap) \
	$(if $(filter ON,$(CMAKE_LV2)),lv2) \
	$(if $(filter ON,$(CMAKE_STANDALONE)),standalone)))
GENERATOR_PROFILE := $(subst $(SPACE),-,$(GENERATOR))
PROFILE := $(GENERATOR_PROFILE)/$(CONFIG)/$(subst ;,+,$(CMAKE_ARCHS))-lto$(LTO_CMAKE)-tw$(THIRDPARTY_WARNINGS_CMAKE)$(if $(filter ON,$(CMAKE_FX)),-fxON)/$(OPTION_PROFILE)
BUILD_ROOT ?= $(CURDIR)/build
BUILD_DIR ?= $(BUILD_ROOT)/$(PROFILE)
CONFIG_STAMP := $(BUILD_DIR)/.make-configured

CMAKE_CONFIGURE_ARGS := \
	-S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
	-DCMAKE_BUILD_TYPE="$(CONFIG)" \
	-DCMAKE_OSX_ARCHITECTURES="$(CMAKE_ARCHS)" \
	-Dgearmulator_ENABLE_LTO=$(LTO_CMAKE) \
	-Dgearmulator_ENABLE_THIRDPARTY_WARNINGS=$(THIRDPARTY_WARNINGS_CMAKE) \
	-Dgearmulator_BUILD_FX_PLUGIN=$(CMAKE_FX) \
	-Dgearmulator_BUILD_JUCEPLUGIN=ON \
	-Dgearmulator_BUILD_JUCEPLUGIN_VST2=$(CMAKE_VST2) \
	-Dgearmulator_BUILD_JUCEPLUGIN_VST3=$(CMAKE_VST3) \
	-Dgearmulator_BUILD_JUCEPLUGIN_Standalone=$(CMAKE_STANDALONE) \
	-Dgearmulator_BUILD_JUCEPLUGIN_AU=$(CMAKE_AU) \
	-Dgearmulator_BUILD_JUCEPLUGIN_CLAP=$(CMAKE_CLAP) \
	-Dgearmulator_BUILD_JUCEPLUGIN_LV2=$(CMAKE_LV2) \
	-Dgearmulator_SYNTH_OSIRUS=$(CMAKE_OSIRUS) \
	-Dgearmulator_SYNTH_OSTIRUS=$(CMAKE_OSTIRUS) \
	-Dgearmulator_SYNTH_VAVRA=$(CMAKE_VAVRA) \
	-Dgearmulator_SYNTH_XENIA=$(CMAKE_XENIA) \
	-Dgearmulator_SYNTH_NODALRED2X=$(CMAKE_NODALRED2X) \
	-Dgearmulator_SYNTH_JE8086=$(CMAKE_JE8086)

BUILD_PARALLEL := $(if $(strip $(JOBS)),--parallel $(JOBS),--parallel)

.DEFAULT_GOAL := build
.PHONY: help clean clean-profile configure reconfigure build \
	install install-vst2 install-au install-standalone install-deps \
	android android-abi package


define BANNER_TOP
╔═══════════════════════════════════════════════════════════════════╗
║                                                                   ║
║      <==[--==[--==[   G E A R M U L A T O R   ]==--]==--]==>      ║
║                                                                   ║
║                     ▄████  ██▀▀ ▄▀█ █▀█ █▄ ▄█                     ║
║                    ██  ▄▄ ██▄▄ █▀█ █▀▄ █ ▀ █                      ║
║                     ▀███▀ ▀▀▀▀ ▀ ▀ ▀ ▀ ▀   ▀                      ║
║                                                                   ║
║                       G E A R M U L A T O R                       ║
║                                                                   ║
║                           Developed by:                           ║
║                      .: THE USUAL SUSPECTS :.                     ║
║                                                                   ║
║            [ OSIRUS ] [ OSTIRUS ] [ VARVA ] [ XENIA ]             ║
║                    [ JE-8086 ] [ NODALRED2X ]                     ║
║                                                                   ║
║      <==[-----------------------------------------------]==>      ║
endef

define BANNER_BOTTOM
║                                                                   ║
╚═══════════════════════════════════════════════════════════════════╝
endef

define BANNER
$(BANNER_TOP)
$(BANNER_BOTTOM)
endef

define HELP_BANNER
$(BANNER_TOP)
║                                                                   ║
║      Proceed, operator. The synthesizers are hungry.              ║
$(BANNER_BOTTOM)
endef

define HELPTEXT
Usage: make [build] [selectors...] [build dimensions...]

Install paths: $(INSTALL_PLATFORM)
  VST3        $(or $(VST3_INSTALL_DIR),not configured)
  VST2        $(or $(VST2_INSTALL_DIR),not configured)
  AU          $(if $(filter Darwin,$(HOST_OS)),$(AU_INSTALL_DIR),not supported on this platform)
  Standalone  $(or $(APP_INSTALL_DIR),not configured)
Override with:
  VST3_INSTALL_DIR=/path
  VST2_INSTALL_DIR=/path
  AU_INSTALL_DIR=/path
  APP_INSTALL_DIR=/path
  Quote paths containing spaces. Environment overrides are also accepted.
$(if $(WINDOWS_HOST),  VST2 is a guessed user-local location; add it to your DAW scan paths if needed.)

Set selectors to 1 to enable them (unset or 0 disables them):
  OSIRUS  OSTIRUS  VAVRA  XENIA  NODALRED2X  JE8086
Formats: VST2  VST3  AU  CLAP  LV2  STANDALONE
FX variants: FX

Build dimensions:
  CONFIG=Release       CMake configuration (Debug, RelWithDebInfo, ...)
  ARCH=native          universal, native, arm64, x86_64, or another CMake arch
  LTO=0                passed through as gearmulator_ENABLE_LTO
  THIRDPARTY_WARNINGS=0 show suppressed dependency warnings when set to 1
  FX=0                 additionally build available FX plugin variants when set to 1
  BUILD_ROOT=path      root containing all Make-managed build profiles
  BUILD_DIR=path       override the current profile build directory
  GENERATOR=Ninja      CMake generator (Xcode remains available explicitly)
  JOBS=N               parallel job limit
  TARGETS="name ..."   explicit additional CMake target(s)

Targets: build (default), package, configure, reconfigure, clean, clean-profile
  clean removes every profile under BUILD_ROOT; clean-profile removes only the selected profile.
  package builds the selected targets, then packages the selected build profile.
Dependency setup: install-deps (Linux only)
Android batch builds (Windows only): android, android-abi (ANDROID_ABI=arm64-v8a)
Install existing OsTIrus builds: install (VST3), install-vst2, install-au, install-standalone
  Installs do not build first. macOS destinations under /Library use sudo.
  Standalone and Windows/Linux defaults are user-local.

No product or format is selected by default.
Build defaults: CONFIG=Release ARCH=native LTO=0 JOBS=12

Examples:
  make OSTIRUS=1 VST2=1 STANDALONE=1
  make OSTIRUS=1 VST3=1 package
  make android-abi ANDROID_ABI=x86_64
  make OSIRUS=1 VST3=1 AU=1 ARCH=universal CONFIG=Release LTO=1
  make VAVRA=1 CLAP=1
  make OSTIRUS=1 GENERATOR=Xcode
endef

define newline


endef
BANNER_TEXT := $(if $(filter help,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),build)),$(HELP_BANNER),$(BANNER))
BANNER_PRINTF := $(subst $(newline),\r\n,$(BANNER_TEXT))

ifeq ($(MAKE_INNER),1)
help:
	@sleep 0.5
	@printf '%s\r\n' \
		'' \
		'Usage: make [build] [selectors...] [build dimensions...]' \
		'' \
		$(call shell_quote,Install paths: $(INSTALL_PLATFORM)) \
		$(call shell_quote,  VST3        $(or $(VST3_INSTALL_DIR),not configured)) \
		$(call shell_quote,  VST2        $(or $(VST2_INSTALL_DIR),not configured)) \
		$(call shell_quote,  AU          $(if $(filter Darwin,$(HOST_OS)),$(AU_INSTALL_DIR),not supported on this platform)) \
		$(call shell_quote,  Standalone  $(or $(APP_INSTALL_DIR),not configured)) \
		'Override with:' \
		'  VST3_INSTALL_DIR=/path' \
		'  VST2_INSTALL_DIR=/path' \
		'  AU_INSTALL_DIR=/path' \
		'  APP_INSTALL_DIR=/path' \
		'  Quote paths containing spaces. Environment overrides are also accepted.'
	@sleep 0.3
ifneq ($(WINDOWS_HOST),)
	@printf '%s\r\n' '  VST2 is a guessed user-local location; add it to your DAW scan paths if needed.'
endif
	@printf '%s\r\n' \
		'' \
		'Set selectors to 1 to enable them (unset or 0 disables them):' \
		'  OSIRUS  OSTIRUS  VAVRA  XENIA  NODALRED2X  JE8086' \
		'Formats: VST2  VST3  AU  CLAP  LV2  STANDALONE' \
		'FX variants: FX' \
		'' \
		'Build dimensions:' \
		'  CONFIG=Release       CMake configuration (Debug, RelWithDebInfo, ...)' \
		'  ARCH=native          universal, native, arm64, x86_64, or another CMake arch' \
		'  LTO=0                passed through as gearmulator_ENABLE_LTO' \
		'  THIRDPARTY_WARNINGS=0 show suppressed dependency warnings when set to 1' \
		'  FX=0                 additionally build available FX plugin variants when set to 1' \
		'  BUILD_ROOT=path      root containing all Make-managed build profiles' \
		'  BUILD_DIR=path       override the current profile build directory' \
		'  GENERATOR=Ninja      CMake generator (Xcode remains available explicitly)' \
		'  JOBS=N               parallel job limit' \
		'  TARGETS="name ..."   explicit additional CMake target(s)' \
		'' \
		'Targets: build (default), package, configure, reconfigure, clean, clean-profile' \
		'  clean removes every profile under BUILD_ROOT; clean-profile removes only the selected profile.' \
		'  package builds the selected targets, then packages the selected build profile.' \
		'Dependency setup: install-deps (Linux only)' \
		'Android batch builds (Windows only): android, android-abi (ANDROID_ABI=arm64-v8a)' \
		'Install existing OsTIrus builds: install (VST3), install-vst2, install-au, install-standalone' \
		'  Installs do not build first. macOS destinations under /Library use sudo.' \
		'  Standalone and Windows/Linux defaults are user-local.' \
		'' \
		'No product or format is selected by default.' \
		'Build defaults: CONFIG=Release ARCH=native LTO=0 JOBS=12' \
		'' \
		'Examples:' \
		'  make OSTIRUS=1 VST2=1 STANDALONE=1' \
		'  make OSTIRUS=1 VST3=1 package' \
		'  make android-abi ANDROID_ABI=x86_64' \
		'  make OSIRUS=1 VST3=1 AU=1 ARCH=universal CONFIG=Release LTO=1' \
		'  make VAVRA=1 CLAP=1' \

$(CONFIG_STAMP): Makefile CMakeLists.txt source/CMakeLists.txt source/cmake/base.cmake source/cmake/juce.cmake
	$(call run_with_vcvars,$(CMAKE) $(CMAKE_CONFIGURE_ARGS))
	$(call run_with_vcvars,$(CMAKE) -E touch "$@")

configure: $(CONFIG_STAMP)

reconfigure:
	$(call run_with_vcvars,$(CMAKE) $(CMAKE_CONFIGURE_ARGS))
	$(call run_with_vcvars,$(CMAKE) -E touch "$(CONFIG_STAMP)")

build: $(CONFIG_STAMP)
	@if test -z "$(BUILD_TARGETS)"; then \
		echo 'No build targets selected. Select a product/format or TARGETS=name.' >&2; \
		exit 2; \
	else \
		$(call run_with_vcvars,$(CMAKE) --build "$(BUILD_DIR)" --config "$(CONFIG)" $(BUILD_PARALLEL) --target $(BUILD_TARGETS)); \
	fi

package: build
	$(call run_with_vcvars,$(CMAKE) -E chdir "$(BUILD_DIR)" $(CMAKE) -P "$(CURDIR)/scripts/pack.cmake")

clean:
	$(CMAKE) -E remove_directory "$(BUILD_ROOT)"

clean-profile:
	$(CMAKE) -E remove_directory "$(BUILD_DIR)"

ifeq ($(HOST_OS),Linux)
install-deps:
	sh ./scripts/install_linux_dependencies.sh
else
install-deps:
	@echo 'install-deps is currently supported only on Linux.' >&2
	@exit 2
endif

ifneq ($(WINDOWS_HOST),)
android:
	cmd //D //S //C call scripts/build_android.bat

android-abi:
	cmd //D //S //C call scripts/build_android_abi.bat "$(ANDROID_ABI)"
else
android android-abi:
	@echo '$@ is currently supported only on Windows.' >&2
	@exit 2
endif

# $(1): artifact relative to bin/plugins/CONFIG; $(2): destination directory.
ifeq ($(HOST_OS),Darwin)
mac_install_privilege = $(if $(filter /Library /Library/%,$(1)),sudo)
define install_artifact
	@test -n $(call shell_quote,$(2)) || { echo 'No install directory configured.' >&2; exit 2; }
	$(call mac_install_privilege,$(2)) $(call run_with_vcvars,$(CMAKE) -E make_directory "$(2)")
	$(call mac_install_privilege,$(2)) /usr/bin/ditto $(call shell_quote,$(CURDIR)/bin/plugins/$(CONFIG)/$(1)) $(call shell_quote,$(2)/$(notdir $(1)))
	$(call mac_install_privilege,$(2)) /usr/bin/codesign --force --sign - $(call shell_quote,$(2)/$(notdir $(1)))
	$(call mac_install_privilege,$(2)) /usr/bin/xattr -dr com.apple.quarantine $(call shell_quote,$(2)/$(notdir $(1)))
endef
else
define install_artifact
	@test -n $(call shell_quote,$(2)) || { echo 'No install directory configured.' >&2; exit 2; }
	$(call run_with_vcvars,$(CMAKE) -E make_directory "$(2)")
	@if test -d $(call shell_quote,$(CURDIR)/bin/plugins/$(CONFIG)/$(1)); then \
		$(call run_with_vcvars,$(CMAKE) -E copy_directory "$(CURDIR)/bin/plugins/$(CONFIG)/$(1)" "$(2)/$(notdir $(1))"); \
	else \
		$(call run_with_vcvars,$(CMAKE) -E copy_if_different "$(CURDIR)/bin/plugins/$(CONFIG)/$(1)" "$(2)/$(notdir $(1))"); \
	fi
endef
endif

install:
	$(call install_artifact,VST3/OsTIrus.vst3,$(VST3_INSTALL_DIR))

install-vst2:
	$(call install_artifact,VST/OsTIrus$(VST2_EXTENSION),$(VST2_INSTALL_DIR))

install-standalone:
	$(call install_artifact,Standalone/OsTIrus$(APP_EXTENSION),$(APP_INSTALL_DIR))

ifeq ($(HOST_OS),Darwin)
install-au:
	$(call install_artifact,AU/OsTIrus.component,$(AU_INSTALL_DIR))
else
install-au:
	@echo 'Audio Units are only supported on macOS.' >&2
	@exit 2
endif
else
REQUESTED_GOALS := $(if $(strip $(MAKECMDGOALS)),$(MAKECMDGOALS),build)
RECURSIVE_DRY_RUN := $(if $(findstring n,$(firstword $(MAKEFLAGS))),-n)
FORWARDED_VARIABLES := \
	CMAKE GENERATOR CONFIG ARCH LTO THIRDPARTY_WARNINGS FX JOBS TARGETS ANDROID_ABI \
	OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086 \
	VST2 VST3 AU CLAP LV2 STANDALONE \
	BUILD_ROOT BUILD_DIR VST3_INSTALL_DIR VST2_INSTALL_DIR AU_INSTALL_DIR APP_INSTALL_DIR \
	HOST_OS HOST_ARCH
.PHONY: __run $(REQUESTED_GOALS)

$(REQUESTED_GOALS): __run

__run:
	@printf '%b\n' '$(BANNER_PRINTF)' | while IFS= read -r line; do \
		printf '%s\r\n' "$$line"; \
		sleep 0.01; \
	done
	+@make --no-print-directory $(RECURSIVE_DRY_RUN) MAKE_INNER=1 $(REQUESTED_GOALS) \
		$(foreach variable,$(FORWARDED_VARIABLES),$(variable)=$(call shell_quote,$($(variable))))
	$(if $(filter help,$(REQUESTED_GOALS)),,@printf '\n%s\n' 'Proceed, operator. The synthesizers are hungry.')
endif
