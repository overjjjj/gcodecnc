# CNEXT-CAM 工作区物理迁移检查稿

日期：2026-09-04  
分支：`codex/workspace-reorg`

## 1. 检查结论

本轮“逻辑模块化 → 物理目录迁移”已经完成。源码目录、qmake 工程注册、主窗口引用、测试入口和 Debug 构建均已对齐。

当前采用的原则是：

- 与界面流程直接对应的代码放入 `src/workspaces/<workspace>/`。
- 跨多个工作区复用的领域能力保留在 `src/core`、`src/import`、`src/postprocessor`、`src/strategies`、`src/tool` 和通用 `src/gcode`。
- UI 不直接拼装正式工序，正式工序仍由确认服务统一创建。
- 迁移只改变物理归档和引用路径，不改变冻结的冷却行为及既有工艺边界。

## 2. 当前目录结构

```text
src/
├─ app/
│  ├─ MainWindow.cpp
│  └─ MainWindow.h
├─ workspaces/
│  ├─ shared/
│  │  ├─ WorkspaceContracts.h
│  │  ├─ WorkspaceFacades.h
│  │  ├─ WorkspaceModule.h
│  │  └─ ui/
│  │     ├─ BottomBar.*
│  │     └─ ViewportWidget.*
│  ├─ process_design/
│  │  ├─ selection/
│  │  ├─ operations/
│  │  └─ ui/
│  ├─ program_verification/
│  │  ├─ generation/
│  │  ├─ safety/
│  │  ├─ fingerprint/
│  │  ├─ simulation/
│  │  └─ ui/
│  └─ machine_operation/
│     ├─ communication/
│     ├─ model/
│     └─ ui/
├─ core/
├─ import/
├─ postprocessor/
├─ strategies/
├─ tool/
└─ gcode/
```

## 3. 工作区职责

### 3.1 工艺设计工作区

入口：选择加工方式、选面/边/串联、方向和顺序、工艺参数、刀具、工序列表。

已归档内容：

- 公共选择会话、特征身份和串联控制。
- 孔、轮廓特征分组与显示过滤。
- 平面、挖槽、型腔、轮廓策略参数界面。
- 刀具库、刀具适配、Setup 原点、毛坯和工序配置对话框。
- `OperationWorkflow` 等工艺流程契约。

边界：

- 选择结果仍然只是不可执行候选。
- 旧项目缺少稳定引用或 Setup 版本时，工序必须重新确认。
- 孔类继续使用专用孔选择流程。

### 3.2 程序验证工作区

入口：生成程序、G-code 安全检查、快照、刀路仿真和最终验收。

已归档内容：

- `ProgramGenerationService`。
- `GCodeSafetyValidator`。
- `ProgramSnapshotFingerprint`。
- `SimulationController`。
- G-code 编辑器和高亮器。

边界：

- 程序生成只接受已确认、启用、未过期且刀路有效的工序。
- 快照记录工序、Setup、选择证据和机床配置指纹。
- 当前首期仍限定单 Setup、三轴和人工确认。

### 3.3 机床操作工作区

入口：机床配置、通信连接、程序发送。

已归档内容：

- `CncCommInterface`。
- `MachineProfile` 和 `MachineProfileValidator`。
- 机床配置和发送程序对话框。

边界：

- 机床配置只提供能力和安全参数，不参与工艺选择。
- 通信层不依赖工艺设计 UI。

### 3.4 共享层和应用壳层

- `src/workspaces/shared/` 提供跨工作区契约、Facade 和共享可视化控件。
- `src/app/` 只负责应用组装、菜单/工具栏和工作区连接。
- 共享层不得反向依赖具体工作区 UI。

## 4. 有意保留在通用目录的内容

以下目录不是迁移遗漏，而是跨工作区领域能力：

- `src/core`：项目、Setup、毛坯、设置和源文件指纹。
- `src/import`：STEP 导入、拓扑分析、特征识别和分类。
- `src/strategies`：孔、槽、型腔、轮廓等策略算法。
- `src/tool`：刀具实体和刀具库。
- `src/postprocessor`：通用后处理器及 Siemens/Fanuc/CQ8 实现。
- `src/gcode`：仍被多个工作区复用的模态优化、宏构造和程序导出组件。

如果后续需要将这些目录继续物理拆分，应先建立稳定的领域接口，不能仅通过移动文件解决。

## 5. 依赖解耦规则

1. `process_design` 可以调用 shared contract、core domain、strategies 和 tool，但不能直接调用 machine communication。
2. `program_verification` 可以读取已确认工序、Setup、刀具和机床配置，但不能依赖具体工艺 UI 控件。
3. `machine_operation` 的通信和机床模型不能依赖 `MainWindow` 或工艺 UI。
4. `app/MainWindow` 是组装层，可以依赖各工作区 Facade/UI，但业务规则必须留在工作区服务中。
5. 测试应优先包含工作区公共接口；需要验证 UI 接线时才读取具体 UI 源文件。

## 6. 已完成项与未实现项

### 已完成

- 公共选择会话和选择证据链路。
- Setup/WCS 版本与来源指纹。
- 当前 Setup 三轴可达性校验。
- 人工确认边界和统一工序创建入口。
- 程序生成前的状态、刀具、参数和安全检查。
- 稳定的工作区物理目录和 qmake 注册。
- 测试脚本在迁移期间的旧路径兼容。

### 明确未纳入首期

- 真正侧面、背面和横向加工。
- G68.2、3+1、3+2 和完整多轴加工。
- 动态铣和完整多孤岛通用算法。
- 通用平面选择放行侧向孔。
- 冷却行为重构。

这些项目应作为后续独立迭代，不应在本次目录迁移中隐式开放。

## 7. 验证证据

- Debug 构建：通过。
- 全量测试：通过。
- 覆盖范围包括：
  - 工作区契约和 Facade。
  - 选择链、撤销恢复、分支、方向、排序。
  - 工艺参数、刀具适配、工序生命周期。
  - G-code 安全、快照指纹、Setup 方向。
  - 程序生成、CQ8 后处理和仿真刀路。
  - STEP 验收、项目保存重开和过期工序阻断。
- 当前 Git 已跟踪修改干净；未跟踪的资料、输出和临时目录未纳入迁移提交。

## 8. 提交记录

- `812978a`：更新工艺设计 UI 测试路径。
- `bc4f3f3`：迁移程序验证模块。
- `fc13fe8`：迁移机床模型与通信。
- `56a088f`：迁移应用壳层和共享 UI。
- `2fc192d`：增加测试脚本迁移路径解析。
- `8ec9263`：更新项目进度文档。

## 9. 请检查的决策点

1. 是否接受 `src/strategies`、`src/tool`、`src/import` 作为跨工作区通用领域层继续保留？
2. 是否接受 `src/app/MainWindow` 作为唯一应用组装入口？
3. 后续多轴/侧面加工是否单独建立新的 workspace 和能力接口，而不是扩展当前三轴流程？
4. 是否需要在下一阶段补充按工作区拆分的 CMake 构建目标，还是继续保持当前 qmake 单工程？
