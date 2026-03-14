# cmake/version.cmake
#
# Generates ${BINARY_DIR}/src/ui/version_generated.h.
# Invoked at build time (not configure time) via add_custom_target so the
# version string reflects the state of the tree when the build runs.
#
# Lookup order:
#   1. cmake/version.txt — populated by git archive / GitHub zip download via
#      the export-subst attribute in .gitattributes. Contains "$Format:%H %D$"
#      in a normal clone (unexpanded = signal to skip to step 2). In an archive
#      it expands to "<full-hash> <ref-decorations>", e.g.:
#        "abc123... HEAD -> main, tag: v0.5.0, origin/main"
#   2. Live git queries (requires git and a .git directory).
#   3. "unknown" — last resort.
#
# Version string rules:
#   - Exact tag (e.g. v0.5.0)  ->  "0.5.0"          (leading 'v' stripped)
#   - Exact tag, dirty tree    ->  "0.5.0+"
#   - No tag, clean            ->  "<short-hash>"    (7 hex chars)
#   - No tag, dirty            ->  "<short-hash>+"
#   - Archive (always clean)   ->  tag or short-hash, never "+"

set(VERSION_STRING "unknown")

# ------------------------------------------------------------------
# Step 1: try the archive-substituted version file
# ------------------------------------------------------------------
set(VERSION_FILE "${SOURCE_DIR}/cmake/version.txt")
if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" VERSION_FILE_CONTENT)
    string(STRIP "${VERSION_FILE_CONTENT}" VERSION_FILE_CONTENT)

    # If git archive ran, the file no longer starts with "$Format"
    if(NOT VERSION_FILE_CONTENT MATCHES "^\\$Format")
        # Content is "<full-hash> <decorations>"
        # Extract the full hash (first token)
        string(REGEX MATCH "^([0-9a-f]+)" _ "${VERSION_FILE_CONTENT}")
        set(ARCHIVE_HASH "${CMAKE_MATCH_1}")

        # Look for "tag: <tagname>" anywhere in the decorations
        if(VERSION_FILE_CONTENT MATCHES "tag: ([^,) ]+)")
            set(ARCHIVE_TAG "${CMAKE_MATCH_1}")
            string(REGEX REPLACE "^v" "" VERSION_STRING "${ARCHIVE_TAG}")
        elseif(ARCHIVE_HASH)
            # No tag — use first 7 chars of the hash
            string(SUBSTRING "${ARCHIVE_HASH}" 0 7 VERSION_STRING)
        endif()

        # Archives are clean snapshots; no "+" suffix needed
    endif()
    # If still unexpanded ("$Format..."), fall through to step 2
endif()

# ------------------------------------------------------------------
# Step 2: live git queries (skipped if step 1 already set a version)
# ------------------------------------------------------------------
if(VERSION_STRING STREQUAL "unknown")
    find_package(Git QUIET)

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
            # On an exact tag: strip a leading 'v' so "v0.5.0" -> "0.5.0"
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
endif()

configure_file(
    ${SOURCE_DIR}/src/ui/version_generated.h.in
    ${BINARY_DIR}/src/ui/version_generated.h
    @ONLY
)
