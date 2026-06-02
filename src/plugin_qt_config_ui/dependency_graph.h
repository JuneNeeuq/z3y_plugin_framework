/**
 * @file dependency_graph.h
 * @brief UI 控件依赖关系运算引擎声明。
 *
 * @details
 * 现代参数系统常常需要联动：比如，只有当勾选了“开启自动测光”，下面的“曝光时间”参数才会从禁用变灰状态解禁。
 * DependencyGraph 负责解析这类在 Schema 中定义的字符串依赖条件（如 "Camera.AutoExp == true"），
 * 形成一张拓扑图，当某个主参数改变时，自动结算依赖该参数的所有下游控件的可用性（Enabled 状态）。
 */

#pragma once
#include <QPointer>
#include <QVariant>
#include <QWidget>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace z3y {
namespace plugins {
namespace qt_ui {

/**
 * @class DependencyGraph
 * @brief 动态表单依赖状态自动结算引擎。
 */
class DependencyGraph {
 public:
  /** 
   * @brief 反向溯源的委托：图谱大脑通过它向外界询问“现在某个路径下的参数值到底是多少”。 
   */
  using ValueProvider = std::function<QVariant(const std::string&)>;

  /** @brief 注入外部的寻值代理。 */
  void SetValueProvider(ValueProvider provider);

  /**
   * @brief 为某个目标 UI 控件增加对特定参数的依赖约束。
   * @param target_path 本控件绑定的配置路径。
   * @param depends_on_path 它所依赖的那个配置参数的路径。
   * @param widget 需要被管控 Enabled 状态的目标控件指针。
   */
  void AddDependency(const std::string& target_path,
                     const std::string& depends_on_path, QWidget* widget);

  /**
   * @brief 当系统感知到某个参数发生了值变动时触发此调用，启动下游状态级联刷新。
   * @param changed_path 发生变化的参数路径。
   */
  void OnParameterChanged(const std::string& changed_path);

  /**
   * @brief 用于页面首次加载时的全局群体状态结算，确保所有控件在显示的第一瞬间处于正确的灰度状态。
   */
  void EvaluateAll();

 private:
  /** @brief 核心布尔逻辑求值器。根据字符串如 "Param > 10"，向 provider 求值比对后返回真假。 */
  bool EvaluateExpression(QWidget* widget);

  /** @brief 反向依赖索引表：主路径 -> [受其牵连的众多目标控件] */
  std::unordered_map<std::string, std::vector<QPointer<QWidget>>> reverse_deps_;
  
  /** @brief 指向系统真实取值来源的函数代理。 */
  ValueProvider value_provider_;
};

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
