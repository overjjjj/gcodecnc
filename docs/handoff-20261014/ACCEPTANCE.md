# 操作弹窗与最终程序另存：审核交付

独立构建标识：gui-errors-export-20261014 / verified-copy。程序版本字段仍为 0.1.0，本批用以下 SHA256 唯一标识：

`D5153DE8DFFDC1BDBC7A58282CD4DE395BEC1F25887D6C7DDAAF7EA5609D0D81`

## 改动

- 30 处实际编译的错误弹窗统一为中文正文、确定/查看详情/收起详情；原始诊断完整保留在可选择复制的详情。两个风险确认流程未改。详见 DIALOG-COVERAGE.md 与 dialog-inventory.json。
- 单文件、CQ8 主宏包、Siemens 单主文件包可以从原有导出入口选择 .nc/.txt；支持补齐缺省后缀和纠正与文件类型不符的后缀。后处理原生扩展仍可选。正文不改写，文件名变化不回写到项目快照。
- CQ8 同时导出主程序及同目录的配套 *_MACROS 文件，保留 M98/O 关联；按已验证包身份继续执行 CQ8 校验，其他控制器包不被重新分类。其他多文件原生包仍保留整体目录导出。
- 覆盖确认包含全部已有目标文件，默认取消；QSaveFile 检查打开、写入和提交结果。包先准备所有文件再提交，目标宏为目录等失败不会先替换主程序。
- 包提交仍以单文件为单位。若提交阶段发生后续失败，中文提示明确说明可能已有部分写入，原始详情列出已提交文件，要求重新导出整包；没有声称跨文件事务原子性。

## 中文前后示例

| 原诊断 | 正文结果 |
|---|---|
| POCKET_SINGLE_FLAT_FLOOR_UNPROVEN | 暂时无法确认槽底是完整平面，因此不能生成当前工序。请检查并重新选择完整槽区域；若槽底有台阶或曲面，需选择支持该形状的加工方式。 |
| POCKET_OPENING_NEGATIVE_ALLOWANCE | 加工余量或深度无效。请确认余量为有效非负数，且加工深度大于底部余量。 |
| current G-code snapshot is no longer current | 当前 G 代码快照已过期，不能导出或发送到机床；按当前模型、工序、刀具和装夹重新生成。 |
| 未知英文诊断 | 当前操作未完成；查看详情、核对输入，仍失败时复制详情反馈。 |
| Tool changes are available in this session but were not saved to disk | 本次会话有效，但尚未保存，重启会丢失；检查写权限和磁盘后重新保存。 |

## 验证

六项相关测试均 EXIT=0：operator_error_dialog_test、final_text_export_ui_test、package_failure_safety_test、renamed_cq8_package_test、program_package_exporter_test、generated_motion_binding_ui_test。日志、实际编译测试副本见本目录，accepted-results.json 记录散列及日常 EXE 只读观察。

隐藏原生 Qt GUI 测试核验正文/原始详情/中文按钮、乱码替代字符、非法负深度仍不能确认；最终 GUI 用例检查原生窗口不可见，三类实际生成程序的 nc/txt 逐字节一致、补后缀/改错后缀、取消、覆盖拒绝/接受、写失败和失效拒绝。独立程序运行包另做空闲与活动仿真退出烟测，两次原生窗口隐藏、事件循环和析构完成、退出码 0。

截图：evidence-operator_error_dialog_test-final/error-1.png、details-1.png、error-4.png；evidence-final_text_export_ui_test-final/save-1.png、overwrite.png、blocked-1.png 等。已查看中文正文、详情、参数错误和覆盖确认截图，无乱码。

样本及散列见 export-samples.json：mode-0 为普通单文件，mode-1 为 CQ8 主宏包，mode-2 为 Siemens 单主文件包。测试使用明确的合成几何，仅证明本次展示和导出行为，不代表用户真实模型槽加工已经完成。

## 复现

使用本批 desktop-tool-launch.py 隐藏启动 PowerShell 7，执行 build-independent.ps1 -Label <新标签>；随后 run-test.ps1 -Name <上述测试名> -Label <新标签>。使用本批独立对象，不使用 -Baseline。最多两个测试同时链接；构建与测试不要并发修改同一批对象。run-hidden-smoke.ps1 -Run <新名称> 从 delivery 复制独立运行目录测试。

代码差异：changes.patch；待同步文件和基线散列：changed-source.json、stage/。生产源同步结果另见 source-sync.json（未出现时表示尚未同步）。没有覆盖日常 EXE、关闭用户窗口或发送机床指令。

槽主线检查点仍在 ../design-d04-slot-entry-20261014/CHECKPOINT.md。本优先批次完成后继续真实槽入口和用户模型验证。

14 项源码/测试已同步至指定生产目录，完整生产源码与本批独立快照逐文件一致。独立审核尚待主线程进行。
