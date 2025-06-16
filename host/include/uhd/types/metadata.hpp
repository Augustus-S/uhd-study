//
// Copyright 2010-2012,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <uhd/config.hpp>
#include <uhd/types/time_spec.hpp>
#include <stdint.h>
#include <string>

namespace uhd {

/*!
 * RX metadata structure for describing sent IF data.
 * Includes time specification, fragmentation flags, burst flags, and error codes.
 * The receive routines will convert IF data headers into metadata.
 */
/*!
 * 用于描述发送中频（IF）数据的接收元数据结构。
 * 包含时间信息、分段标志、突发标志以及错误代码。
 * 接收函数会将中频数据头转换为元数据。
 */
struct UHD_API rx_metadata_t
{
    //! Default constructor.
    rx_metadata_t()
    {
        reset();
    }

    //! Reset values.
    void reset()
    {
        has_time_spec       = false;
        time_spec           = time_spec_t(0.0);
        more_fragments      = false;
        fragment_offset     = 0;
        start_of_burst      = false;
        end_of_burst        = false;
        eov_positions       = nullptr;
        eov_positions_size  = 0;
        eov_positions_count = 0;
        error_code          = ERROR_CODE_NONE;
        out_of_sequence     = false;
    }

    //! Has time specification?
    //! 是否具有时间规范？
    bool has_time_spec;

    //! Time of the first sample.
    //! 第一个采样值的时间
    time_spec_t time_spec;

    /*!
     * Fragmentation flag:
     * Similar to IPv4 fragmentation:
     * http://en.wikipedia.org/wiki/IPv4#Fragmentation_and_reassembly More fragments is
     * true when the input buffer has insufficient size to fit an entire received packet.
     * More fragments will be false for the last fragment.
     */
    /*!
     * 分片标志：
     * 类似 IPv4 分片：http://en.wikipedia.org/wiki/IPv4#Fragmentation_and_reassembly
     * 当输入缓冲区不足以放下一整个接收到的数据包时，此标志为 true。
     * 对于最后一片，该标志为 false。
     */
    bool more_fragments;

    /*!
     * Fragmentation offset:
     * The fragment offset is the sample number at the start of the receive buffer.
     * For non-fragmented receives, the fragment offset should always be zero.
     */
    /*!
     * 分片偏移：
     * 表示在接收缓冲区中起始位置对应的采样序号。
     * 对于未分片接收，此值应始终为零。
     */
    size_t fragment_offset;

    //! Start of burst will be true for the first packet in the chain.
    //! Start of burst 在链路中的第一包为 true.
    bool start_of_burst;

    //! End of burst will be true for the last packet in the chain.
    //! End of burst 在链路中的最后一包为 true.
    bool end_of_burst;

    /*!
     * If this pointer is not null, it specifies the address of an array of
     * `size_t`s into which the sample offset relative to the beginning of
     * a call to `recv()` of each vector (as denoted by packets with the `eov`
     * header byte set) will be written.
     *
     * The caller is responsible for allocating and deallocating the storage
     * for the array and for indicating the maximum number of elements in
     * the array via the `eov_positions_size` value below.
     *
     * Upon return from `recv()`, `eov_positions_count` will be updated to
     * indicate the number of valid entries written to the
     * `end_of_vector_positions` array. It shall never exceed the value of
     * `eov_positions_size`. However, if in the process of `recv()`, storage
     * for new positions is exhausted, then `recv()` shall return.
     */
    /*!
     * \todo:理解这段注释
     * 如果此指针非空，则指向一个 size_t 数组，
     * recv() 调用时，会将每个向量（由带有 eov 头字节的数据包表示）
     * 相对于本次 recv() 调用起始的采样偏移写入该数组。
     *
     * 调用者负责为此数组分配和释放存储，
     * 并通过 eov_positions_size 表示数组的最大元素数。
     *
     * recv() 返回后，eov_positions_count 会更新为写入有效条目的数量，
     * 不得超过 eov_positions_size。如果在 recv() 过程中耗尽可用存储，则 recv() 应返回。
     */
    size_t* eov_positions;
    size_t eov_positions_size;

    /*!
     * Upon return from `recv()`, holds the number of end-of-vector indications
     * in the `eov_positions` array.
     */
    /*!
     * recv() 返回后，表示写入到 eov_positions 数组中的结束向量指示的数量。
     */
    size_t eov_positions_count;

    /*!
     * The error condition on a receive call.
     *
     * Note: When an overrun occurs in continuous streaming mode,
     * the device will continue to send samples to the host.
     * For other streaming modes, streaming will discontinue
     * until the user issues a new stream command.
     *
     * The metadata fields have meaning for the following error codes:
     * - none
     * - late command
     * - broken chain
     * - overflow
     */
    /*!
     * 接收调用中的错误情况。
     *
     * 注意：在连续流模式下发生 overrun 时，设备会继续向主机发送样本。
     * 对于其他流模式，流传输将中止，直到用户发出新的流命令。
     *
     * 下列元数据字段在以下错误码情况下有意义：
     * - none
     * - late command
     * - broken chain
     * - overflow
     */
    enum error_code_t {
        //! No error associated with this metadata.
        //! 元数据无错误
        ERROR_CODE_NONE = 0x0,

        //! No packet received, implementation timed-out.
        //! 未收到数据包，超时
        ERROR_CODE_TIMEOUT = 0x1,

        //! A stream command was issued in the past.
        //! 流命令发出时间过晚
        ERROR_CODE_LATE_COMMAND = 0x2,

        //! Expected another stream command.
        //! 预期有下一个流命令，但未收到
        ERROR_CODE_BROKEN_CHAIN = 0x4,

        /*!
         * An internal receive buffer has filled or a sequence error has been detected.
         * So, why is this overloaded?  Simple: legacy support. It would have been much
         * cleaner to create a separate error code for a sequence error, but that would
         * have broken legacy applications.  So, the out_of_sequence flag was added to
         * differentiate between the two error cases.  In either case, data is missing
         * between this time_spec and the and the time_spec of the next successful
         * receive.
         */
        /*!
         * 内部接收缓冲区已满或检测到序列错误。
         * 之所以与 overflow 复用，是为了兼容旧应用。若要区分序列错误，
         * 使用 out_of_sequence 标志。无论哪种情况，
         * 在此 time_spec 与下一次成功接收的 time_spec 之间缺失数据。
         */
        ERROR_CODE_OVERFLOW = 0x8,

        //! Multi-channel alignment failed.
        //! 多通道对齐失败
        ERROR_CODE_ALIGNMENT = 0xc,

        //! The packet could not be parsed.
        //! 数据包无法解析
        ERROR_CODE_BAD_PACKET = 0xf
    } error_code;

    //! Out of sequence.  The transport has either dropped a packet or received data out
    //! of order.
    //! Out of sequence = 是否发生乱序。表示传输过程中丢包或乱序接收。
    bool out_of_sequence;

    /*!
     * Convert a rx_metadata_t into a pretty print string.
     *
     * \param compact Set to false for a more verbose output.
     * \return a printable string representing the metadata.
     */
    /*!
     * 将 rx_metadata_t 转换为可打印的字符串。
     *
     * \param compact 若为 false，则输出更详细。
     * \return 表示元数据的可打印字符串。
     * 备注：根据输入的错误代码输出错误内容，详见 metadata.cpp。
     */
    std::string to_pp_string(bool compact = true) const;

    /*!
     * Similar to C's strerror() function, creates a std::string describing the error
     * code. \return a printable string representing the error.
     */
    /*!
     * 类似 C 的 strerror()，生成描述错误码的 std::string。
     * \return 表示错误的可打印字符串。
     */
    std::string strerror(void) const;
};

/*!
 * TX metadata structure for describing received IF data.
 * Includes time specification, and start and stop burst flags.
 * The send routines will convert the metadata to IF data headers.
 */
/*!
* TX 元数据结构，用于描述要发送的中频（IF）数据。
* 包含时间规范，以及突发起始和结束标志。
* 发送例程会将此元数据转换为 IF 数据头。
*/
struct UHD_API tx_metadata_t    // 同以上部分类似
{
    /*!
     * Has time specification?
     * - Set false to send immediately.
     * - Set true to send at the time specified by time spec.
     */
    /*!
     * 是否具有时间规范？
     * - 设为 false 表示立即发送。
     * - 设为 true 表示在 time_spec 指定的时间发送。
     */
    bool has_time_spec;

    //! When to send the first sample.
    //! 发送第一采样的时间
    time_spec_t time_spec;

    //! Set start of burst to true for the first packet in the chain.
    bool start_of_burst;

    //! Set end of burst to true for the last packet in the chain.
    bool end_of_burst;

    /*!
     * If this pointer is not null, it specifies the address of an array of
     * `size_t`s specifying the sample offset relative to the beginning of
     * the call to `send()` where an EOV should be signalled.
     *
     * The caller is responsible for allocating and deallocating the storage
     * for the array and for indicating the maximum number of elements in
     * the array via the `eov_positions_size` value below.
     */
    size_t* eov_positions     = nullptr;
    size_t eov_positions_size = 0;

    /*!
     * The default constructor:
     * Sets the fields to default values (flags set to false).
     */
    tx_metadata_t(void);
};

/*!
 * Async metadata structure for describing transmit related events.
 * 异步元数据结构，用于描述与传输相关的异步事件。
 */
struct UHD_API async_metadata_t
{
    //! The channel number in a mimo configuration
    //! 在 MIMO 配置中的通道编号
    size_t channel;

    //! Has time specification?
    //! 是否具有时间规范？
    bool has_time_spec;

    //! When the async event occurred.
    //! 异步事件发生的时间
    time_spec_t time_spec;

    /*!
     * The type of event for a receive async message call.
     * 接收异步消息调用时事件类型。
     */
    enum event_code_t {
        //! A burst was successfully transmitted.
        //! 首发已成功发送确认
        EVENT_CODE_BURST_ACK = 0x1,

        //! An internal send buffer has emptied.
        //! 内部发送缓冲区已清空（欠载）
        EVENT_CODE_UNDERFLOW = 0x2,

        //! Packet loss between host and device.
        //! 主机与设备之间丢包
        EVENT_CODE_SEQ_ERROR = 0x4,

        //! Packet had time that was late.
        //! 数据包时间过晚
        EVENT_CODE_TIME_ERROR = 0x8,

        //! Underflow occurred inside a packet.
        //! 包内部发生欠载
        EVENT_CODE_UNDERFLOW_IN_PACKET = 0x10,

        //! Packet loss within a burst.
        //! burst中发生丢包
        EVENT_CODE_SEQ_ERROR_IN_BURST = 0x20,

        //! Some kind of custom user payload
        //! \todo:我不理解
        EVENT_CODE_USER_PAYLOAD = 0x40
    } event_code;

    /*!
     * A special payload populated by custom FPGA fabric.
     * 由自定义 FPGA 逻辑填充的特殊负载。(?)
     * \todo:我不理解
     */
    uint32_t user_payload[4];
};

} // namespace uhd
