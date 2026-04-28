include("${VCPKG_ROOT_DIR}/triplets/x64-windows-static.cmake")

# Prevent OpenSSL from pinning host DLL when statically linked.
# OpenSSL pins the host DLL by calling `GetModuleHandleEx` with `GET_MODULE_HANDLE_EX_FLAG_PIN` in crypto/init.c, 
# which causes the host DLL to never be unloaded, and thus prevents the plugin from unloading by DeMic normally. 
# Setting `OPENSSL_USE_NOPINSHARED` to `ON` will prevent OpenSSL from calling `GetModuleHandleEx` with `GET_MODULE_HANDLE_EX_FLAG_PIN`.
# See ../docs/openssl-unload-note.md for details.
set(OPENSSL_USE_NOPINSHARED ON)