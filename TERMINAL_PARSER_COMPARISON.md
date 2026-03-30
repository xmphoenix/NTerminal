# 终端解析器功能对比分析

## PuTTY vs NTerminal 终端解析器对比

### 1. 状态机架构

#### PuTTY
- 完整的 VT500 系列状态机
- 状态包括：ground, escape, escape_intermediate, csi_entry, csi_param, csi_intermediate, csi_ignore, dcs_entry, dcs_param, dcs_intermediate, dcs_passthrough, dcs_ignore, osc_string, sos_pm_apc_string
- 严格遵循 DEC ANSI 解析器规范

#### NTerminal (当前实现)
- 简化的状态机
- 状态包括：StateGround, StateEscape, StateCsiEntry, StateCsiParam, StateCsiIntermediate, StateCsiIgnore, StateOscString, StateDcsEntry, StateDcsParam, StateDcsIntermediate, StateDcsPassthrough, StateDcsIgnore, StateSosPmApcString, StateUtf8
- **状态定义完整，但部分状态未实现处理逻辑**

**差距：**
- ✅ 状态定义完整
- ❌ DCS (Device Control String) 状态未实现处理
- ❌ SOS/PM/APC 状态未实现处理
- ❌ Escape intermediate 状态未实现

---

### 2. CSI (Control Sequence Introducer) 命令支持

#### PuTTY 支持的 CSI 命令
- 光标移动：CUU, CUD, CUF, CUB, CNL, CPL, CHA, CUP, HVP, VPA
- 编辑：ICH, DCH, IL, DL, ECH, ED, EL
- 滚动：SU, SD, DECSTBM (设置滚动区域)
- 字符属性：SGR (包括 256 色、真彩色)
- 模式设置：SM, RM, DECSET, DECRST
- 设备查询：DSR, DA, DA2, DA3
- 制表符：HTS, TBC, CHT, CBT
- 其他：REP, CPR, DECSLRM, DECIC, DECDC

#### NTerminal (当前实现)
- ✅ 光标移动：A (CUU), B (CUD), C (CUF), D (CUB), H (CUP)
- ✅ 编辑：J (ED), K (EL), L (IL), M (DL)
- ✅ 滚动：S (SU), T (SD), r (DECSTBM)
- ✅ 字符属性：m (SGR) - 支持 256 色和真彩色
- ✅ 模式设置：h (SM), l (RM), ?h (DECSET), ?l (DECRST)
- ✅ 设备查询：n (DSR), c (DA), >c (DA2)
- ❌ 制表符命令未实现
- ❌ ICH, DCH, ECH 未实现
- ❌ CNL, CPL, CHA, VPA 未实现
- ❌ REP, DECSLRM, DECIC, DECDC 未实现

**差距：**
- 基本命令已实现 (约 60%)
- 缺少高级编辑命令 (ICH, DCH, ECH)
- 缺少制表符处理
- 缺少一些光标定位命令

---

### 3. 私有模式 (DECSET/DECRST) 支持

#### PuTTY 支持的私有模式
- ?1: 应用光标键模式 (DECCKM)
- ?3: 132 列模式 (DECCOLM)
- ?5: 反向视频模式 (DECSCNM)
- ?6: 原点模式 (DECOM)
- ?7: 自动换行模式 (DECAWM)
- ?12: 光标闪烁 (att610)
- ?25: 显示/隐藏光标 (DECTCEM)
- ?47: 备用屏幕缓冲区
- ?1000: X11 鼠标报告
- ?1002: 按钮事件鼠标跟踪
- ?1003: 任意事件鼠标跟踪
- ?1004: 焦点事件报告
- ?1005: UTF-8 鼠标模式
- ?1006: SGR 鼠标模式
- ?1047: 备用屏幕缓冲区 (改进版)
- ?1048: 保存/恢复光标
- ?1049: 备用屏幕 + 光标保存 (vim 使用)
- ?2004: 括号粘贴模式

#### NTerminal (当前实现)
- ✅ 信号发送到 setPrivateMode/resetPrivateMode
- ❌ 具体模式处理在 TerminalWidget 中，需要检查实现

**差距：**
- 需要检查 TerminalWidget 中的私有模式实现
- 特别是 ?1049 (备用屏幕) 对日志记录很重要

---

### 4. 字符集处理

#### PuTTY
- 支持多种字符集：UTF-8, ISO-8859-*, CP437, CP850, 等
- 支持 G0/G1/G2/G3 字符集切换
- 支持 DEC 特殊图形字符集
- 支持单字节/双字节字符集

#### NTerminal (当前实现)
- ✅ UTF-8 支持 (简化实现)
- ❌ 字符集切换未实现
- ❌ DEC 特殊图形字符集未实现
- ❌ 双字节字符集未实现

**差距：**
- UTF-8 基本支持已有
- 缺少字符集切换功能
- 缺少 DEC 图形字符支持 (绘制表格边框等)

---

### 5. OSC (Operating System Command) 支持

#### PuTTY 支持的 OSC 命令
- OSC 0/2: 设置窗口标题
- OSC 1: 设置图标名称
- OSC 4: 修改调色板颜色
- OSC 10/11/12: 设置前景/背景/光标颜色
- OSC 52: 剪贴板操作
- OSC 104: 重置调色板颜色
- OSC 110/111/112: 重置前景/背景/光标颜色

#### NTerminal (当前实现)
- ✅ OSC 0/2: 设置窗口标题
- ❌ OSC 10/11/12: 颜色设置 (已识别但未实现)
- ❌ OSC 52: 剪贴板操作未实现
- ❌ 其他 OSC 命令未实现

**差距：**
- 基本窗口标题支持已有
- 缺少剪贴板集成 (OSC 52)
- 缺少动态颜色修改

---

### 6. DCS (Device Control String) 支持

#### PuTTY
- 支持 SIXEL 图形
- 支持 ReGIS 图形
- 支持用户定义键
- 支持终端状态查询

#### NTerminal (当前实现)
- ❌ DCS 状态已定义但未实现任何处理

**差距：**
- 完全缺失 DCS 支持
- 对于基本终端功能影响不大
- 对于图形显示功能有影响

---

### 7. 错误处理和鲁棒性

#### PuTTY
- 严格的状态机转换
- 对于无效序列有明确的忽略状态
- 对于不完整序列有超时处理
- 对于恶意输入有防护

#### NTerminal (当前实现)
- ✅ 基本的状态机转换
- ✅ CSI ignore 状态
- ❌ 缺少超时处理
- ❌ 缺少输入验证

**差距：**
- 基本错误处理已有
- 缺少超时机制
- 缺少输入长度限制

---

### 8. 性能优化

#### PuTTY
- 批量处理字符
- 屏幕更新优化
- 滚动优化
- 脏区域跟踪

#### NTerminal (当前实现)
- ✅ 逐字节处理
- ❌ 缺少批量优化
- ❌ 缺少屏幕更新优化

**差距：**
- 性能优化空间较大
- 对于大量数据输出可能有性能问题

---

## 优先级建议

### 高优先级 (影响基本功能)
1. ✅ **备用屏幕缓冲区检测** - 已在日志记录中实现
2. **ICH/DCH/ECH 命令** - 影响文本编辑
3. **制表符处理** - 影响文本对齐
4. **字符集切换基础支持** - 影响特殊字符显示

### 中优先级 (改善用户体验)
5. **OSC 52 剪贴板集成** - 改善复制粘贴体验
6. **鼠标报告模式** - 支持鼠标交互
7. **光标显示/隐藏** - 改善视觉效果
8. **DEC 图形字符集** - 支持表格边框绘制

### 低优先级 (高级功能)
9. **DCS 支持** - 图形显示
10. **性能优化** - 大数据量处理
11. **输入验证和超时** - 安全性增强

---

## 总结

### 当前实现的优点
- ✅ 核心 CSI 命令支持完整
- ✅ UTF-8 支持
- ✅ 256 色和真彩色支持
- ✅ 基本的状态机架构
- ✅ OSC 窗口标题支持

### 主要差距
- ❌ 高级编辑命令 (ICH, DCH, ECH)
- ❌ 制表符处理
- ❌ 字符集切换
- ❌ DCS 支持
- ❌ 剪贴板集成
- ❌ 鼠标报告
- ❌ 性能优化

### 建议
对于日志记录功能，当前实现已经足够。建议优先实现：
1. ICH/DCH/ECH 命令 - 改善编辑体验
2. 制表符处理 - 改善文本对齐
3. OSC 52 剪贴板 - 改善复制粘贴

对于完整的终端模拟器，建议参考 PuTTY 的实现，逐步完善各项功能。
