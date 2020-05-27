# The APIs
set(COMPONENT_ADD_INCLUDEDIRS 
# The API for the porting layer
                   "../../../../../api"
# The API for the control interface
                   "../../../../../../ctrl/api"
# The API for the data (sockets) interface
                   "../../../../../../sock/api"
# The API for the MQTT interface
                   "../../../../../../mqtt/api"
# The generic configuration files
                   "../../../../../../cfg"
# The platform specific configuration files
                   "../../cfg")

# The implementations of the APIs
set(COMPONENT_SRCS 
# The control interface
                   "../../../../../../ctrl/src/cellular_ctrl.c"
                   "../../../../../../ctrl/src/cellular_ctrl_at.c"
# The data (sockets) interface
                   "../../../../../../sock/src/cellular_sock.c"
# The MQTT interface
                   "../../../../../../mqtt/src/cellular_mqtt.c"
# The C library portion of the porting layer,
# which can be used unchanged on this platform
                   "../../../../../clib/cellular_port_clib.c"
# The porting layer
                   "../../src/cellular_port.c"
                   "../../src/cellular_port_debug.c"
                   "../../src/cellular_port_gpio.c"
                   "../../src/cellular_port_os.c"
                   "../../src/cellular_port_uart.c")

# The private include directories for the above
set(COMPONENT_PRIV_INCLUDEDIRS "../../../../../../ctrl/src"
                               "../../../../../../sock/src"
                               "../../../../../../mqtt/src"
                               "../../../../../clib"
                               "../../src")
register_component()

if (DEFINED ENV{CELLULAR_FLAGS})
    separate_arguments(CELLULAR_FLAGS NATIVE_COMMAND "$ENV{CELLULAR_FLAGS}")
    target_compile_options(${COMPONENT_TARGET} PUBLIC ${CELLULAR_FLAGS})
    message("cellular: added ${CELLULAR_FLAGS} due to environment variable CELLULAR_FLAGS.")
endif()