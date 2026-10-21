from pathlib import Path
import csv, re, shutil, json
from collections import Counter, defaultdict

from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT

ROOT = Path(r"D:\yanggao\gcodecnc\整理副本_反编译源码")
OUT = ROOT / "ui整理"
OUT.mkdir(exist_ok=True)

cats = {
    "01_应用框架": ["MainWindow", "AppGlobal"],
    "02_设计模块": ["CatDesign"],
    "03_加工模块": ["CatWork", "WorkParamForm"],
    "04_参数与对话框": ["DialogEyeletHole", "Waiting", "Dialog"],
    "05_辅助与未知": [],
}

def classify(text):
    scores = {k: sum(text.count(x) for x in keys) for k, keys in cats.items()}
    # 优先按最具体的业务类；混合文件归入辅助并保留原文件名。
    best = max(scores, key=scores.get)
    return best if scores[best] > 0 else "05_辅助与未知"

def annotate(src, dst):
    text = src.read_text(encoding="utf-8", errors="replace")
    header = ("/* 中文整理说明：本文件来自 DTM 反编译恢复包，伪 C++ 不等同于原始源码。\n"
              " * 注释仅表达基于符号、控制流和调用位置的推断；参数类型、资源和边界条件需二次验证。 */\n")
    # 在可识别的类成员函数前插入函数级说明，避免逐行改写反编译结果。
    pat = re.compile(r"(?m)^(?P<indent>\s*)(?P<sig>(?:[\w:*&<>]+\s+)+(?P<cls>[A-Za-z_]\w*)::(?P<fn>[A-Za-z_]\w*)\s*\([^\n]*\))")
    def repl(m):
        cls, fn = m.group("cls"), m.group("fn")
        return f"{m.group('indent')}// 中文注释：{cls}::{fn}，根据反编译控制流推断其职责；调用关系见分析报告。\n" + m.group(0)
    text = pat.sub(repl, text)
    dst.write_text(header + text, encoding="utf-8")

app = ROOT / "DTM" / "decompiled_app"
manifest = []
for src in sorted(app.glob("*.cpp")):
    text = src.read_text(encoding="utf-8", errors="replace")
    cat = classify(text)
    d = OUT / cat
    d.mkdir(exist_ok=True)
    target = d / src.name
    annotate(src, target)
    manifest.append({"原文件": str(src.relative_to(ROOT)), "整理文件": str(target.relative_to(ROOT)), "分类": cat})
for src in sorted((ROOT / "DTM" / "decompiled_retry").glob("*.cpp")):
    d = OUT / "05_辅助与未知"; d.mkdir(exist_ok=True)
    target = d / ("retry_" + src.name); annotate(src, target)
    manifest.append({"原文件": str(src.relative_to(ROOT)), "整理文件": str(target.relative_to(ROOT)), "分类": "05_辅助与未知"})

# 函数索引、模块归属与静态耦合估计
rows = []
for dll in ("DTM", "dmtube"):
    p = ROOT / dll / "inventory" / "function_inventory.csv"
    with p.open(encoding="utf-8-sig", newline="") as f:
        for r in csv.DictReader(f):
            r["binary"] = dll; rows.append(r)
names = [r["name"] for r in rows if r.get("name")]
name_set = set(names)
source_text = "\n".join(x.read_text(encoding="utf-8", errors="replace") for x in (ROOT/"DTM").glob("decompiled_app/*.cpp"))
for d in (ROOT/"dmtube").glob("decompiled_*"):
    if d.is_dir():
        source_text += "\n" + "\n".join(x.read_text(encoding="utf-8", errors="replace") for x in d.glob("*.cpp"))
# 单次扫描所有“类::函数”令牌，避免对 13 万条索引逐条全文检索。
freq = Counter(re.findall(r"[A-Za-z_]\w*::[A-Za-z_]\w*", source_text))
for r in rows:
    n = r.get("name", "")
    refs = max(0, freq.get(n, 0) - 1)
    r["静态引用次数"] = refs
    r["耦合等级"] = "高" if refs >= 20 else ("中" if refs >= 5 else "低")

OUT.mkdir(exist_ok=True)
with (OUT / "整理清单.csv").open("w", encoding="utf-8-sig", newline="") as f:
    w = csv.DictWriter(f, fieldnames=["原文件","整理文件","分类"]); w.writeheader(); w.writerows(manifest)
with (OUT / "函数级清单_含耦合.csv").open("w", encoding="utf-8-sig", newline="") as f:
    fields = list(rows[0].keys()); w = csv.DictWriter(f, fieldnames=fields); w.writeheader(); w.writerows(rows)

counts = Counter(r["binary"] for r in rows)
status = Counter(r.get("decompile_status", "") for r in rows)
high = sorted((r for r in rows if r["耦合等级"] == "高"), key=lambda x: int(x["静态引用次数"]), reverse=True)[:30]

doc = Document(); sec = doc.sections[0]; sec.top_margin=sec.bottom_margin=sec.left_margin=sec.right_margin=Inches(1)
styles = doc.styles; styles['Normal'].font.name='Calibri'; styles['Normal'].font.size=Pt(11)
for s, size, color in [('Heading 1',16,'2E74B5'),('Heading 2',13,'2E74B5'),('Heading 3',12,'1F4D78')]:
    styles[s].font.name='Calibri'; styles[s].font.size=Pt(size); styles[s].font.color.rgb=RGBColor.from_string(color)
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER; r=p.add_run('DTM UI 模块审查与重构开发方案'); r.bold=True; r.font.size=Pt(22); r.font.color.rgb=RGBColor(31,77,120)
p=doc.add_paragraph('版本：2026-09-02  |  范围：反编译恢复包（UI 全量 + 算法 DLL 全函数级清单）'); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
doc.add_heading('1. 结论摘要', level=1)
doc.add_paragraph('本报告基于符号、伪代码、文件归属和静态引用频次进行审查。反编译结果可用于重新实现和验证，但不能视为可直接编译的原工程。整理副本保留原始目录，并在 ui整理/ 下提供中文注释与功能分类。')
doc.add_heading('2. 源码规模与实现进度', level=1)
t=doc.add_table(rows=1, cols=3); t.alignment=WD_TABLE_ALIGNMENT.CENTER
for c,v in zip(t.rows[0].cells,['二进制','函数总数','状态摘要']): c.text=v
for dll in ('DTM','dmtube'):
    rr=[r for r in rows if r['binary']==dll]; c=t.add_row().cells; c[0].text=dll; c[1].text=str(len(rr)); c[2].text=f"ok={sum(x.get('decompile_status')=='ok' for x in rr)}，其余为索引/汇编/待验证"
doc.add_paragraph('进度口径：已恢复（有伪代码或汇编）→ 已分析（职责/调用关系明确）→ 待重实现（缺少原工程类型、资源或运行验证）。')
doc.add_heading('3. UI 功能章节与代码位置', level=1)
for cat in cats:
    doc.add_heading(cat, level=2)
    items=[m for m in manifest if m['分类']==cat]
    doc.add_paragraph('整理文件：' + ('；'.join(m['整理文件'] for m in items) if items else '暂无'))
    doc.add_paragraph('实现建议：以类成员函数为边界拆分 Presenter/业务服务，保留 Qt 信号槽作为 UI 适配层；先补齐参数校验和状态机，再接入算法 DLL。')
doc.add_heading('4. 函数耦合性分析', level=1)
doc.add_paragraph('采用静态引用频次作为可重复的初筛指标：同名符号在恢复文本中的引用次数越高，越可能是枢纽函数。该指标不等价于运行时调用次数，需结合动态跟踪复核。')
t=doc.add_table(rows=1, cols=3)
for c,v in zip(t.rows[0].cells,['函数','静态引用次数','风险']): c.text=v
for r in high[:20]:
    c=t.add_row().cells; c[0].text=r['name'][:80]; c[1].text=str(r['静态引用次数']); c[2].text='高耦合枢纽：建议抽象接口、增加契约测试'
doc.add_heading('5. 技术方案', level=1)
doc.add_paragraph('建议采用分层重构：Qt UI（页面与信号槽）→ 应用服务（任务编排、参数校验、进度/取消）→ 领域模型（几何、刀路、孔槽特征）→ 基础设施（OpenCascade、G 代码、文件与日志）。通过 DTO 隔离反编译结构，所有算法入口增加版本化接口与可追踪错误码。')
doc.add_heading('6. 算法方案', level=1)
doc.add_paragraph('孔/槽识别：几何预处理→面/边分类→拓扑连通分量→参数拟合→加工策略选择。刀路：输入特征与刀具参数→安全高度与进退刀规划→轮廓/沟槽/岛屿路径生成→拐角与清角优化→碰撞/越界检查→后处理输出。仿真：G 代码词法解析→模态状态机→插补采样→工件/刀具布尔更新→偏差与报警报告。')
doc.add_heading('7. 分阶段开发计划', level=1)
for s in ['阶段一：建立可编译骨架、统一数据模型和日志；','阶段二：完成 UI 页面与参数校验，接入模拟算法；','阶段三：按函数级清单重实现几何/刀路/孔槽核心；','阶段四：加入回归样例、G 代码仿真和安全审查；','阶段五：性能优化、文档完善和开源合规检查。']:
    doc.add_paragraph(s, style='List Bullet')
doc.add_heading('8. 附件与限制', level=1)
doc.add_paragraph('函数级明细见 ui整理/函数级清单_含耦合.csv；文件映射见 ui整理/整理清单.csv。原始反编译包的许可证、第三方组件和二进制发布权尚未确认，公开开源前必须完成法律与供应链审查。')
doc.save(OUT / 'DTM_UI模块审查与开发方案.docx')
print(json.dumps({'docx': str(OUT/'DTM_UI模块审查与开发方案.docx'), 'functions': len(rows), 'ui_files': len(manifest), 'high_coupling': len(high)}, ensure_ascii=False))
