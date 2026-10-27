cmake_minimum_required(VERSION 3.21)
project(SillyTerminal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Concurrent)

add_library(local_account src/accountstore.cpp src/accountstore.h src/localidentity.h)
target_link_libraries(local_account PUBLIC Qt6::Core PRIVATE Qt6::Network)
target_include_directories(local_account PUBLIC src)

add_library(terminal_core src/terminalcore.cpp src/terminalcore.h)
target_link_libraries(terminal_core PUBLIC Qt6::Core)
target_include_directories(terminal_core PUBLIC src)
qt_add_resources(terminal_core initial_world
    PREFIX "/initial-world"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/initial-world"
    FILES
        assets/initial-world/home/guest/notes.txt
        assets/initial-world/home/guest/documents/welcome.txt
        "assets/initial-world/home/guest/documents/quick start.txt"
        assets/initial-world/home/guest/projects/README.md
        assets/initial-world/home/guest/.config/session.conf
        assets/initial-world/var/log/boot.log
)

add_library(vim_editor src/vimeditor.cpp src/vimeditor.h)
target_link_libraries(vim_editor PUBLIC terminal_core Qt6::Widgets)
target_include_directories(vim_editor PUBLIC src)

add_library(browser_core src/browsercore.cpp src/browsercore.h)
target_link_libraries(browser_core PUBLIC Qt6::Core)
target_include_directories(browser_core PUBLIC src)
qt_add_resources(browser_core local_sites
    PREFIX "/sites"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/sites"
    FILES
        assets/sites/pages.json
        assets/sites/daybreak/index.html
        assets/sites/daybreak/train.html
        assets/sites/daybreak/bookshop.html
        assets/sites/daybreak/about.html
        assets/sites/forum/index.html
        assets/sites/forum/chat.html
        assets/sites/forum/audio.html
        assets/sites/forum/computers.html
        assets/sites/forum/t-112.html
        assets/sites/forum/t-81.html
        assets/sites/forum/t-47.html
        assets/sites/forum/t-109.html
        assets/sites/forum/u-moss.html
        assets/sites/forum/u-slowtrain.html
        assets/sites/forum/u-needle.html
        assets/sites/forum/u-side_b.html
        assets/sites/forum/u-rust.html
        assets/sites/forum/u-bottleneck.html
        assets/sites/forum/u-lin.html
        assets/sites/forum/about.html
        assets/sites/lin/index.html
        assets/sites/lin/nightwalk.html
        assets/sites/lin/about.html
        assets/sites/transit/index.html
        assets/sites/transit/loop.html
        assets/sites/transit/nanqiao.html
        assets/sites/transit/notices.html
        assets/sites/transit/late-service.html
        assets/sites/transit/escalator.html
        assets/sites/donggang/index.html
        assets/sites/donggang/yard-cameras.html
        assets/sites/donggang/north-pier-works.html
        assets/sites/donggang/warehouse-market.html
        assets/sites/donggang/about.html
        assets/sites/mingche/mc-210.html
        assets/sites/yuanqiao/r4.html
)
add_library(text_browser src/textbrowser.cpp src/textbrowser.h)
target_link_libraries(text_browser PUBLIC browser_core Qt6::Widgets)
target_include_directories(text_browser PUBLIC src)

add_library(mc_file_manager src/mcfilemanager.cpp src/mcfilemanager.h)
target_link_libraries(mc_file_manager PUBLIC terminal_core Qt6::Widgets)
target_include_directories(mc_file_manager PUBLIC src)

add_library(terminal_ui src/terminalwidget.cpp src/terminalwidget.h src/textpager.cpp src/textpager.h
    src/terminalappearance.cpp src/terminalappearance.h)
target_link_libraries(terminal_ui PUBLIC terminal_core vim_editor text_browser mc_file_manager Qt6::Widgets)
target_include_directories(terminal_ui PUBLIC src)

add_library(local_session src/loginwidget.cpp src/loginwidget.h src/sessionwidget.cpp src/sessionwidget.h)
target_link_libraries(local_session PUBLIC terminal_ui local_account Qt6::Concurrent)
target_include_directories(local_session PUBLIC src)

qt_add_executable(SillyTerminal
    src/main.cpp
)
target_link_libraries(SillyTerminal PRIVATE local_session)
set_target_properties(SillyTerminal PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME SillyTerminal
    MACOSX_BUNDLE_GUI_IDENTIFIER io.esconic.sillyterminal
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

include(CTest)
if(BUILD_TESTING)
    add_executable(mc_tests tests/mc_tests.cpp)
    target_link_libraries(mc_tests PRIVATE terminal_ui)
    add_test(NAME mc_file_manager COMMAND mc_tests)
    set_tests_properties(mc_file_manager PROPERTIES TIMEOUT 25 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    add_executable(account_store_tests tests/account_tests.cpp)
    target_link_libraries(account_store_tests PRIVATE local_account)
    add_test(NAME account_store COMMAND account_store_tests)
    set_tests_properties(account_store PROPERTIES TIMEOUT 45)
    add_executable(local_session_tests tests/session_tests.cpp)
    target_link_libraries(local_session_tests PRIVATE local_session)
    add_test(NAME local_session COMMAND local_session_tests)
    set_tests_properties(local_session PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    add_executable(terminal_core_tests tests/core_tests.cpp)
    target_link_libraries(terminal_core_tests PRIVATE terminal_core)
    add_test(NAME terminal_core COMMAND terminal_core_tests)
    add_executable(vim_editor_tests tests/vim_tests.cpp)
    target_link_libraries(vim_editor_tests PRIVATE vim_editor)
    add_test(NAME vim_editor COMMAND vim_editor_tests)
    add_executable(browser_tests tests/browser_tests.cpp)
    target_link_libraries(browser_tests PRIVATE text_browser terminal_core)
    add_test(NAME browser COMMAND browser_tests)
    add_executable(terminal_interaction_tests tests/terminal_interaction_tests.cpp)
    target_link_libraries(terminal_interaction_tests PRIVATE terminal_ui)
    add_test(NAME terminal_interaction COMMAND terminal_interaction_tests)
    set_tests_properties(terminal_interaction PROPERTIES TIMEOUT 20 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    add_test(NAME terminal_ui COMMAND SillyTerminal --smoke-test ${CMAKE_CURRENT_BINARY_DIR}/terminal-preview.png)
    set_tests_properties(terminal_ui PROPERTIES TIMEOUT 35 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
endif()

install(TARGETS SillyTerminal BUNDLE DESTINATION . RUNTIME DESTINATION .)
