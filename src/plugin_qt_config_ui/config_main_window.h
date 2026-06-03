/**
 * @file config_main_window.h
 * @brief Qt 配置系统主界面的核心类声明。
 *
 * @details
 * 这是配置系统的门面。它主要包含了一个左侧的导航树 (QTreeWidget) 
 * 和一个右侧的动态堆叠页面 (QStackedWidget)。
 * 为了应对高达几百个配置参数带来的卡顿，采用了懒加载 (Lazy Load) 与 LRU 页面缓存置换机制。
 */

#pragma once
#include <QCheckBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QPointer>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QTreeWidget>
#include <QVariant>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dependency_graph.h"
#include "event_bridge.h"
#include "interfaces_ui/i_config_ui_manager.h"

namespace z3y {
namespace plugins {
namespace qt_ui {

/**
 * @class ConfigMainWindow
 * @brief 动态配置系统的主窗体实现。
 */
class ConfigMainWindow : public QMainWindow {
  Q_OBJECT
 public:
  /**
   * @brief 构造函数
   * @param event_bridge 负责提供底层到界面的异步数据通信桥梁指针。
   * @param parent 可选的 Qt 父窗口，如果为空则表现为独立窗口。
   */
  explicit ConfigMainWindow(EventBridge* event_bridge,
                            QWidget* parent = nullptr);
  ~ConfigMainWindow() override;

  /** @brief 设定当前的查阅者角色（例如 "Admin"），以便根据权限屏蔽高级或者保密设置。 */
  void SetRole(const std::string& role);
  
  /** @brief 导入从业务层发来的所有自定义界面创建器（工厂函数）。 */
  void SetCustomPanels(
      const std::map<std::string,
                     z3y::interfaces::ui::IConfigUIManager::CustomPanelCreator>&
          panels);

 public slots:
  /** @brief 强制重新向后台索要最新数据来刷新指定的配置页面。 */
  void RefreshPageValues(const QString& group_key);

 protected:
  /** @brief 拦截窗口关闭事件。如果有未保存的数据会弹出警告。 */
  void closeEvent(QCloseEvent* event) override;

 private slots:
  /** @brief 左侧导航树的节点选中切换时触发，负责页面缓存调度和新建。 */
  void OnTreeSelectionChanged(QTreeWidgetItem* current,
                              QTreeWidgetItem* previous);
  /** @brief 全局搜索栏文本变化时触发，动态过滤显示符合要求的树节点。 */
  void OnGlobalSearchTextChanged(const QString& text);
  /** @brief 接收从后台线程通过 EventBridge 发送过来的批量数据更新包。 */
  void OnBatchedConfigChanged(
      const z3y::plugins::qt_ui::ConfigUpdateMap& updates);
  /** @brief 点击 Apply 按钮后的核心提交流程。 */
  bool ApplyChanges();
  /** @brief 切换“显示高级选项”复选框时触发。 */
  void OnAdvancedModeToggled(bool checked);

  /** @brief 点击“导入配方”按钮，触发 JSON 文件读取并覆盖内存数据。 */
  void OnImportClicked();
  /** @brief 点击“导出配方”按钮，将当前系统的全部内存参数落盘至指定的 JSON 文件。 */
  void OnExportClicked();

 private:
  /** @brief 创建并排列窗口内的所有的控件容器。 */
  void SetupUI();
  /** @brief 扫描底层的所有配置信息，将所有的一级、二级分组装配进左侧导航树。 */
  void LoadNavigationTree();
  /** @brief 递归刷新树节点的可见性，支持高级参数的级联隐藏。 */
  void RefreshTreeVisibility(bool is_advanced_mode);
  /** @brief LRU 缓存淘汰：如果已创建的页面过多，则销毁最早最久未使用的页面以释放内存。 */
  void EvictOldestPageIfNeeded();
  /** @brief 询问用户是否要抛弃/保存那些还未 Apply 的悬而不决的更改。 */
  bool PromptUnsavedChanges();
  /** @brief 放弃目前界面上所有修改，将所有带有高亮边框的组件恢复为后台真实数值。 */
  void DiscardAllPendingChanges();

 private:
  /** @brief 内存中最多同时保留的活跃子页面数量，超过则淘汰。 */
  static constexpr int kMaxCachedPages = 10;

  EventBridge* event_bridge_;  /**< @brief 通信桥指针，由 Service 提供。 */
  std::string current_role_;   /**< @brief 当前角色的字符串标识。 */
  bool is_advanced_mode_ = false; /**< @brief 当前是否已经勾选了高级模式。 */
  QString current_group_;      /**< @brief 记录当前所处的组节点名称。 */

  QCheckBox* cb_advanced_;     /**< @brief 顶部栏的高级勾选框。 */
  QLineEdit* search_box_;      /**< @brief 左上角的搜索框。 */
  QTreeWidget* nav_tree_;      /**< @brief 左侧树状目录。 */
  QStackedWidget* stacked_pages_; /**< @brief 右侧多页面堆栈。 */

  /** @brief 用于在不同的参数之间建立并运算复杂的使能/禁用依赖关系的图谱。 */
  std::unique_ptr<DependencyGraph> dependency_graph_;

  // ---- [ LRU 页面缓存管理结构 ] ----
  std::list<QString> lru_queue_;
  std::unordered_map<QString, std::list<QString>::iterator> lru_mapping_;
  std::unordered_map<QString, QWidget*> page_cache_;

  /** 
   * @brief 全局控件快速索引字典。
   * @details 键是配置参数的全路径(如 "Camera.Exposure")，值是创建出来的对应 UI 控件弱指针。 
   */
  std::unordered_map<QString, QPointer<QWidget>> global_widget_map_;
  
  /** @brief 所有用户在界面上修改了、但是还没有点击 Apply 发往后台的数据缓存池。 */
  std::map<std::string, QVariant> pending_changes_;

  /** @brief 保存从外部注册进来的所有第三方自定义渲染界面的闭包工厂。 */
  std::map<std::string,
           z3y::interfaces::ui::IConfigUIManager::CustomPanelCreator>
      custom_panels_;
};

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
