# OBS Plugin Template

## 构建要求

- Windows 10/11 x64
- Visual Studio 2022
- CMake 3.28+
- [Inno Setup 6](https://jrsoftware.org/isinfo.php)

## 构建步骤

### 1. 配置项目

```bash
cmake --preset windows-x64
```

### 2. 编译项目

```bash
cmake --build build_x64 --config Release
```

### 3. 打包安装程序

```bash
cmake --build build_x64 --target package --config Release
```

输出文件位于 `release/Output/` 目录。

## 备选方案

也可以使用 PowerShell 脚本进行打包：

```powershell
.\build-aux\build-installer.ps1 -Config Release
```

支持的参数：
- `-Config`: 构建配置 (Release, RelWithDebInfo, Debug)
- `-SkipBuild`: 跳过编译步骤
- `-Clean`: 清理后重新构建
