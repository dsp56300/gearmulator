Name:           TheUsualSuspects
Version:        2.2.19
Release:        0
Summary:        Emulations of classic virtual analog synthesizers
License:        GPL-3.0-or-later
URL:            https://theusualsuspects.io
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  cmake >= 3.15
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(alsa)
# JUCE builds juceaide as a host tool in its own sub-build, which does not see
# the bundled freetype (that one is for RmlUi) and needs the system headers.
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(gl)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xcomposite)
BuildRequires:  pkgconfig(xcursor)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xinerama)
BuildRequires:  pkgconfig(xrandr)

# The DSP56300 JIT only has x86_64 and aarch64 backends.
ExclusiveArch:  x86_64 aarch64

%description
Low-level IC emulations of classic virtual analog synthesizers (Access Virus,
Waldorf microQ/XT, Clavia Nord Lead 2x, Roland JP-8000), recreated by emulating
the original DSP56300, MC68K and H8S processors and running the original
firmware.

The plugins ship without firmware. Each emulation needs a ROM image dumped from
the corresponding hardware, placed next to the plugin or in the plugin's data
directory. See the per-plugin changelog in %{_docdir}/%{name}.

%prep
%autosetup -n %{name}-%{version}

%build
# The %%cmake default of RelWithDebInfo is what we want. base.cmake appends
# -Ofast and -fno-stack-protector only to the *Release* config, and -flto never
# reaches a Linux GCC build at all (the IPO branch is skipped for GCC), so
# RelWithDebInfo gets plain %%optflags, keeps the stack protector and
# _FORTIFY_SOURCE, and yields real debuginfo with no flag surgery.
#
# VST2 builds against FST (source/3rdparty/fst), the GPL-3.0 clean-room
# reimplementation of the VST2 interface. findvst2.cmake picks it whenever
# Steinberg's proprietary SDK is absent, which is exactly the OBS case, so this
# VST2 build carries no Steinberg licence at all.
# No -GNinja: openSUSE's %%cmake_build calls make directly rather than
# `cmake --build`, so a Ninja tree leaves it with no makefile.
#
%cmake \
	-Dgearmulator_BUILD_JUCEPLUGIN=ON \
	-Dgearmulator_BUILD_JUCEPLUGIN_VST2=ON \
	-Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON \
	-Dgearmulator_BUILD_JUCEPLUGIN_CLAP=ON \
	-Dgearmulator_BUILD_JUCEPLUGIN_LV2=ON \
	-Dgearmulator_BUILD_JUCEPLUGIN_Standalone=OFF

%cmake_build

%install
%cmake_install

# The tree's install rules are shaped for the CPack component ZIPs, not for a
# system prefix. A plain cmake --install also drops the vendored freetype,
# lunasvg, plutovg and RmlUi static libs, headers, cmake configs and pkgconfig
# files into the prefix, plus the test console, the bridge server plugin and
# the per-plugin changelogs. Whitelist what we actually ship rather than
# chasing each stray: only the four plugin directories survive.
find %{buildroot}%{_prefix} -mindepth 1 -maxdepth 1 ! -name lib ! -name share -exec rm -rf {} +
find %{buildroot}%{_prefix}/lib -mindepth 1 -maxdepth 1 ! -name vst ! -name vst3 ! -name clap ! -name lv2 -exec rm -rf {} +

%check
# The Virus integration and ROM hash tests need firmware images we cannot ship.
%ctest --exclude-regex 'virusIntegrationTests|virusRomHashTests'

%files
%license LICENSE.md
%doc README.md doc/changelog.txt
# Cross-platform plugin search paths, deliberately not %%{_libdir}: VST2, VST3,
# CLAP and LV2 hosts look in /usr/lib/<format> on every architecture.
%dir %{_prefix}/lib/vst
%dir %{_prefix}/lib/vst3
%dir %{_prefix}/lib/clap
%dir %{_prefix}/lib/lv2
%{_prefix}/lib/vst/*
%{_prefix}/lib/vst3/*
%{_prefix}/lib/clap/*
%{_prefix}/lib/lv2/*

%changelog
