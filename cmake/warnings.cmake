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
#
# An older clang and every GCC have no such narrow flag: they fold the same construct into
# -Wmissing-field-initializers, which -Wextra turns on. The first CI run to get as far as linking
# failed there, on both, at the same 213 initialisers. So where the narrow flag is missing the broad
# one is turned off instead. That is a wider suppression than we would choose — it also covers a
# positional aggregate initialiser that runs out of members — and it is the only thing those
# compilers offer. Where the narrow flag exists it is used, so the development compiler keeps the
# tighter setting and would still catch the positional case.
# -Wshadow-field-in-constructor, where the compiler has it.
#
# GCC's -Wshadow rejects a constructor parameter that shadows a member; clang's does not, and puts
# that check behind -Wshadow-field-in-constructor (part of -Wshadow-all). The first CI run found two
# such parameters in src/model/person.h, on GCC, after every local build had been clean for four
# runs. Turning the flag on where clang understands it means the development compiler now says what
# the Linux one would (docs/build-notes.md).
include(CheckCXXCompilerFlag)
# The POSITIVE spelling is what gets probed, and that is the whole point: GCC accepts any -Wno-<x>
# it has never heard of, and only complains if some other diagnostic is emitted. So probing
# -Wno-missing-designated-field-initializers answers "yes" on GCC, the narrow flag is added, it does
# nothing, and the fallback below is never reached — which is exactly what the second CI attempt
# did. Probing -Wmissing-designated-field-initializers is answered honestly by everybody.
check_cxx_compiler_flag(-Wmissing-designated-field-initializers
                        HGPS_HAS_WNO_MISSING_DESIGNATED_FIELD_INITIALIZERS)
check_cxx_compiler_flag(-Wshadow-field-in-constructor HGPS_HAS_WSHADOW_FIELD_IN_CONSTRUCTOR)

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
    else()
        target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
    endif()
    if(HGPS_HAS_WSHADOW_FIELD_IN_CONSTRUCTOR)
        target_compile_options(${target} PRIVATE -Wshadow-field-in-constructor)
    endif()
    target_compile_features(${target} PUBLIC cxx_std_20)
endfunction()

# Dependencies are compiled with the same floating-point rule but without -Werror: their warnings
# are not ours to fix, and their arithmetic is ours to keep predictable.
add_compile_options(-ffp-contract=off)
