# 框架整理与分区解耦实施方案

日期：2026-09-04  
适用分支：`codex/workspace-reorg`

2026-09-07 更新：当前目录归属、分支集成状态和设计加工优先顺序见
[代码基线与设计加工开发顺序](code-baseline-2026-09-07.md)。
AppController 已归入 app，工序/确认对象已归入 process_design/operations，
各工作区 Facade 已拆分；这些结构迁移不代表主窗口事务和项目状态已完全解耦。

## 结论

物理目录迁移已经达到首期目标，当前不建议继续按“看到文件就移动”的方式整理。下一阶段应围绕依赖方向拆分应用壳层，先建立稳定的控制器边界，再迁移实现文件。这样可以保持工艺设计、程序验证和机床操作三个工作区相互解耦，并避免把多轴能力提前混入三轴流程。

## 当前框架状态

### 已完成

- `src/workspaces/process_design`：公共选择、候选、确认和工艺 UI 已归档。
- `src/workspaces/program_verification`：程序生成、安全、快照、仿真和 G-code UI 已归档。
- `src/workspaces/machine_operation`：机床模型、通信和发送 UI 已归档。
- `src/workspaces/shared`：工作区契约、Facade 和共享视图控件已归档。
- 选择证据、Setup/WCS 版本、人工确认边界和程序生成前置校验已存在。

### 当前结构性问题

| 热点文件 | 主要问题 | 目标归属 |
| --- | --- | --- |
| `src/app/MainWindow.cpp` | 页面创建、菜单、工艺协调、程序验证、机床接线集中在一个对象 | `src/app` 仅保留组装与路由 |
| `src/import/FeatureRecognizer.cpp` | STEP 拓扑识别与工艺特征语义耦合较重 | 保留在 import，输出稳定特征契约 |
| `src/workspaces/process_design/ui/StrategyPanel.cpp` | 参数编辑、候选预览和确认接线混合 | UI 只编辑提案，确认交给领域服务 |
| `src/core/ProjectManager.cpp` | 项目 I/O、版本迁移和状态失效逻辑集中 | 拆出序列化与失效策略接口 |
| `src/workspaces/program_verification/generation/ProgramGenerationService.cpp` | 生成前校验和后处理编排集中 | 保留服务入口，细分校验端口 |

## 目标依赖方向

```text
app (组装/路由)
  ├── process_design facade ──> core / import / strategies / tool
  ├── program_verification facade ──> confirmed operation / postprocessor / gcode
  └── machine_operation facade ──> machine model / communication

shared contracts <──────────────────────────────┘
```

禁止方向：

- `process_design` 不得依赖 `machine_operation` 通信实现。
- `program_verification` 不得依赖具体工艺 UI 控件。
- `machine_operation` 不得依赖 `MainWindow` 或工艺设计控件。
- 策略算法不得反向调用 UI、项目窗口或机床通信。

## 分阶段实施

### 阶段 A：应用壳层控制器化（下一步）

新增 `src/app/controllers/`，每个控制器只持有对应 Facade 和必要的 Qt 信号：

- `AppWorkspaceRouter`：页面索引、工作区标题和路由，不执行业务。
- `ProjectCommandController`：新建/打开/保存项目和源模型生命周期。
- `ProcessDesignController`：选择会话、候选提案、Setup 可达性和人工确认。
- `ProgramVerificationController`：生成、快照、安全检查、仿真和输出就绪状态。
- `MachineOperationController`：机床配置、连接状态和程序发送。

迁移规则：先为目标控制器编写测试并验证失败，再迁移现有接线、删除主窗口中的重复实现；每次只迁移一个控制器。最终目标是让 `MainWindow` 仍作为唯一组装入口，但不再保存业务状态。

### 阶段 A 当前落实情况

- 已抽出 `src/app/controllers/AppWorkspaceRouter.*`，接管导航项切换到页面/标题的同步，以及翻译后的标题刷新。
- 主窗口仍负责创建控件和拥有路由器；路由器只依赖 Qt 和 `WorkspaceId`，不持有项目、工序或通信对象。
- 新增真实 Qt 控件测试，覆盖三个工作区映射、导航同步、中文标题刷新和非法路由目标。
- 测试脚本显式选择当前 Qt kit 的平台插件，并在结束后恢复原插件环境设置。
- 这里只完成路由接线抽取，不代表 `MainWindow` 的其余业务状态已经移出。项目、工艺、验证和机床控制器尚待后续迭代。
- 验证：Debug 编译/链接与运行库部署通过，全量 `scripts/run_tests.ps1` 退出码 0；构建提示 OpenCAMLib 源码缺失，因此未验证 OCL 可选能力。

### 阶段 B：领域入口收口

- 将 `MachiningOperation`、`OperationProposal`、`OperationFactory`（或等价确认服务）归档到 `src/workspaces/process_design/operations/` 的领域子目录。
- `src/strategies` 只输出候选刀路/参数建议，不直接创建正式工序。
- 所有正式工序创建必须携带人工确认意图、Setup/WCS revision、选择证据和参数来源。

### 阶段 C：项目与生成端口化

- 从 `ProjectManager` 分离项目序列化、版本迁移和过期判定。
- 为机床通信引入 `IMachineTransport`，`CncCommInterface` 作为适配器。
- 为程序生成引入只读的 `ConfirmedOperationSource`，阻止服务读取未确认候选。

### 阶段 D：构建目标拆分（最后进行）

当前继续使用 qmake 单工程。待阶段 A-C 的接口稳定后，再增加按工作区的静态库/测试目标；在此之前拆分构建目标会放大 include 和 moc 迁移风险。

## 3~5 人开发组分工

| 角色 | 负责范围 | 不负责范围 |
| --- | --- | --- |
| 负责人 | `MainWindow` 组装、Router、集成回归 | 策略算法细节 |
| 工艺开发 | `process_design` 控制器、选择/确认链 | 程序后处理、通信 |
| 验证开发 | 生成、安全、快照、仿真控制器 | 工艺 UI 状态 |
| 机床开发 | `machine_operation` 控制器、transport 端口 | 项目序列化 |
| 测试/构建（可兼职） | 契约测试、Debug、全量验收和路径兼容清理 | 直接修改业务规则 |

## 每阶段验收门槛

1. 先增加一个行为或契约测试，并确认其在旧实现上失败。
2. 迁移后 Debug 构建和对应测试通过。
3. 全量 `run_tests.ps1` 通过，且不依赖临时环境变量。
4. 检查 `git diff --stat`，不得包含反编译资料、渲染输出、临时目录或无关工具产物。
5. 只有阶段完成后才删除旧接线或兼容路径。

## 暂不处理

- 真正侧面/背面、G68.2、3+1/3+2、多 Setup 和动态铣。
- 孔类通用平面选择放行。
- 冷却行为重构。

## Archify 架构图

- Archify 已安装到 `D:\yanggao\gcodecnc\tools\archify`，该目录已加入 `.gitignore`，不进入产品提交。
- 当前架构规格、交付 HTML 和浏览器检查证据位于 `docs/archify/`。
- 架构图通过 Archify showcase 的 9 项检查和自动浏览器检查；人工视觉复核仍单独记录。
- 架构图只表达当前已实现的单 Setup、三轴、人工确认边界；多轴和真正侧面加工仍标记为后续能力。
- 在接口未稳定前继续移动 `src/import`、`src/strategies`、`src/tool` 和 `src/postprocessor`。
