/**
 * @file widget_factory.cpp
 * @brief 工厂类的底层实现，负责数据模型到界面控件的一对一动态生成。
 */

#include "widget_factory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <variant>

#include "interfaces_core/i_config_service.h"
#include "scroll_guard.h"
#include "qt_utils.h"

namespace z3y {
namespace plugins {
namespace qt_ui {

namespace {

/**
 * @brief 辅助工具函数：从万能变体 ConfigValue 中安全汲取浮点数。
 * @details 当我们认定某个参数是浮点型控件(如 DoubleSpinBox) 时，如果底层传了个整数(int64_t)过来，我们在这里强转。
 * @param val 后端变体。
 * @param def_val 拿不到或者拿错时的托底保命值。
 */
double ExtractDouble(const z3y::interfaces::core::ConfigValue& val,
                     double def_val) {
  if (std::holds_alternative<double>(val)) return std::get<double>(val);
  if (std::holds_alternative<int64_t>(val))
    return static_cast<double>(std::get<int64_t>(val));
  return def_val;
}

/**
 * @brief 辅助工具函数：从万能变体 ConfigValue 中安全汲取大整数。
 */
int64_t ExtractInt64(const z3y::interfaces::core::ConfigValue& val,
                     int64_t def_val) {
  if (std::holds_alternative<int64_t>(val)) return std::get<int64_t>(val);
  if (std::holds_alternative<double>(val))
    return static_cast<int64_t>(std::get<double>(val));
  return def_val;
}

/**
 * @brief 辅助工具函数：从附加信息的 JSON 对象中提取浮点属性（如 step, decimals）。
 */
double ExtractJsonDouble(const QJsonObject& obj, const QString& key,
                         double def = 0.0) {
  if (!obj.contains(key)) return def;
  QJsonValue v = obj[key];
  if (v.isString()) return v.toString().toDouble();
  return v.toDouble(def);
}

/**
 * @brief 辅助工具函数：从附加信息的 JSON 对象中提取整型属性。
 */
int ExtractJsonInt(const QJsonObject& obj, const QString& key, int def = 0) {
  if (!obj.contains(key)) return def;
  QJsonValue v = obj[key];
  if (v.isString()) return v.toString().toInt();
  return v.toInt(def);
}

/**
 * @brief 辅助工具函数：安全地在 QComboBox 列表中寻找对应数值所在的 Index 索引。
 */
int FindComboIndexSafe(QComboBox* cb, const QVariant& target_val) {
  int idx = cb->findData(target_val);
  if (idx != -1) return idx;

  QString target_str = target_val.toString();
  for (int i = 0; i < cb->count(); ++i) {
    if (cb->itemData(i).toString() == target_str) return i;
  }
  return -1;
}

}  // namespace

QWidget* WidgetFactory::CreatePage(
    const QString& group_key, const std::string& current_role,
    DependencyGraph* graph,
    const std::map<std::string, QVariant>& pending_changes,
    QObject* context_obj,
    std::function<void(const QString&, const QVariant&)> on_change_callback,
    bool is_advanced_mode,
    const std::map<std::string,
                   z3y::interfaces::ui::IConfigUIManager::CustomPanelCreator>&
        custom_panels) {
  
  // 1. 创建该分组的顶级容器：带滚动条的画板
  QScrollArea* scroll_area = new QScrollArea();
  scroll_area->setWidgetResizable(true);

  // 2. 画板里的中心容器，采用垂直流式布局
  QWidget* container = new QWidget();
  QVBoxLayout* main_layout = new QVBoxLayout(container);

  auto config_srv =
      z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
  if (!config_srv) {
    scroll_area->setWidget(container);
    return scroll_area;
  }

  std::map<std::string, z3y::interfaces::core::ConfigSnapshot> snapshots;
  try {
    snapshots = config_srv->GetConfigsByGroup(group_key.toStdString());
  } catch (...) {
  }

  // 3. 将扁平的一级分组数据，根据 meta.subgroup_key 二次分类合并，准备打包装入各个 GroupBox
  std::map<std::string,
           std::vector<
               std::pair<std::string, z3y::interfaces::core::ConfigSnapshot>>>
      grouped_snaps;
  for (const auto& [path, snap] : snapshots) {
    if (snap.meta.is_hidden) continue;
    std::string subg = snap.meta.subgroup_key.empty() ? "General Attributes"
                                                      : snap.meta.subgroup_key;
    grouped_snaps[subg].push_back({path, snap});
  }

  // 4. 遍历所有二级分组，生成带边框和标题的 QGroupBox 控件组
  for (const auto& [subg_name, items] : grouped_snaps) {
    QGroupBox* group_box = new QGroupBox(QString::fromStdString(subg_name));
    group_box->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #c0c0c0; "
        "border-radius: 5px; margin-top: 1ex; padding: 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: "
        "top center; padding: 0 3px; }");
    QVBoxLayout* group_layout = new QVBoxLayout(group_box);

    // 4.1 在 GroupBox 内部的右上角注入一个 “[↺] Reset Data” 复原按钮
    QHBoxLayout* header_layout = new QHBoxLayout();
    header_layout->addStretch();
    QPushButton* btn_reset = new QPushButton("[↺] Reset Data");
    btn_reset->setToolTip(
        "一键将此框内参数刷回系统底层预设的安全值 (需要点击 Apply "
        "才会实质落地)。");
    header_layout->addWidget(btn_reset);
    group_layout->addLayout(header_layout);

    QFormLayout* form_layout = new QFormLayout();
    group_layout->addLayout(form_layout);

    // 用于收集当前 GroupBox 下所有子控件的重置回调（当用户按下 Reset 按钮时统一触发）
    std::vector<std::function<void()>> resets_actions;

    // 5. 核心循环：将数据快照转换成真实可见的 Qt Widget
    for (const auto& [path, snap] : items) {
      const QString qpath = QString::fromStdString(path);
      QWidget* control = nullptr;
      QVariant display_val;
      bool has_pending = false;

      // 看看这个参数是否正处于“挂起状态”（用户刚改过还没 Apply）
      auto it_pending = pending_changes.find(path);
      if (it_pending != pending_changes.end()) {
        display_val = it_pending->second;
        has_pending = true;
      }

      QJsonObject custom_args;
      if (!snap.meta.custom_args.empty()) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(
            QString::fromStdString(snap.meta.custom_args).toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) custom_args = doc.object();
      }

      // ==== 5.1 解析 Schema 的 widget_type 进行控件动态实例化 ==== 
      
      if (snap.meta.widget_type == z3y::interfaces::core::WidgetType::kCustom) {
        // [定制派生路线]：尝试从 custom_panels 缓存中索要外部注入的画布闭包
        auto it = custom_panels.find(snap.meta.custom_ui_key);
        if (it != custom_panels.end() && it->second) {
          void* raw_ptr = it->second(container);
          if (raw_ptr) control = static_cast<QWidget*>(raw_ptr);
        }
        if (!control) {
          control = new QLabel(
              QString("❌ Custom Panel Missing: %1")
                  .arg(QString::fromStdString(snap.meta.custom_ui_key)));
          control->setStyleSheet("color: red; border: 1px dashed red;");
        }
      } else if (snap.meta.widget_type ==
                     z3y::interfaces::core::WidgetType::kFilePicker ||
                 snap.meta.widget_type ==
                     z3y::interfaces::core::WidgetType::kDirPicker) {
        // [文件挑选器]：输入框 + "..." 按钮的复合体
        if (!std::holds_alternative<std::string>(snap.current_value)) continue;

        QHBoxLayout* h_layout = new QHBoxLayout();
        h_layout->setContentsMargins(0, 0, 0, 0);

        QLineEdit* path_edit = new QLineEdit();
        path_edit->setText(has_pending
                               ? display_val.toString()
                               : QString::fromStdString(std::get<std::string>(
                                     snap.current_value)));
        QPushButton* browse_btn = new QPushButton("...");
        h_layout->addWidget(path_edit);
        h_layout->addWidget(browse_btn);

        QString filter = custom_args.contains("filter")
                             ? custom_args["filter"].toString()
                             : "All Files (*.*)";
        bool is_dir = (snap.meta.widget_type ==
                       z3y::interfaces::core::WidgetType::kDirPicker);

        QPointer<QLineEdit> safe_path_edit(path_edit);
        QObject::connect(
            browse_btn, &QPushButton::clicked, context_obj,
            [safe_path_edit, filter, is_dir]() {
              if (!safe_path_edit) return;
              QString selected =
                  is_dir
                      ? QFileDialog::getExistingDirectory(
                            nullptr, "Select Directory", safe_path_edit->text())
                      : QFileDialog::getOpenFileName(nullptr, "Select File",
                                                     safe_path_edit->text(), filter);
              if (!selected.isEmpty() && safe_path_edit) safe_path_edit->setText(selected);
            });
        QObject::connect(path_edit, &QLineEdit::textChanged, context_obj,
                         [on_change_callback, qpath](const QString& text) {
                           if (on_change_callback)
                             on_change_callback(qpath, text);
                         });

        QWidget* wrap = new QWidget();
        wrap->setLayout(h_layout);
        control = wrap;
      } else if (snap.meta.widget_type ==
                 z3y::interfaces::core::WidgetType::kSlider) {
        // [滑动条型]：细分为精细浮点数旋转盒，或者纯粹的整数滑块
        if (std::holds_alternative<double>(snap.current_value)) {
          QDoubleSpinBox* dspin = new QDoubleSpinBox();
          dspin->setDecimals(ExtractJsonInt(custom_args, "decimals", 6));
          dspin->setMinimum(ExtractDouble(snap.meta.min_val, std::numeric_limits<int>::min()));
          dspin->setMaximum(ExtractDouble(snap.meta.max_val, std::numeric_limits<int>::max()));
          dspin->setSingleStep(ExtractJsonDouble(custom_args, "step", 1.0));
          dspin->setValue(has_pending ? display_val.toDouble()
                                      : std::get<double>(snap.current_value));

          if (on_change_callback) {
            QObject::connect(
                dspin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                context_obj, [on_change_callback, qpath](double val) {
                  on_change_callback(qpath, QVariant(val));
                });
          }
          control = dspin;
        } else if (std::holds_alternative<int64_t>(snap.current_value)) {
          QSlider* slider = new QSlider(Qt::Horizontal);
          slider->setMinimum(
              std::clamp<int64_t>(ExtractInt64(snap.meta.min_val, std::numeric_limits<int>::min()),
                                  std::numeric_limits<int>::min(),
                                  std::numeric_limits<int>::max()));
          slider->setMaximum(
              std::clamp<int64_t>(ExtractInt64(snap.meta.max_val, std::numeric_limits<int>::max()),
                                  std::numeric_limits<int>::min(),
                                  std::numeric_limits<int>::max()));
          slider->setSingleStep(ExtractJsonInt(custom_args, "step", 1));
          slider->setValue(has_pending
                               ? display_val.toInt()
                               : static_cast<int>(std::clamp<int64_t>(
                                     std::get<int64_t>(snap.current_value),
                                     std::numeric_limits<int>::min(),
                                     std::numeric_limits<int>::max())));

          if (on_change_callback) {
            QObject::connect(slider, &QSlider::valueChanged, context_obj,
                             [on_change_callback, qpath](int val) {
                               on_change_callback(
                                   qpath, QVariant(static_cast<qint64>(val)));
                             });
          }
          control = slider;
        } else {
          continue;
        }
      } else if (snap.meta.widget_type ==
                 z3y::interfaces::core::WidgetType::kPasswordInput) {
        // [密码屏蔽框]
        if (!std::holds_alternative<std::string>(snap.current_value)) continue;
        QLineEdit* pwd_edit = new QLineEdit();
        pwd_edit->setEchoMode(QLineEdit::Password);
        pwd_edit->setText(has_pending
                              ? display_val.toString()
                              : QString::fromStdString(
                                    std::get<std::string>(snap.current_value)));
        if (on_change_callback) {
          QObject::connect(pwd_edit, &QLineEdit::textChanged, context_obj,
                           [on_change_callback, qpath](const QString& text) {
                             on_change_callback(qpath, text);
                           });
        }
        control = pwd_edit;
      } else if (snap.meta.widget_type ==
                 z3y::interfaces::core::WidgetType::kProgressBar) {
        // [纯展示进度条]
        QProgressBar* prog = new QProgressBar();
        prog->setMinimum(
            static_cast<int>(ExtractDouble(snap.meta.min_val, 0.0)));
        prog->setMaximum(
            static_cast<int>(ExtractDouble(snap.meta.max_val, 100.0)));
        prog->setValue(static_cast<int>(
            has_pending ? display_val.toDouble()
                        : ExtractDouble(snap.current_value, 0.0)));
        control = prog;
      } else if (snap.meta.widget_type ==
                 z3y::interfaces::core::WidgetType::kComboBox) {
        // [下拉枚举字典]
        QComboBox* combo = new QComboBox();
        for (size_t i = 0; i < snap.meta.enum_display_keys.size(); ++i) {
          QVariant real_data =
              (i < snap.meta.enum_values.size())
                  ? QVariant(QString::fromStdString(snap.meta.enum_values[i]))
                  : QVariant(static_cast<qlonglong>(i));
          combo->addItem(QString::fromStdString(snap.meta.enum_display_keys[i]),
                         real_data);
        }

        if (has_pending) {
          int idx = FindComboIndexSafe(combo, display_val);
          if (idx != -1)
            combo->setCurrentIndex(idx);
          else if (display_val.userType() == QMetaType::Int ||
                   display_val.userType() == QMetaType::LongLong) {
            if (display_val.toInt() >= 0 &&
                display_val.toInt() < combo->count())
              combo->setCurrentIndex(display_val.toInt());
          }
        } else if (std::holds_alternative<int64_t>(snap.current_value)) {
          int idx = FindComboIndexSafe(
              combo, QVariant(static_cast<qlonglong>(
                         std::get<int64_t>(snap.current_value))));
          if (idx != -1)
            combo->setCurrentIndex(idx);
          else
            combo->setCurrentIndex(
                static_cast<int>(std::get<int64_t>(snap.current_value)));
        } else if (std::holds_alternative<std::string>(snap.current_value)) {
          int idx = FindComboIndexSafe(
              combo, QString::fromStdString(
                         std::get<std::string>(snap.current_value)));
          if (idx != -1) combo->setCurrentIndex(idx);
        }

        if (on_change_callback) {
          QObject::connect(
              combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
              context_obj, [on_change_callback, qpath, combo](int index) {
                on_change_callback(qpath, combo->itemData(index));
              });
        }
        control = combo;
      } else if (std::holds_alternative<int64_t>(snap.current_value)) {
        // [缺省分支：推导为 QSpinBox 或大数文本框]
        int64_t act_val = has_pending ? display_val.toLongLong()
                                      : std::get<int64_t>(snap.current_value);
        // 如果整数大得超出了 Qt 控件的承受极限，转而使用 QLineEdit 防爆
        if (act_val > 9007199254740991LL || act_val < -9007199254740991LL) {
          QLineEdit* line_edit = new QLineEdit();
          line_edit->setText(QString::number(act_val));
          line_edit->setValidator(new QRegularExpressionValidator(
              QRegularExpression("^-?\\d{1,19}$"), line_edit));
          if (on_change_callback) {
            QObject::connect(line_edit, &QLineEdit::textChanged, context_obj,
                             [on_change_callback, qpath](const QString& text) {
                               if (text.isEmpty() || text == "-") return;
                               bool ok;
                               qlonglong val = text.toLongLong(&ok);
                               if (ok) on_change_callback(qpath, QVariant(val));
                             });
          }
          control = line_edit;
        } else {
          QDoubleSpinBox* spin = new QDoubleSpinBox();
          spin->setDecimals(0); // 伪装成整数调节器
          spin->setMinimum(ExtractDouble(snap.meta.min_val, -9e15));
          spin->setMaximum(ExtractDouble(snap.meta.max_val, 9e15));
          spin->setSingleStep(ExtractJsonDouble(custom_args, "step", 1.0));
          spin->setValue(static_cast<double>(act_val));

          if (on_change_callback) {
            QObject::connect(
                spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                context_obj, [on_change_callback, qpath](double val) {
                  on_change_callback(qpath, QVariant(static_cast<qint64>(val)));
                });
          }
          control = spin;
        }
      } else if (std::holds_alternative<double>(snap.current_value)) {
        // [缺省分支：推导为浮点型 QDoubleSpinBox]
        QDoubleSpinBox* dspin = new QDoubleSpinBox();
        dspin->setDecimals(ExtractJsonInt(custom_args, "decimals", 6));
        dspin->setMinimum(ExtractDouble(snap.meta.min_val, -9e15));
        dspin->setMaximum(ExtractDouble(snap.meta.max_val, 9e15));
        dspin->setSingleStep(ExtractJsonDouble(custom_args, "step", 1.0));
        dspin->setValue(has_pending ? display_val.toDouble()
                                    : std::get<double>(snap.current_value));

        if (on_change_callback) {
          QObject::connect(
              dspin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
              context_obj, [on_change_callback, qpath](double val) {
                on_change_callback(qpath, QVariant(val));
              });
        }
        control = dspin;
      } else if (std::holds_alternative<bool>(snap.current_value)) {
        // [缺省分支：推导为布尔型 QCheckBox]
        QCheckBox* check = new QCheckBox();
        check->setChecked(has_pending ? display_val.toBool()
                                      : std::get<bool>(snap.current_value));
        if (on_change_callback) {
          QObject::connect(check, &QCheckBox::toggled, context_obj,
                           [on_change_callback, qpath](bool checked) {
                             on_change_callback(qpath, QVariant(checked));
                           });
        }
        control = check;
      } else if (std::holds_alternative<std::string>(snap.current_value)) {
        // [缺省分支：推导为字符串输入框]
        QLineEdit* line_edit = new QLineEdit();
        line_edit->setText(has_pending
                               ? display_val.toString()
                               : QString::fromStdString(std::get<std::string>(
                                     snap.current_value)));
        if (on_change_callback) {
          QObject::connect(line_edit, &QLineEdit::textChanged, context_obj,
                           [on_change_callback, qpath](const QString& text) {
                             on_change_callback(qpath, text);
                           });
        }
        control = line_edit;
      } else if (std::holds_alternative<std::vector<int64_t>>(
                     snap.current_value) ||
                 std::holds_alternative<std::vector<double>>(
                     snap.current_value) ||
                 std::holds_alternative<std::vector<std::string>>(
                     snap.current_value)) {
        // [缺省分支：但凡是集合类型，推导渲染为超级表格控件 QTableWidget]
        QTableWidget* table = new QTableWidget(0, 1);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setMinimumHeight(100);
        table->setMaximumHeight(250);

        if (custom_args.contains("headers")) {
          QJsonArray headers = custom_args["headers"].toArray();
          QStringList header_labels;
          for (auto v : headers) header_labels << v.toString();
          table->setColumnCount(header_labels.size());
          table->setHorizontalHeaderLabels(header_labels);
        } else {
          table->setHorizontalHeaderLabels(
              {QString::fromStdString(snap.meta.name_key)});
        }

        auto populate_table = [table](const auto& vec) {
          const int vec_sz = static_cast<int>(vec.size());
          table->setRowCount(vec_sz);
          for (int i = 0; i < vec_sz; ++i) {
            QTableWidgetItem* item = nullptr;
            if constexpr (std::is_same_v<
                              typename std::decay_t<decltype(vec)>::value_type,
                              std::string>)
              item = new QTableWidgetItem(QString::fromStdString(vec[i]));
            else if constexpr (std::is_same_v<typename std::decay_t<
                                                  decltype(vec)>::value_type,
                                              double>)
              item = new QTableWidgetItem(QString::number(vec[i], 'g', 16));
            else
              item = new QTableWidgetItem(QString::number(vec[i]));
            table->setItem(i, 0, item);
          }
        };

        if (has_pending) {
          QVariantList list = display_val.toList();
          table->setRowCount(static_cast<int>(list.size()));
          for (int i = 0; i < list.size(); ++i)
            table->setItem(i, 0, new QTableWidgetItem(list[i].toString()));
        } else {
          std::visit(
              [&populate_table](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::vector<int64_t>> ||
                              std::is_same_v<T, std::vector<double>> ||
                              std::is_same_v<T, std::vector<std::string>>)
                  populate_table(arg);
              },
              snap.current_value);
        }

        QPushButton* btn_add = new QPushButton("+ Add Row");
        QPushButton* btn_del = new QPushButton("- Remove Row");
        QHBoxLayout* btn_layout = new QHBoxLayout();
        btn_layout->addWidget(btn_add);
        btn_layout->addWidget(btn_del);

        QVBoxLayout* table_layout = new QVBoxLayout();
        table_layout->setContentsMargins(0, 0, 0, 0);
        table_layout->addWidget(table);
        table_layout->addLayout(btn_layout);

        QWidget* table_container = new QWidget();
        table_container->setLayout(table_layout);

        // 【安全补丁】：严防 UAF
        QPointer<QTableWidget> safe_table(table);
        auto extract_and_emit = [safe_table, on_change_callback, qpath]() {
          if (!safe_table) return;
          QVariantList list;
          for (int i = 0; i < safe_table->rowCount(); ++i) {
            list.append(safe_table->item(i, 0) ? safe_table->item(i, 0)->text() : "");
          }
          if (on_change_callback) on_change_callback(qpath, list);
        };

        QObject::connect(
            table, &QTableWidget::itemChanged, context_obj,
            [extract_and_emit](QTableWidgetItem*) { extract_and_emit(); });
        QObject::connect(btn_add, &QPushButton::clicked, context_obj,
                         [safe_table, extract_and_emit]() {
                           if (!safe_table) return;
                           const int row_idx = safe_table->rowCount();
                           safe_table->insertRow(row_idx);
                           {
                             QSignalBlocker blocker(safe_table.data());
                             safe_table->setItem(row_idx, 0,
                                            new QTableWidgetItem(""));
                           }
                           extract_and_emit();
                         });
        QObject::connect(btn_del, &QPushButton::clicked, context_obj,
                         [safe_table, extract_and_emit]() {
                           if (!safe_table) return;
                           const int row_idx = safe_table->currentRow();
                           if (row_idx >= 0) {
                             {
                               QSignalBlocker blocker(safe_table.data());
                               safe_table->removeRow(row_idx);
                             }
                             extract_and_emit();
                           }
                         });
        control = table_container;
      }

      // ==== 5.2 控件通用属性绑定阶段 ====
      if (control) {
        // 给控件打上各种烙印，方便反向溯源和查字典
        QString safe_obj_name =
            QString("ConfigItem_%1").arg(QString(qpath).replace(".", "_"));
        control->setObjectName(safe_obj_name);
        control->setProperty("config_path", qpath);
        control->setProperty("is_advanced", snap.meta.is_advanced);

        // [条件管控挂载]
        if (!snap.meta.enable_condition.empty()) {
          QString cond_str = QString::fromStdString(snap.meta.enable_condition);
          control->setProperty("enable_condition", cond_str);
          static const QRegularExpression re(
              "^(!?)([\\w\\.]+)(?:\\s*(==|!=|>|<|>=|<=)\\s*(.*))?$");
          QRegularExpressionMatch match = re.match(cond_str);
          if (match.hasMatch()) {
            if (graph)
              graph->AddDependency(qpath.toStdString(),
                                   match.captured(2).toStdString(), control);
          }
        }

        // [防护] 为这个控件戴上套套，防止滚轮乱切改变数值
        control->setFocusPolicy(Qt::StrongFocus);
        control->installEventFilter(new ScrollGuardFilter(control));

        // UI视觉反馈
        if (has_pending)
          control->setStyleSheet("font-weight: bold; color: #b35900;");
        else
          control->setToolTip(QString::fromStdString(snap.meta.tooltip_key));

        // 像表单一样左右布局，左边标题，右边是这个刚刚做出来的控件
        form_layout->addRow(QString::fromStdString(snap.meta.name_key),
                            control);

        // 如果权限不够或者需要勾选高级模式，就先让它隐形
        if (snap.meta.is_advanced && !is_advanced_mode) {
          control->setVisible(false);
          QWidget* label = form_layout->labelForField(control);
          if (label) label->setVisible(false);
        }

        // 【重置按钮绑定逻辑】：组装一个闭包，把它塞到 resets_actions 列表里
        // 这里必须按值捕获 QPointer，绝不能用裸指针，因为这是异步随时可能触发的
        QPointer<QWidget> safe_control(control);

        resets_actions.push_back([safe_control, snap]() {
          if (!safe_control) return;

          QWidget* w = safe_control.data();
          QSpinBox* spin = nullptr;
          QDoubleSpinBox* dspin = nullptr;
          QCheckBox* check = nullptr;
          QLineEdit* line = nullptr;
          QSlider* slider = nullptr;
          QComboBox* combo = nullptr;
          QTableWidget* table = nullptr;
          QProgressBar* prog = nullptr;

          if (w) {
            spin = w->inherits("QSpinBox") ? qobject_cast<QSpinBox*>(w)
                                           : w->findChild<QSpinBox*>();
            dspin = w->inherits("QDoubleSpinBox")
                        ? qobject_cast<QDoubleSpinBox*>(w)
                        : w->findChild<QDoubleSpinBox*>();
            check = w->inherits("QCheckBox")
                        ? qobject_cast<QCheckBox*>(w)
                        : w->findChild<QCheckBox*>();
            line = w->inherits("QLineEdit")
                       ? qobject_cast<QLineEdit*>(w)
                       : w->findChild<QLineEdit*>();
            slider = w->inherits("QSlider") ? qobject_cast<QSlider*>(w)
                                            : w->findChild<QSlider*>();
            combo = w->inherits("QComboBox")
                        ? qobject_cast<QComboBox*>(w)
                        : w->findChild<QComboBox*>();
            table = w->inherits("QTableWidget")
                        ? qobject_cast<QTableWidget*>(w)
                        : w->findChild<QTableWidget*>();
            prog = w->inherits("QProgressBar")
                       ? qobject_cast<QProgressBar*>(w)
                       : w->findChild<QProgressBar*>();
          }

          if (spin)
            spin->setValue(ExtractInt64(snap.default_value, 0));
          else if (dspin)
            dspin->setValue(ExtractDouble(snap.default_value, 0.0));
          else if (check)
            check->setChecked(std::get<bool>(snap.default_value));
          else if (line) {
            if (std::holds_alternative<std::string>(snap.default_value))
              line->setText(QString::fromStdString(
                  std::get<std::string>(snap.default_value)));
            else if (std::holds_alternative<int64_t>(snap.default_value))
              line->setText(
                  QString::number(std::get<int64_t>(snap.default_value)));
          } else if (slider)
            slider->setValue(ExtractInt64(snap.default_value, 0));
          else if (combo) {
            QVariant target_val = ConvertToQVariant(snap.default_value);
            int idx = FindComboIndexSafe(combo, target_val);
            if (idx != -1) combo->setCurrentIndex(idx);
          } else if (table) {
            QVariant target_val = ConvertToQVariant(snap.default_value);
            QVariantList list = target_val.toList();
            table->setRowCount(list.size());
            for (int i = 0; i < list.size(); ++i) {
              QTableWidgetItem* item = table->item(i, 0);
              if (!item) {
                item = new QTableWidgetItem();
                table->setItem(i, 0, item);
              }
              item->setText(list[i].toString());
            }
          } else if (prog) {
            prog->setValue(
                static_cast<int>(ExtractDouble(snap.default_value, 0.0)));
          }
        });
      }
    }

    // 将刚才打包的全部 Reset 闭包一次性挂到对应的那个按钮上
    QObject::connect(btn_reset, &QPushButton::clicked, context_obj,
                     [resets_actions]() {
                       for (auto& r : resets_actions) r();
                     });

    main_layout->addWidget(group_box);
  }

  // 画板的最下面用弹簧撑起来，保持紧凑排版
  main_layout->addStretch();
  scroll_area->setWidget(container);
  return scroll_area;
}

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
