function(assure_out_of_source_builds)
    get_filename_component(srcdir "${CMAKE_SOURCE_DIR}" REALPATH)
    get_filename_component(bindir "${CMAKE_BINARY_DIR}" REALPATH)

    if(srcdir STREQUAL bindir)
        message(FATAL_ERROR
            "In-source builds are disabled.\n"
            "Please configure CMake in a separate build directory."
        )
    endif()
endfunction()

assure_out_of_source_builds()