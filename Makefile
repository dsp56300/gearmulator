# Thin convenience wrapper around the CMake build. Run `make help` for usage.

LAST_CONFIGURATION_FILE := $(CURDIR)/build/.last-configuration
CONFIGURATION_SELECTOR_VARS := OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086 VST2 VST3 AU CLAP LV2 STANDALONE
EXPLICIT_CONFIGURATION_SELECTORS := $(strip $(foreach variable,$(CONFIGURATION_SELECTOR_VARS),$(if $(filter command line,$(origin $(variable))),$(variable))))
EARLY_REQUESTED_GOALS := $(if $(strip $(MAKECMDGOALS)),$(MAKECMDGOALS),build)
LAST_CONFIGURATION_REQUESTED := $(if $(filter build install package clean,$(EARLY_REQUESTED_GOALS)),$(if $(EXPLICIT_CONFIGURATION_SELECTORS),,1))
LAST_CONFIGURATION_AVAILABLE := $(wildcard $(LAST_CONFIGURATION_FILE))

# Bare build, install, package, and clean targets reuse the last build selection.
# Explicit selectors continue to describe a fresh configuration in full.
ifneq ($(LAST_CONFIGURATION_REQUESTED),)
ifeq ($(EXPLICIT_CONFIGURATION_SELECTORS),)
-include $(LAST_CONFIGURATION_FILE)
endif
endif

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
  CLAP_INSTALL_DIR ?= $(HOME)/Library/Audio/Plug-Ins/CLAP
  LV2_INSTALL_DIR ?= $(HOME)/Library/Audio/Plug-Ins/LV2
  APP_INSTALL_DIR ?= $(HOME)/Applications
  VST2_EXTENSION := .vst
  APP_EXTENSION := .app
else ifneq ($(WINDOWS_HOST),)
  # This wrapper requires GNU Make with a POSIX shell, e.g. MSYS2/Git Bash.
  SHELL := sh
  INSTALL_PLATFORM := Windows ($(HOST_ARCH))
  LOCALAPPDATA := $(shell powershell -NoProfile -Command "[Environment]::GetFolderPath('LocalApplicationData')")
  VST3_INSTALL_DIR ?= $(LOCALAPPDATA)/Programs/Common/VST3
  VST2_INSTALL_DIR ?= $(LOCALAPPDATA)/Programs/Common/VST2
  AU_INSTALL_DIR ?=
  CLAP_INSTALL_DIR ?= $(LOCALAPPDATA)/Programs/Common/CLAP
  LV2_INSTALL_DIR ?= $(LOCALAPPDATA)/Programs/Common/LV2
  APP_INSTALL_DIR ?= $(LOCALAPPDATA)/Programs/OsTIrus
  VST2_EXTENSION := .dll
  APP_EXTENSION := .exe
else ifeq ($(HOST_OS),Linux)
  INSTALL_PLATFORM := Linux ($(HOST_ARCH))
  VST3_INSTALL_DIR ?= $(HOME)/.vst3
  VST2_INSTALL_DIR ?= $(HOME)/.vst
  AU_INSTALL_DIR ?=
  CLAP_INSTALL_DIR ?= $(HOME)/.clap
  LV2_INSTALL_DIR ?= $(HOME)/.lv2
  APP_INSTALL_DIR ?= $(HOME)/.local/bin
  VST2_EXTENSION := .so
  APP_EXTENSION :=
else
  INSTALL_PLATFORM := unsupported ($(HOST_OS), $(HOST_ARCH))
  VST3_INSTALL_DIR ?=
  VST2_INSTALL_DIR ?=
  AU_INSTALL_DIR ?=
  CLAP_INSTALL_DIR ?=
  LV2_INSTALL_DIR ?=
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

PACKAGE_PRODUCTS := \
	$(if $(call enabled,$(OSIRUS)),Osirus) \
	$(if $(call enabled,$(OSTIRUS)),OsTIrus) \
	$(if $(call enabled,$(VAVRA)),Vavra) \
	$(if $(call enabled,$(XENIA)),Xenia) \
	$(if $(call enabled,$(NODALRED2X)),NodalRed2x) \
	$(if $(call enabled,$(JE8086)),JE8086)
PACKAGE_FX_PRODUCTS := $(if $(call enabled,$(FX)),\
	$(if $(call enabled,$(OSIRUS)),OsirusFX) \
	$(if $(call enabled,$(OSTIRUS)),OsTIrusFX) \
	$(if $(call enabled,$(VAVRA)),VavraFX) \
	$(if $(call enabled,$(XENIA)),XeniaFX))
PACKAGE_FORMATS := \
	$(if $(call enabled,$(VST2)),VST2) \
	$(if $(call enabled,$(VST3)),VST3) \
	$(if $(call enabled,$(AU)),AU) \
	$(if $(call enabled,$(CLAP)),CLAP) \
	$(if $(call enabled,$(LV2)),LV2) \
	$(if $(call enabled,$(STANDALONE)),Standalone)
PACKAGE_COMPONENTS := $(foreach product,$(PACKAGE_PRODUCTS) $(PACKAGE_FX_PRODUCTS),$(foreach format,$(PACKAGE_FORMATS),$(product)-$(format)))

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
PACKAGE_COMPONENTS_CMAKE := $(subst $(SPACE),;,$(strip $(PACKAGE_COMPONENTS)))
LPAREN := (
RPAREN := )
selection_marker = $(if $(call enabled,$($(1))),* )
PRODUCT_ROW_1 = [ $(call selection_marker,OSIRUS)OSIRUS ] [ $(call selection_marker,OSTIRUS)OSTIRUS ] [ $(call selection_marker,VAVRA)VAVRA ] [ $(call selection_marker,XENIA)XENIA ]
PRODUCT_ROW_2 = [ $(call selection_marker,JE8086)JE-8086 ] [ $(call selection_marker,NODALRED2X)NODALRED2X ]
FORMAT_ROW_1 = [ $(call selection_marker,VST2)VST2 ] [ $(call selection_marker,VST3)VST3 ] [ $(call selection_marker,AU)AU ] [ $(call selection_marker,CLAP)CLAP ] [ $(call selection_marker,LV2)LV2 ]
FORMAT_ROW_2 = [ $(call selection_marker,STANDALONE)STANDALONE ] [ $(call selection_marker,FX)FX ]
CONFIG_ROW = [ $(shell printf '%s' $(call shell_quote,$(CONFIG)) | tr '[:lower:]' '[:upper:]') ]
define center_banner_command
text=$(call shell_quote,$(1)); width=67; padding=$$((width-$${#text})); left=$$((padding/2)); right=$$((padding-left)); printf '║%*s%s%*s║' "$$left" '' "$$text" "$$right" ''
endef
center_banner_line = $(shell $(call center_banner_command,$(1)))
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
	$(if $(filter ON,$(CMAKE_STANDALONE)),standalone) \
	$(if $(filter ON,$(CMAKE_FX)),fx)))
GENERATOR_PROFILE := $(subst $(SPACE),-,$(GENERATOR))
BUILD_DIMENSIONS := $(subst ;,+,$(CMAKE_ARCHS))$(if $(filter ON,$(LTO_CMAKE)),-ltoON)$(if $(filter ON,$(THIRDPARTY_WARNINGS_CMAKE)),-twON)
PROFILE := $(GENERATOR_PROFILE)/$(CONFIG)/$(BUILD_DIMENSIONS)/$(OPTION_PROFILE)
BUILD_ROOT ?= $(CURDIR)/build
BUILD_DIR ?= $(BUILD_ROOT)/$(PROFILE)
PRODUCTS_DIR := $(BUILD_DIR)/bin
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
.PHONY: help clean clean-all configure reconfigure build \
	install install-deps \
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
$(call center_banner_line,$(PRODUCT_ROW_1))
$(call center_banner_line,$(PRODUCT_ROW_2))
║                                                                   ║
$(call center_banner_line,$(FORMAT_ROW_1))
$(call center_banner_line,$(FORMAT_ROW_2))
║                                                                   ║
$(call center_banner_line,$(CONFIG_ROW))
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

define newline


endef
BANNER_TEXT := $(if $(filter help,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),build)),$(HELP_BANNER),$(BANNER))
BANNER_PRINTF := $(subst $(newline),\r\n,$(BANNER_TEXT))

ifeq ($(MAKE_INNER),1)
help:
	@sleep 0.5
	@printf '%s\r\n' \
		'Usage:' \
		'  make PRODUCT=1 FORMAT=1 [options] [action...]' \
		'' \
		'Products:' \
		'  OSIRUS  OSTIRUS  VAVRA  XENIA  NODALRED2X  JE8086' \
		'' \
		'Formats:' \
		'  VST2  VST3  AU  CLAP  LV2  STANDALONE' \
		'' \
		'Options:' \
		'  FX=0                   Build available FX variants' \
		'  CONFIG=Release         Debug, Release, RelWithDebInfo, ...' \
		'  ARCH=native            native, universal, arm64, x86_64, or CMake arch' \
		'  LTO=0                  Enable link-time optimization' \
		'  JOBS=12                Parallel job limit'
	@printf '%s\r\n' \
		'' \
		'Actions:' \
		'  build                   Build selected products (default)' \
		'  install                 Install the selected build profile' \
		'  package                 Package the selected build profile' \
		'  configure               Configure the selected profile' \
		'  reconfigure             Reconfigure the selected profile' \
		'  clean                   Remove the selected build profile' \
		'  clean-all               Remove all Make-managed build profiles' \
		'  install-deps            Install/update build dependencies (Linux only)' \
		'  android                 Android batch build (Windows only)' \
		'  android-abi             Android ABI build (ANDROID_ABI=arm64-v8a)'
	@printf '%s\r\n' \
		'' \
		'Build profiles:' \
		'  Explicit build selections are remembered for subsequent commands.' \
		'  Bare build/install/package/clean commands use the last selected build.' \
		'' \
		'  No product or format is selected by default.' \
		'' \
		'Advanced options:' \
		'  GENERATOR=Ninja         CMake generator' \
		'  BUILD_ROOT=path         Root containing Make-managed build profiles' \
		'  BUILD_DIR=path          Override the selected profile build directory' \
		'  TARGETS="name ..."      Additional explicit CMake targets' \
		'  THIRDPARTY_WARNINGS=0   Show suppressed dependency warnings when set to 1'
	@printf '%s\r\n' \
		'' \
		$(call shell_quote,Install paths ($(subst $(LPAREN),,$(subst $(RPAREN),,$(INSTALL_PLATFORM)))):) \
		$(call shell_quote,  VST3        $(or $(VST3_INSTALL_DIR),not configured)) \
		$(call shell_quote,  VST2        $(or $(VST2_INSTALL_DIR),not configured)) \
		$(call shell_quote,  AU          $(if $(filter Darwin,$(HOST_OS)),$(AU_INSTALL_DIR),not supported on this platform)) \
		$(call shell_quote,  CLAP        $(or $(CLAP_INSTALL_DIR),not configured)) \
		$(call shell_quote,  LV2         $(or $(LV2_INSTALL_DIR),not configured)) \
		$(call shell_quote,  Standalone  $(or $(APP_INSTALL_DIR),not configured)) \
		'' \
		'  Override with:' \
		'    VST3_INSTALL_DIR=/path' \
		'    VST2_INSTALL_DIR=/path' \
		'    AU_INSTALL_DIR=/path' \
		'    CLAP_INSTALL_DIR=/path' \
		'    LV2_INSTALL_DIR=/path' \
		'    APP_INSTALL_DIR=/path' \
		'' \
		'  Quote paths containing spaces. Environment overrides are also accepted.'
ifneq ($(WINDOWS_HOST),)
	@printf '%s\r\n' '  VST2 is a guessed user-local location; add it to your DAW scan paths if needed.'
endif
	@printf '%s\r\n' \
		$(if $(filter Darwin,$(HOST_OS)),'  macOS destinations under /Library use sudo.') \
		'' \
		'Examples:' \
		'  make OSTIRUS=1 VST3=1' \
		'  make OSTIRUS=1 VST3=1 FX=1' \
		'  make OSIRUS=1 VST3=1 AU=1' \
		'  make OSIRUS=1 VST3=1 ARCH=universal CONFIG=Debug' \
		'' \
		'  make OSTIRUS=1 VST3=1 install' \
		'  make OSTIRUS=1 VST3=1 package' \
		'  make OSTIRUS=1 VST3=1 package install' \
		'' \
		'  make install                       Install the last selected build' \
		'  make package                       Package the last selected build' \
		'  make clean                         Remove the last selected build profile' \
		'' \
		'  make VAVRA=1 CLAP=1 THIRDPARTY_WARNINGS=1' \
		'  make android-abi ANDROID_ABI=x86_64' \
		''


$(CONFIG_STAMP): Makefile CMakeLists.txt source/CMakeLists.txt source/cmake/base.cmake source/cmake/juce.cmake
	$(call run_with_vcvars,$(CMAKE) $(CMAKE_CONFIGURE_ARGS))
	$(CMAKE) -E touch "$@"

configure: $(CONFIG_STAMP)

reconfigure:
	$(call run_with_vcvars,$(CMAKE) $(CMAKE_CONFIGURE_ARGS))
	$(CMAKE) -E touch "$(CONFIG_STAMP)"

build: $(CONFIG_STAMP)
	@if test -z "$(BUILD_TARGETS)"; then \
		echo 'No build targets selected. Select a product/format or TARGETS=name.' >&2; \
		exit 2; \
	else \
		$(call run_with_vcvars,$(CMAKE) --build "$(BUILD_DIR)" --config "$(CONFIG)" $(BUILD_PARALLEL) --target $(BUILD_TARGETS)); \
	fi

package: build
	$(CMAKE) -E chdir "$(BUILD_DIR)" $(CMAKE) -DTUS_PACK_COMPONENTS="$(PACKAGE_COMPONENTS_CMAKE)" -P "$(CURDIR)/scripts/pack.cmake"

clean-all:
	$(CMAKE) -E remove_directory "$(BUILD_ROOT)"

clean:
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

# $(1): artifact relative to the configured build's bin directory; $(2): destination directory.
ifeq ($(HOST_OS),Darwin)
mac_install_privilege = $(if $(filter /Library /Library/%,$(1)),sudo)
define install_artifact
	@test -n $(call shell_quote,$(2)) || { echo 'No install directory configured.' >&2; exit 2; }
	$(call mac_install_privilege,$(2)) $(CMAKE) -E make_directory "$(2)"
	$(call mac_install_privilege,$(2)) /usr/bin/ditto $(call shell_quote,$(PRODUCTS_DIR)/$(1)) $(call shell_quote,$(2)/$(notdir $(1)))
endef
else
define install_artifact
	@test -n $(call shell_quote,$(2)) || { echo 'No install directory configured.' >&2; exit 2; }
	$(CMAKE) -E make_directory "$(2)"
	@printf '\nInstalling %s -> %s\n' $(call shell_quote,$(PRODUCTS_DIR)/$(1)) $(call shell_quote,$(2)/$(notdir $(1)))
	@if test -d $(call shell_quote,$(PRODUCTS_DIR)/$(1)); then \
		$(CMAKE) -E copy_directory "$(PRODUCTS_DIR)/$(1)" "$(2)/$(notdir $(1))"; \
	else \
		$(CMAKE) -E copy_if_different "$(PRODUCTS_DIR)/$(1)" "$(2)/$(notdir $(1))"; \
	fi
endef
endif

define install_product
$(if $(call enabled,$(VST3)),$(call install_artifact,VST3/$(1).vst3,$(VST3_INSTALL_DIR)))
$(if $(call enabled,$(VST2)),$(call install_artifact,VST/$(1)$(VST2_EXTENSION),$(VST2_INSTALL_DIR)))
$(if $(call enabled,$(AU)),$(call install_artifact,AU/$(1).component,$(AU_INSTALL_DIR)))
$(if $(call enabled,$(CLAP)),$(call install_artifact,CLAP/$(1).clap,$(CLAP_INSTALL_DIR)))
$(if $(call enabled,$(LV2)),$(call install_artifact,LV2/$(1).lv2,$(LV2_INSTALL_DIR)))
$(if $(call enabled,$(STANDALONE)),$(call install_artifact,Standalone/$(1)$(APP_EXTENSION),$(APP_INSTALL_DIR)))
endef

install: build
	$(if $(call enabled,$(OSIRUS)),$(call install_product,Osirus))
	$(if $(call enabled,$(OSTIRUS)),$(call install_product,OsTIrus))
	$(if $(call enabled,$(VAVRA)),$(call install_product,Vavra))
	$(if $(call enabled,$(XENIA)),$(call install_product,Xenia))
	$(if $(call enabled,$(NODALRED2X)),$(call install_product,NodalRed2x))
	$(if $(call enabled,$(JE8086)),$(call install_product,JE8086))
	$(if $(and $(call enabled,$(FX)),$(call enabled,$(OSIRUS))),$(call install_product,OsirusFX))
	$(if $(and $(call enabled,$(FX)),$(call enabled,$(OSTIRUS))),$(call install_product,OsTIrusFX))
	$(if $(and $(call enabled,$(FX)),$(call enabled,$(VAVRA))),$(call install_product,VavraFX))
	$(if $(and $(call enabled,$(FX)),$(call enabled,$(XENIA))),$(call install_product,XeniaFX))
else
REQUESTED_GOALS := $(if $(strip $(MAKECMDGOALS)),$(MAKECMDGOALS),build)
RECURSIVE_DRY_RUN := $(if $(findstring n,$(firstword $(MAKEFLAGS))),-n)
SHOW_BANNER := $(if $(filter clean clean-all,$(REQUESTED_GOALS)),,$(if $(filter install package,$(REQUESTED_GOALS)),$(EXPLICIT_CONFIGURATION_SELECTORS),1))
SHOW_SUCCESS_MESSAGE := $(if $(filter help clean clean-all,$(REQUESTED_GOALS)),,$(if $(filter install package,$(REQUESTED_GOALS)),$(EXPLICIT_CONFIGURATION_SELECTORS),1))
SUCCESS_MESSAGE := Proceed, operator. The synthesizers are hungry.
SUPPORTED_COMMAND_LINE_VARIABLES := \
	CMAKE GENERATOR CONFIG ARCH LTO THIRDPARTY_WARNINGS FX JOBS TARGETS ANDROID_ABI \
	OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086 \
	VST2 VST3 AU CLAP LV2 STANDALONE \
	BUILD_ROOT BUILD_DIR VST3_INSTALL_DIR VST2_INSTALL_DIR AU_INSTALL_DIR CLAP_INSTALL_DIR LV2_INSTALL_DIR APP_INSTALL_DIR \
	HOST_OS HOST_ARCH VCVARS
COMMAND_LINE_VARIABLES := $(foreach variable,$(.VARIABLES),$(if $(filter command line,$(origin $(variable))),$(variable)))
UNKNOWN_COMMAND_LINE_VARIABLES := $(filter-out $(SUPPORTED_COMMAND_LINE_VARIABLES),$(COMMAND_LINE_VARIABLES))
CONFIGURATION_GOALS := build configure reconfigure package install
RECORD_CONFIGURATION := $(filter $(CONFIGURATION_GOALS),$(REQUESTED_GOALS))
SELECTION_REQUIRED := $(filter $(CONFIGURATION_GOALS) clean,$(REQUESTED_GOALS))
VALID_PRODUCT_SELECTION := $(strip $(SELECTED_PRODUCTS))
VALID_FORMAT_SELECTION := $(strip $(SELECTED_SUFFIXES))
CLEAN_ALL_PROMPT := $(if $(filter clean,$(REQUESTED_GOALS)),$(if $(LAST_CONFIGURATION_REQUESTED),$(if $(LAST_CONFIGURATION_AVAILABLE),,1)))
LAST_CONFIGURATION_VARIABLES := \
	GENERATOR CONFIG ARCH LTO THIRDPARTY_WARNINGS FX JOBS TARGETS \
	OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086 \
	VST2 VST3 AU CLAP LV2 STANDALONE BUILD_ROOT BUILD_DIR
FORWARDED_VARIABLES := \
	CMAKE GENERATOR CONFIG ARCH LTO THIRDPARTY_WARNINGS FX JOBS TARGETS ANDROID_ABI \
	OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086 \
	VST2 VST3 AU CLAP LV2 STANDALONE \
	BUILD_ROOT BUILD_DIR VST3_INSTALL_DIR VST2_INSTALL_DIR AU_INSTALL_DIR CLAP_INSTALL_DIR LV2_INSTALL_DIR APP_INSTALL_DIR \
	HOST_OS HOST_ARCH
.PHONY: __run $(REQUESTED_GOALS)

$(REQUESTED_GOALS): __run

__run:
	$(if $(UNKNOWN_COMMAND_LINE_VARIABLES),@echo 'Unknown option(s): $(UNKNOWN_COMMAND_LINE_VARIABLES)' >&2; echo 'Run `make help` to see supported options.' >&2; exit 2)
	$(if $(and $(LAST_CONFIGURATION_REQUESTED),$(if $(LAST_CONFIGURATION_AVAILABLE),,1),$(if $(CLEAN_ALL_PROMPT),,1)),@echo 'No saved build configuration is available. Run make with product and format selectors first.' >&2; exit 2)
	$(if $(and $(SELECTION_REQUIRED),$(if $(and $(VALID_PRODUCT_SELECTION),$(VALID_FORMAT_SELECTION)),,1),$(if $(CLEAN_ALL_PROMPT),,1)),@echo 'Select at least one synthesizer and one output format.' >&2; exit 2)
	$(if $(SHOW_BANNER),@printf '%b\n' '$(BANNER_PRINTF)' | while IFS= read -r line; do \
		printf '%s\r\n' "$$line"; \
		sleep 0.01; \
	done)
	$(if $(RECORD_CONFIGURATION),@mkdir -p "$(dir $(LAST_CONFIGURATION_FILE))")
	$(if $(RECORD_CONFIGURATION),@{ $(foreach variable,$(LAST_CONFIGURATION_VARIABLES),printf '%s\n' $(call shell_quote,$(variable) := $($(variable)));) } > "$(LAST_CONFIGURATION_FILE)")
	$(if $(CLEAN_ALL_PROMPT),+@while true; do \
		printf '%s' 'No saved build configuration is available. Do you wish to `clean-all`? [Y/n] '; \
		if ! IFS= read -r answer; then printf '\n%s\n' 'Nothing cleaned.'; break; fi; \
		case "$$answer" in \
			''|[Yy]|[Yy][Ee][Ss]$(RPAREN) make --no-print-directory $(RECURSIVE_DRY_RUN) MAKE_INNER=1 clean-all; break ;; \
			[Nn]|[Nn][Oo]$(RPAREN) echo 'Nothing cleaned.'; break ;; \
			*$(RPAREN) echo 'Please answer y or n.' ;; \
		esac; \
	done,+@make --no-print-directory $(RECURSIVE_DRY_RUN) MAKE_INNER=1 $(REQUESTED_GOALS) \
		$(foreach variable,$(FORWARDED_VARIABLES),$(variable)=$(call shell_quote,$($(variable)))))
	$(if $(SHOW_SUCCESS_MESSAGE),@printf '\n%s\n\n' '$(SUCCESS_MESSAGE)')
endif
