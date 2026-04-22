# libssh 编译相关 Git 提交指南

## 需要提交的文件

执行以下命令添加必要文件到 Git：

```bash
# 必须提交的核心文件
git add build_libssh.ps1       # 编译脚本（核心）
git add NTerminal.pro           # 项目配置文件
git add .gitignore              # Git 忽略规则

# 可选提交的文件
git add build_libssh.bat        # 批处理脚本备用
git add LIBSSH_BUILD_TROUBLESHOOTING.md  # 编译问题文档
git add LIBSSH_BUILD_GUIDE.md   # 编译指南（如有）
```

## 不需要提交的目录（自动忽略）

以下目录已在 `.gitignore` 中配置，编译时自动下载/生成：

| 目录 | 说明 | 自动处理 |
|------|------|----------|
| `libssh-src/` | libssh 源码 | 编译时自动从 git.libssh.org 克隆 |
| `libssh-build/` | 编译临时文件 | 编译时自动创建 |
| `libssh/` | 编译输出产物 | 编译时自动生成 |
| `libssh-prebuilt/` | 预编译 OpenSSL | 脚本自动从 MSYS2 repo 下载 |
| `deps/` | 依赖库源码 | 编译时自动下载 |
| `bin/` | 输出 DLL 目录 | 编译后自动复制 |

## 编译流程

首次编译时，脚本会自动：

1. **下载预编译 OpenSSL/zlib**（约 60MB）
   - 来源：MSYS2 repo (https://repo.msys2.org/mingw/mingw64)
   
2. **克隆 libssh 源码**
   - 版本：0.10.6
   - 来源：https://git.libssh.org/projects/libssh.git

3. **编译安装 libssh**
   - 使用 Qt MinGW 工具链
   - 支持多线程并行编译

## 执行编译

```powershell
# 基本编译
.\build_libssh.ps1

# 指定并行任务数
.\build_libssh.ps1 -ParallelJobs 8

# 清理重新编译
.\build_libssh.ps1 -Clean

# 详细输出模式
.\build_libssh.ps1 -Verbose
```

## 目录结构（编译后）

```
NTerminal/
├── build_libssh.ps1      ✅ 需提交（编译脚本）
├── NTerminal.pro         ✅ 需提交（项目配置）
├── .gitignore            ✅ 需提交（忽略规则）
│
├── libssh-prebuilt/      ❌ 自动下载（预编译 OpenSSL）
│   ├── bin/              # OpenSSL DLL
│   ├── lib/              # OpenSSL 库文件
│   └── include/          # OpenSSL 头文件
│
├── libssh-src/           ❌ 自动克隆（libssh 源码）
├── libssh-build/         ❌ 自动创建（编译临时文件）
├── libssh/               ❌ 自动生成（编译输出）
│   ├── bin/libssh.dll    # libssh 动态库
│   ├── lib/libssh.dll.a  # 导入库
│   └── include/libssh/   # 头文件
│
└── bin/                  ❌ 自动复制（运行时 DLL）
    ├── libssh.dll
    ├── libcrypto-3-x64.dll
    ├── libssl-3-x64.dll
    └── zlib1.dll
```

## 验证提交

```bash
# 查看将要提交的文件
git status

# 确认忽略的文件
git check-ignore -v libssh-src/
git check-ignore -v libssh-prebuilt/
git check-ignore -v libssh/
```

## 团队协作

团队成员克隆代码库后，只需执行：

```powershell
# 1. 运行编译脚本（自动下载所有依赖）
.\build_libssh.ps1

# 2. 在 Qt Creator 中执行 qmake
# 3. 编译项目
```

无需额外配置，所有依赖自动下载。

---

*文档生成时间：2026年4月22日*