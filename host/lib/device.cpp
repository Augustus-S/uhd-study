//
// Copyright 2010-2011,2014-2015 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/device.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/dict.hpp>
#include <uhd/utils/algorithm.hpp>
#include <uhd/utils/log.hpp>
#include <uhd/utils/static.hpp>
#include <uhdlib/utils/prefs.hpp>
#include <boost/functional/hash.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>

using namespace uhd;

static std::mutex _device_mutex;

/***********************************************************************
 * Helper Functions
 **********************************************************************/
/*!
 * Make a device hash that maps 1 to 1 with a device address.
 * The hash will be used to identify created devices.
 * \param dev_addr the device address
 * \return the hash number
 */
static size_t hash_device_addr(const device_addr_t& dev_addr)
{
    // combine the hashes of sorted keys/value pairs
    size_t hash = 0;

    // The device addr can contain all sorts of stuff, which sometimes gets in
    // the way of hashing reliably. TODO: Make this a whitelist
    const std::vector<std::string> hash_key_blacklist = {
        "claimed", "skip_dram", "skip_ddc", "skip_duc"};

    if (dev_addr.has_key("resource")) {
        boost::hash_combine(hash, "resource");
        boost::hash_combine(hash, dev_addr["resource"]);
    } else {
        for (const std::string& key : uhd::sorted(dev_addr.keys())) {
            if (std::find(hash_key_blacklist.begin(), hash_key_blacklist.end(), key)
                == hash_key_blacklist.end()) {
                boost::hash_combine(hash, key);
                boost::hash_combine(hash, dev_addr[key]);
            }
        }
    }
    return hash;
}

/***********************************************************************
 * Registration
 **********************************************************************/
typedef std::tuple<device::find_t, device::make_t, device::device_filter_t> dev_fcn_reg_t;

// instantiate the device function registry container
// 实例化设备函数注册容器:创建一个用于保存并管理设备相关函数注册信息的对象
UHD_SINGLETON_FCN(std::vector<dev_fcn_reg_t>, get_dev_fcn_regs)

void device::register_device(
    const find_t& find, const make_t& make, const device_filter_t filter)
{
    // UHD_LOGGER_TRACE("UHD") << "registering device";
    get_dev_fcn_regs().push_back(dev_fcn_reg_t(find, make, filter));
}

device::~device(void)
{
    /* NOP */
}

/***********************************************************************
 * Discover
 **********************************************************************/
device_addrs_t device::find(const device_addr_t& hint, device_filter_t filter)
{
    std::lock_guard<std::mutex> lock(_device_mutex);

    device_addrs_t device_addrs;
    std::vector<std::future<device_addrs_t>> find_tasks;
    for (const auto& fcn : get_dev_fcn_regs()) {
        if (filter == ANY or std::get<2>(fcn) == filter) {
            find_tasks.emplace_back(std::async(
                std::launch::async, [fcn, hint]() { return std::get<0>(fcn)(hint); }));
        }
    }
    for (auto& find_task : find_tasks) {
        try {
            device_addrs_t discovered_addrs = find_task.get();
            device_addrs.insert(
                device_addrs.begin(), discovered_addrs.begin(), discovered_addrs.end());
        } catch (const std::exception& e) {
            UHD_LOGGER_ERROR("UHD") << "Device discovery error: " << e.what();
        }
    }

    // find might return duplicate entries if a device received a broadcast multiple
    // times. These entries needs to be removed from the result.
    std::set<size_t> device_hashes;
    device_addrs.erase(std::remove_if(device_addrs.begin(),
                           device_addrs.end(),
                           [&device_hashes](const device_addr_t& other) {
                               size_t hash       = hash_device_addr(other);
                               const bool result = device_hashes.count(hash);
                               device_hashes.insert(hash);
                               return result;
                           }),
        device_addrs.end());

    return device_addrs;
}

/***********************************************************************
 * Make
 **********************************************************************/
device::sptr device::make(const device_addr_t& hint, device_filter_t filter, size_t which)
{
    std::lock_guard<std::mutex> lock(_device_mutex);    // 离开作用域自动释放锁
    // 当前作用域下所有的变量、函数均被加锁，其他函数调用需要等待。

    typedef std::tuple<device_addr_t, make_t> dev_addr_make_t;
    std::vector<dev_addr_make_t> dev_addr_makers;

    // 发现所有符合条件的设备并收集它们的地址信息和创建函数
    /*
     * 设备发现函数：std::get<0>(fcn) - 接受hint参数，返回设备地址列表
     * 设备创建函数：std::get<1>(fcn) - 实际创建设备的工厂函数
     * 设备过滤器类型：std::get<2>(fcn) - 标识设备类型的枚举值
     */
    for (const dev_fcn_reg_t& fcn : get_dev_fcn_regs()) {   // for条件为遍历所有设备
        try {
            /*
             * filter == ANY：当调用者不关心设备类型时（filter参数为ANY），处理所有设备
             * std::get<2>(fcn) == filter：当调用者指定特定设备类型时，只处理匹配类型的设备
             */
            if (filter == ANY or std::get<2>(fcn) == filter) {                 // 见上文
                for (device_addr_t dev_addr : std::get<0>(fcn)(hint)) {        // 见上文
                    // append the discovered address and its factory function
                    dev_addr_makers.push_back(
                        dev_addr_make_t(dev_addr, std::get<1>(fcn)));   // 见上文
                }
            }
        } catch (const std::exception& e) {
            UHD_LOGGER_ERROR("UHD") << "Device discovery error: " << e.what();
        }
    }

    // check that we found any devices
    if (dev_addr_makers.empty()) {
        throw uhd::key_error(
            std::string("No devices found for ----->\n") + hint.to_pp_string());
    }

    // check that the which index is valid
    if (dev_addr_makers.size() <= which) {
        throw uhd::index_error("No device at index " + std::to_string(which)
                               + " for ----->\n" + hint.to_pp_string());
    }

    // create a unique hash(唯一哈希值) for the device address
    device_addr_t dev_addr;
    make_t maker;
    std::tie(dev_addr, maker) = dev_addr_makers.at(which);
    size_t dev_hash           = hash_device_addr(dev_addr);
    UHD_LOGGER_TRACE("UHD") << "Device hash: " << dev_hash;

    // copy keys that were in hint but not in dev_addr
    // this way, we can pass additional transport arguments
    // 复制那些存在于 hint 中但不在 dev_addr 的键值对
    // 这样可以传递额外的传输层参数（transport arguments）
    for (const std::string& key : hint.keys()) {
        if (not dev_addr.has_key(key))
            dev_addr[key] = hint[key];
    }

    // map device address hash to created devices
    static uhd::dict<size_t, std::weak_ptr<device>> hash_to_device;

    // try to find an existing device
    if (hash_to_device.has_key(dev_hash)) {
        if (device::sptr p = hash_to_device[dev_hash].lock()) {
            return p;
        }
    }

    // Add keys from the config files (note: the user-defined keys will
    // always be applied, see also get_usrp_args()
    // Then, create and register a new device.
    device::sptr dev         = maker(prefs::get_usrp_args(dev_addr));
    hash_to_device[dev_hash] = dev;
    return dev;
}

uhd::property_tree::sptr device::get_tree(void) const
{
    return _tree;
}

device::device_filter_t device::get_device_type() const
{
    return _type;
}
