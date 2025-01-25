idf_component_register(SRCS "weather.h" "main.c" "wifi_interface.c" "web_server.c" "web_content.h"
                       INCLUDE_DIRS ".")

add_custom_command(
    PRE_BUILD
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/web_content.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND python ./html_to_c.py 
    DEPENDS web_content/index.html web_content/weather.css web_content/weather.js
    COMMENT "Generating web_content.h from web_content/*"
)