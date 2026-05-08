cmake_minimum_required(VERSION 3.16)
project(UHD2NAS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/encoderengine.cpp
    src/encoderengine.h
    src/cropdetector.cpp
    src/cropdetector.h
    src/doviprocessor.cpp
    src/doviprocessor.h
    src/templatemanager.cpp
    src/templatemanager.h
    src/processrunner.cpp
    src/processrunner.h
    resources.qrc
)

# Windows application icon (.ico)
if(WIN32)
    set(SOURCES ${SOURCES} uhd2nas.rc)
endif()

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
target_include_directories(${PROJECT_NAME} PRIVATE src)

# --- Deployment: create a self-contained distribution directory ---
set(DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy")

# Custom target: deploy
add_custom_target(deploy
    DEPENDS ${PROJECT_NAME}
    COMMENT "Creating deployment package in ${DEPLOY_DIR}"
)

if(WIN32)
    # Windows: use windeployqt to gather all Qt DLLs and compiler runtime
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DEPLOY_DIR}/"
        COMMAND "${WINDEPLOYQT}" --compiler-runtime --no-translations --no-opengl-sw
                "${DEPLOY_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMENT "Running windeployqt..."
    )
elseif(APPLE)
    # macOS: use macdeployqt to create a self-contained .app bundle
    find_program(MACDEPLOYQT macdeployqt HINTS "${Qt6_DIR}/../../../bin")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DEPLOY_DIR}/"
        COMMAND "${MACDEPLOYQT}" "${DEPLOY_DIR}/${PROJECT_NAME}.app"
        COMMENT "Running macdeployqt..."
    )
else()
    # Linux: copy binary and use a helper script to bundle Qt libs
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DEPLOY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E echo "Copying Qt libraries..."
        COMMAND bash -c "ldd '${DEPLOY_DIR}/${PROJECT_NAME}' | grep -i qt | awk '{print $$3}' | xargs -I{} cp -u {} '${DEPLOY_DIR}/lib/'"
        COMMAND ${CMAKE_COMMAND} -E echo "Creating run script..."
        COMMENT "Deploying for Linux..."
    )
    # Create a launch script that sets LD_LIBRARY_PATH
    file(WRITE "${CMAKE_BINARY_DIR}/run_uhd2nas.sh"
"#!/bin/bash\nDIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\nexport LD_LIBRARY_PATH=\"$DIR/lib:$LD_LIBRARY_PATH\"\nexec \"$DIR/UHD2NAS\" \"$@\"\n")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/run_uhd2nas.sh" "${DEPLOY_DIR}/"
        COMMAND chmod +x "${DEPLOY_DIR}/run_uhd2nas.sh"
    )
endif()
