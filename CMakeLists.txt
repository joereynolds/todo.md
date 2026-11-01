cmake_minimum_required(VERSION 3.21)
project(todomd VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent)
qt_standard_project_setup()

# Core: parsing and line editing. No GUI dependency so it can be unit tested.
add_library(todocore STATIC
    src/task.h
    src/parser.h src/parser.cpp
    src/edits.h src/edits.cpp
    src/editor.h src/editor.cpp
    src/templates.h src/templates.cpp
)
target_include_directories(todocore PUBLIC src)
target_link_libraries(todocore PUBLIC Qt6::Core)

# Store (disk sync, undo), item models and the window.
add_library(todoapp STATIC
    src/store.h src/store.cpp
    src/models.h src/models.cpp
    src/dialogs.h src/dialogs.cpp
    src/theme.h src/theme.cpp
    src/taskview.h src/taskview.cpp
    src/sidebar.h src/sidebar.cpp
    src/tagcompleter.h src/tagcompleter.cpp
    src/templatespage.h src/templatespage.cpp
    src/mainwindow.h src/mainwindow.cpp
)
target_link_libraries(todoapp PUBLIC todocore Qt6::Widgets PRIVATE Qt6::Concurrent)

qt_add_executable(todomd WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(todomd PRIVATE todoapp)
target_compile_definitions(todomd PRIVATE TODOMD_VERSION="${PROJECT_VERSION}")

option(TODOMD_BUILD_TESTS "Build unit tests" ON)
if(TODOMD_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE todocore Qt6::Test)
    add_test(NAME test_core COMMAND test_core)
    set_tests_properties(test_core PROPERTIES TIMEOUT 60)
    foreach(t test_store test_window)
        qt_add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE todoapp Qt6::Test)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    endforeach()
endif()

include(GNUInstallDirs)
install(TARGETS todomd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
    install(FILES packaging/todomd.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/todomd.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
