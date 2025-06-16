/*
 * Copyright 2010-2012,2015 Ettus Research LLC
 * Copyright 2018 Ettus Research, a National Instruments Company
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once    // 头文件保护机制，用来防止头文件被多次包含

#include <uhd/config.hpp>
#include <uhd/types/time_spec.hpp>

namespace uhd {

/*!
 * Command struct for configuration and control of streaming:
 *
 * A stream command defines how the device sends samples to the host.
 * Streaming is controlled by submitting a stream command to the rx dsp.
 * Granular control over what the device streams to the host can be
 * achieved through submission of multiple (carefully-crafted) commands.
 *
 * The mode parameter controls how streaming is issued to the device:
 *   - "Start continuous" tells the device to stream samples indefinitely.
 *   - "Stop continuous" tells the device to end continuous streaming.
 *   - "Num samps and done" tells the device to stream num samps and
 *      to not expect a future stream command for contiguous samples.
 *   - "Num samps and more" tells the device to stream num samps and
 *      to expect a future stream command for contiguous samples.
 *
 * The stream now parameter controls when the stream begins.
 * When true, the device will begin streaming ASAP. When false,
 * the device will begin streaming at a time specified by time_spec.
 *
 * Note: When a radio runs at multiple samples per clock cycle, it may not be
 * possible to request samples at any given time, and \p num_samps might have to
 * be an integer multiple of SPC.
 */
/*!
 * 用于配置和控制数据流的命令结构体：
 *
 * 一个流命令（stream command）定义了设备如何将采样数据发送到主机。
 * 通过向接收 DSP 提交流命令，可以控制数据流的行为。
 * 如果精确设计多个命令的参数，可以实现对设备向主机传输数据的精细控制。
 *
 * `mode` 参数控制设备如何进行数据流操作：
 *   - “Start continuous”（开始连续流）告诉设备无限期地持续发送采样数据。
 *   - “Stop continuous”（停止连续流）告诉设备结束连续流传输。
 *   - “Num samps and done”（发送指定数量的采样并结束）告诉设备发送 `num_samps` 个采样后就不再期望接收连续流命令。
 *   - “Num samps and more”（发送指定数量的采样并继续）告诉设备发送 `num_samps` 个采样后，仍然期待后续的连续流命令。
 *
 * `stream_now` 参数控制数据流何时开始。
 * 当该参数为 `true` 时，设备将尽快开始流传输；
 * 当为 `false` 时，设备将在 `time_spec` 指定的时间开始传输。
 *
 * 注意：当一个射频设备在每个时钟周期内运行多个采样时，可能无法在任意时间点请求采样，
 * 此时 `num_samps` 必须是每周期采样数（SPC）的整数倍。
 */
struct UHD_API stream_cmd_t
{
    enum stream_mode_t {
        STREAM_MODE_START_CONTINUOUS   = int('a'),
        STREAM_MODE_STOP_CONTINUOUS    = int('o'),
        STREAM_MODE_NUM_SAMPS_AND_DONE = int('d'),
        STREAM_MODE_NUM_SAMPS_AND_MORE = int('m')
    } stream_mode;
    uint64_t num_samps;

    bool stream_now;
    time_spec_t time_spec;

    stream_cmd_t(const stream_mode_t& stream_mode);
};

} /* namespace uhd */
