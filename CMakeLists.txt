cmake_minimum_required(VERSION 3.21)

project(AquaLens VERSION 1.0.0 DESCRIPTION "Water quality data analyzer" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Charts Sql Concurrent)

set(SOURCES
    src/main.cpp

    src/core/CsvParser.h
    src/core/CsvParser.cpp
    src/core/Analyzer.h
    src/core/Analyzer.cpp
    src/core/Database.h
    src/core/Database.cpp
    src/core/KoreanWqi.h
    src/core/KoreanWqi.cpp

    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/InteractiveChartView.h
    src/ui/InteractiveChartView.cpp
    src/ui/Widgets.h
    src/ui/Widgets.cpp
    src/ui/WqiWidgets.h
    src/ui/WqiWidgets.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp

    resources/resources.qrc
)

qt_add_executable(AquaLens WIN32 MACOSX_BUNDLE ${SOURCES})

target_include_directories(AquaLens PRIVATE src src/core src/ui)
target_link_libraries(AquaLens PRIVATE Qt6::Widgets Qt6::Charts Qt6::Sql Qt6::Concurrent)

if(MSVC)
    # /utf-8: source files contain characters such as "·" and "→"
    target_compile_options(AquaLens PRIVATE /W4 /permissive- /utf-8 /MP /Zc:__cplusplus)
else()
    target_compile_options(AquaLens PRIVATE -Wall -Wextra)
endif()

# Visual Studio: make AquaLens the startup project and group files like the folder tree
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT AquaLens)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

# Windows: copy Qt DLLs + plugins (incl. the SQLite driver) next to the .exe after each build,
# so F5 in Visual Studio and double-clicking the .exe both work.
if(WIN32)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET AquaLens POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:AquaLens>"
            COMMENT "Deploying Qt runtime next to AquaLens.exe")
    endif()
endif()
