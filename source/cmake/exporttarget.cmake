# Every product that is deployed gets its changelog generated with it: the deploy puts that file next to its
# builds, and a product that is not a plugin - this repository's programs - has nothing else asking for it.
function(tus_exportTarget target)
	set_property(GLOBAL APPEND PROPERTY TUS_TARGETS "${target}")
	tus_registerChangelog(${target})
endfunction()
