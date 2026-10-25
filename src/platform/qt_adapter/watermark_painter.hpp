// ============================================================
// scToLocalDesk | src/platform/qt_adapter/watermark_painter.hpp
// 会话水印贴片渲染（PRD P4 被控端知情权）：
//   "访客别名 · yyyy-MM-dd HH:mm" 半透明文字，右下角贴进推流帧
// QImage::Format_ARGB32 小端内存布局即 BGRA，可直接喂
// ScreenStreamService::set_watermark，无需 swizzle
// ============================================================
#pragma once

#include <QImage>
#include <QString>

namespace sc::localdesk::platform {

/// 渲染水印贴片（含透明边距）；peer_alias 为空返回空 QImage
QImage render_session_watermark(const QString& peer_alias);

}  // namespace sc::localdesk::platform
