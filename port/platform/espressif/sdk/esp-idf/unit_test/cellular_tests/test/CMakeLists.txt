# For this platform we can simply bring in the generic test
# source code and map it to the unity framework used by
# Espressif through the macros in cellular_port_test.h
set(COMPONENT_SRCS "../../../../../../../../ctrl/test/cellular_ctrl_test.c"
                   "../../../../../../../../sock/test/cellular_sock_test.c"
                   "../../../../../../../test/cellular_port_test.c")
set(COMPONENT_ADD_INCLUDEDIRS "."
                              "../../../../../../../../ctrl/api"
                              "../../../../../../../../sock/api"
                              "../../../../../../../../cfg"
                              "../../../../../../../api"
                              "../../../../../cfg")
set(COMPONENT_REQUIRES "esp32" "esptool_py" "unity")

register_component()

if (DEFINED ENV{CELLULAR_FLAGS})
    separate_arguments(CELLULAR_FLAGS NATIVE_COMMAND "$ENV{CELLULAR_FLAGS}")
    target_compile_options(${COMPONENT_TARGET} PUBLIC ${CELLULAR_FLAGS})
    message("cellular_tests: added ${CELLULAR_FLAGS} due to environment variable CELLULAR_FLAGS.")
endif()