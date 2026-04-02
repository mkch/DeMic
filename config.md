# DeMic 配置文件使用指南

## 概述

DeMic 使用 INI 格式的配置文件 `DeMic.ini` 来保存程序设置。配置文件位于可执行文件所在的目录中，程序启动时会自动读取，退出时会自动保存修改。

## 配置文件位置

```text
<DeMic.exe 所在目录>\DeMic.ini
```

## 配置文件格式

DeMic.ini 使用标准的 Windows INI 文件格式：

```ini
[节名称]
键名=值
```

## 配置节和键

### 1. [HotKey] - 全局热键配置

配置用于切换麦克风状态的全局热键。

#### 键值

| 键名 | 说明 | 类型 | 默认值 |
|------|------|------|--------|
| `Value` | 热键组合的编码值 | 整数 | 0（无热键） |

#### 编码格式

`Value` 是一个 16 位整数，编码格式为：

- 低字节（0-7位）：虚拟键码（VK）
- 高字节（8-15位）：修饰键标志

修饰键标志：

- `0x01` (HOTKEYF_SHIFT) - Shift 键
- `0x02` (HOTKEYF_CONTROL) - Ctrl 键
- `0x04` (HOTKEYF_ALT) - Alt 键

#### 配置方式

**推荐方式：** 通过系统托盘菜单的`热键设置`对话框配置，程序会自动保存正确的编码值。

**手动配置示例：**

```ini
[HotKey]
Value=634
```

说明：634 = 0x027A = Ctrl (0x02) + VK_F11 (0x7A)

#### 注意事项

- 如果 `Value=0`，表示未设置热键
- 程序会在启动时验证热键是否可用
- 如果热键已被其他程序占用，将显示错误并清除设置

---

### 2. [Sound] - 提示音配置

配置麦克风开启/关闭时的提示音。

#### 键值

| 键名 | 说明 | 类型 | 默认值 |
| ------ | ------ | ------ |-------- |
| `OnEnable` | 是否启用开启提示音 | 整数（0/1） | 0 |
| `OnPath` | 开启提示音文件路径 | 字符串 | 空 |
| `OffEnable` | 是否启用关闭提示音 | 整数（0/1） | 0 |
| `OffPath` | 关闭提示音文件路径 | 字符串 | 空 |

#### 配置示例

```ini
[Sound]
OnEnable=1
OnPath=C:\Windows\Media\Ring01.wav
OffEnable=1
OffPath=C:\Windows\Media\Ring02.wav
```

#### 提示音文件

- **支持格式：** WAV 文件（推荐）
- **路径：** 使用完整的绝对路径
- **空路径：** 如果路径为空且启用了提示音，将播放系统默认声音
  - 开启音：系统默认声音
  - 关闭音：系统警告声

#### 配置方式

**推荐方式：** 通过系统托盘菜单的`提示音设置`对话框配置。

---

### 3. [Plugin] - 插件配置

配置启用的插件文件列表。

#### 键值

| 键名 | 说明 | 类型 | 默认值 |
|------|------|------|--------|
| `Plugin` | 插件文件列表 | 字符串 | 空 |

#### 配置格式

多个插件文件名使用竖线（`|`）分隔：

```ini
[Plugin]
Plugin=plugin1.plugin|plugin2.plugin|plugin3.plugin
```

#### 插件加载

- 插件文件应位于 `<DeMic.exe 所在目录>\plugin\` 目录
- 程序启动时会按照配置列表加载插件
- 只有在配置文件中列出的插件才会被加载

#### 注意事项

- 插件文件名区分大小写
- 无效的插件文件会被忽略
- 通过系统托盘菜单可以管理插件

---

### 4. [CmdLineArgs] - 命令行参数配置

配置程序在不同场景下使用的默认命令行参数。

#### 键值

| 键名 | 说明 | 类型 | 默认值 |
|------|------|------|--------|
| `CmdLineArgs` | 首次启动时的默认参数 | 字符串 | 空 |
| `CmdLineArgs2` | 已有实例运行时的参数 | 字符串 | 空 |

#### 配置示例

```ini
[CmdLineArgs]
CmdLineArgs=/silent
CmdLineArgs2=/toggle
```

#### 工作原理

1. **无参数启动程序时：**
   - 如果没有实例运行 → 使用 `CmdLineArgs` 的值
   - 如果已有实例运行 → 使用 `CmdLineArgs2` 的值

2. **带参数启动程序时：**
   - 命令行参数优先于配置文件
   - 配置文件参数被忽略

#### 使用场景

**场景 1：静默启动**

```ini
[CmdLineArgs]
CmdLineArgs=/silent
CmdLineArgs2=/toggle
```

- 第一次双击：静默启动程序
- 再次双击：切换麦克风状态

**场景 2：快速切换**

```ini
[CmdLineArgs]
CmdLineArgs=/off
CmdLineArgs2=/toggle
```

- 第一次双击：启动程序并关闭麦克风
- 再次双击：切换麦克风状态

#### 可用参数

- `/silent` 或 `-silent` - 静默模式
- `/on` 或 `-on` - 打开麦克风
- `/off` 或 `-off` - 关闭麦克风
- `/toggle` 或 `-toggle` - 切换状态

详见 [命令行使用说明](cli.md)

---

### 5. [Log] - 日志配置

配置程序的日志记录级别。

#### 键值

| 键名 | 说明 | 类型 | 默认值 |
|------|------|------|--------|
| `LogLevel` | 日志记录级别 | 整数 | 8（Error） |

#### 日志级别

| 级别值 | 级别名称 | 说明 |
|--------|----------|------|
| -4 | Debug | 调试信息（最详细） |
| 0 | Info | 一般信息 |
| 4 | Warn | 警告信息 |
| 8 | Error | 错误信息（默认，最少） |

#### 配置示例

```ini
[Log]
LogLevel=-4
```

#### 日志文件

- **位置：** `<DeMic.exe 所在目录>\Log.txt`
- **格式：** 追加模式，不会自动清空
- **查看：** 可通过系统托盘菜单"帮助 → 显示日志"查看

---

## 完整配置示例

```ini
[HotKey]
Value=1538

[Sound]
OnEnable=1
OnPath=C:\Windows\Media\Windows Notify System Generic.wav
OffEnable=1
OffPath=C:\Windows\Media\Windows Background.wav

[Plugin]
Plugin=ExamplePlugin.plugin

[CmdLineArgs]
CmdLineArgs=/silent
CmdLineArgs2=/toggle

[Log]
LogLevel=8
```

## 配置文件管理

### 自动保存

以下操作会触发配置文件自动保存：

1. 通过系统托盘菜单修改设置时（如热键、提示音、插件等）
2. 程序正常退出时

### 手动编辑

1. 退出 DeMic 程序
2. 使用文本编辑器打开 `DeMic.ini`
3. 修改配置项
4. 保存文件
5. 重新启动 DeMic

> [!WARNING]
> 请在程序关闭后再编辑配置文件。程序关闭时会自动写回配置，运行期间的手动修改将被覆盖。

### 重置配置

#### 方法 1：删除配置文件

- 删除 `DeMic.ini` 文件
- 重启程序，将使用默认设置

#### 方法 2：编辑配置文件

- 删除特定配置节或键
- 该项将恢复默认值

> [!NOTE]
> 配置文件应使用 Unicode (UTF-16 LE) 或 ANSI 编码
