/**
 * @file qt_utils.h
 * @brief 后台 C++ 标准库类型与 Qt 特有类型之间的泛型转换工具库。
 */

#pragma once
#include <QStyle>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QWidget>
#include "interfaces_core/config_types.h"

namespace z3y {
namespace plugins {
namespace qt_ui {

/**
 * @brief 中立语义：库只打"语义标记"，不写外观（颜色/尺寸）。
 *
 * @details
 * 属性名用通用词汇，不绑定任何宿主的主题实现：
 *   - `role`  ：`primary` / `secondary` / `danger`  —— 按钮角色
 *   - `state` ：`modified` / `error` / `warning` / `invalid` / `locate` —— 参数状态
 *
 * 外观由"库自带默认皮肤"（`ConfigSkin::Default`）或"宿主风格"
 * （`ConfigSkin::None`，宿主按这些属性渲染）决定——见 `ConfigSkin`。
 */
namespace sem {
/** @brief 设按钮语义角色（primary/secondary/danger）；`role=nullptr` 清除 */
inline void SetRole(QWidget* w, const char* role) {
  if (!w) return;
  w->setProperty("role", role ? QVariant(QString::fromLatin1(role)) : QVariant());
  w->style()->unpolish(w);
  w->style()->polish(w);
}
/** @brief 设参数状态（原样 QVariant，便于保存/恢复）；空 QVariant 即清除 */
inline void SetStateValue(QWidget* w, const QVariant& state) {
  if (!w) return;
  w->setProperty("state", state);
  w->style()->unpolish(w);
  w->style()->polish(w);
}
/** @brief 设参数状态（modified/error/warning/invalid/locate）；`state=nullptr` 清除 */
inline void SetState(QWidget* w, const char* state) {
  SetStateValue(w, state ? QVariant(QString::fromLatin1(state)) : QVariant());
}
}  // namespace sem

/**
 * @brief 使用 std::visit 将纯正 C++ 后端的 ConfigValue 变体降维转换为 Qt 界面世界通用的 QVariant。
 *
 * @details
 * - 纯 C++ 的 std::string 转换为 QString。
 * - std::vector 集合全部装入 QVariantList 队列。
 * - 数值型则原样打包为 QVariant 承载。
 * 这对于让不认识 STL 类型的 Qt 信号和槽系统安全跨线程发车至关重要。
 *
 * @param val 从后端传来的 C++ 变体。
 * @return QVariant 转化完成的 Qt 承载体。
 */
inline QVariant ConvertToQVariant(const z3y::interfaces::core::ConfigValue& val) {
  return std::visit(
      [](auto&& arg) -> QVariant {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return QVariant();
        } else if constexpr (std::is_same_v<T, std::string>) {
          return QString::fromUtf8(arg.data(), arg.size());
        } else if constexpr (std::is_same_v<T, std::vector<int64_t>> ||
                             std::is_same_v<T, std::vector<double>> ||
                             std::is_same_v<T, std::vector<std::string>>) {
          QVariantList list;
          for (const auto& item : arg) {
            if constexpr (std::is_same_v<T, std::vector<std::string>>)
              list.append(QString::fromUtf8(item.data(), item.size()));
            else
              list.append(QVariant::fromValue(item));
          }
          return list;
        } else {
          return QVariant::fromValue(arg);
        }
      },
      val);
}

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
