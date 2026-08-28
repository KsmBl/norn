# Unit tests for the pure-function parts of core/, plus a live check that the
# spawn-and-read path actually works against a real repository.
#
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(GTest REQUIRED)

add_library(norntestsupport STATIC testrepo.cc main.cc)
target_link_libraries(norntestsupport PUBLIC norncore GTest::gtest)

function(norn_add_test name)
    add_executable(${name} ${name}.cc)
    # Deliberately not GTest::gtest_main: the suite supplies its own entry point so
    # it can initialise glibmm and giomm first.
    target_link_libraries(${name} PRIVATE norntestsupport GTest::gtest)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

norn_add_test(statusparsertest)
norn_add_test(gitrunnertest)
norn_add_test(patchbuildertest)
norn_add_test(graphlayouttest)
norn_add_test(logparsertest)
