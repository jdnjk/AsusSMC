# AsusSMC

> **X515EP adaptation / work in progress.** This fork adds retained performance intent with battery fallback and AC restoration, 50–100% charge requests, Fn Lock and a capability-gated ABI 5 app interface. No custom fan curves, power limits or lighting protocol is enabled for this target. CI compilation is not hardware validation. The matching control app is maintained separately and is not bundled here.

[![Build AsusSMC](https://github.com/jdnjk/AsusSMC/actions/workflows/build.yml/badge.svg)](https://github.com/jdnjk/AsusSMC/actions/workflows/build.yml)

## 自动构建 / Build artifacts

Push、Pull Request 和手动运行会触发 GitHub Actions，在 macOS Intel runner 上显式选择 **Xcode 26 / macOS 26（Tahoe）SDK**，构建 **Debug / Release**，并额外构建 **BootBalanced** AsusSMC.kext ZIP。BootBalanced 变体在驱动启动时请求 Balanced；应用启动后如有已保存模式，会通过 ABI 请求改回该模式。普通 Debug/Release 保持原有继承固件模式的行为。最低部署目标为 10.13，不是最高版本上限；**Tahoe SDK 编译不等于 Tahoe 实机已验证**。运行页面的 **Artifacts** 提供驱动包、SHA-256 校验值和构建日志；失败时仍上传诊断日志。保留上游许可证，不上传本机 EFI、配置或 SDK 二进制。

完整说明：[构建与下载指南](docs/BUILD.zh-CN.md)。

```bash
bash build.sh test     # hardware-free host tests
bash build.sh Release       # macOS + Xcode; pinned, verified SDKs downloaded as needed
bash build.sh BootBalanced # Release build that requests Balanced on driver startup
bash build.sh all           # Debug and Release; no installation
```

Outputs: `build/dist/<configuration>/`. Dependencies are pinned in `Scripts/dependencies.lock.json`; Lilu/VirtualSMC use official DEBUG release bundles, not source trees masquerading as kexts. No third-party download proxy or remote shell `eval` is required.

## Original upstream project

The badges and credits below refer to the original upstream, not to releases of this experimental fork.

[![Github release](https://img.shields.io/github/release/hieplpvip/AsusSMC.svg?color=blue)](https://github.com/hieplpvip/AsusSMC/releases/latest)
[![Github downloads](https://img.shields.io/github/downloads/hieplpvip/AsusSMC/total.svg?color=blue)](https://github.com/hieplpvip/AsusSMC/releases)
[![Build Status](https://travis-ci.com/hieplpvip/AsusSMC.svg?branch=master)](https://travis-ci.com/hieplpvip/AsusSMC)
[![Scan Status](https://scan.coverity.com/projects/18304/badge.svg)](https://scan.coverity.com/projects/18304)
[![Gitter chat](https://img.shields.io/gitter/room/nwjs/nw.js.svg?colorB=ed1965)](https://gitter.im/hieplpvip/AsusSMC)
[![Donate with PayPal](https://img.shields.io/badge/paypal-donate-red.svg)](https://paypal.me/lebhiep)

A VirtualSMC plugin provides native support for ALS, keyboard backlight and Fn keys for Asus laptops on macOS.

#### Features
- Full Fn keys support
- Native ALS support
- Native keyboard backlight support (16 levels, smooth transition, auto adjusting, auto turning off)
- Battery Health Charging

#### Requirements
- Asus laptop with ATK device
- Knowing how to patch DSDT (if not, read [this](https://www.tonymacx86.com/threads/guide-patching-laptop-dsdt-ssdts.152573/))

#### Boot arguments
- Add `-asussmcdbg` to enable debug printing (available in DEBUG binaries).

#### How to install
- Instruction is available in the Wiki.

#### Credits
- [Apple](https://www.apple.com) for macOS
- [vit9696](https://github.com/vit9696) for [Lilu](https://github.com/acidanthera/Lilu) and [VirtualSMC](https://github.com/acidanthera/VirtualSMC)
- [lvs1974](https://github.com/lvs1974) and [usr-sse2](https://github.com/usr-sse2) for developing ambient light sensor support
- [EMlyDinEsHMG](https://osxlatitude.com/profile/7370-emlydinesh/) for [AsusNBFnKeys source code](https://github.com/EMlyDinEsHMG/AsusNBFnKeys)
- [Objective-See](https://objective-see.com) for [kernel-userspace communication](https://objective-see.com/blog/blog_0x0B.html)
- [tekezo](https://github.com/tekezo) for [VirtualHIDKeyboard](https://github.com/pqrs-org/Karabiner-VirtualHIDDevice/)
- [williambj1](https://github.com/williambj1) for testing HID support
