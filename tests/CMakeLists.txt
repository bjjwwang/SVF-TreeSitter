# Glob basic tests
file(GLOB BASIC_TESTS basic/*.c)
foreach(t ${BASIC_TESTS})
    get_filename_component(name ${t} NAME_WE)
    add_test(NAME verify_${name}
             COMMAND ts-svf --verify ${t})
endforeach()
