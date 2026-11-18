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

add_executable(bar_edge_tests bar_edge.cpp)
target_include_directories(bar_edge_tests PRIVATE ../core)
target_compile_definitions(bar_edge_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bar_edge_tests PRIVATE user32)
add_test(NAME bar_edge COMMAND bar_edge_tests)

add_executable(near_hover_tests near_hover.cpp)
target_include_directories(near_hover_tests PRIVATE ../core)
target_compile_definitions(near_hover_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME near_hover COMMAND near_hover_tests)

add_executable(hover_wave_tests hover_wave.cpp ../core/near_hover.cpp ../core/render/hover_wave.cpp)
target_include_directories(hover_wave_tests PRIVATE ../core)
target_compile_definitions(hover_wave_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME hover_wave COMMAND hover_wave_tests)

add_executable(stage_excluded_tests stage_excluded.cpp)
target_include_directories(stage_excluded_tests PRIVATE ../core)
target_compile_definitions(stage_excluded_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(stage_excluded_tests PRIVATE user32)
add_test(NAME stage_excluded COMMAND stage_excluded_tests)

add_executable(ime_bubble_tests ime_bubble.cpp)
target_include_directories(ime_bubble_tests PRIVATE ../core)
target_compile_definitions(ime_bubble_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ime_bubble_tests PRIVATE windowsapp user32 imm32)
add_test(NAME ime_bubble COMMAND ime_bubble_tests)

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

add_executable(tray_match_tests tray_match.cpp ../core/shell/tray_match.cpp)
target_include_directories(tray_match_tests PRIVATE ../core)
add_test(NAME tray_match COMMAND tray_match_tests)

add_executable(tray_click_tests tray_click.cpp)
target_include_directories(tray_click_tests PRIVATE ../core)
add_test(NAME tray_click COMMAND tray_click_tests)

add_executable(tray_notify_probe EXCLUDE_FROM_ALL tray_notify_probe.cpp)
target_link_libraries(tray_notify_probe PRIVATE ole32)

# Manual, read-only by default. Explicit --click uses the actual TrayModel worker.
add_executable(tray_live_probe EXCLUDE_FROM_ALL tray_live_probe.cpp ../core/shell/tray_match.cpp ../core/log.cpp)
target_include_directories(tray_live_probe PRIVATE ../core)
target_compile_definitions(tray_live_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(tray_live_probe PRIVATE windowsapp user32 shell32 ole32 oleaut32 uuid shlwapi windowscodecs version)

add_executable(tray_protocol_tests tray_protocol.cpp)
target_include_directories(tray_protocol_tests PRIVATE ../core)
target_compile_definitions(tray_protocol_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME tray_protocol COMMAND tray_protocol_tests)

# Puts a tray host in front of Explorer's for a few seconds: run by hand, with Atrium's host off.
add_executable(tray_host_probe EXCLUDE_FROM_ALL tray_host_probe.cpp ../core/shell/tray_host.cpp ../core/log.cpp)
target_include_directories(tray_host_probe PRIVATE ../core)
target_compile_definitions(tray_host_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(tray_host_probe PRIVATE windowsapp user32 shell32 ole32 shlwapi)
add_executable(stage_scroll_tests stage_scroll.cpp)
target_include_directories(stage_scroll_tests PRIVATE ../core)
target_compile_definitions(stage_scroll_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME stage_scroll COMMAND stage_scroll_tests)

add_executable(messengers_tests messengers.cpp)
target_include_directories(messengers_tests PRIVATE ../core)
target_compile_definitions(messengers_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(messengers_tests PRIVATE user32)
add_test(NAME messengers COMMAND messengers_tests)
