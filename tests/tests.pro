# NTerminal 单元测试工程（Qt Test）
#
# 目录规范：每个被测模块一个子目录，目录内 <目录名>.pro 为该模块测试工程，
# 测试实现为 tst_<模块>.cpp，目标名 tst_<模块>。
#
# 构建：qmake tests.pro && mingw32-make
# 运行：mingw32-make check   （依次运行全部测试）
#
# 模块划分与被测对象：
#   app      src/app/launchoptions            进程模式命令行解析
#   common   src/common                       IPC 消息契约 + 连接设置序列化
#   ipc      src/ipc/ipcprotocol              帧编解码与粘包缓冲
#   parser   src/terminal/terminalparser      DEC ANSI/VT 状态机
#   proxy    src/terminal/terminalproxy       屏幕模型（数据注入 + 状态查询）
#   session  src/session/connectionhandler    连接处理器接口契约
TEMPLATE = subdirs
SUBDIRS = \
    app \
    common \
    ipc \
    parser \
    proxy \
    session
