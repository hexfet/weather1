idf_component_register(SRCS "main.c" "wifi_interface.c" "index.html.h" "weather.css.h"
                       INCLUDE_DIRS ".")

add_custom_command(
    PRE_BUILD
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/index.html.h
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/process_html.py 
            ${CMAKE_CURRENT_SOURCE_DIR}/index.html 
            ${CMAKE_CURRENT_SOURCE_DIR}/index.html.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/index.html
    COMMENT "Generating index.html.h from index.html"
)
add_custom_command(
    PRE_BUILD
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/weather.css.h
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/process_html.py 
            ${CMAKE_CURRENT_SOURCE_DIR}/weather.css 
            ${CMAKE_CURRENT_SOURCE_DIR}/weather.css.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/weather.css
    COMMENT "Generating weather.css.h from weather.css"
)