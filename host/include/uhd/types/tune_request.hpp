//
// Copyright 2010-2012 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <uhd/config.hpp>
#include <uhd/types/device_addr.hpp>

namespace uhd {

/*!
 * A tune request instructs the implementation how to tune the RF chain.
 * The policies can be used to select automatic tuning or
 * fine control over the daughterboard IF and DSP tuning.
 * Not all combinations of policies are applicable.
 * Convenience constructors are supplied for most use cases.
 *
 * See also \ref general_tuning
 */
struct UHD_API tune_request_t
{
    /*!
     * Make a new tune request for a particular center frequency.
     * Use an automatic policy for the RF and DSP frequency
     * to tune the chain as close as possible to the target frequency.
     * \param target_freq the target frequency in Hz
     */
    tune_request_t(double target_freq = 0); // 自动调谐，用户设置中心频率，系统自动将射频（RF）和数字信号处理（DSP）频率调整到尽可能接近目标频率

    /*!
     * Make a new tune request for a particular center frequency.
     * Use a manual policy for the RF frequency,
     * and an automatic policy for the DSP frequency,
     * to tune the chain as close as possible to the target frequency.
     * \param target_freq the target frequency in Hz
     * \param lo_off the LO offset frequency in Hz
     */
    /*!
     * 创建一个新的调谐请求，针对特定的中心频率。
     * 对射频（RF）频率使用手动策略，
     * 对数字信号处理（DSP）频率使用自动策略，
     * 以将信号链调整到尽可能接近目标频率。
     * \param target_freq 目标频率，单位为赫兹（Hz）
     * \param lo_off 本振（LO）偏移频率，单位为赫兹（Hz）
     */
    tune_request_t(double target_freq, double lo_off);  // 半自动调谐，

    //! Policy options for tunable elements in the RF chain.
    enum policy_t {
        //! Do not set this argument, use current setting.
        POLICY_NONE = int('N'),
        //! Automatically determine the argument's value.
        POLICY_AUTO = int('A'),
        //! Use the argument's value for the setting.
        POLICY_MANUAL = int('M')
    };

    /*!
     * The target frequency of the overall chain in Hz.
     * Set this even if all policies are set to manual.
     */
    double target_freq;

    /*!
     * The policy for the RF frequency.
     * Automatic behavior: the target frequency + default LO offset.
     */
    policy_t rf_freq_policy;

    /*!
     * The RF frequency in Hz.
     * Set when the policy is set to manual.
     */
    double rf_freq;

    /*!
     * The policy for the DSP frequency.
     * Automatic behavior: the difference between the target and IF.
     */
    policy_t dsp_freq_policy;

    /*!
     * The DSP frequency in Hz.
     * Set when the policy is set to manual.
     *
     * Note that the meaning of the DSP frequency's sign differs between
     * TX and RX operations. The target frequency is the result of
     * `target_freq = rf_freq + sign * dsp_freq`. For TX, `sign` is
     * negative, and for RX, `sign` is positive.
     * Example: If both RF and DSP tuning policies are set to manual, and
     * `rf_freq` is set to 1 GHz, and `dsp_freq` is set to 10 MHz, the
     * actual target frequency is 990 MHz for a TX tune request, and
     * 1010 MHz for an RX tune request.
     */
    double dsp_freq;

    /*!
     * The args parameter is used to pass arbitrary key/value pairs.
     * Possible keys used by args (depends on implementation):
     *
     * - mode_n: Allows the user to tell the daughterboard tune code
     * to choose between an integer N divider or fractional N divider.
     * Default is fractional N on boards that support fractional N tuning.
     * Fractional N provides greater tuning accuracy at the expense of spurs.
     * Possible options for this key: "integer" or "fractional".
     */
    /*!
     * 参数 args 用于传递任意的键/值对。
     * args 可能使用的键（视具体实现而定）：
     *
     * - mode_n：允许用户告诉子板调谐代码
     *   选择使用整数 N 分频器还是分数 N 分频器。
     *   默认情况下，支持分数 N 调谐的板子使用分数 N。
     *   分数 N 在提供更高调谐精度的同时，可能带来杂散信号。
     *   该键可能的选项值为："integer" 或 "fractional"。
     */
    device_addr_t args;
};

} // namespace uhd
