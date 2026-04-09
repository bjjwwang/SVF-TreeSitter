# =============================================================================
# Layer 1 — structural verifier (must pass for every test)
# =============================================================================
file(GLOB_RECURSE ALL_TESTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.c)

foreach(t ${ALL_TESTS})
    get_filename_component(name ${t} NAME_WE)
    get_filename_component(dir  ${t} DIRECTORY)
    add_test(NAME L1.${dir}.${name}
             COMMAND $<TARGET_FILE:ts-svf> --verify
                     ${CMAKE_CURRENT_SOURCE_DIR}/${t})
    set_tests_properties(L1.${dir}.${name} PROPERTIES
        ENVIRONMENT
        "LD_LIBRARY_PATH=${SVF_DIR}/Release-build/lib:${SVF_DIR}/z3.obj/bin")
endforeach()

# =============================================================================
# Layer 2 — inline `// EXPECTED:` ground truth (exact match)
# =============================================================================
find_program(PYTHON3 python3)
if(PYTHON3)
    foreach(t ${ALL_TESTS})
        get_filename_component(name ${t} NAME_WE)
        get_filename_component(dir  ${t} DIRECTORY)
        add_test(NAME L2.${dir}.${name}
                 COMMAND ${PYTHON3}
                         ${CMAKE_SOURCE_DIR}/scripts/check_expected.py
                         ${CMAKE_CURRENT_SOURCE_DIR}/${t})
        set_tests_properties(L2.${dir}.${name} PROPERTIES
            SKIP_RETURN_CODE 77
            ENVIRONMENT
            "TS_SVF=$<TARGET_FILE:ts-svf>;LD_LIBRARY_PATH=${SVF_DIR}/Release-build/lib:${SVF_DIR}/z3.obj/bin")
    endforeach()
endif()

# =============================================================================
# Layer 3 — points-to soundness vs. SVF's LLVM frontend
# =============================================================================
# Each L3 test runs clang+wpa on the same .c source, normalizes both pts
# dumps, and asserts ours is a sound superset on the overlap.
find_program(BASH_EXE bash)
find_program(PYTHON3  python3)
if(BASH_EXE AND PYTHON3 AND EXISTS ${SVF_DIR}/llvm-18.1.0.obj/bin/clang
              AND EXISTS ${SVF_DIR}/Release-build/bin/wpa)
    foreach(t ${ALL_TESTS})
        get_filename_component(name ${t} NAME_WE)
        get_filename_component(dir  ${t} DIRECTORY)
        add_test(NAME L3.${dir}.${name}
                 COMMAND ${BASH_EXE}
                         ${CMAKE_SOURCE_DIR}/scripts/run_compare.sh
                         ${CMAKE_CURRENT_SOURCE_DIR}/${t})
        set_tests_properties(L3.${dir}.${name} PROPERTIES
            ENVIRONMENT
            "SVF_DIR=${SVF_DIR};TS_SVF=$<TARGET_FILE:ts-svf>")
    endforeach()
else()
    message(STATUS "L3 comparison tests disabled (need clang/wpa/python3/bash)")
endif()
