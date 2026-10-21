# CatDesign 功能到源码入口映射

`CatDesign_recovered.cpp` 内的函数按二进制地址排列。本页按界面功能重新分组，便于重构时查找。

## 工程与导入

- 新建/打开/保存：`newProjectFile`、`openProjectFile`、`saveProjectFile`、`saveasProjectFile`
- 最近文件：`addToRecentFileList`、`openRecentFile`、`clearRecentFile`
- 外部导入：`import3rdPart`、`openDxfFile`、`openAutoCAD`、`convert`、`pretreatment`
- 临时恢复：`checkTempFile`

## 界面构建与状态

- 主设计页构建：`CatDesign`、`createForRibbon`、`createToolBar`
- 工序面板：`updateProcessPanel`、`checkStatus`、`setActionEnabled`
- 页面切换与关闭：`showWorker`、`switchWidgetClose`、`closeWindow`
- 账户和配置：`accountChanged`、`userConfiguration`、`processConfiguration`、`setParam`
- 撤销/重做：`undo`、`redo`、`updateUndoRedoMenuStatus`

## 坐标、夹具和图层

- 坐标与摆正：`moveToOrigin`、`workpieceAligned`
- 夹具：`addClamps`
- 图层：`layerCopy`、`layerMove`、`layerSetting`
- 方盒与公差：`rightAngleBox`、`toleranceSet`

## 孔、钻削和螺纹

- 钻孔：`straightDrill`、`deepHoleDrill`、`peckDrillG73`、`peckDrillG83`
- 镗孔：`boreHoleG76`、`boreHoleG86`
- 螺纹：`tap`、`millWhorl`
- 孔附加工：`drillHoleChamfer`、`drillDodging`

## 二维与三维铣削

- 平面/槽/轮廓：`millPlane`、`millGroove`、`millOutLine`、`millIslandPlane`
- 线和曲线：`millLine`、`millCurve`
- 动态与螺旋：`dynamicMilling`、`spiralMillRing`
- 清角/圆角：`millClearInnerCorner`、`millFilletedCorner`
- 斜面：`millSlopePlane`、`millSlopePlane3D`、`chamferSlopePlane`
- 倒角：`millChamfer`、`millChamfer3D`

## 自动特征和分析

- 特征孔：`featuresHole`
- 自动孔：`autoHole`
- 自动槽框：`autoSlotFrame`
- 智能倒角：`intelligentChamfering`
- 选择分析：`selectAnalysis`、`selectEdgeAnalysis`
- 动态/级联/管材分析：`dynamicAnalyse`、`cascadeAnalyse`、`tubeAnalyse`

## 模板、材料和辅助建模

- 模板：`templateLibrary`、`moduleTemplateLibrary`
- 材料：`materialLibrary`
- 文字和标记：`letter`、`marking`
- 测量：`measuringtool`

## 仿真与发送加工

- 仿真：`emulation`
- 模型发送：`sendModel`、`showSendFileMenu`、`slotSend`
- 消息与页面联动：`sendModelMessage`、`sigShowWorker`、`toolNameChanged`

