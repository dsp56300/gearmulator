Name:           TheUsualSuspects
Version:        2.2.24
Release:        0
Summary:        Emulations of classic virtual analog synthesizers
License:        GPL-3.0-or-later
URL:            https://theusualsuspects.io
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  cmake >= 3.15
BuildRequires:  gcc-c++
# base.cmake links libstdc++ statically; Fedora ships libstdc++.a separately.
%if 0%{?fedora}
BuildRequires:  libstdc++-static
%endif
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
Low-level IC emulations of classic virtual analog synthesizers, recreated by
emulating the original DSP56300, MC68K and H8S processors and running the
original firmware. Every synth is a package of its own with VST2, VST3, CLAP
and LV2 plugins, and each needs a firmware ROM image from its hardware,
which is not included.

%package -n theusualsuspects-osirus
Summary:        Access Virus A, B and C emulation

%description -n theusualsuspects-osirus
Osirus emulates the Access Virus A, B and C, as VST2, VST3, CLAP and LV2
plugins. It needs a firmware ROM image from the hardware, which is not
included.

%package -n theusualsuspects-ostirus
Summary:        Access Virus TI, TI2 and Snow emulation

%description -n theusualsuspects-ostirus
OsTIrus emulates the Access Virus TI, TI2 and Snow, as VST2, VST3, CLAP and
LV2 plugins. It needs a firmware ROM image from the hardware, which is not
included.

%package -n theusualsuspects-vavra
Summary:        Waldorf microQ emulation

%description -n theusualsuspects-vavra
Vavra emulates the Waldorf microQ, as VST2, VST3, CLAP and LV2 plugins. It
needs a firmware ROM image from the hardware, which is not included.

%package -n theusualsuspects-xenia
Summary:        Waldorf Microwave II and XT emulation

%description -n theusualsuspects-xenia
Xenia emulates the Waldorf Microwave II and XT, as VST2, VST3, CLAP and LV2
plugins. It needs a firmware ROM image from the hardware, which is not
included.

%package -n theusualsuspects-nodalred2x
Summary:        Clavia Nord Lead 2X and Nord Rack 2X emulation

%description -n theusualsuspects-nodalred2x
Nodal Red 2x emulates the Clavia Nord Lead 2X and Nord Rack 2X, as VST2,
VST3, CLAP and LV2 plugins. It needs a firmware ROM image from the hardware,
which is not included.

%package -n theusualsuspects-je8086
Summary:        Roland JP-8000 emulation

%description -n theusualsuspects-je8086
JE-8086 emulates the Roland JP-8000, as VST2, VST3, CLAP and LV2 plugins. It
needs a firmware ROM image from the hardware, which is not included.

%package -n theusualsuspects-88emu
Summary:        Roland Sound Canvas emulation

%description -n theusualsuspects-88emu
88emu emulates the hardware inside Roland Sound Canvas modules and related PCM
sound generators. Unlike the synths it is not a plugin: 88emuPlayer is an
application that plays MIDI files and takes MIDI input, and 88EmuCli renders
files to WAV offline. Both need ROM images from the hardware, which are not
included.

%prep
%autosetup -n %{name}-%{version}

%build
# Leave the build type to the distribution's %%cmake: base.cmake appends -Ofast
# and -fno-stack-protector only to Release and defines NDEBUG for every non-Debug
# build type. VST2 builds against the bundled GPL-3.0 FST headers. No -GNinja:
# openSUSE's %%cmake_build calls make directly rather than cmake --build.
# Standalone is on for 88emu, whose products are programs rather than plugins;
# the synths get a standalone build too, which is not installed.
%cmake \
    -Dgearmulator_BUILD_JUCEPLUGIN=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_VST2=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_LV2=ON \
    -Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON

%cmake_build

%install
%cmake_install

# The tree's install rules are shaped for the CPack ZIPs: a plain install also
# drops the vendored lunasvg, plutovg and RmlUi libraries and cmake files, the
# test console, the bridge server plugin and the per-plugin changelogs into the
# prefix. Keep only the four plugin directories and 88emu's two programs.
find %{buildroot}%{_prefix} -mindepth 1 -maxdepth 1 ! -name bin ! -name lib ! -name share -exec rm -rf {} +
find %{buildroot}%{_prefix}/lib -mindepth 1 -maxdepth 1 ! -name vst ! -name vst3 ! -name clap ! -name lv2 -exec rm -rf {} +
find %{buildroot}%{_bindir} -mindepth 1 ! -name 88emuPlayer ! -name 88EmuCli -exec rm -rf {} +

%check
# The Virus integration and ROM hash tests need firmware we cannot ship.
%ctest --exclude-regex 'virusIntegrationTests|virusRomHashTests'

%files -n theusualsuspects-osirus
%license LICENSE.md
%dir %{_prefix}/lib/vst
%dir %{_prefix}/lib/vst3
%dir %{_prefix}/lib/clap
%dir %{_prefix}/lib/lv2
%{_prefix}/lib/vst/Osirus.so
%{_prefix}/lib/vst3/Osirus.vst3
%{_prefix}/lib/clap/Osirus.clap
%{_prefix}/lib/lv2/Osirus.lv2

%files -n theusualsuspects-ostirus
%license LICENSE.md
%dir %{_prefix}/lib/vst
%dir %{_prefix}/lib/vst3
%dir %{_prefix}/lib/clap
%dir %{_prefix}/lib/lv2
%{_prefix}/lib/vst/OsTIrus.so
%{_prefix}/lib/vst3/OsTIrus.vst3
%{_prefix}/lib/clap/OsTIrus.clap
%{_prefix}/lib/lv2/OsTIrus.lv2

%files -n theusualsuspects-vavra
%license LICENSE.md
%dir %{_prefix}/lib/vst
%dir %{_prefix}/lib/vst3
%dir %{_prefix}/lib/clap
%dir %{_prefix}/lib/lv2
%{_prefix}/lib/vst/Vavra.so
%{_prefix}/lib/vst3/Vavra.vst3
%{_prefix}/lib/clap/Vavra.clap
%{_prefix}/lib/lv2/Vavra.lv2

%files -n theusualsuspects-xenia
%license LICENSE.md
%dir %{_prefix}/lib/vst
%dir %{_prefix}/lib/vst3
%dir %{_prefix}/lib/clap
%dir %{_prefix}/lib/lv2
%{_prefix}/lib/vst/Xenia.so
%{_prefix}/lib/vst3/Xenia.vst3
%{_prefix}/lib/clap/Xenia.clap
%{_prefix}/lib/lv2/Xenia.lv2

%files -n theusualsuspects-nodalred2x
%license LICENSE.md
%dir %{_prefix}/lib/vst
%dir %{_prefix}/lib/vst3
%dir %{_prefix}/lib/clap
%dir %{_prefix}/lib/lv2
%{_prefix}/lib/vst/NodalRed2x.so
%{_prefix}/lib/vst3/NodalRed2x.vst3
%{_prefix}/lib/clap/NodalRed2x.clap
%{_prefix}/lib/lv2/NodalRed2x.lv2

%files -n theusualsuspects-je8086
%license LICENSE.md
%dir %{_prefix}/lib/vst
%dir %{_prefix}/lib/vst3
%dir %{_prefix}/lib/clap
%dir %{_prefix}/lib/lv2
%{_prefix}/lib/vst/JE8086.so
%{_prefix}/lib/vst3/JE8086.vst3
%{_prefix}/lib/clap/JE8086.clap
%{_prefix}/lib/lv2/JE8086.lv2

%files -n theusualsuspects-88emu
%license LICENSE.md
%doc doc/88emu_readme.md
%{_bindir}/88emuPlayer
%{_bindir}/88EmuCli

%changelog
