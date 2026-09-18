# The build stamp the run manifest records: engine version, git commit, host platform, compiler.
#
# A run manifest that cannot be traced back to a commit is not provenance, so these are baked into
# the library rather than read from the environment at run time. Each is allowed to be unknown —
# a source tarball has no git metadata — and "unknown" is recorded as such rather than left blank,
# because a blank field reads as "nobody looked".

function(hgps_build_info_header output_var)
    set(HGPS_GIT_COMMIT "unknown")
    set(HGPS_GIT_DESCRIBE "unknown")
    set(HGPS_GIT_DIRTY "false")

    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                        OUTPUT_VARIABLE git_commit OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE git_status)
        if(git_status EQUAL 0 AND git_commit)
            set(HGPS_GIT_COMMIT "${git_commit}")
        endif()

        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --tags --dirty
                        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                        OUTPUT_VARIABLE git_describe OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE describe_status)
        if(describe_status EQUAL 0 AND git_describe)
            set(HGPS_GIT_DESCRIBE "${git_describe}")
        endif()

        execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
                        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                        OUTPUT_VARIABLE git_dirt OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE dirty_status)
        if(dirty_status EQUAL 0 AND NOT git_dirt STREQUAL "")
            set(HGPS_GIT_DIRTY "true")
        endif()
    endif()

    set(HGPS_VERSION "${PROJECT_VERSION}")
    set(HGPS_PLATFORM "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    set(HGPS_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    set(HGPS_BUILD_TYPE "${CMAKE_BUILD_TYPE}")

    # Written through configure_file so an unchanged stamp does not touch the file and force a
    # rebuild of everything that includes it.
    set(header "${CMAKE_BINARY_DIR}/generated/hgps/build_info.generated.h")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/build_info.generated.h.in" "${header}" @ONLY)
    set(${output_var} "${CMAKE_BINARY_DIR}/generated" PARENT_SCOPE)
endfunction()
