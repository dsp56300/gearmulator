cmake_minimum_required(VERSION 3.15)

# The plugin manager is one library with two programs on top of it: the GUI, and a headless one so
# it can be driven from a script. The library is also what the tests exercise.

# JUCE reaches an https URL through libcurl on Linux and through the system on Windows and macOS.
# Without it every catalog fetch fails, so the programs that fetch link it - and a machine that
# cannot build with it is told, rather than quietly producing a plugin manager that downloads
# nothing.
set(TUS_PM_NEEDS_CURL FALSE)
set(TUS_PM_USE_CURL 0)

if(UNIX AND NOT APPLE)
	find_package(PkgConfig QUIET)

	if(PKG_CONFIG_FOUND)
		pkg_check_modules(TUS_PM_LIBCURL QUIET libcurl)
	endif()

	if(TUS_PM_LIBCURL_FOUND)
		set(TUS_PM_NEEDS_CURL TRUE)
		set(TUS_PM_USE_CURL 1)
	else()
		# Fatal, not a warning. A warning is what this was, and it scrolled past in a green build on
		# both Linux agents for months: every Linux plugin manager we shipped could reach nothing at
		# all, and the first anybody knew was a user reporting that even github.com was unreachable
		# while the same URL opened in his browser (BUG-10463).
		#
		# A plugin manager that cannot download is not worth building, so this says so where it is
		# still cheap to fix.
		message(FATAL_ERROR "libcurl development files not found. JUCE reaches an https URL through "
			"libcurl on Linux, so without it the plugin manager can download nothing at all - not the "
			"catalog, not a build, not even from github.com. "
			"Install libcurl4-openssl-dev (Debian/Ubuntu) or libcurl-devel (Fedora), or build without "
			"-Dgearmulator_BUILD_INSTALLER=on.")
	endif()
endif()
# The GUI and the headless program ship together, in one archive named after this - which is also
# the folder they are uploaded to, lowercased. Named once so the two cannot drift apart.
set(TUS_PM_COMPONENT "PluginManager")

add_subdirectory(pluginManagerLib)
add_subdirectory(tusInstaller)
add_subdirectory(tuspm)
add_subdirectory(pluginManagerTest)
