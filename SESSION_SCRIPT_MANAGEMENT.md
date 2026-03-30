# 会话级脚本管理设计

## 需求分析

### 核心需求

1. **每个会话独立运行脚本**
   - 每个终端会话（标签页）可以独立运行一个 Python 脚本
   - 不同会话的脚本互不干扰
   - 同一个会话只能运行一个脚本

2. **脚本状态可见**
   - 显示当前会话是否运行了脚本
   - 显示正在运行的脚本名称和路径
   - 显示脚本运行状态（运行中、已停止、错误）

3. **灵活的脚本选择**
   - 可以在会话连接后随时加载脚本
   - 可以选择任意位置的脚本文件
   - 可以停止当前脚本并加载新脚本

## 架构设计

### 1. 会话与脚本的关系

```
┌─────────────────────────────────────────────┐
│           MainWindow                        │
│  ┌───────────────────────────────────────┐  │
│  │  TabWidget (会话标签页)              │  │
│  │  ┌─────────────┐  ┌─────────────┐    │  │
│  │  │  Session 1  │  │  Session 2  │    │  │
│  │  │  ┌────────┐ │  │  ┌────────┐ │    │  │
│  │  │  │Terminal│ │  │  │Terminal│ │    │  │
│  │  │  │Widget  │ │  │  │Widget  │ │    │  │
│  │  │  └────────┘ │  │  └────────┘ │    │  │
│  │  │  ┌────────┐ │  │  ┌────────┐ │    │  │
│  │  │  │Script  │ │  │  │Script  │ │    │  │
│  │  │  │Engine  │ │  │  │Engine  │ │    │  │
│  │  │  └────────┘ │  │  └────────┘ │    │  │
│  │  │  script1.py │  │  script2.py │    │  │
│  │  └─────────────┘  └─────────────┘    │  │
│  └───────────────────────────────────────┘  │
│                                             │
│  [脚本控制工具栏]                           │
│  [加载脚本] [停止脚本] [脚本状态]           │
└─────────────────────────────────────────────┘
```

### 2. UI 组件设计

#### 2.1 脚本控制工具栏

```
┌──────────────────────────────────────────────┐
│ [▶ 加载脚本] [⏹ 停止脚本] [📄 脚本日志]    │
│                                              │
│ 状态: ● 运行中 - auto_login.py              │
└──────────────────────────────────────────────┘
```

#### 2.2 标签页状态指示

```
┌─────────────────────────────────────────┐
│ [192.168.1.1 🐍] [localhost] [server2] │
└─────────────────────────────────────────┘
     ↑
     脚本运行指示器
```

#### 2.3 脚本选择对话框

```
┌─────────────────────────────────────────┐
│  选择 Python 脚本                       │
├─────────────────────────────────────────┤
│  最近使用:                              │
│  • auto_login.py                        │
│  • log_monitor.py                       │
│  • test_automation.py                   │
├─────────────────────────────────────────┤
│  内置脚本:                              │
│  • 自动登录脚本                         │
│  • 日志监控脚本                         │
│  • 自动化测试脚本                       │
│  • 简单示例脚本                         │
├─────────────────────────────────────────┤
│  [浏览文件...]                          │
├─────────────────────────────────────────┤
│  脚本路径: /path/to/script.py           │
│                                         │
│  [确定]  [取消]                         │
└─────────────────────────────────────────┘
```

#### 2.4 脚本日志查看器

```
┌─────────────────────────────────────────┐
│  脚本日志 - auto_login.py               │
├─────────────────────────────────────────┤
│  [INFO] Script started                  │
│  [INFO] Connected to server             │
│  [INFO] Detected login prompt           │
│  [INFO] Sent username                   │
│  [INFO] Detected password prompt        │
│  [INFO] Sent password                   │
│  [INFO] Login successful!               │
├─────────────────────────────────────────┤
│  [清空日志]  [导出日志]  [关闭]        │
└─────────────────────────────────────────┘
```

### 3. 菜单结构

```
文件(F)
  新建会话
  打开会话
  保存会话
  ---
  退出

编辑(E)
  复制
  粘贴
  ---
  设置

脚本(S)  ← 新增
  加载脚本...        Ctrl+L
  停止脚本          Ctrl+K
  重新加载脚本      Ctrl+R
  ---
  脚本日志...
  ---
  内置脚本 >
    自动登录脚本
    日志监控脚本
    自动化测试脚本
    简单示例脚本
  ---
  打开脚本目录

帮助(H)
  关于
```

### 4. 数据结构

#### 4.1 会话脚本状态

```cpp
struct SessionScriptState {
    QString scriptPath;           // 脚本文件路径
    QString scriptName;           // 脚本文件名
    bool isRunning;               // 是否运行中
    QDateTime startTime;          // 启动时间
    QStringList logMessages;      // 日志消息
    int exitCode;                 // 退出码（如果已停止）
};
```

#### 4.2 脚本配置

```cpp
struct ScriptConfig {
    QString name;                 // 显示名称
    QString description;          // 描述
    QString path;                 // 脚本路径
    bool isBuiltin;               // 是否内置脚本
    QDateTime lastUsed;           // 最后使用时间
};
```

### 5. 实现方案

#### 5.1 TerminalWidget 扩展

```cpp
class TerminalWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    // 脚本管理
    bool loadScript(const QString &scriptPath);
    void stopScript();
    bool isScriptRunning() const;
    QString currentScriptPath() const;
    QString currentScriptName() const;
    QDateTime scriptStartTime() const;
    QStringList scriptLogMessages() const;

signals:
    void scriptStateChanged();  // 脚本状态改变
    void scriptStarted(const QString &scriptPath);
    void scriptStopped(int exitCode);
    void scriptError(const QString &error);
    void scriptLogMessage(const QString &level, const QString &message);

private:
    PythonScriptEngine *m_scriptEngine;
    SessionScriptState m_scriptState;
};
```

#### 5.2 ScriptDialog 类

```cpp
class ScriptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptDialog(QWidget *parent = nullptr);

    QString selectedScriptPath() const;
    void setRecentScripts(const QStringList &scripts);

private slots:
    void onBrowseClicked();
    void onBuiltinScriptClicked(const QString &scriptPath);
    void onRecentScriptClicked(const QString &scriptPath);

private:
    QLineEdit *m_pathEdit;
    QListWidget *m_recentList;
    QListWidget *m_builtinList;
    QString m_selectedPath;
};
```

#### 5.3 ScriptLogViewer 类

```cpp
class ScriptLogViewer : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptLogViewer(QWidget *parent = nullptr);

    void addLogMessage(const QString &level, const QString &message);
    void clearLog();
    void exportLog(const QString &filePath);

private:
    QTextEdit *m_logText;
    QList<QPair<QString, QString>> m_logMessages;  // level, message
};
```

#### 5.4 MainWindow 扩展

```cpp
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 脚本控制
    void loadScriptForCurrentSession();
    void stopScriptForCurrentSession();
    void showScriptLog();
    void reloadScript();

private slots:
    void onLoadScriptAction();
    void onStopScriptAction();
    void onScriptLogAction();
    void onReloadScriptAction();
    void onBuiltinScriptAction(const QString &scriptName);
    void onCurrentSessionChanged(int index);
    void onScriptStateChanged();

private:
    void createScriptMenu();
    void createScriptToolBar();
    void updateScriptActions();
    void updateTabScriptIndicator(int tabIndex);

    QMenu *m_scriptMenu;
    QToolBar *m_scriptToolBar;
    QAction *m_loadScriptAction;
    QAction *m_stopScriptAction;
    QAction *m_scriptLogAction;
    QAction *m_reloadScriptAction;
    QLabel *m_scriptStatusLabel;

    ScriptLogViewer *m_scriptLogViewer;
    QStringList m_recentScripts;
};
```

### 6. 工作流程

#### 6.1 加载脚本流程

```
用户点击"加载脚本"
    ↓
显示脚本选择对话框
    ↓
用户选择脚本文件
    ↓
检查当前会话是否已运行脚本
    ↓
如果是，提示是否停止当前脚本
    ↓
调用 TerminalWidget::loadScript()
    ↓
PythonScriptEngine 启动 Python 进程
    ↓
更新会话脚本状态
    ↓
更新标签页指示器
    ↓
更新工具栏状态
    ↓
发送 "connected" 事件到脚本
```

#### 6.2 停止脚本流程

```
用户点击"停止脚本"
    ↓
调用 TerminalWidget::stopScript()
    ↓
PythonScriptEngine 发送 exit 消息
    ↓
等待进程结束（最多 3 秒）
    ↓
如果超时，强制终止进程
    ↓
更新会话脚本状态
    ↓
更新标签页指示器
    ↓
更新工具栏状态
```

#### 6.3 切换会话流程

```
用户切换标签页
    ↓
获取新会话的脚本状态
    ↓
更新工具栏按钮状态
    ↓
更新脚本状态标签
    ↓
如果有脚本日志查看器打开
    ↓
切换到新会话的日志
```

### 7. 配置存储

#### 7.1 最近使用的脚本

存储在 `~/.nterminal/recent_scripts.json`:

```json
{
  "recent_scripts": [
    {
      "path": "/path/to/auto_login.py",
      "name": "auto_login.py",
      "last_used": "2024-01-15T10:30:00"
    },
    {
      "path": "/path/to/log_monitor.py",
      "name": "log_monitor.py",
      "last_used": "2024-01-14T15:20:00"
    }
  ]
}
```

#### 7.2 会话脚本关联

存储在会话配置中:

```json
{
  "session": {
    "name": "Production Server",
    "host": "192.168.1.1",
    "auto_load_script": true,
    "script_path": "/path/to/auto_login.py"
  }
}
```

### 8. 快捷键

- `Ctrl+L` - 加载脚本
- `Ctrl+K` - 停止脚本
- `Ctrl+R` - 重新加载脚本
- `Ctrl+Shift+L` - 显示脚本日志

### 9. 状态指示

#### 9.1 标签页图标

- 🐍 - 脚本运行中
- ⚠️ - 脚本错误
- 无图标 - 未运行脚本

#### 9.2 状态栏显示

```
● 运行中: auto_login.py (已运行 00:05:23)
○ 未运行脚本
⚠ 脚本错误: script.py (语法错误)
```

### 10. 错误处理

#### 10.1 脚本加载失败

```
┌─────────────────────────────────────┐
│  ⚠ 脚本加载失败                    │
├─────────────────────────────────────┤
│  无法加载脚本: auto_login.py        │
│                                     │
│  错误: Python 可执行文件未找到      │
│                                     │
│  请确保已安装 Python 3.x            │
│                                     │
│  [确定]                             │
└─────────────────────────────────────┘
```

#### 10.2 脚本运行错误

```
┌─────────────────────────────────────┐
│  ⚠ 脚本运行错误                    │
├─────────────────────────────────────┤
│  脚本: auto_login.py                │
│                                     │
│  错误: 语法错误 (第 15 行)          │
│                                     │
│  [查看日志]  [停止脚本]  [关闭]    │
└─────────────────────────────────────┘
```

### 11. 内置脚本管理

内置脚本位于 `scripts/` 目录:

```
scripts/
├── nterminal.py          # API 模块
├── README.md             # 使用文档
├── builtin/              # 内置脚本
│   ├── auto_login.py
│   ├── log_monitor.py
│   ├── test_automation.py
│   └── simple_example.py
└── user/                 # 用户脚本（可选）
    └── my_script.py
```

### 12. 实现优先级

#### 高优先级
1. ✅ PythonScriptEngine 基础功能
2. ⏳ ScriptDialog 脚本选择对话框
3. ⏳ MainWindow 脚本菜单和工具栏
4. ⏳ 标签页脚本状态指示

#### 中优先级
5. ⏳ ScriptLogViewer 日志查看器
6. ⏳ 最近使用脚本记录
7. ⏳ 会话脚本关联保存

#### 低优先级
8. ⏳ 脚本编辑器集成
9. ⏳ 脚本调试功能
10. ⏳ 脚本性能监控

## 总结

这个设计实现了：
- ✅ 每个会话独立运行脚本
- ✅ 同一会话只能运行一个脚本
- ✅ 脚本状态可见
- ✅ 灵活的脚本选择
- ✅ 完整的用户界面
- ✅ 错误处理和日志记录

用户可以：
1. 在任何时候为当前会话加载脚本
2. 从任意位置选择脚本文件
3. 查看脚本运行状态和日志
4. 停止和重新加载脚本
5. 使用内置脚本或自定义脚本
