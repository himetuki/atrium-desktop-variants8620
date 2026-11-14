add_executable(display_settings_tests
    display_settings.cpp
    ../core/shell/displays.cpp
    ../core/settings.cpp
    ../core/localization.cpp
    ../core/log.cpp
    ../core/shell/logon_task.cpp)
target_include_directories(display_settings_tests PRIVATE ../core)
target_compile_definitions(display_settings_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN ATRIUM_DEVELOPMENT)
target_link_libraries(display_settings_tests PRIVATE windowsapp user32 shell32 ole32 oleaut32 taskschd advapi32)
add_test(NAME display_settings COMMAND display_settings_tests)

add_executable(app_processes_tests
    app_processes.cpp
    ../core/shell/app_processes.cpp
    ../core/log.cpp)
target_include_directories(app_processes_tests PRIVATE ../core)
target_compile_definitions(app_processes_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME app_processes COMMAND app_processes_tests)

add_executable(stage_excluded_tests stage_excluded.cpp)
target_include_directories(stage_excluded_tests PRIVATE ../core)
target_compile_definitions(stage_excluded_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME stage_excluded COMMAND stage_excluded_tests)

add_executable(double_click_tests
    double_click.cpp)
target_include_directories(double_click_tests PRIVATE ../core)
add_test(NAME double_click COMMAND double_click_tests)

add_executable(tray_interaction_tests tray_interaction.cpp)
target_include_directories(tray_interaction_tests PRIVATE ../core)
target_compile_definitions(tray_interaction_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(tray_interaction_tests PRIVATE user32)
add_test(NAME tray_interaction COMMAND tray_interaction_tests)
set_tests_properties(tray_interaction PROPERTIES TIMEOUT 20)

add_executable(quiet_profile_tests quiet_profile.cpp)
target_include_directories(quiet_profile_tests PRIVATE ../core)
target_compile_definitions(quiet_profile_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME quiet_profile COMMAND quiet_profile_tests)

add_executable(usb_volume_tests
    usb_volume.cpp
    ../core/shell/usb_volume.cpp
    ../core/localization.cpp
    ../core/log.cpp)
target_include_directories(usb_volume_tests PRIVATE ../core)
target_compile_definitions(usb_volume_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(usb_volume_tests PRIVATE windowsapp)
add_test(NAME usb_volume COMMAND usb_volume_tests)
set_tests_properties(usb_volume PROPERTIES TIMEOUT 20)

add_executable(localization_tests
    localization_checks.cpp
    ../core/localization.cpp)
target_include_directories(localization_tests PRIVATE ../core)
target_compile_definitions(localization_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(localization_tests PRIVATE windowsapp)
add_test(NAME localization COMMAND localization_tests)
add_custom_command(TARGET localization_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/../locales" "$<TARGET_FILE_DIR:localization_tests>/locales"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/../tests/fixtures/locales" "$<TARGET_FILE_DIR:localization_tests>/locales")

add_executable(stage_scroll_tests stage_scroll.cpp)
target_include_directories(stage_scroll_tests PRIVATE ../core)
target_compile_definitions(stage_scroll_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME stage_scroll COMMAND stage_scroll_tests)
