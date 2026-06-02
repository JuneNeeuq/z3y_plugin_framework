/**
 * @file widget_factory.h
 * @brief 工厂类：负责将后台定义的纯数据类型，自动变幻映射出前端所需的 Qt UI 控件。
 *
 * @details
 * 属于 UI 数据驱动引擎 (Data-Driven UI Engine) 的核心部位。
 * 解析诸如 WidgetType::kSlider 等 Schema 枚举，动态生成对应的 QSlider，绑定信号，排版入组。
 */

#pragma once
#include <QString>
#include <QVariant>
#include <QWidget>
#include <functional>
#include <map>
#include <string>

#include "dependency_graph.h"
#include "interfaces_ui/i_config_ui_manager.h"

namespace z3y {
namespace plugins {
namespace qt_ui {

/**
 * @class WidgetFactory
 * @brief 数据驱动界面的静态构建工厂。
 */
class WidgetFactory {
 public:
  /**
   * @brief 自动为一个完整的配置大类（如 "Network"）生成一整个排版好的 QScrollArea 容器页面。
   *
   * @param group_key 需要渲染的大类名称（由底层 Schema 提供支持）。
   * @param current_role 现在的用户权限角色（用于剪除无权查看的行）。
   * @param graph 指向依赖图谱的指针，构建出的新控件如果带有条件，会被挂载到图谱上。
   * @param pending_changes 当前还悬而未决的修改池，如果某个控件在这个池里有值，它会以这个值作为优先级最高的值呈现，并且显示高亮橙边。
   * @param context_obj Qt 对象槽系统所需的生命周期从属对象（通常传主窗口）。
   * @param on_change_callback 核心注入：当控件被用户拉动/输入导致改变时，该向谁汇报。
   * @param is_advanced_mode 用户当前是否已经打开了高级模式（以显示那些 is_advanced=true 的硬核选项）。
   * @param custom_panels 第三方提供的高级逃生舱自定义画板列表。
   * @return QWidget* 返回构建完成的根界面，通常里面已经包含了滚动条、各种分组框（GroupBox）以及网格排版。
   */
  static QWidget* CreatePage(
      const QString& group_key, const std::string& current_role,
      DependencyGraph* graph,
      const std::map<std::string, QVariant>& pending_changes,
      QObject* context_obj,
      std::function<void(const QString&, const QVariant&)> on_change_callback,
      bool is_advanced_mode,
      const std::map<std::string,
                     z3y::interfaces::ui::IConfigUIManager::CustomPanelCreator>&
          custom_panels);
};

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
