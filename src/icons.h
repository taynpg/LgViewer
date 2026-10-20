#pragma once

#include <QColor>
#include <QIcon>

class QPainter;

namespace lv {

// 菜单/工具栏图标：全部用 QPainter 画出，颜色取自系统调色板，
// 自动适配深浅色主题和高分屏（无外部图片资源）。
enum class Icon {
    Open,          // 打开文件（文件夹）
    Reload,        // 重新加载（环形箭头）
    CloseFile,     // 关闭文件（文档 + ×）
    Quit,          // 退出（门 + 箭头）
    Copy,          // 复制（两页）
    SelectAll,     // 全选（虚线框）
    Filter,        // 筛选（漏斗）
    ClearFilter,   // 清除筛选（漏斗 + ×）
    GotoLine,      // 跳到行（下箭头 + 横线）
    ZoomIn,        // 放大（放大镜 +）
    ZoomOut,       // 缩小（放大镜 −）
    ZoomReset,     // 重置字号（1:1）
    Highlight,     // 高亮（荧光笔）
    ScrollTop,     // 回到开头（双上箭头）
    ScrollBottom,  // 跳到末尾（双下箭头）
    Regex,         // 正则测试（.*）
    Book,          // 语法速查（打开的书）
    Spark,         // 让 AI 帮写（魔棒 + 星芒）
    NewProfile,    // 新建配置（方框 +）
    EditProfile,   // 编辑配置（铅笔）
    DeleteProfile, // 删除配置（垃圾桶）
    About,         // 关于（i 圆圈）
    Find,          // 查找（放大镜）
    ArrowUp,       // 上一个命中（向上折线）
    ArrowDown,     // 下一个命中（向下折线）
    CloseMark,     // 关闭（×）
};

QIcon icon(Icon id);
// 单个图标按指定颜色绘制，供需要同色系组合的场景复用
void   paintGlyph(Icon id, QPainter *painter, const QColor &color, int px);
// 调色板变化（切换控件风格 / 深浅色）后清掉缓存，下次 icon() 按新颜色重绘
void   clearIconCache();

} // namespace lv
