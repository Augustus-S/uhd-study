//
// Copyright 2010-2011,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <uhd/config.hpp>
#include <uhd/types/ranges.hpp>
#include <uhd/utils/noncopyable.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace uhd {

/*!
 * A set of function to control a gain element.
 */
 // 猜测：fcns = functions
struct UHD_API gain_fcns_t
{
    std::function<gain_range_t(void)> get_range;
    std::function<double(void)> get_value;
    std::function<void(double)> set_value;  // 是个关键
};

class UHD_API gain_group : uhd::noncopyable
{
public:
    typedef std::shared_ptr<gain_group> sptr;

    virtual ~gain_group(void) = 0;

    /*!
     * Get the gain range for the gain element specified by name.
     * For an empty name, get the overall gain range for this group.
     * Overall step is defined as the minimum step size.
     * \param name name of the gain element (optional)
     * \return a gain range with overall min, max, step
     */
    virtual gain_range_t get_range(const std::string& name = "") = 0;

    /*!
     * Get the gain value for the gain element specified by name.
     * For an empty name, get the overall gain value for this group.
     * \param name name of the gain element (optional)
     * \return a gain value of the element or all elements
     */
    virtual double get_value(const std::string& name = "") = 0;

    /*!
     * Set the gain value for the gain element specified by name.
     * For an empty name, set the overall gain value for this group.
     * The power will be distributed across individual gain elements.
     * The semantics of how to do this are determined by the priority.
     * \param gain the gain to set for the element or across the group
     * \param name name of the gain element (optional)
     */
    /*!
     * 设置由名称指定的增益元素的增益值。
     * 如果名称为空，则设置此分组的总体增益值。
     * 功率将在各个增益元素之间分配。
     * 具体的分配方式由优先级决定。
     *
     * \param gain 要为该元素或整个分组设置的增益值
     * \param name 增益元素的名称（可选）
     */
    virtual void set_value(double gain, const std::string& name = "") = 0;

    /*!
     * Get a list of names of registered gain elements.
     * The names are in the order that they were registered.
     * \return a vector of gain name strings
     */
    virtual const std::vector<std::string> get_names(void) = 0;

    /*!
     * Register a set of gain functions into this group:
     *
     * The name should be a unique and non-empty name.
     * Otherwise, the implementation will rename it.
     *
     * Priority determines how power will be distributed
     * with higher priorities getting the power first,
     * and lower priorities getting the remainder power.
     *
     * \param name the name of the gain element
     * \param gain_fcns the set of gain functions
     * \param priority the priority of the gain element
     */
    /*!
     * 将一组增益函数注册到此分组中：
     *
     * 名称应当是唯一且非空的，
     * 否则实现会对其进行重命名。
     *
     * 优先级决定了功率的分配方式：
     * 优先级高的增益元素会先获得功率，
     * 优先级低的则获取剩余的功率。
     *
     * \param name 增益元素的名称
     * \param gain_fcns 增益函数集合
     * \param priority 增益元素的优先级
     */
    virtual void register_fcns(
        const std::string& name, const gain_fcns_t& gain_fcns, size_t priority = 0) = 0;

    /*!
     * Make a new empty gain group.
     * 创建一个空的增益组。
     * \return a gain group object.
     */
    static sptr make(void);

    /*!
     * Make a new gain group with all zero values.
     * 创建一个全0的新增益组
     * \return a gain group object populated with zeroes
     */
    static sptr make_zero();
};

} // namespace uhd
