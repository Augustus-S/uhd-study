//
// Copyright 2010,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <uhd/config.hpp>
#include <uhd/types/serial.hpp>
#include <uhd/utils/noncopyable.hpp>
#include <boost/asio/buffer.hpp>
#include <cstddef>
#include <memory>
#include <string>

namespace uhd { namespace transport {

class UHD_API udp_simple : uhd::noncopyable
{
public:
    typedef std::shared_ptr<udp_simple> sptr;

    virtual ~udp_simple(void) = 0;

    //! The maximum number of bytes per udp packet.
    //! 每个 udp 包的最大字节数。
    static const size_t mtu = 1500 - 20 - 8; // default ipv4 mtu - ipv4 header - udp
                                             // header

    /*!
     * Make a new connected udp transport:
     * This transport is for sending and receiving
     * between this host and a single endpoint.
     * The primary usage for this transport will be control transactions.
     * The underlying implementation is simple and portable (not fast).
     *
     * The address will be resolved, it can be a host name or ipv4.
     * The port will be resolved, it can be a port type or number.
     *
     * \param addr a string representing the destination address
     * \param port a string representing the destination port
     */
    /*!
     * 创建一个新的已连接的 UDP 传输对象：
     * 该传输用于在本地主机和单一远程端点之间进行收发。
     * 它的主要用途是用于控制事务（control transactions）。
     * 底层实现简单且具有可移植性（但不是高性能的）。

     * 地址将会被解析，可以是主机名或 IPv4 地址。
     * 端口也将会被解析，可以是端口名称或端口号。

     * \param addr 表示目标地址的字符串
     * \param port 表示目标端口的字符串
     */
    static sptr make_connected(const std::string& addr, const std::string& port);

    /*!
     * Make a new broadcasting udp transport:
     * This transport can send udp broadcast datagrams
     * and receive datagrams from multiple sources.
     * The primary usage for this transport will be to discover devices.
     *
     * The address will be resolved, it can be a host name or ipv4.
     * The port will be resolved, it can be a port type or number.
     *
     * \param addr a string representing the destination address
     * \param port a string representing the destination port
     */
    /*!
     * 创建一个新的 UDP 广播传输对象：
     * 此传输可以发送 UDP 广播数据报，
     * 并接收来自多个来源的数据报。
     * 它的主要用途是用于设备发现。

     * 地址将会被解析，可以是主机名或 IPv4 地址。
     * 端口也将会被解析，可以是端口名称或端口号。

     * \param addr 表示目标地址的字符串
     * \param port 表示目标端口的字符串
     */
    static sptr make_broadcast(const std::string& addr, const std::string& port);

    /*!
     * Make a UART interface from a UDP transport.
     * 从 UDP 传输对象中创建一个串口接口
     * \param udp the UDP transport object
     * \return a new UART interface
     */
    static uart_iface::sptr make_uart(sptr udp);

    /*!
     * Send a single buffer.
     * Blocks until the data is sent.
     * \param buff single asio buffer
     * \return the number of bytes sent
     */
    /*!
     * 发送一个单独的缓冲区。
     * 此函数会阻塞，直到数据发送完成。
     * \param buff 单个 ASIO 缓冲区
     * \return 已发送的字节数
     */
    virtual size_t send(const boost::asio::const_buffer& buff) = 0;

    /*!
     * Receive into the provided buffer.
     * Blocks until data is received or a timeout occurs.
     * \param buff a mutable buffer to receive into
     * \param timeout the timeout in seconds
     * \return the number of bytes received or zero on timeout
     */
    /*!
     * 接收数据到提供的缓冲区中。
     * 此函数会阻塞，直到接收到数据或超时发生。
     * \param buff 可写缓冲区，用于接收数据
     * \param timeout 超时时间（单位：秒）
     * \return 接收到的字节数；若超时则返回 0
     */
    virtual size_t recv(
        const boost::asio::mutable_buffer& buff, double timeout = 0.1) = 0;

    /*!
     * Get the last IP address as seen by recv().
     * Only use this with the broadcast socket.
     */
    /*!
     * 获取上一次 recv() 接收到数据时所见的 IP 地址。
     * 仅用于广播套接字（broadcast socket）。
     */
    virtual std::string get_recv_addr(void) = 0;

    /*!
     * Get the IP address for the destination
     * 获取目标的 IP 地址
     */
    virtual std::string get_send_addr(void) = 0;
};

}} // namespace uhd::transport
