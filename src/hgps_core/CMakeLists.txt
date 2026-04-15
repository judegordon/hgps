find_package(fmt REQUIRED)

add_library(hgps_core STATIC)
target_compile_features(hgps_core PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

target_sources(
    hgps_core
    PRIVATE
        data/data_table.cpp
        diagnostics/input_issue.cpp
        diagnostics/internal_error.cpp
        types/identifier.cpp
        types/interval.cpp
        types/univariate_summary.cpp
        utils/string_util.cpp
)

target_include_directories(
    hgps_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hgps_core PUBLIC fmt::fmt)