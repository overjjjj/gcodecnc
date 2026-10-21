from pathlib import Path
from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT

ROOT=Path(r'D:\yanggao\gcodecnc'); OUT=ROOT/'docs'/'DTM_设计界面与G代码质量专项开发方案.docx'
doc=Document(); sec=doc.sections[0]; sec.top_margin=sec.bottom_margin=sec.left_margin=sec.right_margin=Inches(1)
doc.styles['Normal'].font.name='Calibri'; doc.styles['Normal'].font.size=Pt(10.5)
for n,z,col in [('Heading 1',16,'2E74B5'),('Heading 2',13,'2E74B5'),('Heading 3',11.5,'1F4D78')]:
 st=doc.styles[n]; st.font.name='Calibri'; st.font.size=Pt(z); st.font.color.rgb=RGBColor.from_string(col)
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER; r=p.add_run('DTM 设计界面与 G 代码质量专项开发方案'); r.bold=True; r.font.size=Pt(22); r.font.color.rgb=RGBColor(31,77,120)
p=doc.add_paragraph('项目经理下发版｜事实基线：untitled/ 与 项目进度.md｜目标：生产候选版本（人工确认下机）'); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
doc.add_heading('1. 执行基线与阶段目标',1)
doc.add_paragraph('本方案以 D:\\yanggao\\gcodecnc\\untitled 为当前开发工程，以根目录项目进度.md、现有测试和 Release 构建结果为事实基线。整理副本中的反编译代码只用于行为比对，不作为待修复工程。')
doc.add_paragraph('本阶段主线是设计界面功能实现，G 代码生成必须通过质量门；不新增三维高级功能，不进行真实机床上机和自动下发。')
doc.add_heading('2. 设计界面九步功能矩阵',1)
rows=[('1 模型导入','已完成/持续收口','untitled/src/import、ViewportWidget、acceptance_step_model_test.cpp','STEP 导入、模型指纹、包围盒；异常模型必须阻断后续生成。'),('2 加工正面/Setup','已完成','src/core/SetupOrientation.*、MainWindow、setup_orientation_test.cpp','正面确认、Setup 状态、侧/背面安全门。'),('3 原点/WCS','已完成','src/core/SetupOrigin.*、SetupOriginDialog.*、setup_origin_test.cpp','九点/自定义点、偏移、G54-G59、指纹失效。'),('4 毛坯/余量','已完成','src/core/StockDefinition.*、StockDefinitionDialog.*、stock_definition_test.cpp','六向余量、尺寸预览、未确认阻断。'),('5 特征识别/筛选','安全子集/持续收口','src/import/FeatureClassifier.*、FeatureDisplayFilter.*、HoleFeatureGrouping.*','孔/槽/型腔/轮廓候选、稳定 ID、人工选择；圆柱凹凸和槽深度边界优先。'),('6 刀具/工艺参数','已完成基础闭环','ToolLibraryPanel、ParameterEditorDialog、ToolOperationCompatibility.*','参数来源、范围、单位、刀具兼容性和模板恢复。'),('7 工序确认','已完成基础闭环','OperationFactory、OperationWorkflow、OperationListPanel、operation_lifecycle_wiring_test.cpp','只生成已确认、启用、有效工序；依赖变化标记 stale。'),('8 G 代码生成','已完成多项 P0/P1，持续回归','src/gcode、src/postprocessor、ProgramGenerationService、gcode_safety_validator_test.cpp','CQ8 主验证；Fanuc/Siemens 兼容；失败阻断导出。'),('9 仿真/输出','已完成基础闭环','SimulationController、GCodeEditor、CncSendDialog、simulation_gcode_test.cpp','最终代码驱动仿真；CQ8 仅输出接口，人工确认下机。')]
t=doc.add_table(rows=1,cols=5); t.alignment=WD_TABLE_ALIGNMENT.CENTER
for c,v in zip(t.rows[0].cells,['步骤/功能','当前状态','代码位置','测试位置','本阶段验收重点']): c.text=v
for row in rows:
 x=t.add_row().cells
 for i,v in enumerate(row): x[i].text=v
doc.add_heading('3. G 代码质量门（强制）',1)
for s in ['几何正确：坐标、深度、圆弧方向、刀补与已确认特征一致；默认几何位置误差不超过 0.01 mm。','工艺正确：刀具、转速、进给、切深、余量、安全高度与 Setup/工序快照一致。','模态正确：G90/G91、平面、WCS、固定循环、进给模式和刀补状态明确。','安全正确：G0 仅在安全高度跨区域；主轴/冷却启停、G80、M5、M9、M30 完整；危险动作失败即阻断。','追溯正确：程序保存模型指纹、Setup 指纹、参数来源、工序 ID、后处理器和版本。','验证正确：GCodeSafetyValidator、最终代码仿真、黄金样例回归和 Release 构建全部通过。','输出策略：任一质量门失败时禁止导出和 CQ8 发送，不生成部分可用程序。']:
 doc.add_paragraph(s,style='List Bullet')
doc.add_heading('4. 下一条垂直切片：特征分类与槽深度边界',1)
doc.add_paragraph('目标：修复圆柱凹面被误归类为 Boss、槽深度超过模型厚度仍进入候选等问题，使设计界面显示、人工选择和后续 G 代码生成建立在可靠特征之上。')
for h,b in [('几何组','基于 OCCT 面法向、拓扑邻接、圆柱半径/轴向范围和模型厚度建立可测试判定；复杂或证据不足时返回 Unknown。'),('核心组','统一 FeatureIdentity、候选/确认/过期状态和依赖失效；禁止识别结果直接创建正式工序。'),('UI 组','在 FeatureListPanel、FeatureDisplayFilter、HoleFeatureGrouping 中显示类别、数量、错误提示和人工确认状态。'),('G 代码组','确认前阻断；确认后进入 OperationFactory 和 ProgramGenerationService；保持 CQ8 输出边界。'),('QA 组','补充真实 STEP、孔/槽/型腔/轮廓黄金样例，覆盖误分类、越界深度、过期程序和安全门。')]: doc.add_heading(h,2); doc.add_paragraph(b)
doc.add_heading('5. 模块设计与接口约束',1)
for h,b in [('UI 层','MainWindow 负责路由和状态展示；各 Dialog/Panel 只编辑 DTO 或发出命令，不直接访问 OCCT 内存或后处理器。'),('应用服务层','ProgramGenerationService、AppController、ProjectManager 负责编排、校验、快照和失效传播；所有生成入口统一走服务层。'),('领域层','Feature、Setup、StockDefinition、ToolSpec、ProcessParams、Operation、Toolpath、ProgramSnapshot 作为稳定模型。'),('算法适配层','GeometryAdapter、FeatureClassifier、ToolpathStrategy、PostProcessorBase、SimulationController 通过明确输入/输出、错误码、进度和取消句柄隔离实现。'),('基础设施层','Qt 5.8、OCCT 7.6、文件/日志/配置、SQLite 和现有构建链保持稳定；本阶段不升级核心运行时。')]: doc.add_heading(h,2); doc.add_paragraph(b)
doc.add_heading('6. 任务清单与里程碑',1)
tasks=[('P0-1','设计界面入口与状态收口','UI/核心','九步流程前置条件、禁用态、错误提示统一','页面契约测试通过'),('P0-2','圆柱凹凸与槽深度边界','几何/核心','修复分类证据和 Unknown 阻断','真实 STEP 回归无误分类越界'),('P0-3','CQ8 代码质量回归','G代码/QA','孔、槽、型腔、轮廓、倒角、坡面黄金样例','质量门全通过'),('P0-4','程序快照与仿真一致性','平台/QA','最终代码同源、失效传播和取消','仿真与导出字节/字段一致'),('P1-1','二维内核收口','算法组','凹轮廓、多孤岛、窄通道和连接策略','误差/性能达到阈值'),('P1-2','Release 候选','项目经理/QA','全量测试、构建、安装包和回归报告','人工确认下机前置条件齐全')]
t=doc.add_table(rows=1,cols=5)
for c,v in zip(t.rows[0].cells,['编号','任务','责任','工作内容','完成标准']): c.text=v
for row in tasks:
 x=t.add_row().cells
 for i,v in enumerate(row): x[i].text=v
doc.add_heading('7. 测试、风险与边界',1)
for s in ['测试层级：单元测试 → UI 契约测试 → 算法黄金样例 → 最终 G 代码安全门 → 仿真 → Release 全量回归。','风险：反编译行为与新实现不一致、复杂 OCC 拓扑证据不足、全局状态耦合、历史项目兼容和多后处理器差异。','安全边界：CQ8 仅作为程序输出接口；不新增实时通讯、自动下发或未经人工确认的机床动作。','本阶段暂不做：自由曲面、3D 倒角、复杂自动框、高级动态铣和真实机床切削。','完成定义：UI 流程、安全门、G 代码校验、自动回归/真实模型验证四项同时满足。']:
 doc.add_paragraph(s,style='List Bullet')
doc.add_heading('8. 项目经理执行要求',1); doc.add_paragraph('采用端到端垂直切片交付。每个任务必须同时提交代码、测试、进度记录和接口说明；禁止只提交按钮或局部算法。每次合入前必须说明影响的 Setup/工序/程序快照，并附最终代码仿真证据。')
doc.save(OUT); print(OUT)
