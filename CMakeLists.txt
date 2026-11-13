cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to your VitaSDK installation")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()
project(psvsd_native_settings VERSION 0.1.3 LANGUAGES C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(TAIHEN "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/taihen")
set(VITA_ELF_CREATE_FLAGS "" CACHE STRING "vita-elf-create flags")
set(VITA_MAKE_FSELF_FLAGS "" CACHE STRING "vita-make-fself flags")

function(plugin_target target)
  add_executable(${target} ${ARGN} src/common/settings.c)
  target_include_directories(${target} PRIVATE "${TAIHEN}/include")
  target_compile_definitions(${target} PRIVATE PSVSD_AM_EXTENDED_TARGETS=1)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -fno-builtin)
  target_link_options(${target} PRIVATE -nostdlib -Wl,-q)
endfunction()

plugin_target(psvsd_settings
  src/settings/module.c src/settings/layout.c src/settings/footer.c src/settings/storage.c src/settings/runtime.c
  src/settings/manager.c src/settings/session.c)
target_compile_definitions(psvsd_settings PRIVATE
  PSVSD_AM_NATIVE_SETTINGS=1 PSVSD_SETTINGS_VERSION="${PROJECT_VERSION}")
target_compile_options(psvsd_settings PRIVATE -ffunction-sections -fdata-sections)
target_link_options(psvsd_settings PRIVATE -Wl,--gc-sections,--undefined=module_stop)
target_link_libraries(psvsd_settings PRIVATE "${TAIHEN}/lib/libtaihen_stub.a"
  SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub SceProcessmgr_stub SceVshBridge_stub
  SceKernelThreadMgr_stub SceKernelModulemgr_stub ScePower_stub gcc)
configure_file(src/settings/exports.yml.in settings-exports.yml @ONLY)
function(settings_self)
  vita_create_self(psvsd_settings.suprx psvsd_settings CONFIG "${CMAKE_CURRENT_BINARY_DIR}/settings-exports.yml" UNSAFE)
endfunction()
settings_self()

function(kernel_plugin target role exports)
  plugin_target(${target} src/plugin/main.c)
  target_compile_definitions(${target} PRIVATE "${role}=1")
  target_compile_options(${target} PRIVATE -O2)
  target_link_libraries(${target} PRIVATE
    SceIofilemgrForDriver_stub SceSysclibForDriver_stub SceModulemgrForDriver_stub
    SceThreadmgrForDriver_stub SceKernelSuspendForDriver_stub
    "${TAIHEN}/lib/libtaihenForKernel_stub.a" "${TAIHEN}/lib/libtaihenModuleUtils_stub.a" gcc)
  set(AM_MODULE_MAJOR 1)
  set(AM_MODULE_MINOR 35)
  configure_file("src/plugin/${exports}.yml.in" "${target}-exports.yml" @ONLY)
  # Inherit the USBMC kernel payload's loader metadata.
  set(VITA_MAKE_FSELF_FLAGS "-a 0x2800000000000001")
  vita_create_self(${target}.skprx ${target} CONFIG "${CMAKE_CURRENT_BINARY_DIR}/${target}-exports.yml" UNSAFE)
endfunction()

kernel_plugin(psvsd_am USBMC_MANAGED_BOOT exports)
kernel_plugin(psvsd_am_status USBMC_STATUS_ONLY exports_status)
kernel_plugin(usbmc_accessory_probe USBMC_ACCESSORY_PROBE exports_probe)

option(ACCESSORY_MOUNT_INSTALLER "Build the standalone installer VPK" ON)
if(ACCESSORY_MOUNT_INSTALLER)
  enable_language(CXX)
  add_subdirectory(installer)
endif()
