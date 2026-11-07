# 自动构建与下载产物

本仓库是 AsusSMC 的 X515EP 本地适配分支，保留上游 MIT 许可证与作者署名。构建成功只表示源码通过编译、打包和主机测试，**不等于已通过 macOS 实机测试**。

## GitHub Actions

工作流：`.github/workflows/build.yml`，名称 **Build AsusSMC**。

触发方式：

- 推送提交到任意分支或标签；
- 创建或更新 Pull Request；
- 在默认分支合并工作流后，从 Actions 页面手动选择 **Run workflow**。

流水线先在 Ubuntu 上运行不访问硬件的 C++ 策略和 Python 构建工具测试，再使用 `macos-15-intel` 分别构建 **Debug** 和 **Release** 的 x86_64 驱动。

### 下载

打开 `https://github.com/jdnjk/AsusSMC/actions`，选择成功运行，在页面下方 **Artifacts** 下载：

- `AsusSMC-Debug-<提交 SHA>`：调试驱动包；
- `AsusSMC-Release-<提交 SHA>`：发布构建驱动包；
- `AsusSMC-<配置>-logs-<提交 SHA>`：完整构建日志、工具链和依赖记录。构建失败时也会尝试上传。

驱动附件保存 **30 天**，日志保存 **14 天**。下载 Actions 附件通常需要登录 GitHub。附件外层由 GitHub 打包，里面的 `AsusSMC-1.5.0-<配置>-<提交>.zip` 才是保留 `.kext` 目录结构的驱动压缩包。

随附文件：

- `SHA256SUMS.txt`：驱动 ZIP 和元数据的校验值；
- `dependencies.lock.json`：固定的 SDK 版本、提交和 ZIP 校验值；
- `toolchain.txt`：Xcode 和 macOS SDK 版本；
- `build-info.txt`：源码提交、配置、架构与未进行硬件验证的说明；
- `LICENSE.md`：原项目的 MIT 许可证。

当前工作流不创建 GitHub Release，也不包含完整 EFI、RogControl 应用或本机配置。

## macOS 本地构建

需要完整 Xcode、Command Line Tools、Git 和 Python 3。只构建、不安装：

```bash
bash build.sh Release
bash build.sh Debug
bash build.sh all
```

运行硬件无关测试：

```bash
bash build.sh test
```

输出目录为 `build/dist/Release/` 和 `build/dist/Debug/`。没有 macOS/Xcode 时，驱动构建以退出码 **77** 明确跳过，不会伪报成功。

### 依赖布局修复

不再把 Lilu/VirtualSMC 的源码仓库当成 `.kext` 开发包：

- MacKernelSDK 固定到 `Scripts/dependencies.lock.json` 里的完整 Git 提交；
- Lilu **1.7.2 DEBUG** 和 VirtualSMC **1.3.8 DEBUG** 使用官方 Release ZIP；
- 下载前后校验固定 SHA-256，并检查实际头文件路径；
- 依赖保存在 `.build-deps/`，通过符号链接提供 Xcode 所需的 `MacKernelSDK/`、`Lilu.kext/`、`VirtualSMC.kext/`；
- CI 直连官方 GitHub，不需要第三方代理，不执行旧 `bootstrap.sh` 的远程 `eval`。

若这些路径已经有旧源码克隆或手工 SDK，脚本会拒绝覆盖并提示先备份/改名。**不会自动 `rm -rf` 现有依赖目录。** 固定依赖变更后，旧的受管符号链接同样需要人工核对再调整。

## 部署边界

产物是未签名的 OpenCore 注入用 kext，不是已公证的系统安装包。仍需匹配版本的 Lilu 与 VirtualSMC；本地应用依赖本扩展的 ABI 5。

- 不要同时注入 AsusSMC 和此前试改的 AsusWmi，避免两个驱动争用 `_WED` 事件。
- X515EP 模式支持固件预设、50–100% 充电请求和 Fn Lock；不提供功耗限制、手动风扇曲线或灯效写入。
- F7/F11 捕获、摄像头真正的硬件开关以及 GPU 温度来源，不因 CI 编译成功就视为已验证。
- 本构建脚本不刷 BIOS、不执行硬件测试、不修改 EFI/ESP、不安装或启动任何守护进程。
- 原工程仍会编译其既有 daemon 依赖目标，但本工作流只打包驱动，不安装或启动该 daemon。
