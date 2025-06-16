//
// Copyright 2011-2013 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <uhd/config.hpp>
#include <uhd/rfnoc/actions.hpp>
#include <uhd/types/device_addr.hpp>
#include <uhd/types/ref_vector.hpp>
#include <uhd/types/stream_cmd.hpp>
#include <uhd/utils/noncopyable.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace uhd {
struct async_metadata_t;
struct rx_metadata_t;
struct tx_metadata_t;

/*!
 * A struct of parameters to construct a streamer.
 *
 * Here is an example of how a stream args object could be used in conjunction
 * with uhd::device::get_rx_stream():
 *
 * \code{.cpp}
 * // 1. Create the stream args object and initialize the data formats to fc32 and sc16:
 * uhd::stream_args_t stream_args("fc32", "sc16");
 * // 2. Set the channel list, we want 3 streamers coming from channels
 * //    0, 1 and 2, in that order:
 * stream_args.channels = {0, 1, 2};
 * // 3. Set optional args:
 * stream_args.args["spp"] = "200"; // 200 samples per packet
 * // Now use these args to create an rx streamer:
 * // (We assume that usrp is a valid uhd::usrp::multi_usrp)
 * uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
 * // Now, any calls to rx_stream must provide a vector of 3 buffers,
 * // one per channel.
 * \endcode
 *
 * \b Note: Not all combinations of CPU and OTW format have conversion support.
 * You may however write and register your own conversion routines.
 */
/*!
 * 构造 streamer 的参数结构体。
 *
 * 下面是 stream args 对象与 uhd::device::get_rx_stream() 配合使用的示例：
 *
 * \code{.cpp}
 * // 1. 创建 stream args 对象，并初始化主机数据格式为 fc32，线控格式为 sc16:
 * uhd::stream_args_t stream_args("fc32", "sc16");
 * // 2. 设置通道列表，我们希望从通道 0、1、2 各创建一个流，顺序如示例所示：
 * stream_args.channels = {0, 1, 2};
 * // 3. 设置可选参数：
 * stream_args.args["spp"] = "200"; // 每包 200 个样本
 * // 现在使用这些参数创建一个 rx streamer：
 * // （假设 usrp 是一个有效的 uhd::usrp::multi_usrp 对象）
 * uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
 * // 之后，对 rx_stream 的任何 recv 调用都必须提供一个包含 3 个缓冲区的向量，
 * // 每个通道对应一个缓冲区。
 * \endcode
 *
 * \b 注意：并非所有 CPU 格式和线控格式的组合都有现成转换支持。但可以自行编写并注册转换例程。
 */
struct UHD_API stream_args_t
{
    //! Convenience constructor for streamer args
    stream_args_t(const std::string& cpu = "", const std::string& otw = "")
    {
        cpu_format = cpu;
        otw_format = otw;
    }

    /*!
     * The CPU format is a string that describes the format of host memory.
     * Conversions for the following CPU formats have been implemented:
     *  - fc64 - complex<double>
     *  - fc32 - complex<float>
     *  - sc16 - complex<int16_t>
     *  - sc8 - complex<int8_t>
     *
     * The following are not implemented, but are listed to demonstrate naming convention:
     *  - f32 - float
     *  - f64 - double
     *  - s16 - int16_t
     *  - s8 - int8_t
     *
     * The CPU format can be chosen depending on what the application requires.
     */
    std::string cpu_format;

    /*!
     * The OTW format is a string that describes the format over-the-wire.
     * The following over-the-wire formats have been implemented:
     *  - sc16 - Q16 I16
     *  - sc8 - Q8_1 I8_1 Q8_0 I8_0
     *  - sc12 (Only some devices)
     *
     * The following are not implemented, but are listed to demonstrate naming convention:
     *  - s16 - R16_1 R16_0
     *  - s8 - R8_3 R8_2 R8_1 R8_0
     *
     * Setting the OTW ("over-the-wire") format is, in theory, transparent to the
     * application, but changing this can have some side effects. Using less bits for
     * example (e.g. when going from `otw_format` `sc16` to `sc8`) will reduce the dynamic
     * range, and increases quantization noise. On the other hand, it reduces the load on
     * the data link and thus allows more bandwidth (a USRP N210 can work with 25 MHz
     * bandwidth for 16-Bit complex samples, and 50 MHz for 8-Bit complex samples).
     */
    /*!
     * 线控格式（OTW，over-the-wire）是描述在传输链路上数据格式的字符串。
     * 已实现的线控格式包括：
     *  - sc16 - Q16 I16
     *  - sc8  - Q8_1 I8_1 Q8_0 I8_0
     *  - sc12 （仅部分设备支持）
     *
     * 下列格式暂未实现，仅示意命名规则：
     *  - s16 - R16_1 R16_0
     *  - s8  - R8_3 R8_2 R8_1 R8_0
     *
     * 从理论上讲，更改线控格式对应用应透明，但会有副作用：
     * 使用更少位宽（例如从 sc16 降到 sc8）会降低动态范围、增加量化噪声，
     * 但可降低链路负载，从而允许更大带宽（例如 USRP N210 对于 16 位复数采样可工作于 25 MHz 带宽，
     * 对于 8 位复数采样可工作于 50 MHz 带宽）。
     */
    std::string otw_format;

    /*!
     * The args parameter is used to pass arbitrary key/value pairs.
     * Possible keys used by args (depends on implementation):
     *
     * - fullscale: specifies the full-scale amplitude when using floats.
     * By default, the fullscale amplitude under floating point is 1.0.
     * Set the "fullscale" to scale the samples in the host to the
     * expected input range and/or output range of your application.
     *
     * - peak: specifies a fractional sample level to calculate scaling with the sc8 wire
     * format. When using sc8 samples over the wire, the device must scale samples (both
     * on the host and in the device) to satisfy the dynamic range needs. The peak value
     * specifies a fraction of the maximum sample level (1.0 = 100%). Set peak to
     * max_sample_level/full_scale_level to ensure optimum dynamic range.
     *
     * - underflow_policy: how the TX DSP should recover from underflow.
     * Possible options are "next_burst" or "next_packet".
     * In the "next_burst" mode, the DSP drops incoming packets until a new burst is
     * started. In the "next_packet" mode, the DSP starts transmitting again at the next
     * packet.
     *
     * - spp: (samples per packet) controls the size of RX packets.
     * When not specified, the packets are always maximum frame size.
     * Users should specify this option to request smaller than default
     * packets, probably with the intention of reducing packet latency.
     *
     * - noclear: Used by tx_dsp_core_200 and rx_dsp_core_200
     *
     * The following are not implemented, but are listed for conceptual purposes:
     * - function: magnitude or phase/magnitude
     * - units: numeric units like counts or dBm
     *
     * Other options are device-specific:
     * - port, addr: Alternative receiver streamer destination.
     */
    /*!
     * args 参数用于传递任意键值对。
     * 常见可能键（依赖设备/实现）：
     *
     * - fullscale: 当使用浮点时，指定满幅幅度。默认浮点满幅为 1.0。
     *   设置 "fullscale" 用于将主机样本缩放到应用所需的输入或输出范围。
     *
     * - peak: 指定用于 sc8 线控格式的样本峰值比例。在使用 sc8 线控时，设备和主机都需对样本进行缩放以满足动态范围需求。
     *   peak 值表示最大样本电平的比例（1.0 = 100%）。将 peak 设为 max_sample_level/full_scale_level，可确保最佳动态范围。
     *
     * - underflow_policy: 指定 TX DSP 在欠载时如何恢复。选项有 "next_burst" 或 "next_packet"。
     *   在 "next_burst" 模式下，DSP 会丢弃输入数据包直到新突发开始；在 "next_packet" 模式下，DSP 会在下一个数据包时重新开始发送。
     *
     * - spp: (samples per packet) 控制 RX 数据包的大小。若未指定，则使用最大帧大小。
     *   用户可指定小于默认的包大小，以降低包延迟。
     *
     * - noclear: 由 tx_dsp_core_200 和 rx_dsp_core_200 使用。
     *
     * 下列选项暂未实现，仅示意概念：
     * - function: magnitude 或 phase/magnitude
     * - units: 数值单位，例如 counts 或 dBm
     *
     * 其他选项与设备相关：
     * - port, addr: 可选的接收 streamer 目标地址/端口。
     */
    device_addr_t args;

    /*! List of channel numbers (only used by non-RFNoC devices)
     *
     * Note: For RFNoC devices, this value is not used. To create a streamer
     * with multiple channels, the uhd::rfnoc::rfnoc_graph::create_tx_streamer()
     * and uhd::rfnoc::rfnoc_graph::create_rx_streamer() API calls have a
     * \p num_ports argument.
     *
     * For non-RFNoC devices (i.e., USRP1, B100, B200, N200), this argument
     * defines how streamer channels map to the front-end selection (see also
     * \ref config_subdev).
     *
     * A very simple example is a B210 with a subdev spec of `A:A A:B`. This
     * means the device has two channels available.
     *
     * Setting `stream_args.channels = {0, 1}` therefore configures MIMO
     * streaming from both channels. By switching the channel indexes,
     * `stream_args.channels = {1, 0}`, the channels are switched and the first
     * channel of the USRP is mapped to the second channel in the application.
     *
     * If only a single channel is used for streaming, e.g.,
     * `stream_args.channels = {1}` would only select a single channel (in this
     * case, the second one). When streaming a single channel from the B-side
     * radio of a USRP, this is a more versatile solution than setting the
     * subdev spec globally to "A:B".
     *
     * Leave this blank to default to channel 0 (single-channel application).
     */
    /*! 通道编号列表（仅在非 RFNoC 设备上使用）
     *
     * 注意：对于 RFNoC 设备，此值不生效。若需多通道 streamer，应使用
     * uhd::rfnoc::rfnoc_graph::create_tx_streamer() / create_rx_streamer() 中的 num_ports 参数。
     *
     * 对于非 RFNoC 设备（如 USRP1、B100、B200、N200），此参数定义 streamer 通道如何映射到前端选择（见 config_subdev）。
     *
     * 例如 B210 的子设备说明为 `A:A A:B`，表示有两个可用通道。
     * 设置 `stream_args.channels = {0, 1}` 则配置从两个通道的 MIMO 流；若设为 `{1, 0}`，则交换应用层通道顺序。
     * 若仅用单通道，如 `stream_args.channels = {1}`，则仅选择第二个通道。这比全局设置子设备 spec 更灵活。
     * 留空则默认只使用通道 0（单通道应用）。
     */
    std::vector<size_t> channels;
};

/*!
 * The RX streamer is the host interface to receiving samples.
 * It represents the layer between the samples on the host
 * and samples inside the device's receive DSP processing.
 */
/*!
 * RX streamer 是接收样本的主机接口。
 * 它在主机样本和设备接收 DSP 处理之间起桥梁作用。
 */
class UHD_API rx_streamer : uhd::noncopyable
{
public:
    typedef std::shared_ptr<rx_streamer> sptr;

    virtual ~rx_streamer(void);

    //! Get the number of channels associated with this streamer
    //! 获取此 streamer 关联的通道数
    virtual size_t get_num_channels(void) const = 0;

    //! Get the max number of samples per buffer per packet
    //! 获取每个数据包每缓冲区的最大样本数
    virtual size_t get_max_num_samps(void) const = 0;

    //! Typedef for a pointer to a single, or a collection of recv buffers
    //! 用于指向单个或多个接收缓冲区的类型别名
    typedef ref_vector<void*> buffs_type;

    /*!
     * Receive buffers containing samples described by the metadata.
     *
     * Receive handles fragmentation as follows:
     * If the buffer has insufficient space to hold all samples
     * that were received in a single packet over-the-wire,
     * then the buffer will be completely filled and the implementation
     * will hold a pointer into the remaining portion of the packet.
     * Subsequent calls will load from the remainder of the packet,
     * and will flag the metadata to show that this is a fragment.
     * The next call to receive, after the remainder becomes exhausted,
     * will perform an over-the-wire receive as usual.
     * See the rx metadata fragment flags and offset fields for details.
     *
     * This is a blocking call and will not return until the number
     * of samples returned have been written into each buffer.
     * Under a timeout condition, the number of samples returned
     * may be less than the number of samples specified.
     *
     * The one_packet option allows the user to guarantee that
     * the call will return after a single packet has been processed.
     * This may be useful to maintain packet boundaries in some cases.
     *
     * Note on threading: recv() is *not* thread-safe, to avoid locking
     * overhead. The application calling recv() is responsible for making
     * sure that not more than one thread can call recv() on the same streamer
     * at the same time. If there are multiple streamers, receiving from
     * different sources, then those may be called from different threads
     * simultaneously.
     *
     * \section stream_rx_error_handling Error Handling
     *
     * \p metadata is a value that is set inside this function (effectively, a
     * return value), and should be checked
     * for potential error codes (see rx_metadata_t::error_code_t).
     *
     * The most common error code when something goes wrong is an overrun (also
     * referred to as overflow: error_code_t::ERROR_CODE_OVERFLOW). This error
     * code means that the device produced data faster than the application
     * could read, and various buffers filled up leaving no more space for the
     * device to write data to. Note that an overrun on the device will not
     * immediately show up when calling recv(). Depending on the device
     * implementation, there may be many more valid samples available before the
     * device had to stop writing samples to the FIFO. Only when all valid
     * samples are returned to the call site will the error code be set to
     * "overrun". When this happens, all valid samples have been returned to
     * application where recv() was called.
     * If the device is streaming continuously, it will reset itself when the
     * FIFO is cleared, and recv() can be called again to retrieve new, valid data.
     *
     * \section stream_rx_timeouts Timeouts
     *
     * The recv() call has a timeout parameter. This parameter is used to limit
     * the time the call will block. If no data is available within the timeout
     * period, the call will return with an error code of
     * error_code_t::ERROR_CODE_TIMEOUT. This is not necessarily an error
     * condition, but can occur naturally, for example if the upstream source
     * produces data in a bursty fashion.
     *
     * An important fact about the timeout parameter is that it is not a total
     * timeout value, but is applied to every single call within recv() that
     * uses a timeout. This means that the total time recv() can block can be
     * significantly larger than the timeout parameter passed to recv().
     *
     * When the timeout is set to zero, recv() will attempt to return as fast
     * as possible, to minimize latency. This is useful when the application
     * polls for data in a busy loop. However, note that in this case, it is
     * possible that the call will return with an error code of timeout, even
     * though a different error condition occurred, but has not been fully
     * processed. It is therefore not sufficient to call recv() with a timeout
     * of zero to check for any outstanding error conditions.
     *
     * \section stream_rx_nsamps_zero Calling recv() with nsamps_per_buff = 0
     *
     * It is not forbidden to call recv() with nsamps_per_buff = 0, and this
     * can be useful to retrieve metadata only. In this case, the call will
     * still apply the timeout value internally (which means the recv() call
     * will wait at least for the timeout value for any incoming data, even
     * though it won't be processed). However, the call will never return an
     * error code of ERROR_CODE_TIMEOUT, as no data is expected to be processed.
     *
     * The following code snippet demonstrates how to call recv() with a zero
     * number of samples to retrieve metadata only:
     * \code{.cpp}
     * int num_samps_rcvd = rx_streamer->recv(
     *     buffs, num_samps_expected, metadata, timeout);
     * if (num_samps_rcvd < num_samps_expected &&
     *     metadata.error_code != error_code_t::ERROR_CODE_NONE &&
     *     metadata.error_code != error_code_t::ERROR_CODE_TIMEOUT) {
     *     // If we reach this point, we know that an error occurred, but we
     *     // don't know what it was.
     *     rx_streamer->recv(buffs, 0, metadata, timeout);
     *     // Now meta_data.error_code will contain the actual error code,
     *     // if it was received within timeout.
     * }
     * \endcode
     *
     * \param buffs a vector of writable memory to fill with samples
     * \param nsamps_per_buff the size of each buffer in number of samples
     * \param[out] metadata data to fill describing the buffer
     * \param timeout the timeout in seconds to wait for a packet
     * \param one_packet return after the first packet is received
     * \return the number of samples received or 0 on error
     */
    /*!
     * 接收包含样本的缓冲区，并使用 metadata 描述接收情况。
     *
     * 对分片的处理如下：
     * 如果缓冲区空间不足以容纳一次线控接收到的所有样本，则该缓冲区将被填满，
     * 实现会在剩余的数据上保持指针。后续调用会继续加载剩余部分，并在 metadata 中标记为分片（fragment）。
     * 当剩余部分处理完毕后，下次调用才会进行新的线控接收。
     *
     * 此调用为阻塞调用，直到样本写入各缓冲区后才返回。在超时情况下，返回写入的样本数可能少于请求数量。
     *
     * one_packet 选项可保证调用在处理完单个数据包后立即返回。
     * 在某些情况下，为了保持包边界，此选项可能有用。
     *
     * 线程注意：recv() 不是线程安全的，为避免锁开销，调用方需确保同一 streamer 在同一时刻只有一个线程调用 recv()。
     * 若有多个 streamer，可在不同线程分别调用。
     *
     * \section stream_rx_error_handling 错误处理
     *
     * metadata 是此函数内部填充的返回信息，调用方应检查其中的错误码（见 rx_metadata_t::error_code_t）。
     * 当出现问题时，最常见的错误码是 overrun（也称 overflow: error_code_t::ERROR_CODE_OVERFLOW）。
     * 该错误表示设备产生数据速度超过应用读取速度，缓冲区填满，设备无法继续写入。
     * 注意设备上的 overrun 不会立即反映在 recv() 返回上；具体行为依设备实现，可能在多次调用后才出现错误码。
     * 当出现 overrun 错误码时，所有有效样本已返回给调用方，应用可处理完后再次调用 recv() 继续接收。
     *
     * \section stream_rx_timeouts 超时
     *
     * recv() 有 timeout 参数，用于限制等待时间。如果在该超时内无数据可用，调用返回并在 metadata 中设置 error_code_t::ERROR_CODE_TIMEOUT。
     * 这不一定是错误，例如上游源以突发方式发送时可能自然发生。
     *
     * 重要：timeout 不是总超时，而是应用于 recv() 内部每次等待的超时。因此总阻塞时间可能远大于传入的 timeout。
     *
     * 当 timeout 设为 0 时，recv() 会尽快返回，以降低延迟；适用于在 busy loop 中轮询数据。但此时可能返回 timeout 错误码，即使实际有其他错误尚未完全处理。
     * 因此，仅用 timeout=0 并不足以检测所有未处理错误。
     *
     * \section stream_rx_nsamps_zero 使用 nsamps_per_buff = 0 调用 recv()
     *
     * 允许使用 nsamps_per_buff=0 调用 recv() 以仅获取 metadata。在此情况下，调用仍然会应用 timeout（意味着会等待至少 timeout 时间以接收可能的元数据），
     * 但不会返回 ERROR_CODE_TIMEOUT，因为不期望处理任何数据。但若遇其他错误，可用此方式查询 metadata。
     *
     * 示例：
     * \code{.cpp}
     * int num_samps_rcvd = rx_streamer->recv(
     *     buffs, num_samps_expected, metadata, timeout);
     * if (num_samps_rcvd < num_samps_expected &&
     *     metadata.error_code != error_code_t::ERROR_CODE_NONE &&
     *     metadata.error_code != error_code_t::ERROR_CODE_TIMEOUT) {
     *     // 若到此，说明发生了错误，但尚不知错误类型。
     *     rx_streamer->recv(buffs, 0, metadata, timeout);
     *     // 现在 metadata.error_code 会在 timeout 内更新为实际错误码。
     * }
     * \endcode
     *
     * \param buffs      一个可写缓冲区向量，用于填充样本
     * \param nsamps_per_buff 每个缓冲区请求写入的样本数
     * \param[out] metadata 用于填充并描述本次接收情况
     * \param timeout    等待数据包的超时时间（秒）
     * \param one_packet 若为 true，则在接收到并处理完首个数据包后立即返回
     * \return 实际接收到的样本数量，或在错误时返回 0
     */
    virtual size_t recv(const buffs_type& buffs,
        const size_t nsamps_per_buff,
        rx_metadata_t& metadata,
        const double timeout  = 0.1,
        const bool one_packet = false) = 0;

    /*!
     * Issue a stream command to the usrp device.
     * This tells the usrp to send samples into the host.
     * See the documentation for stream_cmd_t for more info.
     *
     * With multiple devices, the first stream command in a chain of commands
     * should have a time spec in the near future and stream_now = false;
     * to ensure that the packets can be aligned by their time specs.
     *
     * \param stream_cmd the stream command to issue
     */
    /*!
     * 向 USRP 设备发出流命令，指示设备开始向主机发送样本。
     * 参见 stream_cmd_t 文档了解更多信息。
     *
     * 在多设备场景中，命令链中第一条命令应包含一个未来的时间规范（time spec），
     * 并将 stream_now 设为 false；
     * 以保证各设备按时间对齐后再开始流。
     *
     * \param stream_cmd 要发送的流命令
     */
    virtual void issue_stream_cmd(const stream_cmd_t& stream_cmd) = 0;

    /*!
     * Post an action to the input edge of the Streamer.
     * \param action shared pointer to the corresponding action_info request
     * \param port the port to which to post the action
     */
    /*!
     * 在 Streamer 的输入端发布一个动作（action）。
     * \param action 关联的 action_info 请求的 shared_ptr
     * \param port   要发布到的端口号
     */
    virtual void post_input_action(
        const std::shared_ptr<uhd::rfnoc::action_info>& action, const size_t port) = 0;
};

/*!
 * The TX streamer is the host interface to transmitting samples.
 * It represents the layer between the samples on the host
 * and samples inside the device's transmit DSP processing.
 */
/*!
 * TX streamer 是发送样本到设备的主机接口。
 * 它在主机样本与设备传输 DSP 处理之间起桥梁作用。
 */
class UHD_API tx_streamer : uhd::noncopyable
{
public:
    typedef std::shared_ptr<tx_streamer> sptr;

    virtual ~tx_streamer(void);

    //! Get the number of channels associated with this streamer
    virtual size_t get_num_channels(void) const = 0;

    //! Get the max number of samples per buffer per packet
    virtual size_t get_max_num_samps(void) const = 0;

    //! Typedef for a pointer to a single, or a collection of send buffers
    typedef ref_vector<const void*> buffs_type;

    /*!
     * Send buffers containing samples described by the metadata.
     *
     * Send handles fragmentation as follows:
     * If the buffer has more items than the maximum per packet,
     * the send method will fragment the samples across several packets.
     * Send will respect the burst flags when fragmenting to ensure
     * that start of burst can only be set on the first fragment and
     * that end of burst can only be set on the final fragment.
     *
     * This is a blocking call and will not return until the number
     * of samples returned have been read out of each buffer.
     * Under a timeout condition, the number of samples returned
     * may be less than the number of samples specified.
     *
     * Note on threading: send() is *not* thread-safe, to avoid locking
     * overhead. The application calling send() is responsible for making
     * sure that not more than one thread can call send() on the same streamer
     * at the same time. If there are multiple streamers, transmitting to
     * different destinations, then those may be called from different threads
     * simultaneously.
     *
     * \param buffs a vector of read-only memory containing samples
     * \param nsamps_per_buff the number of samples to send, per buffer
     * \param metadata data describing the buffer's contents
     * \param timeout the timeout in seconds to wait on a packet
     * \return the number of samples sent
     */
    virtual size_t send(const buffs_type& buffs,
        const size_t nsamps_per_buff,
        const tx_metadata_t& metadata,
        const double timeout = 0.1) = 0;

    /*!
     * Receive an asynchronous message from this TX stream.
     * \param async_metadata the metadata to be filled in
     * \param timeout the timeout in seconds to wait for a message
     * \return true when the async_metadata is valid, false for timeout
     */
    /*!
     * 从此 TX 流接收异步消息。
     * \param async_metadata 用于填充返回的异步消息信息
     * \param timeout        等待消息的超时时间（秒）
     * \return 如果接收到有效 async_metadata，则返回 true；超时返回 false
     */
    virtual bool recv_async_msg(
        async_metadata_t& async_metadata, double timeout = 0.1) = 0;

    /*!
     * Post an action to the output edge of the Streamer.
     * \param action shared pointer to the corresponding action_info request
     * \param port the port to which to post the action
     */
    virtual void post_output_action(
        const std::shared_ptr<uhd::rfnoc::action_info>& action, const size_t port) = 0;
};

} // namespace uhd
