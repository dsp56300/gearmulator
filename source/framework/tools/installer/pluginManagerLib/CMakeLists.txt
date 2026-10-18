cmake_minimum_required(VERSION 3.15)

project(pluginManagerLib VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES
	asset.cpp asset.h
	backup.cpp backup.h
	catalog.cpp catalog.h
	catalogSource.cpp catalogSource.h
	config.cpp config.h
	credentials.cpp credentials.h
	directoryIndex.cpp directoryIndex.h
	elevatedTask.cpp elevatedTask.h
	httpClient.cpp httpClient.h
	installPaths.cpp installPaths.h
	installedPlugins.cpp installedPlugins.h
	loginFile.cpp loginFile.h
	log.cpp log.h
	platformUtils.cpp platformUtils.h
	pluginInstaller.cpp pluginInstaller.h
	pluginManager.cpp pluginManager.h
	selfUpdate.cpp selfUpdate.h
	zipExtractor.cpp zipExtractor.h
)

add_library(pluginManagerLib STATIC)

target_sources(pluginManagerLib PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})

# For the folder the settings live in: baseLib is where this project decides what "the user's
# documents" means on each system, and the plugins reach that same answer through it.
target_link_libraries(pluginManagerLib PUBLIC baseLib)

# The changelog is read the same way changelogGenerator reads it, so the two cannot disagree about
# where a version or a product starts.
target_link_libraries(pluginManagerLib PUBLIC changelogLib)

# Consumers include "pluginManagerLib/<file>.h", as with every other library here.
target_include_directories(pluginManagerLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The JUCE modules are used as headers only; whichever program links this library compiles the
# module sources. Same arrangement as juceRmlUi.
target_include_directories(pluginManagerLib PUBLIC ${JUCE_MODULES_DIR})
target_compile_definitions(pluginManagerLib PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)

# Both programs report this, so it is set once here rather than per program.
target_compile_definitions(pluginManagerLib PUBLIC TUS_PLUGIN_MANAGER_VERSION="${CMAKE_PROJECT_VERSION}")

set_property(TARGET pluginManagerLib PROPERTY FOLDER "Gearmulator")
