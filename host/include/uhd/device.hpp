//
// Copyright 2010-2011,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <uhd/config.hpp>
#include <uhd/property_tree.hpp>
#include <uhd/stream.hpp>
#include <uhd/types/device_addr.hpp>
#include <uhd/utils/noncopyable.hpp>
#include <cstddef>
#include <functional>
#include <memory>

namespace uhd {
struct async_metadata_t;

/*!
 * The device interface represents the hardware.
 * The API allows for discovery, configuration, and streaming.
 */
class UHD_API device : uhd::noncopyable
{
public:
    typedef std::shared_ptr<device> sptr;   // 是 make_t 的返回类型
    typedef std::function<device_addrs_t(const device_addr_t&)> find_t;

    /*
     * make_t 是一个“接受 const device_addr_t& 参数，
     * 返回 sptr（即 std::shared_ptr<device>）”的可调用对象类型。
     */
    typedef std::function<sptr(const device_addr_t&)> make_t;   // std::function<返回类型(参数列表)> 名字;

    //! Device type, used as a filter in make
    enum device_filter_t { ANY, USRP, CLOCK };
    virtual ~device(void) = 0;

    /*!
     * Register a device into the discovery and factory system.
     *
     * \param find a function that discovers devices
     * \param make a factory function that makes a device
     * \param filter include only USRP devices, clock devices, or both
     */
    static void register_device(
        const find_t& find, const make_t& make, const device_filter_t filter);

    /*!
     * \brief Find devices attached to the host.
     *
     * The hint device address should be used to narrow down the search
     * to particular transport types and/or transport arguments.
     *
     * \param hint a partially (or fully) filled in device address
     * \param filter an optional filter to exclude USRP or clock devices
     * \return a vector of device addresses for all devices on the system
     */
    static device_addrs_t find(const device_addr_t& hint, device_filter_t filter = ANY);

    /*!
     * \brief Create a new device from the device address hint.
     *
     * The method will go through the registered device types and pick one of
     * the discovered devices.
     *
     * By default, the first result will be used to create a new device.
     * Use the which parameter as an index into the list of results.
     *
     * \param hint a partially (or fully) filled in device address
     * \param filter an optional filter to exclude USRP or clock devices
     * \param which which address to use when multiple are found
     * \return a shared pointer to a new device instance
     */
    static sptr make(
        const device_addr_t& hint, device_filter_t filter = ANY, size_t which = 0);

    /*! \brief Make a new receive streamer from the streamer arguments
     *
     * Note: For RFNoC devices, there can always be only one streamer per channel. When
     * calling get_rx_stream() a second time, the first streamer connected to this channel
     * must be destroyed beforehand. Multiple streamers for different channels are
     * allowed.
     * For non-RFNoC devices, you can only have one RX streamer at a time. Be careful to
     * destroy the old one if you want to create a new one.
     */
    virtual rx_streamer::sptr get_rx_stream(const stream_args_t& args) = 0;

    /*! \brief Make a new transmit streamer from the streamer arguments
     *
     * Note: For RFNoC devices, there can always be only one streamer per channel. When
     * calling get_tx_stream() a second time, the first streamer connected to this channel
     * must be destroyed beforehand. Multiple streamers for different channels are
     * allowed.
     * For non-RFNoC devices, you can only have one TX streamer at a time. Be careful to
     * destroy the old one if you want to create a new one.
     */
    virtual tx_streamer::sptr get_tx_stream(const stream_args_t& args) = 0;

    /*! DEPRECATED: Receive asynchronous message from the device
     *
     * Prefer calling recv_async_msg on the associated TX streamer. This method
     * has the problem that it doesn't necessarily know which Tx streamer is
     * being addressed, and thus might not be delivering the expected outcome.
     *
     * \param async_metadata the metadata to be filled in
     * \param timeout the timeout in seconds to wait for a message
     * \return true when the async_metadata is valid, false for timeout
     */
    virtual bool recv_async_msg(
        async_metadata_t& async_metadata, double timeout = 0.1) = 0;

    //! Get access to the underlying property structure
    uhd::property_tree::sptr get_tree(void) const;

    //! Get device type
    device_filter_t get_device_type() const;

protected:
    uhd::property_tree::sptr _tree;
    device_filter_t _type;
};

} // namespace uhd
