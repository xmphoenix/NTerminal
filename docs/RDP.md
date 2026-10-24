# RDP 远程桌面

NTerminal 的「新建会话」支持 **RDP** 协议，使用 Windows 自带的 Remote Desktop ActiveX 控件（MsTscAx）把远端桌面嵌入主窗口标签页，不打开外部 `mstsc` 窗口。

## 新建 RDP 会话

1. 点击「新建会话」
2. 连接类型选择 **RDP**
3. 填写：
   - 主机：例如 `192.168.71.120`
   - 端口：默认 `3389`
   - 用户名、密码
   - 域：可选（Windows 域账号填写）
4. 按服务器类型选择是否勾选 **使用 NLA / CredSSP**：
   - xrdp / 多数 Linux RDP 服务：不勾选
   - Windows 域服务器且要求网络级认证：勾选
5. 确定后当前标签页会显示远端桌面

## 行为

- 连接成功后状态栏显示 `RDP · user@host:port`，标签页状态点随连接状态变化
- RDP 标签页内不再使用终端命令输入；点击画面即可操作远端桌面
- 关闭标签页会断开 RDP 会话
- 使用 ActiveX 控件版本 12；系统缺少时回退到版本 11
- 证书警告被设置为自动继续（`AuthenticationLevel = 0`），适用于 xrdp 常见的自签名证书

## 限制

- RDP 仅支持 Windows 客户端（依赖 Microsoft Remote Desktop ActiveX）
- 当前为单桌面嵌入模式，不支持多显示器、全屏独占、动态分辨率协商失败后的手动恢复
- RDP 密码随会话保存在 `sessions.json` 中（与现有 SSH/Telnet 会话密码行为一致）
- 若服务器要求 NLA 而未勾选，连接会失败；反之 xrdp 勾选 NLA 通常会失败
- ActiveX 控件使用系统的 RDP 客户端组件，系统需保留「远程桌面连接」功能

## 故障排查

- **连接立即断开 / 无画面**：确认端口 3389 可访问；xrdp 服务器不要勾选 NLA；Windows 域服务器勾选 NLA
- **证书错误**：当前会自动忽略服务器证书警告；仍失败时检查服务器 RDP 配置
- **密码错误**：清除密码后重新输入，或直接连接让控件提示凭据
- **显示黑屏**：xrdp 会话可能正在启动，等待数秒；确认远端会话未被其他客户端独占

## 相关实现

- `src/ui/rdpwidget.cpp` — ActiveX 控件创建、连接参数和事件处理
- `src/ui/mainwindow.cpp` — RDP 标签页生命周期、状态栏
- `src/common/connectionsettings.h` — RDP 类型、域、NLA 与会话持久化
