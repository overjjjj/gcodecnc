# 核心代码审查与中文注释补充

日期：2026-09-22。审查基于当时的工作区，包括已有未提交改动。重点为工序确认、选择证据、程序生成、安全校验、快照、项目存取和串口发送调用链；不代表对全部几何算法或全部界面的穷尽审查。

本轮修改了 17 个源码文件的注释，没有修改业务逻辑。以下问题属于审查发现，尚未修复。

## 审查发现

### 1. [P1] 紧凑 G-code 可绕过低位快速移动检查（已复现）

位置：[GCodeSafetyValidator.cpp:28](D:/yanggao/gcodecnc/untitled/src/workspaces/program_verification/safety/GCodeSafetyValidator.cpp:28)、[轴字匹配:41](D:/yanggao/gcodecnc/untitled/src/workspaces/program_verification/safety/GCodeSafetyValidator.cpp:41)。实际输出前调用位于 [MainWindow.cpp:2907](D:/yanggao/gcodecnc/untitled/src/app/MainWindow.cpp:2907)。

`containsWord()` 要求空白分隔，`containsAxis()` 要求轴字后有整数部分。在有完整安全头尾的程序中，执行 `G1 Z-1 F100` 后：

| 随后的运动行 | 当前检查结果 |
| --- | --- |
| `G0 X10 Y0` | 拒绝 |
| `G0X10Y0` | 通过 |
| `G0 X.5` | 通过 |

后两种写法使检查漏掉低于工件零面的 XY 快移。建议统一 NC 字解析，或对无法完整解释的运动行明确拒绝，并补充紧凑字、无整数部分小数的回归用例。

### 2. [P2] 开放轮廓的不同路径得到相同几何标识（已复现）

位置：[FeatureIdentity.cpp:56](D:/yanggao/gcodecnc/untitled/src/workspaces/process_design/selection/FeatureIdentity.cpp:56)，`canonicalPath()`。

开放路径也从首点按环形索引逆向遍历。对于 A=(0,0)、B=(10,0)、C=(10,10)，其他特征字段相同时，`A→B→C` 与 `A→C→B` 的 `stableContourId()` 相同，尽管线段不同；真正反向的 `C→B→A` 反而得到不同 ID。这会影响选择候选的几何身份及工序的 `geometryRefs`。

建议开放路径反向从末点开始，不使用首点环绕；保留闭合轮廓的循环起点规范化。现有身份测试覆盖闭合轮廓，需增加开放路径的反向等价与不同线段不等价用例。

### 3. [P2] 跳过孔工序后，新生成快照立即过期（已复现）

位置：[ProgramGenerationService.cpp:520](D:/yanggao/gcodecnc/untitled/src/workspaces/program_verification/generation/ProgramGenerationService.cpp:520)、[快照刀具摘要:728](D:/yanggao/gcodecnc/untitled/src/workspaces/program_verification/generation/ProgramGenerationService.cpp:728)。

同一孔同时有 `hole_peck` 和 `hole_deephole` 时，前者可能被跳过。如果被跳过工序独占另一把刀具，该刀具不会进入 `generationTools`，但工序仍保留在 `sourceOperationIds` 中。随后从缓存计算全部来源工序的刀具摘要，结果为空；服务仍返回成功。快照状态检查正确地阻断了这份缺少刀具证据的结果。

复现结果：`generation_ok=1`、`stored_tool_fingerprint_empty=1`、`output_allowed=0`。建议统一“来源工序集合”与“刀具证据集合”的覆盖范围，保留缺失证据时的阻断；增加不同刀具的同孔去重用例。

### 4. [P2] 项目写入失败仍可被报告为保存成功（静态确认）

位置：[ProjectManager.cpp:925](D:/yanggao/gcodecnc/untitled/src/core/ProjectManager.cpp:925)，`saveToFile()`。

文件打开后忽略 `file.write()` 的返回值，直接清除 `m_modified` 并返回 `true`。因此在文件打开成功、实际写入失败或不完整时，调用方仍会提示保存成功。建议采用检查完整写入及提交结果的原子保存，仅在成功提交后清除修改标记。现有测试覆盖正常存取，未模拟磁盘满、设备断开等写入失败；本轮也未做此类故障注入。

### 5. [P2] 发送失败后仍追加整份程序的发送字节数（静态确认）

位置：[CncSendDialog.cpp:110](D:/yanggao/gcodecnc/untitled/src/workspaces/machine_operation/ui/CncSendDialog.cpp:110)，`onSend()`。

调用方忽略 `sendGCode()` 的布尔返回值，无论结果如何都追加“[发送] N 字节”。底层存在端口未连接、写入长度不符等返回失败的分支，因此失败路径也会留下相同的发送记录。建议至少根据返回结果区分提交失败与已提交，并在后续协议实现中分别表达传输完成和控制器接收确认。本轮未进行串口硬件验证。

## 注释变更

注释集中解释职责、依据和边界，替换部分仅重复函数名的占位注释：

- 工序确认只建立工序身份，刀路状态初始仍为 `Empty`；选择确认不等于工序确认。
- 整批铣削先用最终几何和参数复算，再调用一次提交回调。
- 选择证据核对、确认后的取消基线、撤销与重做边界。
- 槽几何参数的来源、孔合批条件、刀具缓存、后处理后检查及 CQ8 宏替换核对。
- 工序顺序、刀具资料及 Setup 指纹各自的作用，以及 `Unchecked` 的真实含义。
- 项目 JSON 与源 STEP 的职责分工、重新关联来源时何时清空派生数据。
- 修正“替换源模型不删除历史程序”“ProjectManager 加载时校验源文件”“AppController 占位入口会生成程序”等与实现不符的注释。
- 说明文本检查的解析限制，以及串口缓冲区提交不代表控制器收件确认。

完整改动见 [本轮注释差异](D:/yanggao/gcodecnc/outputs/code-review-comments-20260922/comments-only.diff)，仅与本轮修改前副本比较，避免混入已有工作区改动。

## 验证

- [注释检查](D:/yanggao/gcodecnc/outputs/code-review-comments-20260922/comments-check.log)：17 个修改文件去除注释和空白后，代码 token 与修改前完全一致；独立注释复核提出的两处表述问题已修正。
- [回归日志](D:/yanggao/gcodecnc/outputs/code-review-comments-20260922/targeted-tests.log)：10 项现有 C++ 测试通过，覆盖批量提交、选择、确认、安全检查、生成、快照、序列化及失效传播。架构边界检查通过，检查 178 个源码文件。
- [复现结果](D:/yanggao/gcodecnc/outputs/code-review-comments-20260922/probe-results.log)：前三项缺陷均由最小程序复现。运行入口为 [run_probe.ps1](D:/yanggao/gcodecnc/outputs/code-review-comments-20260922/run_probe.ps1)；其退出码 0 表示观察到列出的缺陷，不表示功能正确。
- `git diff --check -- untitled/src` 通过。
- 未执行完整应用构建、全量测试、GUI 验收或实机加工；本轮验证范围对应注释修改及上述审查发现。
