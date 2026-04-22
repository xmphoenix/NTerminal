# libssh 编译问题解决总结

## 概述

本文档记录了在 Windows 环境下使用 Qt MinGW 编译 libssh 时遇到的问题及其解决方案。

## 环境信息

| 项目 | 版本/路径 |
|------|-----------|
| 操作系统 | Windows |
| Qt 版本 | 6.10.2 |
| MinGW | GCC 13.1.0 (Qt Tools/mingw1310_64) |
| CMake | 3.29.2 |
| libssh 版本 | 0.10.6 |
| OpenSSL 版本 | 3.4.0 (预编译) |
| zlib 版本 | 1.3.1 (预编译) |

---

## 问题一：OpenSSL Configure 需要 Unix 风格 Perl

### 问题现象

```
******************************************************************************
This perl implementation doesn't produce Unix like paths (with forward slash
directory separators).  Please use an implementation that matches your
building platform.
******************************************************************************
```

### 问题原因

- OpenSSL 的 Configure 脚本（Perl）需要输出 Unix 风格的路径（使用 `/` 作为分隔符）
- Strawberry Perl 是 Windows 原生版本，输出 Windows 风格路径（使用 `\`）
- MSYS2 Perl 可以输出 Unix 路径，但与 Qt MinGW 的 mingw32-make 不兼容

### 解决方案

**使用项目已有的预编译 OpenSSL 库（libssh-prebuilt 目录）**

项目已包含预编译的 OpenSSL 和 zlib：
- `libssh-prebuilt/lib/libcrypto.dll.a`
- `libssh-prebuilt/lib/libssl.dll.a`
- `libssh-prebuilt/lib/libz.dll.a`

修改 `build_libssh.ps1` 脚本，直接使用预编译库编译 libssh，跳过 OpenSSL 的编译。

---

## 问题二：MSYS2 与 Qt MinGW 路径不兼容

### 问题现象

```
C:\Qt\Tools\mingw1310_64\bin\ar.exe: providers/implementa: No such file or directory
C:\Qt\Tools\mingw1310_64\bin\ar.exe: cryp: No such file or directory
```

### 问题原因

- MSYS2 bash 生成的 Makefile 使用 Unix 风格路径（如 `/C/Qt/...`）
- Qt MinGW 的工具（gcc、ar、ld）使用 Windows 风格路径
- 在并行编译时，路径转换出现问题，导致路径被截断

### 解决方案

**避免使用 MSYS2，使用纯 Windows 工具链**

在 CMake 配置时，将所有路径转换为正斜杠格式：

```powershell
# PowerShell 中的路径转换
$CMakeArgs = $CMakeArgs | ForEach-Object { $_ -replace '\\', '/' }
```

---

## 问题三：zlib 库文件名不匹配

### 问题现象

```
mingw32-make[2]: *** No rule to make target 'D:\\Ai\\NTerminal\\libssh-prebuilt\\lib\\libzlib.dll.a'
```

### 问题原因

预编译目录中的 zlib 库文件名是 `libz.dll.a`，而脚本中引用的是 `libzlib.dll.a`。

### 解决方案

**修改脚本中的库文件名**

```powershell
# 修改前
"-DZLIB_LIBRARY=$OpenSSLPrebuiltDir\lib\libzlib.dll.a"

# 修改后
"-DZLIB_LIBRARY=$OpenSSLPrebuiltDir\lib\libz.dll.a"
```

---

## 问题四：链接器路径截断

### 问题现象

```
ld.exe: cannot find D:AiNTerminallibssh-prebuiltliblibz.dll.a: No such file or directory
```

路径中的反斜杠 `\` 全部丢失。

### 问题原因

MinGW 的 ld.exe 在处理某些路径参数时会截断反斜杠。

### 解决方案

**使用正斜杠路径**

CMake 配置参数中的所有路径使用正斜杠 `/`：

```powershell
# 转换所有路径为正斜杠格式
$CMakeArgs = $CMakeArgs | ForEach-Object { $_ -replace '\\', '/' }
```

---

## 问题五：NTerminal.pro 中 copy_libssh_dlls 失败

### 问题现象

```
process_begin: CreateProcess(NULL, copy_libssh_dlls, ...) failed.
make (e=2): 系统找不到指定的文件。
```

### 问题原因

- `$(COPY_DIR)` 是 qmake 的内置变量，但在 MinGW 环境下可能不正确工作
- `copy_libssh_dlls` 作为命令直接执行，但系统无法识别

### 解决方案

**使用 PowerShell 命令复制文件**

修改 NTerminal.pro：

```qmake
# 使用 PowerShell 进行文件复制
QMAKE_POST_LINK += powershell -ExecutionPolicy Bypass -Command \
    "Copy-Item -Path '$$shell_path($$LIBSSH_ROOT/bin/*.dll)' \
    -Destination '$$shell_path($$DESTDIR)' -Force -ErrorAction SilentlyContinue"
```

---

## 多线程编译配置

### 编译脚本参数

`build_libssh.ps1` 支持 `-ParallelJobs` 参数控制并行编译任务数：

```powershell
# 使用 8 个并行任务
.\build_libssh.ps1 -ParallelJobs 8

# 使用 CPU 核心数（自动检测）
.\build_libssh.ps1 -ParallelJobs 16
```

### 编译命令

```bash
# MinGW make 多线程编译
mingw32-make -j8

# CMake 构建
cmake --build . --parallel 8
```

### 编译时间对比

| 配置 | 编译时间 |
|------|----------|
| 单线程 (-j1) | ~2-3 分钟 |
| 8 线程 (-j8) | ~34 秒 |
| 16 线程 (-j16) | ~30 秒 |

---

## 最终编译脚本结构

修改后的 `build_libssh.ps1` 简化为：

1. **检查预编译 OpenSSL**（libssh-prebuilt）
2. **克隆 libssh 源码**（如不存在）
3. **CMake 配置**（使用正斜杠路径）
4. **多线程编译**（mingw32-make -j）
5. **安装到 libssh 目录**
6. **复制依赖 DLL**

### 关键配置参数

```powershell
$CMakeArgs = @(
    "-S", $LibsshSrcDir,
    "-B", $LibsshBuildDir,
    "-G", "MinGW Makefiles",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$LibsshInstallDir",
    "-DOPENSSL_ROOT_DIR=$OpenSSLPrebuiltDir",
    "-DOPENSSL_CRYPTO_LIBRARY=$OpenSSLPrebuiltDir/lib/libcrypto.dll.a",
    "-DOPENSSL_SSL_LIBRARY=$OpenSSLPrebuiltDir/lib/libssl.dll.a",
    "-DZLIB_LIBRARY=$OpenSSLPrebuiltDir/lib/libz.dll.a",
    "-DWITH_EXAMPLES=OFF",
    "-DWITH_SERVER=OFF",
    "-DBUILD_SHARED_LIBS=ON"
)

# 关键：路径转换
$CMakeArgs = $CMakeArgs | ForEach-Object { $_ -replace '\\', '/' }
```

---

## 编译输出

### 目录结构

```
NTerminal/
├── libssh/                  # libssh 安装目录
│   ├── bin/
│   │   ├── libssh.dll       # libssh 动态库
│   │   ├── libcrypto-3-x64.dll
│   │   ├── libssl-3-x64.dll
│   │   └── zlib1.dll
│   ├── lib/
│   │   └── libssh.dll.a     # 导入库
│   └── include/
│       └── libssh/          # 头文件
├── libssh-prebuilt/         # 预编译 OpenSSL/zlib
├── libssh-src/              # libssh 源码
└── libssh-build/            # 构建临时文件
```

---

## 最佳实践总结

### 1. 工具链一致性

- 编译工具链必须与目标项目使用相同的 MinGW 版本
- Qt 项目使用 Qt MinGW，则 libssh 也应使用 Qt MinGW 编译

### 2. 路径格式

- CMake/MinGW 环境下，始终使用正斜杠 `/` 作为路径分隔符
- PowerShell 中转换路径：`$path -replace '\\', '/'`

### 3. 预编译库的使用

- 如果项目已有预编译的依赖库，优先使用
- 自行编译 OpenSSL 在 Windows 上较为复杂，容易遇到问题

### 4. 多线程编译

- 使用 `-j` 参数显著减少编译时间
- 推荐使用 CPU 核心数作为并行任务数

### 5. qmake 后处理

- Windows 环境下，使用 PowerShell 命令代替 shell 命令
- `QMAKE_POST_LINK` 使用完整命令路径

---

## 参考链接

- [libssh 官方文档](https://api.libssh.org/stable/)
- [libssh GitHub](https://github.com/libssh/libssh)
- [OpenSSL 编译指南](https://wiki.openssl.org/index.php/Compilation_and_Installation)
- [CMake MinGW 路径问题](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html)

---

*文档生成时间：2026年4月22日*