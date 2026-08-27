# 参考资料使用边界

本目录保存可直接用于 CNEXT-CAM 需求追溯和合同测试设计的外部参考文本。参考资料不属于产品源码，也不证明对应功能已经实现。

## DTM 设计界面资料

`dtm-design-interface/` 归档以下只读参考：

- `DESIGN_INTERFACE_FUNCTIONS.md`：用户可见功能和 DTM 入口，可用于建立需求矩阵。
- `DESIGN_INTERFACE_UI_SPEC.md`：界面信息架构和交互规格，可用于 UI 合同核对。
- `FUNCTION_INDEX.md`、`CATDESIGN_FUNCTION_MAP.md`、`design_interface_functions.csv`：恢复符号和函数入口索引，只用于名称、入口和参数交叉核对。
- `DTM_TEST_SOFTWARE_README.md`、`dtm_test_acceptance.py`：独立测试软件的模拟场景，只用于提炼 CNEXT-CAM 自己的验收合同。

## 禁止直接复用

- DTM 测试软件使用模拟模型、模拟工序和模拟刀路状态，不能作为真实 CAM 业务实现。
- 恢复源码和反编译伪代码只能帮助核对入口、参数名和大致流程，不能直接编译、复制或并入产品。
- EXE、DLL、PDB、恢复的伪 C++、模拟业务代码和测试软件生成物不得进入本目录或产品构建。
- 任何刀路能力仍必须在 CNEXT-CAM 中通过真实几何、正式工序、最终 CQ8、安全检查和最终输出仿真独立验证。

## 来源

资料来自 `DTM_分析交付包_2026-08-21/02_设计界面`。归档日期：2026-08-25。
