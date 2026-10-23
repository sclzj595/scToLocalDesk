// ============================================================
// scToLocalDesk | src/ui/qwidget/glass_theme.h
// QWidget 版全局主题（Doc13 §2/§7）：双主题色板 + QSS 生成
// 红线3：色值全部集中本文件，QSS/控件禁止散落硬编码
// 注：QSS 无背景模糊能力，玻璃观感以半透底+描边模拟（Doc13 §13.2）
// ============================================================
#pragma once

#include <QColor>
#include <QString>

namespace sc::localdesk::ui {

class GlassTheme final {
public:
    // ---------------- 固定参数（Doc13 §1.2，与 QML 版一致） ----------------
    static constexpr int kGlassBlur = 18;      ///< 模糊半径基准（记录用）
    static constexpr int kWinRadius = 20;      ///< 主窗口圆角
    static constexpr int kPanelRadius = 16;    ///< 大面板圆角
    static constexpr int kCardRadius = 12;     ///< 卡片/弹窗圆角
    static constexpr int kBtnRadius = 8;       ///< 按钮/输入框圆角
    static constexpr int kMenuRadius = 6;      ///< 菜单条目圆角
    static constexpr int kMenuBarHeight = 32;  ///< 顶部菜单栏高度
    static constexpr int kDockWidth = 72;      ///< 左侧 Dock 宽度

    // ---------------- 动效（Doc13 §6） ----------------
    static constexpr int kDurHover = 120;
    static constexpr int kDurPopup = 180;
    static constexpr int kDurPage = 220;

    static GlassTheme& instance();

    bool isDark() const {
        return dark_;
    }
    void setDark(bool dark);
    void toggleDark() {
        setDark(!dark_);
    }

    // ---------------- 主色调（Doc13 §2.1） ----------------
    QColor primary() const {
        return {"#9D4EDD"};
    }
    QColor primaryHover() const {
        return {"#AC66E4"};
    }
    QColor primaryDisabled() const {
        return {0x9D, 0x4E, 0xDD, 115};
    }
    QColor accentBlue() const {
        return {"#3A86FF"};
    }
    QColor accentRed() const {
        return {"#F72585"};
    }
    QColor accentGreen() const {
        return {"#4CC9F0"};
    }

    // ---------------- 双主题基础色（Doc13 §2.2） ----------------
    QColor winBg() const {
        return dark_ ? QColor{"#15171C"} : QColor{"#F7F9FC"};
    }
    /// 玻璃半透底（QSS 无模糊，以纯色模拟磨砂观感）。浅色必须与 winBg 拉开
    /// 明度差：QML 玻璃采样真实桌面天然带灰调，与内容区分界清晰；纯白半透
    /// 叠在 winBg 上会同色无界，Dock 视觉上渗入内容区
    QColor glassBg() const {
        return dark_ ? QColor(30, 33, 40, 217) : QColor(236, 239, 243, 242);
    }
    QColor glassBorder() const {
        return dark_ ? QColor(255, 255, 255, 90) : QColor(255, 255, 255, 191);
    }
    QColor textMain() const {
        return dark_ ? QColor{"#F2F3F5"} : QColor{"#1D2129"};
    }
    QColor textSecond() const {
        return dark_ ? QColor{"#909399"} : QColor{"#6E7681"};
    }
    QColor textHint() const {
        return dark_ ? QColor{"#6B7280"} : QColor{"#A0A8B3"};
    }
    QColor divider() const {
        return dark_ ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 20);
    }
    QColor hoverBg() const {
        return dark_ ? QColor(255, 255, 255, 31) : QColor(0, 0, 0, 15);
    }
    QColor cardBg() const {
        return dark_ ? QColor{"#1E2128"} : QColor{"#FFFFFF"};
    }
    QColor cardBorder() const {
        return dark_ ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 15);
    }

    /// 全局 QSS（一次性 setStyleSheet 到 QApplication）
    QString buildQss() const;

private:
    GlassTheme() = default;
    bool dark_{false};
};

}  // namespace sc::localdesk::ui
