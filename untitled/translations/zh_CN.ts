<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>MainWindow</name>
    <message><source>CNEXT-CAM — 蒋巨人</source><translation>CNEXT-CAM — 蒋巨人</translation></message>
    <message><source>导入 STEP...</source><translation>导入 STEP...</translation></message>
    <message><source>打开项目...</source><translation>打开项目...</translation></message>
    <message><source>保存项目</source><translation>保存项目</translation></message>
    <message><source>导出 G 代码...</source><translation>导出 G 代码...</translation></message>
    <message><source>退出</source><translation>退出</translation></message>
    <message><source>复位视角</source><translation>复位视角</translation></message>
    <message><source>中文</source><translation>中文</translation></message>
    <message><source>English</source><translation>English</translation></message>
    <message><source>语言</source><translation>语言</translation></message>
    <message><source>关于</source><translation>关于</translation></message>
    <message><source>帮助</source><translation>帮助</translation></message>
    <message><source>主工具栏</source><translation>主工具栏</translation></message>
    <message><source>孔特征</source><translation>孔特征</translation></message>
    <message><source>加工策略</source><translation>加工策略</translation></message>
    <message><source>刀具库</source><translation>刀具库</translation></message>
    <message><source>文件</source><translation>文件</translation></message>
    <message><source>视图</source><translation>视图</translation></message>
    <message><source>导入 STEP 文件</source><translation>导入 STEP 文件</translation></message>
    <message><source>STEP 文件 (*.step *.stp *.STEP *.STP);;所有文件 (*)</source><translation>STEP 文件 (*.step *.stp *.STEP *.STP);;所有文件 (*)</translation></message>
    <message><source>正在加载 STEP …</source><translation>正在加载 STEP …</translation></message>
    <message><source>CNEXT 项目 (*.cnext)</source><translation>CNEXT 项目 (*.cnext)</translation></message>
    <message><source>打开项目</source><translation>打开项目</translation></message>

    <message><source>导出 G 代码</source><translation>导出 G 代码</translation></message>
    <message><source>G 代码 (*%1);;所有文件 (*)</source><translation>G 代码 (*%1);;所有文件 (*)</translation></message>
    <message><source>G代码已导出: %1</source><translation>G代码已导出: %1</translation></message>
    <message><source>重启后生效。</source><translation>重启后生效。</translation></message>
    <message><source>Language</source><translation>语言</translation></message>
    <message><source>Please restart to apply.</source><translation>请重启以应用语言设置。</translation></message>
    <message><source>关于 CNEXT-CAM</source><translation>关于 CNEXT-CAM</translation></message>
    <message><source>&lt;b&gt;CNEXT-CAM&lt;/b&gt;&lt;br&gt;蒋巨人 CNC 加工中心 CAM 软件&lt;br&gt;&lt;br&gt;版本: 0.1.0&lt;br&gt;编译: Qt %1&lt;br&gt;© 2024 蒋巨人</source><translation>&lt;b&gt;CNEXT-CAM&lt;/b&gt;&lt;br&gt;蒋巨人 CNC 加工中心 CAM 软件&lt;br&gt;&lt;br&gt;版本: 0.1.0&lt;br&gt;编译: Qt %1&lt;br&gt;© 2024 蒋巨人</translation></message>
    <message><source>已加载: %1</source><translation>已加载: %1</translation></message>
    <message><source>G代码已生成</source><translation>G代码已生成</translation></message>
    <message><source>错误: %1</source><translation>错误: %1</translation></message>
    <message><source>错误</source><translation>错误</translation></message>
</context>
<context>
    <name>FeatureListPanel</name>
    <message><source>识别特征</source><translation>识别特征</translation></message>
</context>
<context>
    <name>StrategyPanel</name>
    <message><source>加工策略:</source><translation>加工策略:</translation></message>
    <message><source>刀具:</source><translation>刀具:</translation></message>
    <message><source>参数</source><translation>参数</translation></message>
    <message><source>值</source><translation>值</translation></message>
    <message><source>生成G代码</source><translation>生成G代码</translation></message>
</context>
<context>
    <name>ToolLibraryPanel</name>
    <message><source>刀具库</source><translation>刀具库</translation></message>
</context>
<context>
    <name>BottomBar</name>
    <message><source>就绪</source><translation>就绪</translation></message>
</context>
<context>
    <name>ChamferStrategy</name>
    <message><source>孔倒角</source><translation>孔倒角</translation></message>
</context>
<context>
    <name>ReamingStrategy</name>
    <message><source>铰孔</source><translation>铰孔</translation></message>
</context>
<context>
    <name>SiemensPostProcessor</name>
    <message><source>西门子 840D</source><translation>西门子 840D</translation></message>
</context>
<context>
    <name>QObject</name>
    <message><source>No model is available for geometric checks; trajectory playback only.</source><translation>没有可用于几何检查的模型；仅能播放轨迹。</translation></message>
    <message><source>The model has no usable XY surface coverage for rapid-motion checks.</source><translation>模型缺少可用的 XY 表面，快速移动检查未完成。</translation></message>
    <message><source>Non-finite motion coordinate near G-code line %1.</source><translation>NC 第 %1 行附近存在非有限运动坐标。</translation></message>
    <message><source>Collision warning: tool path goes far below stock bottom near G-code line %1.</source><translation>碰撞警告：NC 第 %1 行附近的刀路明显低于模型底面。</translation></message>
    <message><source>Initial positioning starts at an unknown machine position; only subsequent known segments were checked.</source><translation>初始机床位置未知；仅检查了后续坐标已知的运动段。</translation></message>
    <message><source>Some motion lacks known XYZ endpoints and could not be checked.</source><translation>部分运动段的 XYZ 端点坐标未知，检查未完成。</translation></message>
    <message><source>Tool diameter is missing or invalid; only tool-center samples are available.</source><translation>刀具直径缺失、无效或不足以采样；仅有刀心检查，不能输出。</translation></message>
    <message><source>Geometric check budget exceeded; remaining surface samples were not checked.</source><translation>几何检查预算已耗尽；剩余表面采样未完成，不能输出。</translation></message>
    <message><source>A vertical retract starts at surface contact and moves away; clearance at its start is not established.</source><translation>竖直退刀从表面接触处向上离开；起点尚无净空，仅完成采样初筛。</translation></message>
    <message><source>Collision warning: sampled tool footprint approaches the model surface near G-code line %1.</source><translation>碰撞警告：NC 第 %1 行附近的刀尖截面采样接近或进入模型表面。</translation></message>
    <message><source>Collision warning: rapid Z move approaches the model surface near G-code line %1.</source><translation>碰撞警告：NC 第 %1 行附近的 Z 向快速移动接近或进入模型表面。</translation></message>
    <message><source>No fully known motion segment was available for geometric checks.</source><translation>没有坐标完全已知的运动段，几何检查未完成。</translation></message>
</context>
<context>
    <name>MainWindow</name>
    <message><source>Geometric check blocks output</source><translation>几何检查阻断输出</translation></message>
    <message><source>Cannot %1 because the final motion check is incomplete or has reported a problem.

%2</source><translation>最终运动检查未完成或发现问题，不能执行 %1。

%2</translation></message>
    <message><source>Trajectory playback ended. %1</source><translation>轨迹播放结束。%1</translation></message>
</context>
</TS>
