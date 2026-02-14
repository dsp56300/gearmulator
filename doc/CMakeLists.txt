project(doc)

add_custom_target( doc SOURCES changelog.txt copilot_todo.md)

# Copilot TODO list (for development tracking)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/copilot_todo.md")
    message(STATUS "Copilot TODO: see doc/copilot_todo.md for current tasks.")
endif()

