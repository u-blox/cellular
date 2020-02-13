# The APIs
set(COMPONENT_ADD_INCLUDEDIRS 
# The API for the porting layer
                   "../../api"
# The API for the control interface
                   "../../../ctrl/api"
# The API for the data (sockets) interface
                   "../../../sock/api"
# The configuration files
                   "../../../cfg")

# The implementations of the APIs
set(COMPONENT_SRCS 
# The control interface
                   "../../../ctrl/src/cellular_ctrl.c"
                   "../../../ctrl/src/cellular_ctrl_at.c"
# The data (sockets) interface
                   "../../../sock/src/cellular_sock.c"
# The C library portion of the porting layer,
# which can be used unchanged on this platform
                   "../../clib/cellular_port_clib.c"
# This porting layer
                   "src/cellular_port.c"
                   "src/cellular_port_gpio.c"
                   "src/cellular_port_os.c"
                   "src/cellular_port_uart.c")

# The private include directories for the above
set(COMPONENT_PRIV_INCLUDEDIRS "../../../ctrl/src"
                               "../../../sock/src"
                               "../../clib"
                               "src")

register_component()
