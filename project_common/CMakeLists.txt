# Project-local shared implementation, compiled separately into each image.
add_library(project_common INTERFACE)
target_include_directories(project_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/flash
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    ${CMAKE_CURRENT_SOURCE_DIR}/fault
    ${CMAKE_CURRENT_SOURCE_DIR}/startup
    ${CMAKE_CURRENT_SOURCE_DIR}/link
)
target_sources(project_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/flash/bsp_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fault/bsp_fault_capture.S
    ${CMAKE_CURRENT_SOURCE_DIR}/link/hdlc_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/link/hdlc_frame.c
)

# C and GNU ld consume the same layout constants. Only preprocessing runs
# during configuration; no firmware object is generated here.
function(project_linker_script target source)
    get_filename_component(repo "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.."
                           ABSOLUTE)
    set(layout ${repo}/project_common/startup/project_layout.h)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/${target}.ld)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -E -P -x c
                -I${repo}/project_common/startup ${source} -o ${output}
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot preprocess linker script: ${source}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${source} ${layout})
    target_link_options(${target} PRIVATE -T${output})
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${output})
endfunction()
