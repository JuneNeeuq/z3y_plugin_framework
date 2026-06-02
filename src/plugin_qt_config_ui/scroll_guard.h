/**
 * @file scroll_guard.h
 * @brief Qt 界面滑轮交互防护过滤器。
 *
 * @details
 * 在较长或者包含过多 QComboBox, QSpinBox 等可以被滚轮直接调节数值的组件的界面中，
 * 如果用户纯粹为了滚动查看下方内容而快速滑动滚轮，很容易意外将鼠标路过某个 QSpinBox，
 * 导致该组件的数值被错误地上下修改。
 * 该过滤器用于拦截所有没有被真正点亮焦点（hasFocus == false）的组件吸收滚轮事件，
 * 让滑轮只作用于最外层的 QScrollArea，大大提升界面的容错使用体验。
 */

#pragma once
#include <QEvent>
#include <QObject>
#include <QWidget>
#include <QWheelEvent>
#include <QCoreApplication>

namespace z3y {
namespace plugins {
namespace qt_ui {

/**
 * @class ScrollGuardFilter
 * @brief 基于事件过滤机制拦截未获焦点的危险滚轮事件。
 */
class ScrollGuardFilter : public QObject {
  Q_OBJECT
 public:
  explicit ScrollGuardFilter(QObject* parent = nullptr) : QObject(parent) {}

 protected:
  /** @brief 核心过滤钩子：阻断 QEvent::Wheel */
  bool eventFilter(QObject* obj, QEvent* event) override {
    if (event->type() == QEvent::Wheel) {
      QWidget* widget = qobject_cast<QWidget*>(obj);
      if (widget && !widget->hasFocus()) {
        event->ignore(); // 直接忽略这个滚动行为
        // 因为我们在事件过滤器中如果返回 true 会彻底阻断事件的传播（也不会冒泡给父窗口）
        // 这样会导致滚轮滑到这里时界面就无法滚动了，有一种被卡住的感觉
        // 所以我们需要在这里手动将滚轮事件透传给它的父容器，直到被 QScrollArea 接收
        if (widget->parentWidget()) {
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            QCoreApplication::sendEvent(widget->parentWidget(), wheelEvent);
        }
        return true;
      }
    }
    return QObject::eventFilter(obj, event);
  }
};

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
