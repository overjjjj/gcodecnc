#pragma once
#include <QString>
#include <QVector>

// Product scope, existing entry points and machining input requirements share one catalogue.
// An existing implementation is not a claim that the whole feature has passed acceptance.
struct DesignFeatureEntry {
    QString id;
    QString title;
    QString entry;
    QString strategy;
    QString input;
    QString scope;
    QString acceptance = QStringLiteral("待完整验收");
    bool closedBoundary = false;
    bool edgePickingOnly = false;
    QString commandGroup;
    QString commandLabel;
    bool commandEnabled = false;
};

inline const QVector<DesignFeatureEntry> &designFeatureCatalog()
{
    static const QVector<DesignFeatureEntry> entries = {
        {"D01.import", "模型导入", "MainWindow::onImportStep", "", "STEP", "已有导入流程"},
        {"D01.visibility", "模型显示隐藏", "ViewportWidget::setModelVisible", "", "模型", "窗口显示偏好；隐藏时禁止拓扑拾取，保留加工几何", "软件专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-d01-20261004"},
        {"D01.views", "标准视图", "ViewportWidget::setStandardView", "", "模型", "七方向及适应窗口", "既有入口软件专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-d01-20261004"},
        {"D01.origin", "工件原点", "SetupOriginDialog", "", "Setup", "模型证据不变；加工副本减原点，仿真按快照原点对齐；仅平移", "平移原点专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-origin-20261005"},
        {"D01.offset", "坐标偏移", "SetupOriginDialog", "", "Setup", "XYZ偏移进入最终工作坐标；绝对安全Z不随偏移改值", "XYZ偏移专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-origin-20261005"},
        {"D01.stock", "毛坯", "StockDefinitionDialog", "", "Setup", "矩形毛坯及六向余量"},
        {"D01.filters", "特征识别筛选", "FeatureListPanel", "", "识别候选", "全部、孔、槽型腔、平面、轮廓筛选", "已识别特征筛选软件专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-d01-20261004"},
        {"D01.broken_holes", "破孔识别", "", "", "孔候选", "待实现正面固定刀轴可达的破孔识别及证明", "未验收"},
        {"D01.side_walls", "侧壁识别", "", "", "边面候选", "待实现正面可达侧壁筛选；不含侧面装夹", "未验收"},
        {"D02.edge", "单边选择", "SelectionChainDialog", "", "模型边", "共用选择会话"},
        {"D02.chain", "串联选择", "SelectionChainDialog", "", "模型边", "真实拓扑串联"},
        {"D02.face", "实体面选择", "SelectionChainDialog", "", "模型面", "保留完整边界及岛屿证据"},
        {"D02.partial", "局部串联", "SelectionChainDialog", "", "模型边", "局部选链及预览"},
        {"D02.inside", "铣内", "SelectionChainDialog", "", "选择候选", "无岛简单凸XY闭合轮廓，CAM半径/余量及进退刀检查；凹域待补", "限定轮廓专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-d02-contour-side-20261004"},
        {"D02.outside", "铣外", "SelectionChainDialog", "", "选择候选", "无岛简单凸XY闭合轮廓，方向和起点进入最终路径；开放链自动侧别待补", "限定轮廓专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-d02-contour-side-20261004"},
        {"D02.mixed", "混合加工侧", "SelectionChainDialog", "", "选择候选", "显式刀补选择，G40默认左侧；机床补偿D值未验证", "限定轮廓专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-d02-contour-side-20261004"},
        {"D02.undo", "选择撤销", "SelectionChainController", "", "选择会话", "撤销及恢复"},
        {"D02.last", "选择上次", "SelectionChainDialog", "", "已建立工序", "单域及原始整组选区保存恢复并重验；部分删除保留整组，旧项目不猜测历史", "选择历史及整组专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-d02-coplanar-20261005"},
        {"D02.branch", "分支调整", "SelectionChainDialog", "", "拓扑分支", "显式选择分支"},
        {"D02.anchors", "起终点", "SelectionChainDialog", "", "选择链", "起终锚点"},
        {"D02.reverse", "反向", "SelectionChainController", "", "选择链", "方向保留到候选"},
        {"D02.sort", "排序", "SelectionChainController", "", "选择链", "显式顺序"},
        {"D02.coplanar", "同平面选择", "SelectionChainDialog::selectCoplanarRegions", "", "实体源平面", "精确源平面逐域批选、可移除、整组恢复、一域一工序；含岛加工及线框批选待补", "实体源平面子集已验收；证据：D:/yanggao/gcodecnc/outputs/design-d02-coplanar-20261005"},
        {"D02.highlight", "候选及选中高亮", "ViewportWidget", "", "选择会话", "局部预览与选中保留"},
        {"D03.spot", "打点", "MainWindow::beginOperationWorkflow", "hole_spot", "孔", "正面轴向打点", "待完整验收", false, false, "孔类", "打点", true},
        {"D03.g81", "普通钻孔", "MainWindow::beginOperationWorkflow", "hole_g81", "孔", "G81；确认刀具、孔深及安全高度", "待完整验收", false, false, "孔类", "G81", true},
        {"D03.g83", "深孔", "MainWindow::beginOperationWorkflow", "hole_deephole", "孔", "G83 排屑循环", "待完整验收", false, false, "孔类", "G83", true},
        {"D03.peck", "啄钻", "MainWindow::beginOperationWorkflow", "hole_peck", "孔", "既有排屑钻策略", "待完整验收", false, false, "孔类", "啄钻", true},
        {"D03.g73", "断屑钻", "MainWindow::beginOperationWorkflow", "hole_g73", "孔", "G73；回退距离需控制器配置", "待完整验收", false, false, "孔类", "G73", true},
        {"D03.g76", "精镗孔", "MainWindow::beginOperationWorkflow", "hole_g76", "孔", "G76；横向让刀未验证", "待完整验收", false, false, "孔类", "G76", true},
        {"D03.g86", "镗孔", "MainWindow::beginOperationWorkflow", "hole_g86", "孔", "G86；主轴停转退刀未验证", "待完整验收", false, false, "孔类", "G86", true},
        {"D03.ream", "铰孔", "MainWindow::beginOperationWorkflow", "hole_reaming", "孔", "既有铰孔策略", "待完整验收", false, false, "孔类", "铰孔", true},
        {"D03.chamfer", "孔倒角", "MainWindow::beginOperationWorkflow", "hole_chamfer", "孔", "圆周与轴向模式显式确认", "待完整验收", false, false, "孔类", "孔倒角", true},
        {"D03.circular", "圆铣孔", "MainWindow::beginOperationWorkflow", "hole_circular_mill", "孔", "圆插补扩孔", "待完整验收", false, false, "孔类", "圆铣孔", true},
        {"D03.tap", "攻牙", "MainWindow::beginOperationWorkflow", "hole_tapping", "孔", "轨迹示意不等于同步机床验证", "待完整验收", false, false, "牙类", "攻牙", true},
        {"D03.thread_mill", "内外螺纹铣", "", "", "孔或外圆", "待实现", "未验收", false, false, "牙类", "铣牙", false},
        {"D04.face", "平面加工", "MainWindow::beginOperationWorkflow", "mill_face", "平面", "正面可证明平面边界", "待完整验收", false, false, "铣削", "铣平面", true},
        {"D04.open_slot", "开口槽", "MillingOperationDialog", "mill_slot", "槽边", "既有开口槽范围", "待完整验收", false, true},
        {"D04.blind_slot", "盲槽", "MillingOperationDialog", "mill_blind_slot", "闭合槽边", "既有盲槽范围", "待完整验收", true, true},
        {"D04.tapered_slot", "斜底槽", "MillingOperationDialog", "mill_tapered_slot", "闭合槽边", "保留实际斜底约束", "待完整验收", true, true},
        {"D04.pocket_rough", "型腔粗加工", "MainWindow::beginOperationWorkflow", "mill_pocket_rough", "闭合区域", "真实区域及来源证明", "真实四边/圆角子集已验收", true, false, "铣削", "挖槽", true},
        {"D04.rounded_slot", "圆角槽", "MainWindow::beginOperationWorkflow", "mill_pocket_rough", "直线圆弧闭环", "公共浅层开口证明", "真实八段闭环子集已验收", true},
        {"D04.wall_finish", "侧壁精加工", "MainWindow::beginOperationWorkflow", "mill_pocket_finish", "闭合区域", "单一平底凸区域；源模型证明及明确留量", "待完整验收", true, false, "铣削", "侧壁精加工", true},
        {"D04.floor_finish", "底面精加工", "MainWindow::beginOperationWorkflow", "mill_pocket_floor_finish", "闭合区域", "单一平底凸区域；源模型证明及明确留量", "待完整验收", true, false, "铣削", "底面精加工", true},
        {"D04.planar_finish", "平面精加工", "MillingOperationDialog", "mill_surface_finish", "平面", "单Z平面；不代表曲面能力"},
        {"D04.islands", "环形及含岛", "", "", "区域与岛屿", "已有基础算法；完整入口待验收"},
        {"D04.dynamic", "动态铣", "", "", "区域", "待实现负载约束算法", "未验收"},
        {"D05.closed", "闭合轮廓", "MainWindow::beginOperationWorkflow", "mill_closed_contour", "闭合链", "正面二维轮廓", "待完整验收", true, false, "铣削", "外形铣", true},
        {"D05.open", "开放轮廓", "MillingOperationDialog", "mill_open_contour", "开放链", "正面二维轮廓"},
        {"D05.finish", "轮廓精加工", "MillingOperationDialog", "mill_contour_finish", "轮廓", "既有精加工策略"},
        {"D05.circle", "封闭圆铣边", "CircleMillDialog", "mill_circle", "圆", "既有圆轮廓策略"},
        {"D05.chamfer", "外形倒角", "", "", "轮廓", "待实现"},
        {"D05.slope", "正面斜面", "", "", "可达曲面", "待实现有误差界的曲面刀路"},
        {"D05.fillet", "正面圆角", "", "", "可达曲面", "待实现有误差界的曲面刀路"},
        {"D05.engrave", "刻字", "", "", "二维文字轮廓", "待实现"},
        {"D05.spatial_curve", "空间曲线", "", "", "空间曲线", "待实现有误差界的离散"},
        {"D06.line", "直线", "SketchEditorDialog", "", "二维绘图", "原始double首尾点，开放图元编辑保存；加工待验证"},
        {"D06.circle", "圆", "SketchEditorDialog", "", "二维绘图", "原始double圆心半径，独立闭合曲边，编辑保存；曲线加工待验证"},
        {"D06.arc", "圆弧", "SketchEditorDialog", "", "二维绘图", "原始double圆心半径及有向扫角，开放曲边编辑保存；加工待验证"},
        {"D06.rectangle", "矩形", "SketchEditorDialog", "", "二维绘图", "两角绘制、精确坐标、变换和保存；单凸+Z Outside G40已接入加工"},
        {"D06.polyline", "多段线", "SketchEditorDialog", "", "二维绘图", "闭合直线多段线、精确坐标、变换和保存；单凸+Z Outside G40已接入加工"},
        {"D06.trim", "修剪", "SketchTrimDialog", "", "直线、圆、圆弧及闭合直线轮廓边", "有限边界解析求交、明确删除段、预览确认；闭合轮廓拆为独立直线，原目标标注失效"},
        {"D06.extend", "延伸", "SketchTrimDialog", "", "独立直线／圆弧与有限边界", "明确端点及交点，保持方向和解析参数，预览确认；闭合图元不延伸"},
        {"D06.offset", "偏移", "SketchOffsetDialog", "", "直线、圆、圆弧及闭合直线轮廓", "明确距离和侧别、预览后创建独立副本；尖角连接及内缩分裂，保留原件标注"},
        {"D06.fillet", "绘图圆角", "SketchFilletDialog", "", "闭合直线角点／共端点两直线", "明确半径、预览修剪并创建解析相切圆弧，各段独立保存；线弧／弧弧及复合链待支持"},
        {"D06.move", "移动", "SketchEditorDialog", "", "直线、圆、圆弧及闭合直线轮廓", "完整图元精确 XY 位移，保存才提交"},
        {"D06.copy", "复制", "SketchEditorDialog", "", "直线、圆、圆弧及闭合直线轮廓", "保留原件及独立身份副本；多图元只编辑保存"},
        {"D06.rotate", "旋转", "SketchEditorDialog", "", "直线、圆、圆弧及闭合直线轮廓", "显式中心及逆时针角度，保留原始 double"},
        {"D06.mirror", "镜像", "SketchEditorDialog", "", "直线、圆、圆弧及闭合直线轮廓", "显式轴点及方向角，曲线扫角反向，旧加工证据失效"},
        {"D06.distance", "距离测量", "SketchMeasurementDialog", "", "草图解析点", "明确选择两点，直线距离 mm，可保存绑定标注"},
        {"D06.angle", "角度测量", "SketchMeasurementDialog", "", "草图直线边", "按顶点顺序的两边较小夹角 0–180°，可保存绑定标注"},
        {"D06.radius", "半径测量", "SketchMeasurementDialog", "", "草图圆与圆弧", "解析半径 mm，可保存绑定标注"},
        {"D06.coordinate", "坐标测量", "SketchMeasurementDialog", "", "草图解析点", "模型 X/Y/Z，Z为草图平面；不作WCS转换"},
        {"D06.dimension", "标注", "SketchMeasurementDialog", "", "草图测量引用", "创建、编辑、删除、保存重开；几何变化实时重算或明确失效"},
        {"D07.tools", "刀具库", "ToolLibraryPanel", "", "刀具", "编辑保存及工序消费"},
        {"D07.materials", "材料库", "MaterialLibraryDialog", "", "用户材料切削建议", "按策略及刀具规格预览，确认后复制转速/进给到草稿"},
        {"D07.templates", "参数模板", "StrategyPanel", "", "切削参数", "显式应用；不覆盖几何与刀具"},
        {"D07.auto_hole", "自动孔", "MainWindow::previewAutomaticHoles", "", "孔候选", "按孔径深度分组预览，选一组进入参数页，确认后创建"},
        {"D07.auto_chamfer", "智能倒角", "MainWindow::previewSmartChamfer", "hole_chamfer", "普通孔口候选", "分组预览、明确宽度和锥形刀具，参数页确认后创建；外形边面待支持"},
        {"D08.new", "新建工序", "OperationFactory::confirm", "", "确认提案", "显式确认后创建"},
        {"D08.edit", "编辑工序", "OperationEditDialog", "", "工序", "统一参数应用及限制"},
        {"D08.copy", "复制工序", "OperationListPanel::onCopy", "", "选中工序", "新身份、保留来源、需重算", "复制软件专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-foundation-followup-20261004"},
        {"D08.delete", "删除工序", "OperationListPanel::onDelete", "", "选中工序", "已有删除"},
        {"D08.order", "工序排序", "OperationListPanel", "", "工序", "移动及流程排序"},
        {"D08.enabled", "工序启停", "OperationListPanel", "", "工序", "加工启用与路径显示分离", "启停软件专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-foundation-followup-20261004"},
        {"D08.batch", "批量参数", "OperationBatchEditDialog", "", "选中工序", "明确字段、整批校验和提交", "批量参数软件专项已验收；证据：D:/yanggao/gcodecnc/outputs/design-foundation-followup-20261004"},
        {"D08.save", "项目保存", "ProjectManager::saveToFile", "", "项目", "保留参数与来源证明"},
        {"D08.reload", "项目重载", "ProjectManager::loadFromFile", "", "项目", "恢复工序与身份"},
        {"D08.recalculate", "工序重算", "OperationListPanel::onRecalculate", "", "工序", "修改影响最终代码"},
        {"D08.stale", "失效管理", "ProgramSnapshotStatus", "", "程序快照", "来源、参数及刀具变化后阻断旧程序"},
        {"D09.generate", "生成代码", "ProgramGenerationService", "", "已确认工序", "CQ8为主要新增验收目标；保留其他后处理"},
        {"D09.check", "最终代码检查", "ProgramGenerationService", "", "最终代码", "型腔区域运动与解析门禁", "运动专项已验收"},
        {"D09.export", "程序导出", "ProgramPackageExporter", "", "程序快照", "程序状态门禁"},
        {"D09.simulate", "轨迹仿真", "SimulationController", "", "最终代码", "完整已知XYZ运动；不支持运动拒绝", "运动专项已验收"},
        {"D09.import", "外部代码导入", "MainWindow::loadExternalGCodeFile", "", "外部NC", "独立来源，只读回放", "两份26段已验收"},
        {"D09.sections", "分段回放", "SimulationController::externalPrograms", "", "外部NC归档", "段间断开及全局行号", "两份26段已验收"}
    };
    return entries;
}

inline const DesignFeatureEntry *designFeatureForStrategy(const QString &strategy)
{
    if (strategy.isEmpty()) return nullptr;
    for (const auto &entry : designFeatureCatalog())
        if (entry.strategy == strategy) return &entry;
    return nullptr;
}
