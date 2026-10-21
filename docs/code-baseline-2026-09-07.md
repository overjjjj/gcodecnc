# CNEXT-CAM 代码基线与设计加工开发顺序

日期：2026-09-07

## 基线范围

当前集成入口为 `codex/workspace-reorg` 的工作区，起始提交为 `f2de400`。
本文件记录该提交之上的结构整理，不表示所有工作区修改已经提交或完成生产验收。
本轮保留已有 AppWorkspaceRouter、ProjectCommandController 及其他未提交工作。

参考 `D:/yanggao/archify/outputs/dtm/dtm.architecture.json` 中的职责划分：
界面编排、CAD/CAM 核心、几何算法、工艺、程序验证、设备适配分开。
该参考来自二进制/PDB/反编译分析，只作为职责参考，不证明算法或通信协议正确。

本文件作为当前目录归属和近期开发顺序的入口；历史方案保留为背景。
产品仍是一个 CNEXT-CAM.exe，CQ8 负责实时插补、PLC、IO 和控制仲裁。

## 当前模块归属

| 模块 | 当前目录 | 职责与限制 |
| --- | --- | --- |
| 应用组装与项目命令 | `untitled/src/app` | MainWindow、AppController、路由和项目命令；不将应用控制器归为领域核心。 |
| 项目与公共值 | `untitled/src/core` | 项目持久化、Setup、毛坯、指纹、StrategyParams；ProjectManager 尚为跨模块聚合对象。 |
| 几何与识别 | `untitled/src/import` | STEP/OCC 与特征识别；不得依赖工作区或应用层。 |
| 设计加工 | `untitled/src/workspaces/process_design` | selection 管选择，operations 管工序与人工确认，ui 管输入和展示。 |
| 策略算法 | `untitled/src/strategies` | 刀路算法和策略注册；不拥有正式工序、候选确认或工作区控制器。 |
| 程序验证 | `untitled/src/workspaces/program_verification` | 生成编排、安全、指纹和最终代码仿真。 |
| 后处理与程序格式 | `untitled/src/postprocessor`、`untitled/src/gcode` | 控制器转换、宏、程序包和快照状态。 |
| 机床适配 | `untitled/src/workspaces/machine_operation` | MachineProfile、通信和发送界面；串口发送不等于完整 CQ8 作业协议。 |
| 工作区共用内容 | `untitled/src/workspaces/shared` | 工作区标识、路由契约、共用视图；非 UI 头文件不聚合具体工作区。 |
| 刀具资料 | `untitled/src/tool` | 刀具值和目录；版本快照及依赖失效还需完善。 |

## 本轮落实

1. `AppController.h/.cpp` 从 core 移入 app，更新调用方、qmake、moc 和测试路径。
2. `MachiningOperation.h`、`OperationProposal.h/.cpp` 从 strategies 移入设计加工的 operations。
3. 提取 `core/StrategyParams.h`，使工序领域不再经 StrategyBase 引入策略与刀路结果。
4. 将三个 WorkspaceFacade 分别放回所属工作区，删除 shared 的聚合头。
5. 加入 `tests/architecture_boundary_test.ps1`，扫描真实本地 include 的传递依赖，并注册到全量测试入口。

此次为职责整理，不更改刀路、参数默认值、冷却、文件格式或确认行为。
Facade 当前仍是界面预检查，不是已经完整实现的应用服务，也不能作为最终输出安全凭证。

## 已建立的依赖门禁

- strategies、import 不得直接或间接 include app 或 workspaces。
- process_design/operations 与 selection 不得引入 UI、策略实现、后处理、程序验证或机床工作区。
- shared 顶层契约头不得聚合 core、import、strategies、postprocessor 或具体工作区。
- 本次迁移的文件必须位于所属目录并在 qmake 工程中注册。

检查覆盖带引号的本地 include，包含条件编译分支；不等同于链接依赖分析。
当前没有宣称整个项目已实现严格分层：ProjectManager 仍依赖 MachineProfile、工序和导入类型，
MainWindow 仍承担较多事务，工序列表仍在 UI 和项目中各有一份。
下一步围绕真实业务迁移这些边界，不添加无调用方的空控制器。

## 分支能力与集成状态

| 来源 | 已核实内容 | 当前判断 |
| --- | --- | --- |
| 当前工作区 | STEP、Setup、孔选择、SelectionChain、基础 OperationFactory、孔/槽/型腔策略、生成/仿真/持久化测试 | 当前开发基础；具体加工范围仍以代码和验收模型为准。 |
| `codex/process-settings-safe-batches-20260827`，`0fe1f55` | 树中有 ProcessParameterSchema、ProcessTemplateLibrary、CompoundHoleRecognizer、AutoHolePlanningService、候选调和服务等 | 待逐模块审核与迁入；不能计为当前基线已完成。 |
| 其他 detached worktree | 本轮未逐个审查未提交实现 | 不计入当前基线能力。 |

起始提交与上述工艺分支左右独有提交数为 25 / 77，且目录布局已有变化。
集成时以“模块实现＋对应测试＋工程注册”为单位核查依赖和行为；不得仅复制旧状态文档作为完成证明。
现有 MachiningOperation、OperationFactory 已有同名实现，迁入前必须合并契约，避免两套正式工序入口。

## 设计加工优先的开发顺序

### D1：工序状态和确认事务收口

- 确定项目为正式工序的唯一状态源；OperationListPanel 展示状态并发出编辑命令。
- 将批量确认事务从 MainWindow 移入设计加工应用服务。
- 保留显式人工确认、Setup/源模型检查与整批失败不提交。
- 验收：无需 MainWindow 可运行确认测试；批量中一项失败时项目零新增；撤销/编辑后列表与项目一致。

### D2：孔加工最小完整链

- 先使用当前正面三轴孔与已有策略，统一选择、参数、确认、重算、保存重开。
- 逐项核查后迁入参数 Schema、模板建议与来源信息；已确认工序保存值快照。
- 验收：同一 STEP 和已确认参数保存重开后来源一致；修改 Setup、刀具或参数会失效相关结果；拒绝不可达孔。

### D3：槽与型腔公共选择

- 统一稳定引用、拓扑证据、候选与确认接口，避免直接把 SelectionChain::commit 当正式工序创建。
- 先完成闭合矩形槽/简单型腔，再扩展可证明的孤岛和开放边界。
- 验收：过期引用、自交、证据缺失被拒绝；候选阶段不写正式工序；确认后接入现有策略回归。

### D4：按需要集成自动工艺与算法

- 基础选择确认闭环通过后，再迁入复合孔规则、候选调和和更多工艺。
- 结构化刀路按一个算法族逐步引入，保留最终控制器代码回归。
- 动态铣、一般曲面、多 Setup 和多轴不作为本轮基线整理的交付项。

## 设计开发期间仍保留的输出缺口

2026-09-07 审核发现的以下问题没有在本次目录整理中修复，正式输出验收前必须完成：

- 快照状态对禁用、过期和缺失指纹的处理，以及启用工序集合不一致。
- 编辑器原地覆盖快照仍保留工序指纹。
- 最终代码验证结果与程序哈希绑定，以及输出门禁统一。
- CQ8 宏库完整交付、任务接收确认和运行状态协议。

可以继续开展离线设计加工开发，但不能把结构整理通过解释为上述输出问题已关闭。

## 验证入口

- 架构边界：`untitled/tests/architecture_boundary_test.ps1`。
- 全量测试：`untitled/scripts/run_tests.ps1`，使用匹配 Qt kit 的 MinGW。
- 构建和回归：`untitled/scripts/build_and_test.ps1`。
- 本轮构建和测试日志：`outputs/architecture-review-20260907/baseline-build.log`、`baseline-tests.log`。

本轮验证结果：架构测试先失败、迁移后通过（173 个源码文件）；Debug 构建、链接、运行库部署通过；
全量 run_tests.ps1 退出码 0，包含孔/槽 CQ8 集成、OCC STEP 验收、项目序列化与失效回归；
build_pipeline_test.ps1 和 git diff --check 通过。
初次编译发现 ToolEntry 的隐式头文件依赖，已在 ProgramGenerationService.h 显式包含后通过构建。
OpenCAMLib 源码缺失，OCL 可选能力仍关闭；未进行实机验证。
