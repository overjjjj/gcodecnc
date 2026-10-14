#pragma once

#include <QMessageBox>
#include <QAbstractButton>
#include <QRegularExpression>
#include <QTextEdit>
#include <QTimer>

// Presentation only. Callers retain the original validation and return paths.
namespace OperationMessageBox {
inline QString summary(const QString &raw)
{
    struct Message { const char *key; const char *text; };
    static const Message messages[] = {
        {"FINAL_PROGRAM_WRITE_FAILED", "最终 G 代码保存失败。请检查目标文件夹权限、文件占用和磁盘空间，或另选位置保存。"},
        {"Tool number must be a positive integer", "刀具编号必须是正整数。请修改刀具编号。"},
        {"Tool dimensions must be finite and nonnegative", "刀具尺寸必须是有效的非负数。请检查直径、刃长和总长等尺寸。"},
        {"Flute count must be nonnegative", "刀具刃数不能为负数。请修改刃数。"},
        {"Invalid tool entry or duplicate tool number", "刀库文件中存在无效刀具或重复编号，当前刀库已保留。请修正文件后重新加载。"},
        {"Missing required safe-start code", "机床配置缺少必要的安全起始指令。请根据详情补全安全起始行。"},
        {"Incremental coordinate mode G91", "当前机床配置不允许使用 G91 增量坐标模式。请改用 G90 绝对坐标模式。"},
        {"Missing work offset G54-G59", "机床配置缺少工件坐标系。请选择 G54 至 G59 中的坐标系。"},
        {"Machine Profile ID is required", "机床配置编号为空。请填写配置编号。"},
        {"Machine Profile version is required", "机床配置版本为空。请填写版本。"},
        {"Controller ID is required", "控制系统编号为空。请填写控制系统编号。"},
        {"POCKET_SINGLE_FLAT_FLOOR_UNPROVEN", "暂时无法确认槽底是完整平面，因此不能生成当前工序。请检查并重新选择完整槽区域；若槽底有台阶或曲面，需选择支持该形状的加工方式。"},
        {"POCKET_FINISH_ALLOWANCES_AND_VERTICAL_ENTRY_REQUIRED", "当前精加工需要明确的侧壁、底部余量和垂直下刀条件。请核对精加工参数及已清空的下刀区域。"},
        {"POCKET_OPENING_NEGATIVE_ALLOWANCE", "加工余量或深度无效。请确认余量为有效非负数，且加工深度大于底部余量。"},
        {"POCKET_OPENING_SOURCE_PROOF_MISMATCH", "当前槽区域的来源校验与模型不一致，不能继续生成。请重新选择完整槽区域并重新计算工序。"},
        {"POCKET_FLOOR_SOURCE_PROOF_MISMATCH", "槽底来源校验与当前模型不一致。请重新选择完整槽区域并重新计算工序。"},
        {"POCKET_OPENING_REGION_MISMATCH", "当前工序区域与已验证的槽边界不一致。请重新核对选区及加工参数。"},
        {"POCKET_OPENING_EVIDENCE_INVALID", "当前槽选区缺少完整的边界校验信息。请重新选择完整槽区域并确认工序。"},
        {"REGION_INNER_WIRE_REQUIRED", "当前选择不能作为完整的槽口边界使用。请选择槽口的完整闭合边界。"},
        {"REGION_CHAIN_NOT_CLOSED", "所选边界没有组成连续闭合环。请检查断点并重新选择完整边界。"},
        {"REGION_SETUP_DIRECTION_MISMATCH", "当前区域方向与加工装夹方向不匹配。请核对加工正面和选区。"},
        {"REGION_ISLAND_REQUIRES_SURROUNDING_OUTER", "当前选择是岛屿边界，缺少外围加工区域。请检查并选择包含该岛屿的完整槽区域。"},
        {"REGION_EXACT_GEOMETRY_UNAVAILABLE", "当前无法取得精确模型几何，不能完成区域校验。请检查模型导入结果和程序的几何支持。"},
        {"REGION_POSITIVE_DEPTH_UNPROVEN", "暂时无法确认该区域的有效加工深度。请检查模型区域和深度参数。"},
        {"SELECTION_SOURCE_STALE", "选区对应的模型已变化。请重新选择加工区域并确认工序。"},
        {"SELECTION_SETUP_MISMATCH", "选区对应的装夹设置已变化。请核对当前装夹后重新选择加工区域。"},
        {"SELECTION_EVIDENCE_REQUIRED", "尚未明确选择加工区域。请在模型中选择并确认要加工的区域。"},
        {"SELECTION_TOPOLOGY_INSUFFICIENT", "当前选区的边界信息不足。请重新选择完整的面或连续边界。"},
        {"SELECTION_DIRECTION_UNREACHABLE", "当前装夹方向无法满足所选区域的加工方向要求。请核对加工正面和刀轴方向。"},
        {"Explicit user confirmation is required", "工序尚未确认。请核对参数后点击确认创建工序。"},
        {"A machining strategy is required", "尚未选择加工方式。请先选择适合当前区域的加工方式。"},
        {"A valid tool is required", "尚未选择有效刀具。请从刀库选择刀具并核对尺寸。"},
        {"Hole geometry is invalid", "孔的尺寸或深度无效。请检查所选孔及加工深度。"},
        {"Contour geometry is invalid", "当前轮廓几何无法用于此工序。请检查并重新选择完整加工区域。"},
        {"Pocket entry method", "槽加工的切入方式尚未明确或不受支持。请在参数页选择切入方式并核对下刀条件。"},
        {"snapshot is no longer current", "当前 G 代码快照已过期，不能导出或发送到机床。请按当前模型、工序、刀具和装夹设置重新生成程序。"},
        {"no longer matches its generation evidence", "最终代码或程序包与生成记录不一致，不能输出。请重新生成程序。"},
        {"final motion check is incomplete", "最终刀路检查尚未完成或发现问题，不能输出。请查看详情，核对刀具、模型和加工参数后重新生成并检查。"},
        {"failed safety checks", "最终 G 代码未通过安全检查，不能输出。请查看详情并修正相关参数后重新生成程序。"},
        {"until the front face, origin", "装夹设置尚未完整确认，不能输出。请确认加工正面、原点、工件坐标系和毛坯余量。"},
        {"Failed to save project", "项目保存失败。请检查目标文件夹是否可写、磁盘空间是否充足，或另选保存位置。"},
        {"Failed to load project file", "项目文件未能打开。请检查文件是否完整，并选择有效的项目文件。"},
        {"Failed to apply project data", "项目数据未能载入。请检查项目文件，或尝试打开备份。"},
        {"Cannot fingerprint the source STEP", "无法读取源模型以完成一致性校验。请检查源 STEP 文件是否可访问。"},
        {"Cannot open the source STEP", "无法打开项目关联的源模型。请检查文件位置和读取权限，必要时重新关联源 STEP 文件。"},
        {"does not reference a source STEP", "项目缺少关联的源模型。请重新关联所需的 STEP 文件。"},
        {"source STEP file has changed", "项目关联的源模型已变化，旧工序暂不能使用。请重新关联并识别模型，再核对工序。"},
        {"sketch project cannot be relinked", "二维草图项目不能关联 STEP 模型。请打开正确的草图项目，或另建模型项目。"},
        {"STEP re-import failed", "项目的源模型重新导入失败。请检查 STEP 文件是否完整，或选择可读取的源模型。"},
        {"STEP 导入失败", "模型导入失败。请检查 STEP 文件是否完整、可读取，并查看详情中的导入诊断。"},
        {"Cannot write Program Package", "程序包文件写入失败。请检查目标文件夹权限及磁盘空间，或另选位置重新导出完整程序包。"},
        {"Cannot commit Program Package", "程序包保存未完成，可能已有部分文件写入。请查看详情中的已写文件清单，检查文件占用及磁盘空间后重新导出整包；本次结果不能作为完整程序包使用。"},
        {"CQ8 package", "CQ8 程序包未通过结构校验。请查看详情并重新生成、导出完整程序包。"},
        {"Tool changes are available in this session", "刀具修改已在本次会话生效，但尚未保存到磁盘，重启后这些修改会丢失。请检查刀库文件的写入权限和磁盘空间后重新保存。"},
        {"The current tool library was kept", "刀库文件加载失败，当前刀库保持原样。请检查刀库文件是否完整、可读取，并查看详情。"},
        {"no confirmed operation provenance", "当前程序缺少已确认的工序来源，不能发送到机床。请从已确认的工序重新生成程序。"},
        {"must be numeric", "参数中存在非数字输入。请根据详情检查对应参数并填写有效数字。"},
        {"must be greater than 0", "参数必须大于 0。请根据详情检查对应参数并修改数值。"},
        {"cannot be less than 0", "参数不能小于 0。请根据详情检查对应参数并修改数值。"},
        {"must be an integer from G54-G59", "工件坐标系无效。请选择 G54 至 G59 中的整数编号。"},
        {"Only absolute depth mode", "当前仅支持绝对深度模式。请将深度模式设为 0，并核对深度。"},
        {"Chamfer mode must be", "倒角方式无效。请选择 0（圆周 G1/G2）或 1（轴向 G81）。"},
        {"Program Package", "程序包未能通过导出检查。请查看详情，重新生成并导出完整程序包。"},
        {"CQ8 main program", "CQ8 主程序与宏调用未通过检查。请查看详情并重新生成完整程序包。"},
        {"CQ8 macro", "CQ8 宏文件未通过检查。请查看详情并重新生成完整程序包。"}
    };
    QStringList result;
    for(const auto &message:messages)if(raw.contains(QString::fromLatin1(message.key)) &&
        !result.contains(QString::fromUtf8(message.text)))result.append(QString::fromUtf8(message.text));
    if(!result.isEmpty())return result.join(QStringLiteral("\n\n"));
    if(raw.contains("Tool number") && raw.contains("already exists"))return QStringLiteral("该刀具编号已存在。请使用其他编号，或编辑已有刀具。");
    if(raw.contains("Tool number") && raw.contains("does not exist"))return QStringLiteral("要更新的刀具已不在当前刀库中。请刷新刀库后重新选择刀具。");
    const QRegularExpression chinese(QStringLiteral("[\\x{4e00}-\\x{9fff}]"));
    const QRegularExpression code(QStringLiteral("\\b[A-Z][A-Z0-9]+(?:_[A-Z0-9]+)+\\b"));
    const QRegularExpression sentence(QStringLiteral("\\b[A-Za-z]{3,} [A-Za-z]{3,} [A-Za-z]{3,}\\b"));
    if(chinese.match(raw).hasMatch() && !code.match(raw).hasMatch() && !sentence.match(raw).hasMatch())
        return raw.contains(QStringLiteral("请")) ? raw : raw + QStringLiteral("\n请检查上述项目后再试。");
    if(raw.contains("POCKET_") || raw.contains("REGION_") || raw.contains("CURVE_"))
        return QStringLiteral("当前加工区域未通过几何校验，不能继续此操作。请检查所选区域是否完整、形状是否受当前加工方式支持，并查看详情。下次操作仍需通过校验。");
    if(raw.contains("SELECTION_"))return QStringLiteral("当前选区未通过校验。请核对模型、装夹及所选边界，重新选择后再确认工序。");
    return QStringLiteral("当前操作未完成。请查看详情中的诊断信息，核对输入后再试；若仍失败，请复制详情反馈。");
}

inline QMessageBox::StandardButton show(QMessageBox::Icon icon,QWidget *parent,
                                        const QString &title,const QString &raw)
{
    const bool chinese=title.contains(QRegularExpression(QStringLiteral("[\\x{4e00}-\\x{9fff}]")));
    QMessageBox box(icon,chinese?title:QStringLiteral("操作未完成"),summary(raw),QMessageBox::Ok,parent);
    box.setTextFormat(Qt::PlainText);
    box.setDetailedText(raw);
    box.button(QMessageBox::Ok)->setText(QStringLiteral("确定"));
    for(auto *button:box.buttons())if(box.standardButton(button)==QMessageBox::NoButton) {
        button->setText(QStringLiteral("查看详情"));
        QObject::connect(button,&QAbstractButton::clicked,&box,[&box,button]{
            QTimer::singleShot(0,&box,[&box,button]{
                auto *details=box.findChild<QTextEdit *>();
                button->setText(details && details->isVisible()?QStringLiteral("收起详情"):QStringLiteral("查看详情"));
            });
        });
    }
    box.exec();return QMessageBox::Ok;
}
inline QMessageBox::StandardButton warning(QWidget *parent,const QString &title,const QString &raw)
{ return show(QMessageBox::Warning,parent,title,raw); }
inline QMessageBox::StandardButton critical(QWidget *parent,const QString &title,const QString &raw)
{ return show(QMessageBox::Critical,parent,title,raw); }
}
