# For this platform we can simply bring in the generic test
# source code and map it to the unity framework used by
# Espressif through the macros in cellular_port_test.h
set(COMPONENT_SRCS "../../../../../ctrl/test/cellular_ctrl_test.c"
                   "../../../../../sock/test/cellular_sock_test.c"
                   "../../../../test/cellular_port_test.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_REQUIRES "unity" "esp-idf")

register_component()