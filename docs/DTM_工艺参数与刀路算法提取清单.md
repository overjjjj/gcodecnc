# DTM 工艺参数与刀路算法提取清单

日期：2026-08-25

## 使用原则

- 本清单只记录恢复源码表达的算法意图、参数、状态和几何前置条件；没有复制反编译伪 C++、EXE、DLL 或模拟业务代码。
- 恢复符号、局部变量和类型可能失真，不能作为可编译实现。所有产品能力必须在 CNEXT-CAM 自有模型中重新实现，并走真实工序、最终后处理、安全检查和仿真。
- 已确认 CatDesign 多数入口只是命令分发，DTM 测试软件只是模拟 UI；二者仅作低优先级入口核对，不主导本批代码改动。
- 冷却参数只保留既有兼容性，本批没有修改冷却代码或行为。

## 九类恢复文件审计

| 恢复文件 | 是否含真实算法 | 可提取参数、状态与几何前置条件 | 对应 CNEXT 能力 | 可立即安全实现的最小子集 | 不能直接复用的原因 |
|---|---|---|---|---|---|
| `ModuleAutoHole_recovered` | 部分是。存在形体分析、孔分组、加工参数/方案列表生成和排序流程 | 体/面选择，孔直径与深度，通孔/盲孔，多段深度 H1/H2，沉头/沉孔状态，刀具与方案库，排序和确认状态 | `FeatureRecognizer`、`AutoHolePlanningService`、`OperationProposal`、`OperationFactory` | 在稳定 `GeometryRef` 上产生多层/沉头孔候选草稿；仅当分类、刀具、公式和安全高度均可验证后允许人工确认 | 深度耦合 Shape3D、OCCT、Excel/资料库和界面对象；反编译类型与控制流不可靠，不能复制，也不能绕过人工确认 |
| `ModuleIntelligentChamfering_recovered` | 主要是工作流和参数状态，几何核心不足 | 深度/直径/长度限制，倒角类型、加工方式、刀具、选择集合、路径更新状态 | `OuterContourChamferStrategy`；未来 3D 倒角候选审计 | 继续使用已验证的二维凸闭合外形倒角；3D 只输出可审计候选或拒绝原因 | 未恢复可靠的 STEP 修剪边界、曲面偏置和刀具包络算法，不能用固定平面路径冒充 3D 倒角 |
| `ModuleAutoSlotFrame_recovered` | 是。可见面邻接、闭合性、底面、补线、保护形体、相交/干涉和路径对象流程 | `analyFace/analyShape` 对应面类型和候选聚合；`checkIsClosed/checkBottomValid` 对应边—面/点—边邻接与完整外环；`calNonCloseComplementType` 对应开放槽自由端元数据；`createProtectFace` 对应不可切边界；`generatePath` 只作为后续策略输入意图 | `FeatureRecognizer`、`SlotFrameRecognizer`、`SlotFramePlanningService`、`PocketRoughingStrategy`、`SlotMillingStrategy` | 已独立实现正面 Z 向平底直壁的闭合矩形/凸多边形型腔、一个孤岛/二维保护环及简单开放槽候选；人工确认后原子复用现有型腔/槽策略 | DTM 保护体依赖 OCCT 实体拉伸与布尔相交，恢复类型/所有权不可靠；当前仅采用可验证二维保护多边形，真实开放槽 OCC 自由边和一般凹多边形仍保持阻断 |
| `DialogSort_recovered` | 否，属于界面/状态管理 | 排序类型、加工余量、确认/取消和排序按钮状态 | `SelectionChainController`、`HoleSelectionSession` | 只作为排序字段和交互入口参考；不产生刀路 | 没有几何或路径算法，且 UI 状态不能证明最终程序顺序 |
| `GeometryOcct_recovered` | 少量变换辅助；主体是几何/工程聚合 | 层、子线框、工艺对象、包围盒、点变换、坐标/毛坯/装夹关联 | `ProjectManager`、`GeometryRef`、`SelectionChain`、`SetupContext` | 用于核对几何引用和工程状态应持久化的字段 | 恢复类职责过宽且依赖内部 OCCT 对象；缺稳定标识和明确单位合同，不能直接成为产品领域模型 |
| `MdgOcct_recovered` | 基本不含刀路算法，主要是参数容器 | 工艺类型/方式、排序、方向、深度模式、最大/最小直径、通孔、取消沉孔、刀具半径、补偿、转速/进给、进退 Z、螺旋直径、圆角参数、多层深度列表 | `MachiningOperation`、`StrategyParams`、`ProcessParameterSchema` | 作为 schema 字段、默认值和跨字段校验的来源索引 | 大量 getter/setter 不能证明参数真正进入路径；单位、范围和业务含义需由 CNEXT 测试重新定义 |
| `MdgOcctTube_recovered` | 否，主要是持久化容器 | 管/线框/层、坐标、毛坯、装夹及其关联状态 | `ProjectManager`、`SetupContext`、`StockDefinition` | 核对保存/重开和失效依赖，不新增刀路 | 不含可验证的刀具中心轨迹；对象关系和序列化格式属于 DTM 私有实现 |
| `ContactPointSolver_recovered` | 是。包含非线性方程、Jacobian、变量/方程数量等接触点求解结构 | 刀具曲面、目标曲面、接触参数、初值、收敛条件、法向/切向约束 | `SurfaceFinishStrategy`；未来 3D 曲面接触求解服务 | 当前不实施；只保留为未来真实 STEP 曲面求交/刀具接触求解的算法线索 | 构造状态、方程语义、曲面参数域、收敛和异常策略不完整；错误接触点会直接造成过切，必须保持阻断 |
| `G682Transform_recovered` | 是。包含 ZXZ 欧拉角、矩阵/逆矩阵、轴向量、点变换和机床角匹配 | 工件坐标、旋转角、旋转顺序、正逆变换、机床轴约束、变换前后点 | `SetupContext`、未来工序级专用 Setup/3+2 坐标变换 | 当前不实施；先建立有单位、旋转顺序和机床限位测试的独立 Setup 模型 | 反编译函数签名和返回值有破损，当前工序也没有专用侧面 Setup 绑定；直接接入会掩盖既有侧面型腔安全阻断 |

## 本批算法选择

按“清内角 → 自动孔复合层 → 动态铣圆槽”的优先级审计后，上一批选择清内角二维安全子集：

1. `ModuleAutoSlotFrame_recovered` 明确给出清角线、保护形体和干涉检查的算法意图。
2. 当前 `ContourFeature` 能可靠表达正面、轴对齐、四角矩形闭合边界；`MachiningOperation` 可补充显式前工序依赖；工具库可取得前后刀径。
3. 对一般凹多边形、圆角、孤岛、侧面和三维框仍缺可靠残料布尔/偏置基础，因此继续拒绝生成。

## 已独立实现的安全子集

- 新增策略 `mill_inner_corner_cleanup`，只接受正面、无孤岛、四个唯一角点、轴对齐的闭合矩形型腔。
- 正式生成要求恰好一个更早的 `mill_pocket_rough` 开粗依赖，前后工序必须绑定相同且非空的 `GeometryRef`。
- 前刀必须存在且直径大于清角刀；记录的 `previousToolDiameter` 必须与依赖工序实际刀具一致。
- 刀具中心从前刀已清除的安全内缩点沿对角线进入目标角点；目标中心始终按当前刀半径、余量和清角公差留在边界内。
- `stepOver` 不得大于当前刀径；深度按 `stepDown` 分层；每个角、每层均在安全高度退刀后才进行 XY 快移。
- 最终 CQ8、Fanuc、Siemens 文本分别通过既有后处理和 `GCodeSafetyValidator`；仿真读取最终代码（CQ8 固定循环仅在需要时精确展开）。
- 入口暂不开放：现有 UI 尚不能让用户可靠选择并确认前工序依赖。策略层安全合同已完成，但完整交互仍是半成品。

## 明确未实现

- 一般凹轮廓、圆角矩形、异形型腔、孤岛型腔和三维/侧面清角。
- DTM 保护实体布尔、任意线框偏置和全残料区域求交。
- 一般多轴/侧面、相交/破孔、密封槽，以及未能从 STEP 验证层边界的复合孔。
- 动态铣圆槽；当前没有足够证据证明连续绕行、径向吃刀和刀具中心边界。

## 自动化证据

- `inner_corner_cleanup_strategy_test`：工具中心边界、三层深度、`stepOver` 实际改变路径、进刀平面、刀刃长度、安全高度、前刀大小、边界完整性和孤岛阻断。
- `inner_corner_cleanup_cq8_integration_test`：显式前工序依赖、最终 CQ8、Fanuc/Siemens 差异、安全检查、最终代码仿真、几何不一致和缺依赖阻断。
- `project_manager_serialization_test`：前工序依赖保存/重开。
- `ProgramSnapshotFingerprint`：依赖变化进入最终程序快照指纹，避免旧代码被误认为有效。

## 下一条算法路线

复合孔批次已完成正面 Z 向的候选→人工确认→原子工序链安全子集，详细证据见 `自动孔多层与沉头孔算法验收记录.md`。下一条路线是“槽框识别基础”：先输出稳定 `Recognized Feature` 和保护面/闭合性证据，不接入三维或动态铣刀路。
