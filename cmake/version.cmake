# cmake/version.cmake
#
# Generates ${BINARY_DIR}/src/ui/version_generated.h from git metadata.
# Invoked at build time (not configure time) via add_custom_target so the
# version string reflects the state of the tree when the build runs.
#
# Version string rules:
#   - Exact tag (e.g. v0.5.0)  →  "0.5.0"   (leading 'v' stripped)
#   - Exact tag, dirty tree    →  "0.5.0+"
#   - No tag, clean tree       →  "<short-hash>"   (7 hex digits)
#   - No tag, dirty tree       →  "<short-hash>+"

find_package(Git QUIET)

set(VERSION_STRING "unknown")

if(GIT_FOUND)
    # Is HEAD an exact tag?
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD
        WORKING_DIRECTORY ${SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_TAG_RESULT
    )

    # Is the working tree dirty?
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DIRTY
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG)
        # On an exact tag: strip a leading 'v' so "v0.5.0" → "0.5.0"
        string(REGEX REPLACE "^v" "" VERSION_STRING "${GIT_TAG}")
    else()
        # Not on a tag: use the short commit hash
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${SOURCE_DIR}
            OUTPUT_VARIABLE GIT_HASH
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(GIT_HASH)
            set(VERSION_STRING "${GIT_HASH}")
        endif()
    endif()

    # Dirty tree gets a "+" suffix in both cases
    if(GIT_DIRTY)
        string(APPEND VERSION_STRING "+")
    endif()
endif()

configure_file(
    ${SOURCE_DIR}/src/ui/version_generated.h.in
    ${BINARY_DIR}/src/ui/version_generated.h
    @ONLY
)
