# OBS 小米手环心率插件 (OBS Mi Band Heart Rate Plugin)

这是一个 OBS Studio 插件，用于通过蓝牙连接小米手环（及其他兼容 BLE 的心率设备），并将实时心率显示在直播画面中。

## 主要功能

- **HTML 渲染**：完全摒弃原生 GDI 绘制，使用 Web 技术（HTML/CSS/JS）进行渲染，样式完全可自定义。
- **配置面板**：集成在 OBS 工具菜单中，提供独立的设备扫描与连接界面。
- **自动添加源**：插件加载时会自动检测并创建名为“心率显示 (Heart Rate)”的浏览器源，无需手动配置 URL。
- **自动重连**：记住上次连接的设备，OBS 启动时自动尝试连接。
- **智能断开**：OBS 退出时自动发送停止指令，防止手环卡在广播状态。
- **中文支持**：全中文操作界面。

## 使用方法

1.  **安装插件**：将编译好的插件文件放入 OBS 插件目录。
2.  **启动 OBS**：打开 OBS Studio。
3.  **打开配置**：在顶部菜单栏点击 **“工具” (Tools)** -> **“小米手环心率配置”**。
4.  **连接设备**：
    - 在弹出的网页配置面板中，点击 **“扫描设备”**。
    - 在列表中找到你的手环，点击 **“连接”**。
    - 连接成功后，心率数据将实时更新。
5.  **调整显示**：
    - 场景中会自动出现一个 **“心率显示 (Heart Rate)”** 的浏览器源。
    - 你可以像调整普通源一样调整其大小和位置。
    - 如需修改样式，可编辑插件目录下的 `data/web/style.css` 文件。

## 构建要求

- Windows 10/11 x64
- Visual Studio 2022 (需安装 C++ 桌面开发工作负载)
- CMake 3.28+
- [Inno Setup 6](https://jrsoftware.org/isinfo.php) (用于打包安装程序)

## 构建步骤

### 1. 配置项目

```bash
cmake --preset windows-x64
```

_注意：如果遇到 CMake 错误，尝试删除 `build_x64` 目录后重试。_

### 2. 编译项目

```bash
cmake --build build_x64 --config Release
```

### 3. 打包安装程序

```bash
.\build-aux\build-installer.ps1 -Config Release
```

```bash
cmake --build build_x64 --target package --config Release
```

输出文件位于 `release/Output/` 目录。

## 目录结构说明

- `src/`: C++ 源代码
  - `plugin-main.cpp`: 插件核心逻辑（HTTP 服务器、OBS API 集成）
  - `ble-manager-winrt.cpp`: Windows BLE 通信实现
- `data/web/`: 前端资源文件
  - `index.html`: 心率显示页面
  - `settings.html`: 配置面板页面
  - `style.css`: 样式文件
  - `script.js`: 前端逻辑
