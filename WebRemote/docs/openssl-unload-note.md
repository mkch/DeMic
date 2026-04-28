# OpenSSL 导致 Plugin DLL 无法卸载问题排查记录

## 问题现象

WebRemote 在启用 HTTPS 时，DeMic 主程序通过“插件”菜单卸载 WebRemote（调用 `FreeLibrary`） 后，`WebRemote.plugin` DLL 无法从内存中真正卸载。HTTP（不启用 HTTPS）模式下无此问题，其他 plugin 均无此问题。

## 根本原因

### OpenSSL 静态链接 + Windows DLL PIN 机制

WebRemote 通过 vcpkg 静态链接 OpenSSL（被 `asio` 使用）。`asio::ssl::context` 的构造函数会初始化并调用 OpenSSL 的功能。 OpenSSL 在初始化时（`crypto/init.c`）会主动调用 `GetModuleHandleEx` 并附带 `GET_MODULE_HANDLE_EX_FLAG_PIN` 标志，将自身所在的模块 PIN 住，使其永远不会被 `FreeLibrary` 真正卸载。

由于 OpenSSL 是**静态链接**进 `WebRemote.plugin` 的，OpenSSL 的代码和符号地址都在 plugin DLL 的内存空间内，因此被 PIN 住的就是 `WebRemote.plugin` 本身。

### 源码位置

`crypto/init.c` 中有两处 `GET_MODULE_HANDLE_EX_FLAG_PIN` 调用：

**第一处（第 145 行）：`ossl_init_load_crypto_nodelete`**

```c
// OpenSSL 初始化时自动调用，以 base_inited 符号地址为参照 PIN 住所在模块
ret = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        | GET_MODULE_HANDLE_EX_FLAG_PIN,
    (void *)&base_inited,
    &handle);
```

触发时机：任何 SSL 操作之前，`OPENSSL_init_crypto()` 阶段（构造 `ssl::context` 即触发）。

**第二处（第 717 行）：`OPENSSL_atexit`**

```c
// 注册 atexit 清理回调时，以回调函数地址为参照 PIN 住所在模块
ret = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        | GET_MODULE_HANDLE_EX_FLAG_PIN,
    handlersym.sym,
    &handle);
```

触发时机：`ossl_init_base` 阶段，注册 OpenSSL 进程退出清理回调时。

两处调用均受同一对宏保护：

```c
#if !defined(OPENSSL_USE_NODELETE) \
    && !defined(OPENSSL_NO_PINSHARED)
```

只要定义了 `OPENSSL_USE_NOPINSHARED`，两处 PIN 均不会执行。

## 解决方案

在 vcpkg 的 overlay triplet 中设置 `OPENSSL_USE_NOPINSHARED ON`，让 vcpkg 在编译 OpenSSL 时追加 `no-pinshared` Configure 选项，从而使 OpenSSL 定义 `OPENSSL_NO_PINSHARED`，禁用上述两处 PIN 行为。

### 变更文件

**`WebRemote/vcpkg-configuration.json`**：注册 overlay triplets 目录。

```json
"overlay-triplets": [
  "vcpkg-triplets"
]
```

**`WebRemote/vcpkg-triplets/x64-windows-static.cmake`**：继承官方 triplet 并追加选项。

```cmake
include("${VCPKG_ROOT_DIR}/triplets/x64-windows-static.cmake")

# Prevent OpenSSL from pinning host DLL when statically linked.
# OpenSSL pins the host DLL by calling `GetModuleHandleEx` with
# `GET_MODULE_HANDLE_EX_FLAG_PIN` in crypto/init.c, which causes the host DLL
# to never be unloaded, preventing DeMic from unloading the plugin normally.
set(OPENSSL_USE_NOPINSHARED ON)
```

### 调用链

```
vcpkg-triplets/x64-windows-static.cmake
  └─ OPENSSL_USE_NOPINSHARED=ON
       └─ vcpkg ports/openssl/portfile.cmake 检测到该变量
            └─ OpenSSL Configure 追加 no-pinshared
                 └─ 编译时定义 OPENSSL_NO_PINSHARED
                      └─ crypto/init.c 中两处 FLAG_PIN 调用被 #if 跳过
```

## 注意事项

- 此修改需要重新编译 OpenSSL（`vcpkg install` 会重建 `x64-windows-static` triplet 对应的包）。
- 禁用 PIN 后，必须保证不在 WebRemote 从进程中 detach 后继续访问 OpenSSL 。由于 `OnUnload` 中已调用 `StopHTTPServer()` 完成了服务器的主动清理，实际上不会在 plugin 卸载后继续访问 OpenSSL。
- 如果未来新增其他平台/架构的 triplet，需在对应的 overlay triplet 文件中同样添加 `set(OPENSSL_USE_NOPINSHARED ON)`。

## 附注：CRT `_beginthreadex` （`std::thread`）的类似行为

Windows CRT（`ucrt/startup/thread.cpp`）在 `_beginthreadex` / `_beginthread` 创建线程时，同样会对线程函数所在模块调用 `GetModuleHandleExW`：

```cpp
// Attempt to bump the reference count of the module in which the user's
// thread procedure is defined, to ensure that the module will stay loaded
// as long as the thread is executing.  We will release this HMODULE when
// the thread procedure returns or _endthreadex is called.
GetModuleHandleExW(
    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
    reinterpret_cast<LPCWSTR>(procedure),
    &parameter.get()->_module_handle);
```

这与 OpenSSL 的做法有相似之处也有本质区别：

| | CRT `_beginthreadex` | OpenSSL `ossl_init_load_crypto_nodelete` |
|---|---|---|
| 调用标志 | `GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS`（无 PIN） | `... \| GET_MODULE_HANDLE_EX_FLAG_PIN` |
| 引用计数性质 | 临时增加 | **永久 PIN，无法通过 `FreeLibrary` 减少** |
| 释放时机 | 线程退出时（`return` 或者 `_endthreadex` 时）通过 `FreeLibrary`系列函数主动释放 | 永不释放，直到进程退出 |
| 对 DLL 卸载的影响 | 线程全部退出后引用计数归零，DLL 可正常卸载 | 引用计数永远 ≥ 1，DLL 永远无法卸载 |

CRT 机制的目的是防止线程函数所在 DLL 在线程运行期间被卸载导致崩溃。但这也导致当 DeMic 使用 `FreeLibrary` 卸载 plugin 时，如果 plugin 内还有 CRT 线程在运行那么此 plugin 就不能真正被被卸载。plugin 应当在 `OnUnload` callback 中等待自己创建的所有线程终止后再返回。

C++ `std::thread` 在 Windows 上底层就是调用 `_beginthreadex`，因此同样受此机制影响。
