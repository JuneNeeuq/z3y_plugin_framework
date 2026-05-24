/**
 * @file config_main_window.cpp
 * @brief Qt 配置系统主界面的底层实现代码。
 *
 * @details
 * 本文件构成了整个 Qt UI 插件的核心大脑。它管理了左侧菜单树的点击刷新、
 * 异步批量数据更新的界面局部渲染（而不用刷新整页），
 * 并且涵盖了强类型数组验证、LRU页面置换算法、文件导入导出以及配置重置功能。
 */

#include "config_main_window.h"

#include <QApplication>
#include <QStyle>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaType>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

#include "framework/z3y_service_locator.h"
#include "interfaces_core/i_config_service.h"
#include "widget_factory.h"
#include "qt_utils.h"

namespace z3y {
namespace plugins {
namespace qt_ui {

ConfigMainWindow::ConfigMainWindow(EventBridge* bridge, QWidget* parent)
    : QMainWindow(parent), event_bridge_(bridge), current_group_("") {
  dependency_graph_ = std::make_unique<DependencyGraph>();

  // 挂载一个内部属性列表，专门用来记住那些“提交被后台拒绝”的控件路径
  this->setProperty("backend_errors", QStringList());

  // 注入获取实时数据的能力给图谱，以便其进行布尔逻辑推导
  dependency_graph_->SetValueProvider(
      [this](const std::string& path) -> QVariant {
        // 先看用户有没有刚刚在这个界面改过（优先级最高）
        auto it = pending_changes_.find(path);
        if (it != pending_changes_.end()) return it->second;

        // 没有改过，就去问底层要真正的系统当前值
        auto config_srv =
            z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
        if (config_srv) {
          try {
            return ConvertToQVariant(config_srv->GetValue(path));
          } catch (...) {
          }
        }
        return QVariant();
      });

  SetupUI();
  LoadNavigationTree();
  // 最核心的骨架连接：当桥接器收到数据时，通知本窗口进行批量局部重绘
  connect(event_bridge_, &EventBridge::batchedConfigChanged, this,
          &ConfigMainWindow::OnBatchedConfigChanged);
}

ConfigMainWindow::~ConfigMainWindow() = default;

void ConfigMainWindow::SetRole(const std::string& role) {
  current_role_ = role;
}

void ConfigMainWindow::SetCustomPanels(
    const std::map<std::string,
                   z3y::interfaces::ui::IConfigUIManager::CustomPanelCreator>&
        panels) {
  custom_panels_ = panels;
}

void ConfigMainWindow::SetupUI() {
  this->setWindowTitle("Z3Y System Configuration");
  this->resize(1200, 800);
  this->setStatusBar(new QStatusBar(this));

  QWidget* central_widget = new QWidget(this);
  this->setCentralWidget(central_widget);
  QVBoxLayout* main_layout = new QVBoxLayout(central_widget);

  // === 顶部工具栏设计 ===
  QHBoxLayout* toolbar_layout = new QHBoxLayout();
  cb_advanced_ = new QCheckBox("显示高级选项");
  cb_advanced_->setChecked(false);
  connect(cb_advanced_, &QCheckBox::toggled, this,
          &ConfigMainWindow::OnAdvancedModeToggled);
  toolbar_layout->addWidget(cb_advanced_);
  toolbar_layout->addStretch();

  QPushButton* btn_import = new QPushButton("导入配方");
  btn_import->setStyleSheet("padding: 4px 15px;");
  connect(btn_import, &QPushButton::clicked, this,
          &ConfigMainWindow::OnImportClicked);
  toolbar_layout->addWidget(btn_import);

  QPushButton* btn_export = new QPushButton("导出配方");
  btn_export->setStyleSheet("padding: 4px 15px;");
  connect(btn_export, &QPushButton::clicked, this,
          &ConfigMainWindow::OnExportClicked);
  toolbar_layout->addWidget(btn_export);

  QPushButton* btn_apply = new QPushButton("Apply (提交全部更改)");
  btn_apply->setStyleSheet(
      "background-color: #0078D7; color: white; font-weight: bold; padding: "
      "4px 15px; margin-left: 10px;");
  connect(btn_apply, &QPushButton::clicked, this,
          &ConfigMainWindow::ApplyChanges);
  toolbar_layout->addWidget(btn_apply);

  main_layout->addLayout(toolbar_layout);

  // === 左右切分布局 ===
  QSplitter* splitter = new QSplitter(Qt::Horizontal);
  main_layout->addWidget(splitter, 1);

  QWidget* left_panel = new QWidget();
  QVBoxLayout* left_layout = new QVBoxLayout(left_panel);
  left_layout->setContentsMargins(0, 0, 0, 0);

  search_box_ = new QLineEdit();
  search_box_->setPlaceholderText("Search items...");
  connect(search_box_, &QLineEdit::textChanged, this,
          &ConfigMainWindow::OnGlobalSearchTextChanged);
  left_layout->addWidget(search_box_);

  nav_tree_ = new QTreeWidget();
  nav_tree_->setHeaderHidden(true);
  connect(nav_tree_, &QTreeWidget::currentItemChanged, this,
          &ConfigMainWindow::OnTreeSelectionChanged);
  left_layout->addWidget(nav_tree_);

  splitter->addWidget(left_panel);
  stacked_pages_ = new QStackedWidget();
  splitter->addWidget(stacked_pages_);
  splitter->setSizes({300, 900}); // 默认侧边栏宽度 300
}

void ConfigMainWindow::LoadNavigationTree() {
  nav_tree_->clear();
  auto config_srv =
      z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
  if (!config_srv) return;

  auto all_configs = config_srv->GetAllConfigs();
  std::map<QString, QTreeWidgetItem*> group_nodes;

  for (const auto& [path, snap] : all_configs) {
    if (snap.meta.is_hidden) continue;
    QString g_key = QString::fromStdString(snap.meta.group_key);
    if (g_key.isEmpty()) continue;

    // 分组节点构建 (例如 "Network")
    if (!group_nodes.count(g_key)) {
      QTreeWidgetItem* grp_item =
          new QTreeWidgetItem(nav_tree_, QStringList() << g_key);
      grp_item->setData(0, Qt::UserRole, g_key);
      grp_item->setData(0, Qt::UserRole + 1, "GROUP");
      group_nodes[g_key] = grp_item;
    }

    // 参数节点构建 (挂在组下面，如 "IP Address")
    QString name = QString::fromStdString(snap.meta.name_key);
    QTreeWidgetItem* item_node =
        new QTreeWidgetItem(group_nodes[g_key], QStringList() << name);
    item_node->setData(0, Qt::UserRole, QString::fromStdString(path));
    item_node->setData(0, Qt::UserRole + 1, "ITEM");
    item_node->setData(0, Qt::UserRole + 2, g_key);
  }
}

void ConfigMainWindow::OnTreeSelectionChanged(QTreeWidgetItem* current,
                                              QTreeWidgetItem* previous) {
  if (!current) return;

  QString type = current->data(0, Qt::UserRole + 1).toString();
  QString group_key;
  QString target_path;

  if (type == "GROUP") {
    group_key = current->data(0, Qt::UserRole).toString();
  } else if (type == "ITEM") {
    target_path = current->data(0, Qt::UserRole).toString();
    group_key = current->data(0, Qt::UserRole + 2).toString();
  } else {
    return;
  }

  // == 场景1：发生了大页面间的切换 ==
  if (current_group_ != group_key) {
    // 离开页面前，检查并询问是否有未保存的更改
    if (!PromptUnsavedChanges()) {
      nav_tree_->blockSignals(true);
      nav_tree_->setCurrentItem(previous);
      nav_tree_->blockSignals(false);
      return;
    }
    current_group_ = group_key;
  }

  // == 页面缓存调度 ==
  if (page_cache_.count(group_key) > 0) {
    // 缓存命中 (Cache Hit)：刷新 LRU 队列，直接展示
    lru_queue_.splice(lru_queue_.end(), lru_queue_, lru_mapping_[group_key]);
    stacked_pages_->setCurrentWidget(page_cache_[group_key]);
  } else {
    // 缓存未命中 (Cache Miss)：召唤工厂，生成这个全新的重型页面
    auto value_changed_cb = [this](const QString& path, const QVariant& val) {
      std::string safe_path = path.toStdString();
      this->pending_changes_[safe_path] = val; // 记录到悬浮更改池中

      // 如果这个控件原来带着红色的错误边框，只要它一改动，立刻消除红框
      QStringList errs = this->property("backend_errors").toStringList();
      if (errs.removeAll(path) > 0) {
        this->setProperty("backend_errors", errs);
      }

      if (global_widget_map_.count(path) &&
          !global_widget_map_[path].isNull()) {
        global_widget_map_[path]->setStyleSheet(
            "font-weight: bold; color: #b35900;"); // 改为醒目的橙色修改态
        global_widget_map_[path]->setToolTip("");
      }
      // 通知依赖图谱更新其它联动控件
      if (dependency_graph_) dependency_graph_->OnParameterChanged(safe_path);
    };

    QWidget* new_page = WidgetFactory::CreatePage(
        group_key, current_role_, dependency_graph_.get(), pending_changes_,
        this, value_changed_cb, is_advanced_mode_, custom_panels_);

    // 将生成出来的控件全部录入全局雷达 (global_widget_map_)
    auto child_widgets = new_page->findChildren<QWidget*>();
    child_widgets.append(new_page);

    QStringList current_errors =
        this->property("backend_errors").toStringList();

    for (auto* child : child_widgets) {
      QString path = child->property("config_path").toString();
      if (!path.isEmpty()) {
        global_widget_map_[path] = QPointer<QWidget>(child);

        // 如果这个页面是带着旧的未修复错误加载进来的，自动补红框
        if (current_errors.contains(path)) {
          child->setStyleSheet(
              "border: 2px solid red; background-color: #ffe6e6;");
        } else if (pending_changes_.count(path.toStdString())) {
          child->setStyleSheet("font-weight: bold; color: #b35900;");
        }
      }
    }

    // 录入 LRU 缓存池
    page_cache_[group_key] = new_page;
    lru_queue_.push_back(group_key);
    lru_mapping_[group_key] = std::prev(lru_queue_.end());

    stacked_pages_->addWidget(new_page);
    stacked_pages_->setCurrentWidget(new_page);
    EvictOldestPageIfNeeded();
    dependency_graph_->EvaluateAll(); // 结算该新页面的依赖规则
  }

  // == 场景2：用户其实是点击了树里面的某个特定参数节点 ==
  if (!target_path.isEmpty() && global_widget_map_.count(target_path)) {
    QPointer<QWidget> target_w = global_widget_map_[target_path];
    if (!target_w.isNull()) {
      // 1. 自动滚过去
      QScrollArea* scroll =
          stacked_pages_->currentWidget()->findChild<QScrollArea*>();
      if (!scroll && stacked_pages_->currentWidget()->inherits("QScrollArea")) {
        scroll = qobject_cast<QScrollArea*>(stacked_pages_->currentWidget());
      }
      if (scroll) {
        scroll->ensureWidgetVisible(target_w.data(), 50, 50);
      }

      // 2. 闪烁黄光提示一下
      QString orig_style = target_w->styleSheet();
      target_w->setStyleSheet(
          orig_style +
          " background-color: #ffff99; border: 2px solid #ffcc00;");
      QTimer::singleShot(1000, target_w.data(), [target_w, orig_style]() {
        if (target_w) target_w->setStyleSheet(orig_style);
      });
    }
  }
}

void ConfigMainWindow::EvictOldestPageIfNeeded() {
  if (page_cache_.size() <= kMaxCachedPages) return;

  QString evict_key = lru_queue_.front();
  lru_queue_.pop_front();
  lru_mapping_.erase(evict_key);

  QWidget* old_page = page_cache_[evict_key];
  auto widgets_to_check = old_page->findChildren<QWidget*>();
  widgets_to_check.append(old_page);

  // 清除全局雷达中对应的指引
  for (auto* child : widgets_to_check) {
    QString path = child->property("config_path").toString();
    if (!path.isEmpty()) global_widget_map_.erase(path);
  }

  stacked_pages_->removeWidget(old_page);
  page_cache_.erase(evict_key);
  old_page->deleteLater(); // 彻底从堆上物理超度释放内存
}

void ConfigMainWindow::OnBatchedConfigChanged(
    const z3y::plugins::qt_ui::ConfigUpdateMap& updates) {
  for (const auto& [path, val] : updates) {
    auto it = global_widget_map_.find(path);
    if (it != global_widget_map_.end() && !it->second.isNull()) {
      QWidget* widget = it->second.data();
      std::string safe_path = path.toStdString();

      // 【核心冲突防范】：如果用户刚好在编辑这个输入框，突然底层的硬件串口改了这数据。
      // 我们绝对不能把用户正在打字的数据直接覆盖掉！否则会让人极其抓狂。
      // 只能变成警告态提示用户。
      if (pending_changes_.count(safe_path)) {
        widget->setStyleSheet("border: 2px solid orange;");
        widget->setToolTip(
            "⚠️ 警告：系统后台强行更改了该数据！请提交覆盖或重置此页。");
        if (dependency_graph_) dependency_graph_->OnParameterChanged(safe_path);
        continue;
      }

      // 如果原来是因为错误提交被红框标记的，现在既然别人给了合法值，就洗白
      QStringList errs = this->property("backend_errors").toStringList();
      if (errs.removeAll(path) > 0) {
        this->setProperty("backend_errors", errs);
      }
      widget->setStyleSheet("");

      // 寻址对应的具体控件，进行精细化的同值拦截与数值更新
      QSpinBox* spin = widget->inherits("QSpinBox")
                           ? qobject_cast<QSpinBox*>(widget)
                           : widget->findChild<QSpinBox*>();
      QDoubleSpinBox* dspin = widget->inherits("QDoubleSpinBox")
                                  ? qobject_cast<QDoubleSpinBox*>(widget)
                                  : widget->findChild<QDoubleSpinBox*>();
      QCheckBox* check = widget->inherits("QCheckBox")
                             ? qobject_cast<QCheckBox*>(widget)
                             : widget->findChild<QCheckBox*>();
      QLineEdit* line = widget->inherits("QLineEdit")
                            ? qobject_cast<QLineEdit*>(widget)
                            : widget->findChild<QLineEdit*>();
      QSlider* slider = widget->inherits("QSlider")
                            ? qobject_cast<QSlider*>(widget)
                            : widget->findChild<QSlider*>();
      QComboBox* combo = widget->inherits("QComboBox")
                             ? qobject_cast<QComboBox*>(widget)
                             : widget->findChild<QComboBox*>();
      QTableWidget* table = widget->inherits("QTableWidget")
                                ? qobject_cast<QTableWidget*>(widget)
                                : widget->findChild<QTableWidget*>();
      QProgressBar* prog = widget->inherits("QProgressBar")
                               ? qobject_cast<QProgressBar*>(widget)
                               : widget->findChild<QProgressBar*>();

      bool actually_changed = false;

      // 【精细化拦截】：只有当数值确确实实发生了改变时，才去触发控件的 setValue。
      // 否则会引发 Qt 自己的死循环连环信号！(QSignalBlocker 用于防止设置数值时再次向图谱报告)
      if (spin) {
        if (spin->value() != val.toInt()) {
          QSignalBlocker b(spin);
          spin->setValue(val.toInt());
          actually_changed = true;
        }
      } else if (dspin) {
        double ui_val = dspin->value();
        double be_val = val.toDouble();
        if (std::abs(ui_val - be_val) >
            (0.51 * std::pow(10.0, -dspin->decimals()))) {
          QSignalBlocker b(dspin);
          dspin->setValue(be_val);
          actually_changed = true;
        }
      } else if (check) {
        if (check->isChecked() != val.toBool()) {
          QSignalBlocker b(check);
          check->setChecked(val.toBool());
          actually_changed = true;
        }
      } else if (prog) {
        int be_val = static_cast<int>(val.toDouble());
        if (prog->value() != be_val) {
          QSignalBlocker b(prog);
          prog->setValue(be_val);
          actually_changed = true;
        }
      } else if (combo) {
        QVariant current_data = combo->itemData(combo->currentIndex());
        if (current_data != val && current_data.toString() != val.toString()) {
          int idx = combo->findData(val);
          if (idx == -1 && (val.userType() == QMetaType::LongLong ||
                            val.userType() == QMetaType::Int)) {
            idx = combo->findData(
                QVariant::fromValue<qlonglong>(val.toLongLong()));
            if (idx == -1)
              idx = combo->findData(QVariant::fromValue<int>(val.toInt()));
          }
          if (idx == -1) {
            QString target_str = val.toString();
            for (int i = 0; i < combo->count(); ++i) {
              if (combo->itemData(i).toString() == target_str) {
                idx = i;
                break;
              }
            }
          }
          QSignalBlocker b(combo);
          if (idx != -1 && combo->currentIndex() != idx) {
            combo->setCurrentIndex(idx);
            actually_changed = true;
          }
        }
      } else if (table) {
        QVariantList list = val.toList();
        bool identical = (table->rowCount() == list.size());
        if (identical) {
          for (int i = 0; i < list.size(); ++i) {
            QTableWidgetItem* item = table->item(i, 0);
            if (!item || item->text() != list[i].toString()) {
              identical = false;
              break;
            }
          }
        }
        if (!identical) {
          QSignalBlocker b(table);
          table->setRowCount(static_cast<int>(list.size()));
          for (int i = 0; i < list.size(); ++i) {
            QTableWidgetItem* item = table->item(i, 0);
            if (!item) {
              item = new QTableWidgetItem();
              table->setItem(i, 0, item);
            }
            item->setText(list[i].toString());
          }
          actually_changed = true;
        }
      } else if (slider) {
        int safe_val = static_cast<int>(std::clamp<qint64>(
            val.toLongLong(), std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max()));
        if (slider->value() != safe_val) {
          QSignalBlocker b(slider);
          slider->setValue(safe_val);
          actually_changed = true;
        }
      } else if (line) {
        if (line->text() != val.toString()) {
          QSignalBlocker b(line);
          line->setText(val.toString());
          actually_changed = true;
        }
      }

      if (actually_changed && dependency_graph_) {
        dependency_graph_->OnParameterChanged(safe_path);
      }
    }
  }
}

void ConfigMainWindow::OnGlobalSearchTextChanged(const QString& text) {
  if (text.trimmed().isEmpty()) {
    QTreeWidgetItemIterator it_reset(nav_tree_);
    while (*it_reset) {
      (*it_reset)->setHidden(false);
      (*it_reset)->setExpanded(false);
      ++it_reset;
    }
    return;
  }

  QTreeWidgetItemIterator it_hide(nav_tree_);
  while (*it_hide) {
    (*it_hide)->setHidden(true);
    (*it_hide)->setExpanded(false);
    ++it_hide;
  }

  QTreeWidgetItemIterator it_match(nav_tree_);
  while (*it_match) {
    QString display_name = (*it_match)->text(0);
    QString raw_key = (*it_match)->data(0, Qt::UserRole).toString();

    if (display_name.contains(text, Qt::CaseInsensitive) ||
        raw_key.contains(text, Qt::CaseInsensitive)) {
      QTreeWidgetItem* node = *it_match;
      // 命中节点之后，把它到根部所有的长辈节点全显出来，并且展开树枝
      while (node) {
        node->setHidden(false);
        node->setExpanded(true);
        node = node->parent();
      }

      // 并且把这个命中节点下面的小分支也全展露出来
      QList<QTreeWidgetItem*> children_queue;
      for (int i = 0; i < (*it_match)->childCount(); ++i) {
        children_queue.append((*it_match)->child(i));
      }
      while (!children_queue.isEmpty()) {
        QTreeWidgetItem* child = children_queue.takeFirst();
        child->setHidden(false);
        for (int i = 0; i < child->childCount(); ++i) {
          children_queue.append(child->child(i));
        }
      }
    }
    ++it_match;
  }
}

void ConfigMainWindow::OnAdvancedModeToggled(bool checked) {
  is_advanced_mode_ = checked;
  QList<QFormLayout*> all_forms = stacked_pages_->findChildren<QFormLayout*>();

  for (auto& [path, widget_ptr] : global_widget_map_) {
    if (!widget_ptr.isNull() && widget_ptr->property("is_advanced").toBool()) {
      widget_ptr->setVisible(checked);
      // 把表单左边陪伴的那个 Label 也一起干掉
      for (auto* form : all_forms) {
        QWidget* label = form->labelForField(widget_ptr.data());
        if (label) {
          label->setVisible(checked);
          break;
        }
      }
    }
  }
  // 通知页面重新计算长宽高布局以消除空鼓
  if (stacked_pages_->currentWidget() &&
      stacked_pages_->currentWidget()->layout()) {
    stacked_pages_->currentWidget()->layout()->invalidate();
    stacked_pages_->currentWidget()->adjustSize();
  }
}

void ConfigMainWindow::OnImportClicked() {
  if (!PromptUnsavedChanges()) return;

  QString file_path = QFileDialog::getOpenFileName(
      this, "导入配置配方", "", "JSON Files (*.json);;All Files (*.*)");
  if (file_path.isEmpty()) return;

  auto config_srv =
      z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
  if (!config_srv) return;

  // 通知底层读取硬盘文件并直接套用到内存中
  if (config_srv->ImportFromFile(file_path.toStdString(), true)) {
    QMessageBox::information(this, "导入成功",
                             "配置配方导入成功！系统已自动应用新参数。");
  } else {
    QMessageBox::critical(this, "导入失败",
                          "配置导入失败！请检查文件格式或系统读取权限。");
  }
}

void ConfigMainWindow::OnExportClicked() {
  QString file_path =
      QFileDialog::getSaveFileName(this, "导出配置配方", "config_backup.json",
                                   "JSON Files (*.json);;All Files (*.*)");
  if (file_path.isEmpty()) return;

  auto config_srv =
      z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
  if (!config_srv) return;

  if (config_srv->ExportToFile(file_path.toStdString())) {
    QMessageBox::information(this, "导出成功", "当前所有配置已成功导出落地。");
  } else {
    QMessageBox::critical(this, "导出失败",
                          "导出文件失败，请检查指定路径及系统写入权限！");
  }
}

void ConfigMainWindow::DiscardAllPendingChanges() {
  if (pending_changes_.empty()) return;
  auto config_srv =
      z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
  if (!config_srv) return;

  // 抛弃：直接拿路径去底层查真正的原本面貌，塞到一个更新包里模拟是“底层变回去了”
  z3y::plugins::qt_ui::ConfigUpdateMap batch_updates;
  for (const auto& [path, _] : pending_changes_) {
    QString qs_path = QString::fromUtf8(path.data(), path.size());
    try {
      auto val = config_srv->GetValue(path);
      batch_updates[qs_path] = ConvertToQVariant(val);
    } catch (...) {
      auto it = global_widget_map_.find(qs_path);
      if (it != global_widget_map_.end() && !it->second.isNull()) {
        it->second->setStyleSheet("");
        it->second->setToolTip("");
      }
    }
  }

  pending_changes_.clear();
  this->setProperty("backend_errors", QStringList());

  OnBatchedConfigChanged(batch_updates); // 发给更新机强行洗白

  // 抹掉左边树上的红叉
  QTreeWidgetItemIterator tree_clear_it(nav_tree_);
  while (*tree_clear_it) {
    (*tree_clear_it)->setIcon(0, QIcon());
    ++tree_clear_it;
  }
}

bool ConfigMainWindow::PromptUnsavedChanges() {
  if (pending_changes_.empty()) return true;

  auto result = QMessageBox::warning(
      this, "未保存的更改",
      "当前页面包含尚未提交的数据，强行离开将会导致修改丢失。是否应用并保存？",
      QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel);

  if (result == QMessageBox::Apply) {
    return ApplyChanges();
  }
  if (result == QMessageBox::Discard) {
    DiscardAllPendingChanges();
    return true;
  }
  return false;
}

bool ConfigMainWindow::ApplyChanges() {
  if (pending_changes_.empty()) return true;

  auto config_srv =
      z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
  if (!config_srv) {
    QMessageBox::critical(this, "系统错误", "致命：核心配置服务意外丢失！");
    return false;
  }

  // 先清空树和变量中的旧错误记录
  QTreeWidgetItemIterator tree_clear_it(nav_tree_);
  while (*tree_clear_it) {
    (*tree_clear_it)->setIcon(0, QIcon());
    ++tree_clear_it;
  }
  this->setProperty("backend_errors", QStringList());

  for (const auto& [path, val] : pending_changes_) {
    QString qs_path = QString::fromStdString(path);
    if (global_widget_map_.count(qs_path) &&
        !global_widget_map_[qs_path].isNull()) {
      global_widget_map_[qs_path]->setStyleSheet(
          "font-weight: bold; color: #b35900;");
    }
  }

  // 1. 在底层开启一个宏大的 ACID 事务包
  auto batch = config_srv->CreateBatch();

  // 2. 根据用户在 UI 上的操作，严密推导并转回 C++ 强类型，塞入事务中
  for (const auto& [path, val] : pending_changes_) {
    if (val.userType() == QMetaType::Bool) {
      batch.Set(path, val.toBool());
    } else if (val.userType() == QMetaType::Int ||
               val.userType() == QMetaType::LongLong) {
      batch.Set(path, val.toLongLong());
    } else if (val.userType() == QMetaType::Double) {
      batch.Set(path, val.toDouble());
    } else if (val.userType() == QMetaType::QString) {
      std::string safe_val = val.toString().toStdString();
      batch.Set(path, safe_val);
    } else if (val.userType() == QMetaType::QVariantList) {
      // ==== 数组容器极为复杂，需要跟底层的数据类型进行二次核对 ====
      QVariantList list = val.toList();

      z3y::interfaces::core::ConfigValue backend_val;
      try {
        backend_val = config_srv->GetValue(path);
      } catch (const std::exception& e) {
        QMessageBox::critical(
            this, "验证崩溃",
            QString("数组路径丢失：%1\n报错信息：%2")
                .arg(QString::fromUtf8(path.c_str()), e.what()));
        return false;
      } catch (...) {
        return false;
      }

      bool is_int_array =
          std::holds_alternative<std::vector<int64_t>>(backend_val);
      bool is_double_array =
          std::holds_alternative<std::vector<double>>(backend_val);
      bool is_string_array =
          std::holds_alternative<std::vector<std::string>>(backend_val);

      if (std::holds_alternative<std::monostate>(backend_val)) {
        if (list.empty()) {
          batch.Set(path, std::monostate{});
          continue;
        } else {
          QVariant first_elem = list.first();
          QString str_val = first_elem.toString();

          bool is_int_str = false;
          str_val.toLongLong(&is_int_str);
          bool is_double_str = false;
          str_val.toDouble(&is_double_str);

          if (first_elem.userType() == QMetaType::Double ||
              (is_double_str && str_val.contains('.'))) {
            is_double_array = true;
          } else if (first_elem.userType() == QMetaType::Int ||
                     first_elem.userType() == QMetaType::LongLong ||
                     (is_int_str && !str_val.contains('.'))) {
            is_int_array = true;
          } else {
            is_string_array = true;
          }
        }
      }

      if (is_int_array) {
        std::vector<int64_t> vec;
        if (std::holds_alternative<std::vector<int64_t>>(backend_val)) {
           vec = std::get<std::vector<int64_t>>(backend_val);
           vec.clear();
        }

        for (const auto& v : list) {
          bool ok;
          qlonglong d = v.toLongLong(&ok);
          if (!ok) {
            QMessageBox::critical(
                this, "类型严查",
                QString("在数组中检测到非法整数: '%1'\n参数路径: %2")
                    .arg(v.toString(), QString::fromUtf8(path.c_str())));
            return false;
          }
          vec.push_back(d);
        }
        batch.Set(path, vec);
      } else if (is_double_array) {
        std::vector<double> vec;
        if (std::holds_alternative<std::vector<double>>(backend_val)) {
           vec = std::get<std::vector<double>>(backend_val);
           vec.clear();
        }

        for (const auto& v : list) {
          bool ok;
          double d = v.toDouble(&ok);
          if (!ok) {
            QMessageBox::critical(
                this, "类型严查",
                QString("在数组中检测到非法浮点: '%1'\n参数路径: %2")
                    .arg(v.toString(), QString::fromUtf8(path.c_str())));
            return false;
          }
          vec.push_back(d);
        }
        batch.Set(path, vec);
      } else if (is_string_array) {
        std::vector<std::string> vec;
        if (std::holds_alternative<std::vector<std::string>>(backend_val)) {
           vec = std::get<std::vector<std::string>>(backend_val);
           vec.clear();
        }

        for (const auto& v : list) {
          vec.push_back(v.toString().toStdString());
        }
        batch.Set(path, vec);
      } else {
        return false;
      }
    }
  }

  // 3. 将组装好的这辆巨型货车推入底层去 Commit！
  std::vector<std::string> errors;
  try {
    errors = batch.Commit(current_role_);
  } catch (const std::exception& e) {
    QMessageBox::critical(
        this, "事务致命错误",
        QString("后台引擎拒绝提交并抛出底层异常:\n%1").arg(e.what()));
    return false;
  } catch (...) {
    return false;
  }

  // 4. 结算：检查后台有没有退货（校验失败拦截）
  if (!errors.empty()) {
    QString error_msg = "操作由于未通过合规校验而被全部回滚：\n\n";
    auto all_configs = config_srv->GetAllConfigs();
    static const QRegularExpression re("\\[(.*?)\\]");

    // 分析退货单上的每一条罪名
    for (const auto& err : errors) {
      QString err_qstr = QString::fromUtf8(err.data(), err.size());
      error_msg += err_qstr + "\n";
      QRegularExpressionMatch match = re.match(err_qstr);

      if (match.hasMatch()) {
        QString qpath = match.captured(1);
        std::string std_path = qpath.toStdString();

        // 记入系统的“犯罪名册”中
        QStringList errs = this->property("backend_errors").toStringList();
        if (!errs.contains(qpath)) {
          errs.append(qpath);
          this->setProperty("backend_errors", errs);
        }

        // 立刻把 UI 上的输入框糊上一层大红油漆，以示惩戒
        if (global_widget_map_.count(qpath) &&
            !global_widget_map_[qpath].isNull()) {
          global_widget_map_[qpath]->setStyleSheet(
              "border: 2px solid red; background-color: #ffe6e6;");
        }

        // 把左边导航树的那一截染成红十字，让用户就算在别的页面也能注意到
        if (all_configs.count(std_path)) {
          QString group_key =
              QString::fromUtf8(all_configs[std_path].meta.group_key.data(),
                                all_configs[std_path].meta.group_key.size());
          QTreeWidgetItemIterator tree_it(nav_tree_);
          while (*tree_it) {
            if ((*tree_it)->data(0, Qt::UserRole).toString() == group_key &&
                (*tree_it)->data(0, Qt::UserRole + 1).toString() == "GROUP") {
              (*tree_it)->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical));
              QTreeWidgetItem* parent = (*tree_it)->parent();
              while (parent) {
                parent->setExpanded(true);
                parent = parent->parent();
              }
              (*tree_it)->setExpanded(true);
              break;
            }
            ++tree_it;
          }
        }
      }
    }

    this->statusBar()->showMessage(
        "提交失败：请修正左侧红叉标记页面的错误，或放弃修改。", 8000);
    QMessageBox::critical(this, "被拦截的危险操作", error_msg);
    return false;
  } else {
    // 【圆满成功】：交易完成，清空修改缓存，宣告界面洗白
    pending_changes_.clear();
    this->setProperty("backend_errors", QStringList());
    this->statusBar()->showMessage("所有事务均已成功提交并通过持久化落盘。",
                                   3000);

    for (const auto& [cached_group_key, _] : page_cache_) {
      RefreshPageValues(cached_group_key);
    }
    return true;
  }
}

void ConfigMainWindow::RefreshPageValues(const QString& group_key) {
  auto config_srv =
      z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
  if (!config_srv) return;

  std::map<std::string, z3y::interfaces::core::ConfigSnapshot> snapshots;
  try {
    snapshots = config_srv->GetConfigsByGroup(group_key.toStdString());
  } catch (...) {
    return;
  }

  // 构建一个包含整个页面满血状态的假更新包发给底层方法更新界面
  z3y::plugins::qt_ui::ConfigUpdateMap batch_updates;
  for (const auto& [path, snap] : snapshots) {
    pending_changes_.erase(path);
    QString qs_path = QString::fromUtf8(path.data(), path.size());
    batch_updates[qs_path] = ConvertToQVariant(snap.current_value);

    auto it = global_widget_map_.find(qs_path);
    if (it != global_widget_map_.end() && !it->second.isNull()) {
      it->second->setStyleSheet("");
      it->second->setToolTip(QString::fromUtf8(snap.meta.tooltip_key.data(),
                                               snap.meta.tooltip_key.size()));
    }
  }
  OnBatchedConfigChanged(batch_updates);
}

void ConfigMainWindow::closeEvent(QCloseEvent* event) {
  if (!PromptUnsavedChanges()) {
    event->ignore(); // 用户选择了“取消”，或者点击了申请但又改错了，阻止关门
  } else {
    event->accept(); // 放行关门
  }
}

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
