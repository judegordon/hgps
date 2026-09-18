# Warning and floating-point settings for this project's own targets.
#
# Two rules from docs/decisions/0004-toolchain-cpp20-cmake-vcpkg-googletest.md:
#  - warnings are errors in our code, and every one is fixed rather than suppressed — with **one**
#    stated exception, below;
#  - -ffp-contract=off, so the compiler may not fuse a*b+c into an FMA behind our back. The audit
#    recorded the baseline's unpinned contraction as determinism source N-14.
#
# The exception: -Wno-missing-designated-field-initializers.
#
# clang 19 added that warning to -Wextra. It fires on a designated initialiser that leaves a field to
# its default member initialiser — `IssueLocation{.file = path}`, where `field`, `line` and `column`
# are all optional and all have defaults. That is the construct this tree uses to say "here is the
# part of the location that is known", in 129 places for `IssueLocation` alone and 213 altogether, and
# it is precisely defined C++20 rather than an oversight. Satisfying the warning means writing
# `IssueLocation{.file = path, .field = {}, .line = {}, .column = {}}` at every one of them, which is
# strictly harder to read and hides the thing the initialiser was saying.
#
# So this one is turned off, with the reason written down, rather than worked around 213 times. It is
# added only where the compiler knows it, so an older clang or a gcc does not see an unknown option.
# It was found by building with a newer clang than the development one, which is also how two real
# portability defects were found; see docs/build-notes.md.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-missing-designated-field-initializers
                        HGPS_HAS_WNO_MISSING_DESIGNATED_FIELD_INITIALIZERS)

function(hgps_target_options target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wold-style-cast
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wdouble-promotion
        -Wformat=2
        -ffp-contract=off)
    if(HGPS_HAS_WNO_MISSING_DESIGNATED_FIELD_INITIALIZERS)
        target_compile_options(${target} PRIVATE -Wno-missing-designated-field-initializers)
    endif()
    target_compile_features(${target} PUBLIC cxx_std_20)
endfunction()

# Dependencies are compiled with the same floating-point rule but without -Werror: their warnings
# are not ours to fix, and their arithmetic is ours to keep predictable.
add_compile_options(-ffp-contract=off)
