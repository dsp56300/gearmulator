# Archive debug symbols so crashes in released builds - including older versions - can be symbolized later.
# macOS: .dSYM bundles (see base.cmake, which emits them for Release). Windows: linker .pdb files.
#
# Symbols are private debugging data: they go to the "deploy" remote (the NAS) only, never the public
# "upload" remote, and are keyed by branch + version so every released build stays symbolizable.
#
# The shipped plugins put their PDB / dSYM under <source>/bin/plugins/Release (JUCE PDB_OUTPUT_DIRECTORY,
# see source/juce.cmake), while the DSP bridge server + libraries land under the build dir - so both roots
# are scanned. Only Release symbols are collected.
#
# Invoke with: cmake -Dgearmulator_BINARY_DIR=<buildDir> [-Dgearmulator_SOURCE_DIR=<src>]
#                    [-DBRANCH=<branch>] [-DUPLOAD=1] -P scripts/deploySymbols.cmake
# Without UPLOAD it only builds the zip (dry run). Linux produces no symbols, so it is a no-op there.

if(NOT gearmulator_BINARY_DIR)
	message(FATAL_ERROR "Binary directory 'gearmulator_BINARY_DIR' not specified")
endif()

# make absolute so rclone/glob do not depend on the current working directory
get_filename_component(gearmulator_BINARY_DIR "${gearmulator_BINARY_DIR}" ABSOLUTE)

# CPACK_PACKAGE_NAME / CPACK_PACKAGE_VERSION / CPACK_SYSTEM_NAME
include(${gearmulator_BINARY_DIR}/CPackConfig.cmake)

# Roots that can hold symbols: the build dir (server/bridge, libraries) and <source>/bin (the shipped
# plugin products). relBase is their common ancestor so archive paths stay relative and tidy; in CI the
# build dir lives under the source dir. Falls back to the build dir when no source dir is given.
set(scanRoots "${gearmulator_BINARY_DIR}")
set(relBase "${gearmulator_BINARY_DIR}")
if(gearmulator_SOURCE_DIR)
	get_filename_component(gearmulator_SOURCE_DIR "${gearmulator_SOURCE_DIR}" ABSOLUTE)
	list(APPEND scanRoots "${gearmulator_SOURCE_DIR}/bin")
	set(relBase "${gearmulator_SOURCE_DIR}")
endif()

set(symbols "")
foreach(root ${scanRoots})
	if(NOT EXISTS "${root}")
		continue()
	endif()
	if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
		# whole .dSYM bundles (directories). GLOB_RECURSE with LIST_DIRECTORIES ignores the pattern for
		# directories and returns every traversed dir, so keep only the .dSYM roots - otherwise we would tar
		# their parents and archive the whole tree.
		file(GLOB_RECURSE candidates LIST_DIRECTORIES true "${root}/*")
		foreach(c ${candidates})
			if(IS_DIRECTORY "${c}" AND c MATCHES "\\.dSYM$" AND c MATCHES "/Release/")
				list(APPEND symbols "${c}")
			endif()
		endforeach()
	elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
		# module/linker .pdb files. Skip vcNNN.pdb (per-object compiler intermediates) which carry no module
		# symbols. A PDB matches its binary by embedded GUID/age, not by name or path, so every remaining
		# Release .pdb is a usable symbol file wherever the linker dropped it.
		file(GLOB_RECURSE pdbs "${root}/*.pdb")
		foreach(p ${pdbs})
			get_filename_component(pdbName "${p}" NAME)
			if(NOT pdbName MATCHES "^vc[0-9]+\\.pdb$" AND p MATCHES "/Release/")
				list(APPEND symbols "${p}")
			endif()
		endforeach()
	endif()
endforeach()
list(REMOVE_DUPLICATES symbols)

if(NOT symbols)
	message(STATUS "No Release debug symbols (.dSYM/.pdb) found, nothing to archive")
	return()
endif()

# zip them, paths relative to relBase so same-named PDBs from different formats (VST/VST3/CLAP) do not
# collide and copyArtefacts (which globs the top level of ROOT_DIR) can pick the archive up.
set(zipName "${CPACK_PACKAGE_NAME}-symbols-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}.zip")
set(zipPath "${gearmulator_BINARY_DIR}/${zipName}")
file(REMOVE "${zipPath}")

set(relSymbols "")
foreach(s ${symbols})
	file(RELATIVE_PATH r "${relBase}" "${s}")
	list(APPEND relSymbols "${r}")
endforeach()

list(LENGTH relSymbols symbolCount)
message(STATUS "Archiving ${symbolCount} symbol file(s)/bundle(s) into ${zipName}")

execute_process(
	COMMAND ${CMAKE_COMMAND} -E tar "cf" "${zipPath}" "--format=zip" ${relSymbols}
	WORKING_DIRECTORY "${relBase}"
	RESULT_VARIABLE zipResult COMMAND_ECHO STDOUT)
if(zipResult)
	message(FATAL_ERROR "Failed to create symbol archive: ${zipResult}")
endif()

if(NOT UPLOAD)
	message(STATUS "UPLOAD not set - built ${zipName} but not uploading")
	return()
endif()

# reuse the rclone helpers, pointed at the build dir where the zip now sits
set(ROOT_DIR ${gearmulator_BINARY_DIR})
include(${CMAKE_CURRENT_LIST_DIR}/rclone.cmake)

# store under symbols/<branch>/<version> on the deploy remote so different branches/versions never collide
set(branch "${BRANCH}")
if(NOT branch)
	set(branch "unknown")
endif()
string(REPLACE "/" "_" branch "${branch}")
string(REPLACE "\\" "_" branch "${branch}")

copyArtefacts("dsp56300:deploy" "symbols/${branch}/${CPACK_PACKAGE_VERSION}" "symbols")
