set(SMB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../third_party/libsmb2")
if(NOT EXISTS "${SMB_ROOT}/lib/libsmb2.c")
    message(FATAL_ERROR "Run git submodule update --init --recursive before building")
endif()
# Match the pinned library's core sources without its desktop build system.
file(GLOB SMB_SOURCES "${SMB_ROOT}/lib/*.c")
list(REMOVE_ITEM SMB_SOURCES "${SMB_ROOT}/lib/aes_apple.c" "${SMB_ROOT}/lib/krb5-wrapper.c")
idf_component_register(SRCS ${SMB_SOURCES} "esp_compat.c"
    INCLUDE_DIRS "${SMB_ROOT}/include" "${SMB_ROOT}/include/smb2"
    PRIV_INCLUDE_DIRS "${SMB_ROOT}/lib" "${SMB_ROOT}/include/esp"
    REQUIRES lwip PRIV_REQUIRES esp_hw_support)
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H
    NEED_READV NEED_WRITEV NEED_GETLOGIN_R HAVE_ARC4RANDOM_BUF
    arc4random_buf=esp_fill_random gethostname=router_smb_gethostname "_U_=__attribute__((unused))")
target_compile_options(${COMPONENT_LIB} PRIVATE -include esp_random.h
    -Wno-error=implicit-fallthrough -Wno-error=type-limits)
