/*
* Copyright [2025] [Yue Liu]
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/**
 * @file plugin_test_base.h
 * @brief [测试基建] 插件测试的公共基类 (Test Fixture)。
 * @details
 *
 * [GTest 核心概念解析]
 * 1. **Test Fixture (测试夹具)**:
 * - 原理：继承自 `::testing::Test` 的类。
 * - 作用：允许你在多个测试用例之间共享数据配置（如 `manager_`）和逻辑。
 * - 机制：GTest 会为每一个 `TEST_F` 宏创建一个**新**的 `PluginTestBase` 对象。
 * 这意味着 Test A 和 Test B 是完全隔离的，互不影响（状态不共享）。
 *
 * 2. **生命周期函数**:
 * - `SetUp()`: 在测试体执行**前**自动调用。类似于构造函数，用于初始化环境。
 * - `TearDown()`: 在测试体执行**后**自动调用。类似于析构函数，用于清理资源（无论测试是否通过）。
 */

#pragma once

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <iostream>

 // 引入框架核心，用于管理插件生命周期
#include "framework/z3y_framework.h"

class PluginTestBase : public ::testing::Test {
protected:
    /**
     * @brief [GTest Hook] 环境初始化
     * @details 每个测试用例 (TEST_F) 开始前都会执行此函数。
     */
    void SetUp() override {
        std::cout << "[TestBase] SetUp: Creating PluginManager..." << std::endl;
        z3y::PluginManager::Destroy();
        // 1. 创建全新的 PluginManager 实例
        //    设计意图：确保每个测试都在一个干净的框架环境中运行，避免上一个测试的残留状态（如单例）干扰当前测试。
        manager_ = z3y::PluginManager::Create();

        // 2. 定位构建目录 (bin)
        bin_dir_ = z3y::utils::GetExecutableDir();
        std::cout << "[TestBase] SetUp: Done." << std::endl;
    }

    /**
     * @brief [GTest Hook] 环境清理
     * @details 每个测试用例 (TEST_F) 结束后都会执行，即使测试中间 ASSERT 失败了也会执行。
     */
    void TearDown() override {
        if (manager_) {
            std::cout << "[TestBase] TearDown: Unloading plugins..." << std::endl;
            // 安全卸载所有插件，触发 Shutdown() 钩子
            manager_->UnloadAllPlugins();
            std::cout << "[TestBase] TearDown: Resetting manager..." << std::endl;
            // 销毁管理器，停止事件线程
            manager_.reset();
            std::cout << "[TestBase] TearDown: Done." << std::endl;
        }

        z3y::PluginManager::Destroy();
    }

    /**
     * @brief [自适应] 加载插件
     * @details 自动根据当前的构建配置 (Debug/Release) 和架构 (x64/x86) 拼接正确的文件名。
     * @param plugin_base_name 插件基础名称 (如 "plugin_spdlog_logger")
     */
    bool LoadPlugin(const std::string& plugin_base_name) {
        std::stringstream ss;

        // 1. 前缀 (Linux/Mac 非 Windows 环境通常以 lib 开头)
#ifndef _WIN32
        ss << "lib";
#endif

        // 2. 基础名称
        ss << plugin_base_name;

        // 3. 架构后缀 (由 CMake 传入，例如 "_x64")
#ifdef Z3Y_ARCH_SUFFIX
        ss << Z3Y_ARCH_SUFFIX;
#endif

        // 4. Debug 后缀 (由 CMake 生成器表达式传入)
        // 对应根目录 CMakeLists.txt 中的 set(CMAKE_DEBUG_POSTFIX "d")
#ifdef Z3Y_IS_DEBUG_BUILD
        ss << "d";
#endif

        // 5. 扩展名
        ss << z3y::utils::GetSharedLibraryExtension();

        std::string filename = ss.str();
        std::filesystem::path path = bin_dir_ / filename;

        // [容错] 如果计算出的文件名不存在，尝试加载最原始的文件名 (应对某些特殊手动编译情况)
        if (!std::filesystem::exists(path)) {
            // 尝试构建一个不带任何后缀的原始路径
            std::filesystem::path fallback_path = bin_dir_ / (plugin_base_name +
                z3y::utils::GetSharedLibraryExtension());

            if (std::filesystem::exists(fallback_path)) {
                std::cout << "[TestBase] Standard path not found: " << filename
                    << ". Falling back to: " << fallback_path.filename().string() << std::endl;
                path = fallback_path;
            }
        }

        std::string err;
        bool ret = manager_->LoadPlugin(path, err);
        if (!ret) {
            std::cerr << "[TestBase] Failed to load " << path.string() << ": " << err << std::endl;
        }
        return ret;
    }

    // [成员变量]
    // 它们是 protected 的，所以继承此类的测试用例可以直接访问。
    z3y::PluginPtr<z3y::PluginManager> manager_;
    std::filesystem::path bin_dir_;
};