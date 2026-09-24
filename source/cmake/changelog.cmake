set(TUS_CHANGELOG_SOURCE ${CMAKE_SOURCE_DIR}/doc/changelog.txt)
set(TUS_CHANGELOG_OUT_DIR ${CMAKE_SOURCE_DIR}/doc/changelog_split)
set(TUS_CHANGELOG_DEPENDS_FILE ${CMAKE_SOURCE_DIR}/doc/changelog_split/changelog_Osirus.txt)

add_custom_command(
    OUTPUT ${TUS_CHANGELOG_DEPENDS_FILE}
    COMMAND $<TARGET_FILE:changelogGenerator>
            -i ${TUS_CHANGELOG_SOURCE}
			-o ${TUS_CHANGELOG_OUT_DIR}
    DEPENDS
        changelogGenerator
        "${TUS_CHANGELOG_SOURCE}"
    COMMENT "Generating per-product changelogs"
)

# ponytail: a cross build cannot run the changelogGenerator it builds, so its packages carry whatever an earlier
# native build left in doc/changelog_split. Build the generator for the host if they must always have them.
if(TUS_CAN_RUN_BUILT_BINARIES)
	add_custom_target(tus_genChangelogs
		DEPENDS ${TUS_CHANGELOG_DEPENDS_FILE}
	)
else()
	add_custom_target(tus_genChangelogs)
endif()

macro(tus_registerChangelog targetName)
	add_dependencies(${targetName} tus_genChangelogs)
endmacro()
