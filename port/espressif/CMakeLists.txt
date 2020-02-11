set(COMPONENT_SRCS 
# The control interface
                   "../../ctrl/src/cellular_ctrl.c"
                   "../../ctrl/src/cellular_ctrl_at.c"
# This porting layer
                   "src/cellular_port.c"
                   "src/cellular_port_clib.c"
                   "src/cellular_port_gpio.c"
                   "src/cellular_port_os.c"
                   "src/cellular_port_uart.c"
# The data (sockets) interface
                   )

#  The APIs for all of the above
set(COMPONENT_ADD_INCLUDEDIRS "../api"
                              "../../ctrl/api"
                              "../../sock/api"
                              "../../cfg")

# The private include directories for all of the above
set(COMPONENT_PRIV_INCLUDEDIRS "../../ctrl/src"
                               "../../sock/src"
                               "src")

register_component()
