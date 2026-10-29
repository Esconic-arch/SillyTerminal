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

add_library(ai_services src/aiservices.cpp src/aiservices.h src/aicredentials.cpp src/aicameraprofile.cpp src/aicameraprofile.h)
target_link_libraries(ai_services PUBLIC Qt6::Network)
target_include_directories(ai_services PUBLIC src)
qt_add_resources(ai_services ai_profile_example
    PREFIX "/ai" BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/ai"
    FILES assets/ai/camera-profile.example.json)
if(WIN32)
    target_link_libraries(ai_services PRIVATE advapi32)
elseif(APPLE)
    target_link_libraries(ai_services PRIVATE "-framework Security" "-framework CoreFoundation")
endif()
add_library(ai_settings_ui src/aisettingsdialog.cpp src/aisettingsdialog.h)
target_link_libraries(ai_settings_ui PUBLIC ai_services Qt6::Widgets)
target_include_directories(ai_settings_ui PUBLIC src)

add_library(terminal_core src/terminalcore.cpp src/terminalcore.h)
target_link_libraries(terminal_core PUBLIC Qt6::Core)
target_include_directories(terminal_core PUBLIC src)
qt_add_resources(terminal_core initial_world
    PREFIX "/initial-world"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/initial-world"
    FILES
        assets/initial-world/home/guest/notes.txt
        assets/initial-world/home/guest/documents/welcome.txt
        assets/initial-world/home/guest/documents/home-network.txt
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

add_library(camera_core src/cameracore.cpp src/cameracore.h src/cameraimageprovider.h)
target_link_libraries(camera_core PUBLIC Qt6::Core PRIVATE Qt6::Network)
target_include_directories(camera_core PUBLIC src)
add_library(ai_camera_provider src/aicameraimageprovider.cpp src/aicameraimageprovider.h)
target_link_libraries(ai_camera_provider PUBLIC ai_services camera_core Qt6::Gui)
target_include_directories(ai_camera_provider PUBLIC src)
qt_add_resources(camera_core initial_network
    PREFIX "/initial-network"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/initial-network"
    FILES assets/initial-network/device.json assets/initial-network/scene.json
)
add_library(camera_ui src/cameradialog.cpp src/cameradialog.h)
target_link_libraries(camera_ui PUBLIC camera_core terminal_core Qt6::Widgets)
target_include_directories(camera_ui PUBLIC src)

add_library(image_viewer src/imageviewer.cpp src/imageviewer.h)
target_link_libraries(image_viewer PUBLIC terminal_core Qt6::Widgets)
target_include_directories(image_viewer PUBLIC src)

add_library(terminal_ui src/terminalwidget.cpp src/terminalwidget.h src/textpager.cpp src/textpager.h
    src/terminalappearance.cpp src/terminalappearance.h)
target_link_libraries(terminal_ui PUBLIC terminal_core vim_editor text_browser mc_file_manager image_viewer camera_ui Qt6::Widgets)
target_include_directories(terminal_ui PUBLIC src)

add_library(local_session src/loginwidget.cpp src/loginwidget.h src/sessionwidget.cpp src/sessionwidget.h)
target_link_libraries(local_session PUBLIC terminal_ui local_account Qt6::Concurrent PRIVATE ai_camera_provider)
target_include_directories(local_session PUBLIC src)

qt_add_executable(SillyTerminal
    src/main.cpp
)
target_link_libraries(SillyTerminal PRIVATE local_session ai_settings_ui)
set_target_properties(SillyTerminal PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME SillyTerminal
    MACOSX_BUNDLE_GUI_IDENTIFIER io.esconic.sillyterminal
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

include(CTest)
if(BUILD_TESTING)
    add_executable(image_viewer_tests tests/image_viewer_tests.cpp)
    target_link_libraries(image_viewer_tests PRIVATE terminal_ui)
    add_test(NAME image_viewer COMMAND image_viewer_tests)
    set_tests_properties(image_viewer PROPERTIES TIMEOUT 25 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    add_executable(ai_camera_tests tests/ai_camera_tests.cpp)
    target_link_libraries(ai_camera_tests PRIVATE ai_camera_provider)
    add_test(NAME ai_camera COMMAND ai_camera_tests)
    set_tests_properties(ai_camera PROPERTIES TIMEOUT 40 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    add_executable(camera_tests tests/camera_tests.cpp)
    target_link_libraries(camera_tests PRIVATE terminal_ui)
    add_test(NAME camera COMMAND camera_tests)
    set_tests_properties(camera PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    add_executable(ai_services_tests tests/ai_services_tests.cpp)
    target_link_libraries(ai_services_tests PRIVATE ai_settings_ui)
    add_test(NAME ai_services COMMAND ai_services_tests)
    set_tests_properties(ai_services PROPERTIES TIMEOUT 25 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
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
