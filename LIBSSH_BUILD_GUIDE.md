# libssh 源码编译指南

本文档说明如何从源码编译 libssh 库，供 NTerminal 项目使用。

## 自动编译（推荐）

libssh 编译已集成到项目的构建系统中，在 Qt Creator 中构建项目时会自动检测并编译 libssh。

### 自动编译流程

1. 打开 Qt Creator，加载 NTerminal 项目
2. 运行 qmake
3. 点击构建
4. 如果 `libssh` 目录不存在，会自动执行以下步骤：
   - 从 GitHub 克隆 libssh 0.10.6 源码
   - 使用 CMake 配置和编译
   - 安装到 `libssh` 目录
   - 复制 DLL 文件到输出目录

### 自动编译原理

NTerminal.pro 中定义了自定义目标：

```qmake
build_libssh.commands = powershell -ExecutionPolicy Bypass -File "build_libssh.ps1"
build_libssh.target = libssh/include/libssh/libssh.h

!exists(libssh/include/libssh/libssh.h) {
    PRE_TARGETDEPS += libssh/include/libssh/libssh.h
}
```

## 手动编译

如果需要手动编译 libssh，可以使用以下方法。

### 前提条件

在编译 libssh 之前，请确保已安装以下工具：

| 工具 | 版本要求 | 说明 |
| :--- | :--- | :--- |
| CMake | >= 3.16 | 构建系统 |
| Git | 最新版 | 用于克隆源码 |
| MinGW-w64 | GCC 13+ | Windows 编译工具链 |
| Qt | 6.x | 已安装 Qt Creator |

### 方法一：使用 PowerShell 脚本（推荐）

```powershell
# 在 PowerShell 中运行
.\build_libssh.ps1
```

### 方法二：使用批处理脚本

```cmd
# 在命令提示符中运行
build_libssh.bat
```

### 方法三：使用 Qt Creator

1. 打开 Qt Creator
2. 选择 **文件** → **打开文件或项目**
3. 选择 `libssh.cmake` 文件
4. 配置项目并构建

## 编译选项

### PowerShell 脚本参数

```powershell
.\build_libssh.ps1 -BuildType Release -Generator "MinGW Makefiles" -ParallelJobs 8
```

| 参数 | 默认值 | 说明 |
| :--- | :--- | :--- |
| BuildType | Release | 构建类型 (Release/Debug) |
| Generator | MinGW Makefiles | CMake 生成器 |
| OpenSSLRoot | ..\openssl-deps | OpenSSL 安装路径 |
| ParallelJobs | 8 | 并行编译任务数 |

## 目录结构

编译完成后的目录结构：

```
NTerminal/
├── libssh-src/          # libssh 源码（自动下载）
├── libssh-build/        # 构建临时文件
├── libssh/              # 安装目录
│   ├── bin/            # DLL 文件
│   │   ├── ssh.dll     # libssh 动态库
│   │   ├── libcrypto-*.dll
│   │   ├── libssl-*.dll
│   │   └── zlib*.dll
│   ├── lib/            # 导入库
│   │   └── libssh.dll.a
│   └── include/        # 头文件
│       └── libssh/
├── openssl-deps/        # OpenSSL/zlib 依赖（保留）
└── build_libssh.ps1     # 编译脚本
```

## 依赖说明

libssh 依赖以下库（已包含在 openssl-deps 中）：

| 库 | 版本 | 说明 |
| :--- | :--- | :--- |
| OpenSSL | 3.x | 加密库 |
| zlib | 1.x | 压缩库 |

## 更新 NTerminal.pro

编译完成后，NTerminal.pro 会自动使用新编译的 libssh：

```qmake
win32 {
    LIBSSH_ROOT = $$PWD/libssh
    INCLUDEPATH += $$LIBSSH_ROOT/include
    LIBS += -L$$LIBSSH_ROOT/lib -lssh
    LIBS += -lws2_32
}
```

## 常见问题

### 1. CMake 找不到 OpenSSL

确保 `openssl-deps` 目录存在且包含 OpenSSL 头文件和库文件。

### 2. 编译错误：找不到 zlib

检查 `openssl-deps/lib` 目录是否包含 `libz.dll.a`。

### 3. 运行时错误：找不到 DLL

将以下 DLL 复制到可执行文件目录：
- `libssh/bin/ssh.dll`
- `openssl-deps/bin/libcrypto-*.dll`
- `openssl-deps/bin/libssl-*.dll`
- `openssl-deps/bin/zlib*.dll`

### 4. MinGW 版本不兼容

确保使用的 MinGW 版本与 Qt Creator 使用的版本一致。

## 手动编译步骤

如果脚本无法正常工作，可以手动执行以下步骤：

```cmd
# 1. 克隆源码
git clone --depth 1 --branch libssh-0.10.6 https://git.libssh.org/projects/libssh.git libssh-src

# 2. 创建构建目录
mkdir libssh-build
cd libssh-build

# 3. 配置 CMake
cmake -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX=../libssh ^
    -DOPENSSL_ROOT_DIR=../openssl-deps ^
    -DWITH_EXAMPLES=OFF ^
    -DWITH_TESTING=OFF ^
    -DWITH_SERVER=OFF ^
    ../libssh-src

# 4. 编译
mingw32-make -j8

# 5. 安装
mingw32-make install
```

## 版本信息

| 组件 | 版本 |
| :--- | :--- |
| libssh | 0.10.6 |
| OpenSSL | 3.x |
| zlib | 1.x |

## 参考资料

- [libssh 官方文档](https://api.libssh.org/stable/)
- [libssh GitHub](https://github.com/libssh/libssh)
- [CMake 文档](https://cmake.org/documentation/)
