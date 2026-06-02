/**
 * @file dependency_graph.cpp
 * @brief 动态表单依赖状态自动结算引擎的实现。
 */

#include "dependency_graph.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace z3y {
namespace plugins {
namespace qt_ui {

void DependencyGraph::SetValueProvider(ValueProvider provider) {
  // 保存回调，后续 EvaluateExpression 需要通过这个通道溯源真值
  value_provider_ = std::move(provider);
}

void DependencyGraph::AddDependency(const std::string& target_path,
                                    const std::string& depends_on_path,
                                    QWidget* widget) {
  // 建立反向依赖：当 depends_on_path 改变时，必须顺藤摸瓜通知到 target_path 绑定的 widget
  reverse_deps_[depends_on_path].push_back(QPointer<QWidget>(widget));

  // 预编译表达式：将条件字符串 ("!Camera.AutoExp == true") 一次性拆解提炼并注入给控件自己的 Property。
  // 这样在海量重算时就不需要重复去进行缓慢的正则匹配。
  QString cond = widget->property("enable_condition").toString();
  if (!cond.isEmpty()) {
    // 捕获组定义：1. 是否取反  2. 所依赖的后台路径  3. 操作符(如>=)  4. 目标阈值
    static const QRegularExpression re(
        "^(!?)([\\w\\.]+)(?:\\s*(==|!=|>|<|>=|<=)\\s*(.*))?$");
    QRegularExpressionMatch match = re.match(cond);
    if (match.hasMatch()) {
      widget->setProperty("pred_parsed", true);
      widget->setProperty("pred_invert", !match.captured(1).isEmpty());
      widget->setProperty("pred_dep_path", match.captured(2));
      widget->setProperty("pred_op", match.captured(3));

      // 剥离两端包裹的纯字符串外衣单双引号
      QString target_val_str = match.captured(4).trimmed();
      if (target_val_str.startsWith('\'') && target_val_str.endsWith('\'')) {
        target_val_str = target_val_str.mid(1, target_val_str.length() - 2);
      } else if (target_val_str.startsWith('"') && target_val_str.endsWith('"')) {
        target_val_str = target_val_str.mid(1, target_val_str.length() - 2);
      }
      widget->setProperty("pred_target_val", target_val_str);
    }
  }
}

void DependencyGraph::OnParameterChanged(const std::string& changed_path) {
  // 收到风声说 changed_path 被人修改了。顺着它的名字去图谱里查一下有谁依赖了它
  if (reverse_deps_.find(changed_path) == reverse_deps_.end()) return;

  auto& targets = reverse_deps_[changed_path];

  // 消除 O(N^2) 内存碎片：安全的 Erase-Remove 惯用法，踢出那些已经被销毁的 QPointer
  targets.erase(
      std::remove_if(targets.begin(), targets.end(),
                     [](const QPointer<QWidget>& w) { return w.isNull(); }),
      targets.end());

  // 挨个点名，让这些依附的小弟们重新计算一下自己现在的条件还成不成立
  for (auto& w : targets) {
    if (w) {
      bool should_enable = EvaluateExpression(w.data());
      w->setEnabled(should_enable);
    }
  }
}

void DependencyGraph::EvaluateAll() {
  // 这是一招大杀器：遍历全图，让每一个绑定了附庸条件的组件都进行自我反省
  for (auto& [dep_path, targets] : reverse_deps_) {
    targets.erase(
        std::remove_if(targets.begin(), targets.end(),
                       [](const QPointer<QWidget>& w) { return w.isNull(); }),
        targets.end());

    for (auto& w : targets) {
      if (w) {
        bool should_enable = EvaluateExpression(w.data());
        w->setEnabled(should_enable);
      }
    }
  }
}

bool DependencyGraph::EvaluateExpression(QWidget* widget) {
  // 如果当前根本没有注入代理通道去要数据，为了防止界面瘫痪，必须一律放行
  if (!value_provider_) return true;

  // 如果当初预编译正则表达式失败，也一律放行
  if (!widget->property("pred_parsed").toBool()) {
    QString cond = widget->property("enable_condition").toString();
    return cond.isEmpty() ? true : true; 
  }

  // 从控件身上把预编译好的结构体拿出来
  bool invert_logic = widget->property("pred_invert").toBool();
  std::string dep_path = widget->property("pred_dep_path").toString().toStdString();
  QString op = widget->property("pred_op").toString();
  QString target_val_str = widget->property("pred_target_val").toString();

  // 找大哥（代理通道）问一下目前依赖的这个参数是多少
  QVariant current_val = value_provider_(dep_path);

  // 【防卡死护城河】：如果后台路径被热更移除或尚未建立，强制放行，避免 UI 被永久锁死
  if (!current_val.isValid()) return true;

  // 【隐式逻辑推断】：纯变量没有提供操作符时的直接判定 (如 "!Camera.AutoExp")
  if (op.isEmpty()) {
    bool res = true;
    if (current_val.userType() == QMetaType::Bool) {
      res = current_val.toBool();
    } else if (current_val.userType() == QMetaType::Int ||
               current_val.userType() == QMetaType::LongLong) {
      res = current_val.toLongLong() != 0;
    }
    return invert_logic ? !res : res;
  }

  // 布尔状态特化安全处理
  if (current_val.userType() == QMetaType::Bool) {
    bool c_bool = current_val.toBool();
    bool t_bool = (target_val_str.toLower() == "true" || target_val_str == "1");
    if (op == "==") return c_bool == t_bool;
    if (op == "!=") return c_bool != t_bool;
  }

  // 数值特化处理，避免用字符串方式比较数字大小
  bool is_num = false;
  double t_num = target_val_str.toDouble(&is_num);
  if (is_num) {
    double c_num = current_val.toDouble();
    if (op == "==") return std::abs(c_num - t_num) < 1e-9;
    if (op == "!=") return std::abs(c_num - t_num) >= 1e-9;
    if (op == ">")  return c_num > t_num;
    if (op == "<")  return c_num < t_num;
    if (op == ">=") return c_num >= t_num;
    if (op == "<=") return c_num <= t_num;
  }

  // 字符串托底退化比较
  QString c_str = current_val.toString();
  if (op == "==") return c_str == target_val_str;
  if (op == "!=") return c_str != target_val_str;

  return true;
}

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
