# The control interface
set(COMPONENT_ADD_INCLUDEDIRS "../../ctrl/api")
set(COMPONENT_SRCS "../../ctrl/src/cellular_ctrl.c"
                   "../../ctrl/src/cellular_at.c")

# The data (sockets) interface
set(COMPONENT_ADD_INCLUDEDIRS "../../sock/api")
#set(COMPONENT_SRCS "")

# This porting layer
set(COMPONENT_ADD_INCLUDEDIRS "../api")
set(COMPONENT_SRCS "src/cellular_port.c"
                   "src/cellular_port_clib.c"
                   "src/cellular_port_gpio.c"
                   "src/cellular_port_os.c"
                   "src/cellular_port_uart.c")

# The private include directories for all of the above
set(COMPONENT_PRIV_INCLUDEDIRS "../../ctrl/src"
                               "../../sock/src"
                               "src")

register_component()
