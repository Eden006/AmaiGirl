# 第三方许可证说明 / Third-Party Licenses

## 简体中文

本文档用于列出本项目使用的第三方组件及其许可证/使用条款。

> 说明：本文件仅用于说明第三方依赖与合规边界。

### 1）Qt 6

- **项目**：Qt Framework
- **本项目使用模块（来自当前 CMake 配置）**：
  - Qt6::Core
  - Qt6::Gui
  - Qt6::Widgets
  - Qt6::OpenGL
  - Qt6::OpenGLWidgets
  - Qt6::Network
  - Qt6::Multimedia
- **许可证模型**：开源使用通常基于 LGPL v3（本项目暂未使用基于 GPL 的模块/组件）。
- **上游许可证信息**：
  - https://www.qt.io/development/open-source-lgpl-obligations
  - https://www.gnu.org/licenses/lgpl-3.0.html

#### Qt 合规提示（面向二进制分发）

- 向用户提供 Qt 许可证文本和必要署名信息。
- 不得附加与 LGPL 权利冲突的限制性条款。
- 在 LGPL 场景下，动态链接通常比静态链接更容易满足合规要求。
- 若使用到 GPL-only 的 Qt 组件，分发义务可能升级为 GPL。

### 2）Live2D Cubism SDK 

- **项目**：Cubism SDK for Native
- **下载链接**：
  - https://www.live2d.com/zh-CHS/sdk/about/
- **本项目使用模块**：
  - Cubism Core
- **本项目当前集成方式**：通过 CMake 链接 `libLive2DCubismCore.a`。
- **许可证**：Live2D Proprietary Software License（专有协议，非开源）。
- **中文许可证链接**：
  - https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_cn.html
- **可再分发文件列表**：
  - `CubismSdkForNative-5-r.4.1/Core/RedistributableFiles.txt`

#### Live2D 合规提示

- Live2D 组件受专有条款约束，**不会**因本项目而被再次授权。
- 发布/分发可能需要满足 Live2D 的资格条件或另行协议。
- 仅可分发 Live2D 条款及 `RedistributableFiles.txt` 允许的文件。

### 3）`res/models` 下的 Live2D 模型资源

- `res/models` 中的模型文件与贴图属于第三方素材，可能受单独素材协议约束。
- 其中 `Hiyori` 模型使用需同意《无偿提供素材使用授权协议》：
  - https://www.live2d.com/eula/live2d-free-material-license-agreement_cn.html
- 在公开分发（仓库或 Release）前，请逐项确认模型资源许可；若许可不明确，建议从公开分发中移除并改为用户侧下载。

### 4）权利与许可边界

- 本项目原创源代码：由维护者通过根目录 `LICENSE`（Apache-2.0）授权。
- 本文件列出的第三方组件继续适用其原始许可证/条款。
- 相应商标、著作权及其他权利归各自权利人所有。

---

## English

This document lists third-party components used by this project and their license terms.

> Note: This file documents third-party dependencies and compliance boundaries only.

### 1) Qt 6

- **Project**: Qt Framework
- **Used modules (from current CMake config)**:
  - Qt6::Core
  - Qt6::Gui
  - Qt6::Widgets
  - Qt6::OpenGL
  - Qt6::OpenGLWidgets
  - Qt6::Network
  - Qt6::Multimedia
- **License model**: Open-source usage in this project is typically under LGPL v3 (this project is not currently using GPL-based modules/components).
- **Upstream license information**:
  - https://www.qt.io/development/open-source-lgpl-obligations
  - https://www.gnu.org/licenses/lgpl-3.0.html

#### Qt compliance notes (for binary distribution)

- Keep Qt license texts and attribution notices available to users.
- Do not impose terms that conflict with LGPL rights.
- For LGPL usage, dynamic linking is generally easier to comply with than static linking.
- If any GPL-only Qt component is used, distribution obligations may escalate to GPL.

### 2) Live2D Cubism SDK

- **Project**: Cubism SDK for Native
- **Download page**:
  - https://www.live2d.com/en/sdk/about/
- **Used module in this project**:
  - Cubism Core
- **Current integration in this project**: linked via `libLive2DCubismCore.a` in CMake.
- **License**: Live2D Proprietary Software License (not open-source).
- **English license link**:
  - https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html
- **Redistributable files list**:
  - `CubismSdkForNative-5-r.4.1/Core/RedistributableFiles.txt`

#### Live2D compliance notes

- Live2D components are under proprietary terms and are **not** relicensed by this project.
- Distribution/publishing may require separate eligibility or agreements under Live2D terms.
- Only redistribute files allowed by Live2D terms and `RedistributableFiles.txt`.

### 3) Live2D model assets in `res/models`

- Model files and textures in `res/models` are third-party assets and may have separate use terms.
- The `Hiyori` model requires acceptance of the Free Material License Agreement (English link):
  - https://www.live2d.com/eula/live2d-free-material-license-agreement_en.html
- Before public redistribution (repository or release artifacts), verify each model's license/terms.
- If terms are unclear, remove these assets from public distribution and provide a user-side download step.

### 4) Ownership and licensing boundaries

- Original source code of this project is licensed by the maintainer via the root `LICENSE` (Apache-2.0).
- Third-party components listed here keep their own licenses.
- Their trademarks, copyrights, and other rights belong to their respective owners.

---

If this file is out of date with actual dependencies, please update it together with build/dependency changes.
