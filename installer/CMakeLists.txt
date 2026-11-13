set(ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set(INSTALLER_VERSION "0.1.5")
set(INSTALLER_PACKAGE_VERSION "00.15")

add_executable(accessory_mount_installer main.cpp install.c config.c ../src/common/settings.c)
set_target_properties(accessory_mount_installer PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(accessory_mount_installer PRIVATE PSVSD_AM_EXTENDED_TARGETS=1)
target_compile_options(accessory_mount_installer PRIVATE -Wall -Wextra
  "$<$<COMPILE_LANGUAGE:CXX>:-fshort-wchar;-fno-exceptions;-fno-rtti;-D__declspec(x)=>")
# PAF strings use UTF-16. The C runtime is used only for narrow text and file buffers.
# VitaSDK reserves this gap for the import tables appended by vita-elf-create.
target_link_options(accessory_mount_installer PRIVATE
  -Wl,-q,--no-wchar-size-warning,--defsym=__sce_headroom=0x10000)
# Search newlib before the PAF stubs, which also export standard C functions.
# Pass -lc through to the linker so the compiler driver preserves this order.
target_link_libraries(accessory_mount_installer PRIVATE
  -Wl,-lc
  SceSysmodule_stub SceAppMgr_stub ScePower_stub SceRegistryMgr_stub
  SceIofilemgr_stub SceLibKernel_stub SceProcessmgr_stub SceKernelThreadMgr_stub
  SceCtrl_stub SceVshBridge_stub ScePaf_stub_weak)
add_custom_command(TARGET accessory_mount_installer POST_BUILD
  COMMAND "${CMAKE_COMMAND}" "-DOBJDUMP=${CMAKE_OBJDUMP}"
    "-DELF=$<TARGET_FILE:accessory_mount_installer>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_runtime_imports.cmake"
  VERBATIM)

# The installer uses the Vita PAF application startup configuration.
set(VITA_MAKE_FSELF_FLAGS "-a 0x2F00000000000101")
vita_create_self(accessory_mount.self accessory_mount_installer UNSAFE STRIPPED REL_OPTIMIZE)

set(vpk "Accessory_Mount_Plugin_${INSTALLER_VERSION}.vpk")
vita_create_vpk(${vpk} RTPSV0002 accessory_mount.self
  VERSION "${INSTALLER_PACKAGE_VERSION}"
  NAME "Accessory Mount Plugin"
  FILE "${ASSETS}/icon0.png" sce_sys/icon0.png
  FILE "${ASSETS}/template.xml" sce_sys/livearea/contents/template.xml
  FILE "${ASSETS}/bg.png" sce_sys/livearea/contents/bg.png
  FILE "${ASSETS}/startup.png" sce_sys/livearea/contents/startup.png
  FILE "${RESOURCES}/installer_menu.rco" installer_menu.rco
  FILE "${CMAKE_BINARY_DIR}/psvsd_settings.suprx" plugins/psvsd_settings.suprx
  FILE "${CMAKE_BINARY_DIR}/psvsd_am.skprx" plugins/psvsd_am.skprx
  FILE "${CMAKE_BINARY_DIR}/psvsd_am_status.skprx" plugins/psvsd_am_status.skprx
  FILE "${CMAKE_BINARY_DIR}/usbmc_accessory_probe.skprx" plugins/usbmc_accessory_probe.skprx)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.20")
  string(APPEND vpk "-vpk")
endif()
add_dependencies(${vpk} psvsd_settings.suprx-self psvsd_am.skprx-self
  psvsd_am_status.skprx-self usbmc_accessory_probe.skprx-self)
