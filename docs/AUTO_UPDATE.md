# 自动更新

NTerminal 内置自动更新框架，类似 VS Code 的 “Check for Updates”：

- 帮助 → **检查更新...**：读取远程 `update.json`，比较版本
- 发现新版本：显示更新说明，下载 zip 包并校验 SHA-256
- 安装：无需重新安装。更新程序等待 NTerminal 退出后解压覆盖程序文件，并自动重启
- 用户配置保存在 `~/.nterminal`，不会随程序文件被替换

## 使用

### 1. 配置更新源

设置 → **更新设置...**，填写 `update.json` 的地址，例如：

```
https://example.com/nterminal/update.json
```

- 必须是 HTTPS；本地测试允许 `http://127.0.0.1` 和 `file://`
- 可勾选“启动时自动检查更新”（默认开启）
- 设置保存在 QSettings 的 `Update/FeedUrl`、`Update/AutoCheck`

### 2. 检查并安装

帮助 → **检查更新...**

1. 显示当前版本与远程版本
2. 发现新版本后显示 Markdown 更新说明
3. 点击“下载并安装”，进度条显示下载状态
4. 下载完成后校验 SHA-256
5. 选择：
   - **立即重启**：退出 NTerminal，更新脚本替换文件并重启
   - **下次启动**：写入 `~/.nterminal/pending-update.json`，下次启动 NTerminal 时自动安装

安装过程中不会弹出安装程序，也不会改动 `~/.nterminal` 下的会话、主题、日志等用户数据。

## 更新清单格式

`update.json`：

```json
{
  "version": "2.1.0",
  "published": "2026-10-24T14:30:00Z",
  "notes": "## 更新内容\n\n- 新增自动更新框架\n- 修复若干问题",
  "platforms": {
    "windows-x86_64": {
      "url": "https://example.com/nterminal/NTerminal-2.1.0-win64.zip",
      "sha256": "853aa5c32e15aa80d29c1188ec7f96bd1510af9dc1f480b5e8ea1494fbf56788",
      "size": 1214160
    }
  }
}
```

- `version`：语义化版本（`major.minor.patch`，可带 `-beta.1`）
- `notes`：Markdown，显示在更新对话框中
- `platforms`：按平台选择；当前平台键为 `windows-x86_64` / `linux-x86_64` / `macos-arm64` 等
- `url`：zip 包地址，可为相对路径（相对 `update.json` 解析）
- `sha256`：必填，64 位十六进制；下载后和更新脚本中各校验一次
- `size`：可选，用于进度和大小校验

也可以省略 `platforms`，直接在顶层写 `url` / `sha256` / `size`。

## 打包与发布

仓库提供打包脚本：

```powershell
powershell -ExecutionPolicy Bypass -File tools\package_update.ps1 `
    -Version 2.1.0 `
    -BaseUrl https://example.com/nterminal `
    -Notes "修复若干问题" `
    -DeployQt -QtBin C:\Qt\6.10.2\mingw_64\bin
```

产物：

- `dist\NTerminal-2.1.0-win64.zip`
- `dist\update.json`

说明：

- 脚本只复制程序文件，排除 `debug/`、`*.log`、`received.*` 等运行产物
- `-DeployQt` 会调用 `windeployqt` 把 Qt 运行库和插件放入包内；正式发布建议开启
- 把 zip 和 `update.json` 上传到同一个静态 HTTPS 目录（或 GitHub Release 的固定链接）
- 发布前先在 `.pro` 中修改 `VERSION` 并重新构建，保持可执行文件版本与清单一致

## 安装机制

Windows：

1. 主程序把更新脚本写入 `%TEMP%\NTerminal-update-<uuid>.ps1`
2. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File <script>` 独立启动
3. 脚本等待主进程退出，再次校验 zip 的 SHA-256
4. `Expand-Archive` 解压到临时目录，找到 `NTerminal.exe`，把包内容递归复制到安装目录
5. 删除临时文件、待安装标记，启动新版 `NTerminal.exe`
6. 失败时写入 `~/.nterminal/updater.log` 并尝试重新启动旧版本

Linux/macOS：

- 使用 `sh` 脚本，依赖 `unzip`；其余流程相同
- 更新包需要包含对应平台的 `NTerminal` 可执行文件

启动时若存在 `~/.nterminal/pending-update.json`，主窗口创建前会直接启动更新脚本并退出，由脚本完成替换和重启。

## 安全

- 更新源和下载地址默认只接受 HTTPS（localhost/file 仅用于本地测试）
- zip 的 SHA-256 在下载后校验一次，更新脚本解压前再校验一次
- 拒绝版本回退：只有远程版本高于本地版本才会提示
- 安装目录必须可写；写入测试失败时提示把程序放到用户可写目录或以管理员权限运行
- 更新包不包含用户配置；不会删除 `~/.nterminal`

当前框架使用 HTTPS + SHA-256 保证传输与包完整性，但没有做代码签名/公钥验签。若更新服务器被攻破，攻击者仍可发布恶意包；正式分发建议配合 Authenticode 代码签名和 HTTPS 服务器加固。

## 故障排查

**“未配置更新源”**

- 设置 → 更新设置，填写 `update.json` 地址

**“更新清单中没有当前平台的下载信息”**

- 检查 `platforms` 的键是否与 `windows-x86_64` 等一致

**“更新包校验失败”**

- 重新运行打包脚本，确保 `update.json` 中的 sha256/size 与 zip 一致
- 检查服务器是否改写了 zip（例如错误的内容压缩）

**下载完成但没有安装**

- 选择“下次启动”时会写入 `pending-update.json`，下次启动自动安装
- 查看 `~/.nterminal/updater.log`

**安装目录不可写**

- 把 NTerminal 放到用户目录（便携版），或以管理员权限运行
- 更新程序不会自动提权

## 限制

- 没有增量更新，每次下载完整 zip
- 没有自动回滚；更新脚本复制失败时会重启旧版本并记录日志
- 没有代码签名/公钥验签（见“安全”）
- 需要安装目录在程序退出后可写
