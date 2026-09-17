# Warning and floating-point settings for this project's own targets.
#
# Two rules from docs/decisions/0004-toolchain-cpp20-cmake-vcpkg-googletest.md:
#  - warnings are errors in our code, and every one is fixed rather than suppressed, so there are
#    deliberately no -Wno-* entries below;
#  - -ffp-contract=off, so the compiler may not fuse a*b+c into an FMA behind our back. The audit
#    recorded the baseline's unpinned contraction as determinism source N-14.

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
    target_compile_features(${target} PUBLIC cxx_std_20)
endfunction()

# Dependencies are compiled with the same floating-point rule but without -Werror: their warnings
# are not ours to fix, and their arithmetic is ours to keep predictable.
add_compile_options(-ffp-contract=off)
