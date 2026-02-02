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
#include <uhdlib/utils/serial_number.hpp>
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
 * 这段代码的作用是为 device_addr_t 类型的设备地址生成一个哈希值，用于唯一标识设备。
 */
static size_t hash_device_addr(const device_addr_t& dev_addr)
{
    // combine the hashes of sorted keys/value pairs
    // 对设备地址 dev_addr 中的键值对进行哈希组合。
    size_t hash = 0;

    // The device addr can contain all sorts of stuff, which sometimes gets in
    // the way of hashing reliably. TODO: Make this a whitelist
    // 设备地址可能包含各种内容，这些内容有时会影响可靠哈希计算。
    const std::vector<std::string> hash_key_blacklist = {   // 黑名单
        "claimed", "skip_dram", "skip_ddc", "skip_duc"};

    // 如果存在 "resource" 键，则仅使用该键值进行哈希（优先级最高）。
    if (dev_addr.has_key("resource")) {
        /*
         * 标准的 std::hash 只能处理单一类型（如 int、std::string）。
         * hash_combine()是将多个键值（key）合并为一个整体的哈希值，
         * 主要用于复合类型（如 自定义 struct）作为哈希表的键时生成良好的哈希。
         */
        boost::hash_combine(hash, "resource");
        boost::hash_combine(hash, dev_addr["resource"]);
    } else {
        // 先排序，再遍历
        for (const std::string& key : uhd::sorted(dev_addr.keys())) {   // 对 dev_addr 的所有键进行排序，保证哈希与键的顺序无关
            if (std::find(hash_key_blacklist.begin(), hash_key_blacklist.end(), key)    // 在 [begin, end] 范围内查找第一个等于 key 的元素。
                == hash_key_blacklist.end()) {
                // 找到了，就不等于end，不进入if；找完了，没找到，才会进入if
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

// Internal function that discovers devices and returns them with their make functions
static std::vector<std::tuple<device_addr_t, device::make_t>>
discover_devices_with_makers(const device_addr_t& hint, device::device_filter_t filter)
{
    std::vector<std::tuple<device_addr_t, device::make_t>> device_maker_pairs;

    // Launch async find tasks with their corresponding make functions
    std::vector<std::pair<std::future<device_addrs_t>, device::make_t>> tasks;
    for (const auto& fcn : get_dev_fcn_regs()) {
        if (filter == device::ANY or std::get<2>(fcn) == filter) {
            tasks.emplace_back(std::async(std::launch::async,
                                   [fcn, hint]() { return std::get<0>(fcn)(hint); }),
                std::get<1>(fcn));
        }
    }

    // Collect results from async tasks
    for (auto& [task, maker] : tasks) {
        try {
            device_addrs_t discovered_addrs = task.get();
            for (const auto& addr : discovered_addrs) {
                device_maker_pairs.emplace_back(addr, maker);
            }
        } catch (const std::exception& e) {
            UHD_LOGGER_ERROR("UHD") << "Device discovery error: " << e.what();
        }
    }

    return device_maker_pairs;
}

// Internal function that returns filtered device-maker pairs
static std::vector<std::tuple<device_addr_t, device::make_t>>
find_filtered_devices_with_makers(
    const device_addr_t& hint, device::device_filter_t filter)
{
    // Get devices with their makers
    auto device_maker_pairs = discover_devices_with_makers(hint, filter);

    std::vector<std::tuple<device_addr_t, device::make_t>> filtered_pairs;

    // Filter the discovered devices
    for (const auto& pair : device_maker_pairs) {
        const device_addr_t& discovered_addr = std::get<0>(pair);

        // Filter with the optional keys name/serial/type/product
        if ((not hint.has_key("name") or hint["name"] == discovered_addr["name"])
            and (not hint.has_key("serial")
                 or utils::serial_numbers_match(
                     hint["serial"], discovered_addr["serial"]))
            and (not hint.has_key("type") or hint["type"] == discovered_addr["type"]
                 or hint["type"] == "sim") // special case for simulator
            and (not hint.has_key("product") or not discovered_addr.has_key("product")
                 or hint["product"] == discovered_addr["product"])) {
            UHD_LOG_DEBUG("Device FIND",
                "Found device that matches hints: " << discovered_addr.to_string());
            filtered_pairs.push_back(pair);
        } else {
            UHD_LOG_DEBUG("Device FIND",
                "Found device, but does not match hint: " << discovered_addr.to_string());
        }
    }

    // find might return duplicate entries if a device received a broadcast multiple
    // times. These entries needs to be removed from the result.
    // find 操作可能返回重复条目（当设备多次收到广播时）
    // 需要从结果中移除这些重复项
    std::set<size_t> device_hashes;
    filtered_pairs.erase(
        std::remove_if(filtered_pairs.begin(),
            filtered_pairs.end(),
            [&device_hashes](const std::tuple<device_addr_t, device::make_t>& pair) {
                size_t hash       = hash_device_addr(std::get<0>(pair));
                const bool result = device_hashes.count(hash);
                device_hashes.insert(hash);
                return result;
            }),
        filtered_pairs.end());

    return filtered_pairs;
}

device_addrs_t device::find(const device_addr_t& hint, device_filter_t filter)
{
    std::lock_guard<std::mutex> lock(_device_mutex);

    // Get filtered device-maker pairs
    auto filtered_pairs = find_filtered_devices_with_makers(hint, filter);

    // Extract just the device addresses
    device_addrs_t device_addrs;
    std::transform(filtered_pairs.begin(),
        filtered_pairs.end(),
        std::back_inserter(device_addrs),
        [](const auto& pair) { return std::get<0>(pair); });

    return device_addrs;
}

/***********************************************************************
 * Make
 **********************************************************************/
device::sptr device::make(const device_addr_t& hint, device_filter_t filter, size_t which)
{
    std::lock_guard<std::mutex> lock(_device_mutex);    // 离开作用域自动释放锁
    // 当前作用域下所有的变量、函数均被加锁，其他函数调用需要等待。

    // Use the internal function that returns filtered device-maker pairs
    auto filtered_pairs = find_filtered_devices_with_makers(hint, filter);

    typedef std::tuple<device_addr_t, make_t> dev_addr_make_t;
    std::vector<dev_addr_make_t> dev_addr_makers;

    // Convert to the expected format (no additional filtering needed)
    for (const auto& pair : filtered_pairs) {
        dev_addr_makers.push_back(dev_addr_make_t(std::get<0>(pair), std::get<1>(pair)));
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
    // 从配置文件中添加键值
    // （注意：用户自定义键值将始终生效，另见 get_usrp_args()）
    // 随后创建并注册新设备
    /*
     * maker的作用是返回内部函数的指针
     * dev_addr 在未指定时为空，
     * get_usrp_args的作用是根据输入的 dev_addr(实际是user_args) 查找设备地址
     */
    device_addr_t args = prefs::get_usrp_args(dev_addr);    // add by SWH
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
