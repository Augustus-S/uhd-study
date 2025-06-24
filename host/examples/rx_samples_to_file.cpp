//
// Copyright 2010-2011,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/convert.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#ifdef __linux__
#    include <boost/filesystem.hpp>
#    include <boost/version.hpp>
#    if BOOST_VERSION >= 108800
#        define BOOST_PROCESS_VERSION 1
#        include <boost/process/v1/child.hpp>
#        include <boost/process/v1/io.hpp>
#        include <boost/process/v1/pipe.hpp>
#    else
#        include <boost/process.hpp>
#    endif
#endif
#include <chrono>
#include <complex>
#include <csignal>
#include <fstream>
#include <iostream>
#include <numeric>
#include <regex>
#include <thread>

using namespace std::chrono_literals;
/**
 * 引入时间字面量
 * s	std::chrono::seconds
 * ms	std::chrono::milliseconds
 * us	std::chrono::microseconds
 * ns	std::chrono::nanoseconds
 * h	std::chrono::hours
 * min	std::chrono::minutes
 */

namespace po = boost::program_options;

std::mutex recv_mutex;

static bool stop_signal_called = false;
static bool overflow_message   = true;

void sig_int_handler(int)
{
    stop_signal_called = true;
}

#ifdef __linux__
/*
 * Very simple disk write test using dd for at most 1 second.
 * Measures an upper bound of the maximum
 * sustainable stream to disk rate. Though the rate measured
 * varies depending on the system load at the time.
 *
 * Does not take into account OS cache or disk cache capacities
 * filling up over time to avoid extra complexity.
 *
 * Returns the measured write speed in bytes per second
 */
double disk_rate_check(const size_t sample_type_size,
    const size_t channel_count,
    size_t samps_per_buff,
    const std::string& file)
{
    std::string err_msg =
        "Disk benchmark tool 'dd' did not run or returned an unexpected output format";
    boost::process::ipstream pipe_stream;
    boost::filesystem::path temp_file =
        boost::filesystem::path(file).parent_path() / boost::filesystem::unique_path();

    std::string disk_check_proc_str =
        "dd if=/dev/zero of=" + temp_file.native()
        + " bs=" + std::to_string(samps_per_buff * channel_count * sample_type_size)
        + " count=100";

    try {
        boost::process::child c(
            disk_check_proc_str, boost::process::std_err > pipe_stream);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (c.running()) {
            c.terminate();
        }
    } catch (std::system_error& err) {
        std::cerr << err_msg << std::endl;
        if (boost::filesystem::exists(temp_file)) {
            boost::filesystem::remove(temp_file);
        }
        return 0;
    }
    // sig_int_handler will absorb SIGINT by this point, but other signals may
    // leave a temporary file on program exit.
    // 此时 sig_int_handler 会捕获 SIGINT 信号，
    // 但其他信号可能导致程序退出时遗留临时文件
    boost::filesystem::remove(temp_file);

    std::string line;
    std::string dd_output;
    while (pipe_stream && std::getline(pipe_stream, line) && !line.empty()) {
        dd_output += line;
    }

    // Parse dd output this format:
    //   1+0 records in
    //   1+0 records out
    //   80000000 bytes (80 MB, 76 MiB) copied, 0.245538 s, 326 MB/s
    // and capture the measured disk write speed (e.g. 326 MB/s)
    std::smatch dd_matchs;
    std::regex dd_regex(
        R"(\d+\+\d+ records in)"
        R"(\d+\+\d+ records out)"
        R"(\d+ bytes \(\d+(?:\.\d+)? [KMGTP]?B, \d+(?:\.\d+)? [KMGTP]?iB\) copied, \d+(?:\.\d+)? s, (\d+(?:\.\d+)?) ([KMGTP]?B/s))"

    );
    std::regex_match(dd_output, dd_matchs, dd_regex);

    if ((dd_output.empty()) || (dd_matchs[0].str() != dd_output)) {
        std::cerr << err_msg << std::endl;
    } else {
        double disk_rate_sigfigs = std::stod(dd_matchs[1]);

        switch (dd_matchs[2].str().at(0)) {
            case 'K':
                return disk_rate_sigfigs * 1e3;
            case 'M':
                return disk_rate_sigfigs * 1e6;
            case 'G':
                return disk_rate_sigfigs * 1e9;
            case 'T':
                return disk_rate_sigfigs * 1e12;
            case 'P':
                return disk_rate_sigfigs * 1e15;
            case 'B':
                return disk_rate_sigfigs;
            default:
                std::cerr << err_msg << std::endl;
        }
    }
    return 0;
}
#endif


template <typename samp_type>
void recv_to_file(const uhd::usrp::multi_usrp::sptr& usrp,  // 原代码：uhd::usrp::multi_usrp::sptr usrp,
    const std::string& cpu_format,
    const std::string& wire_format,
    const std::vector<size_t>& channel_nums,
    const size_t total_num_channels,
    const std::string& file,
    size_t samps_per_buff,
    unsigned long long num_requested_samples,
    double& bw,
    double time_requested            = 0.0,
    bool stats                       = false,
    bool null                        = false,
    bool enable_size_map             = false,
    bool continue_on_bad_packet      = false,
    const std::string& thread_prefix = "")
{
    unsigned long long num_total_samps = 0;
    // create a receive streamer
    uhd::stream_args_t stream_args(cpu_format, wire_format);
    stream_args.channels             = channel_nums;
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    uhd::rx_metadata_t md;

    // Cannot use std::vector as second dimension type because recv will call
    // reinterpret_cast<char*> on each subarray, which is incompatible with
    // std::vector. Instead create new arrays and manage the memory ourselves
    // 不能使用 std::vector 作为第二维度的类型，因为 recv 函数会对每个子数组
    // 调用 reinterpret_cast<char*>，这与 std::vector 不兼容。
    // 因此我们改为创建新数组并自行管理内存。
    /*
     * UHD 的 recv() 函数会把接收到的 IQ 样本写入你提供的原始内存地址，
     * 因为 recv() 是 C 风格函数，其内部使用 reinterpret_cast<char*>(buffs[i]) 把指针强转为 char*，
     * 正确的做法是：使用 std::vector<samp_type*> 存放裸指针，例如 samp_type* buffs[]，
     * 每个 samp_type* 指向 new samp_type[] 动态数组，
     * 这是为什么要避免使用 std::vector<std::vector<T>> 的原因。
     */
    std::vector<samp_type*> buffs(rx_stream->get_num_channels());   // 如果"sc16"，则samp_type = std::complex<short>
    try {
        for (size_t ch = 0; ch < rx_stream->get_num_channels(); ch++) {
            // 分配的是裸指针，后面需要手动 delete[]。
            buffs[ch] = new samp_type[samps_per_buff];      // buffs是接收到的IQ数据，这里为每个通道分配一个长度为 samps_per_buff 的动态数组，用于接收样本。
        }
    } catch (std::bad_alloc& exc) {
        UHD_LOGGER_ERROR("UHD")
            << "Bad memory allocation. "
               "Try a smaller samples per buffer setting or free up additional memory";
        std::exit(EXIT_FAILURE);
    }

    // 创建空的保存文件
    std::vector<std::ofstream> outfiles(rx_stream->get_num_channels());
    std::string filename;
    for (size_t ch = 0; ch < rx_stream->get_num_channels(); ch++) {
        if (not null) {
            if (rx_stream->get_num_channels() == 1) { // single channel
                filename = file;
            } else { // multiple channels
                // check if file extension exists
                if (file.find('.') != std::string::npos) {
                    const std::string base_name = file.substr(0, file.find_last_of('.'));
                    const std::string extension = file.substr(file.find_last_of('.'));
                    filename = std::string(base_name)
                                   .append("_")
                                   .append("ch")
                                   .append(std::to_string(channel_nums[ch]))
                                   .append(extension);
                } else {
                    // file extension does not exist
                    filename = file + "_" + "ch" + std::to_string(channel_nums[ch]);
                }
            }
            outfiles[ch].open(filename.c_str(), std::ofstream::binary);
        }
    }

    // setup streaming
    uhd::stream_cmd_t stream_cmd((num_requested_samples == 0)                   // num_requested_samples == 0 → 表示连续流模式（接收直到停止），否则是接收固定数量样本。
                                     ? uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS      // 无限接收
                                     : uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    stream_cmd.num_samps  = size_t(num_requested_samples);      // 设置要接收的样本数（仅在 NUM_SAMPS_AND_DONE 模式下有效）
    stream_cmd.stream_now = rx_stream->get_num_channels() == 1; // 如果是单通道接收，立即开始接收
    stream_cmd.time_spec  = usrp->get_time_now() + uhd::time_spec_t(0.05);  // 给0.05秒等待时间
    rx_stream->issue_stream_cmd(stream_cmd);    // 向设备发出刚才构造的流命令，正式启动流接收过程

    typedef std::map<size_t, size_t> SizeMap;
    SizeMap mapSizes;
    const auto start_time = std::chrono::steady_clock::now();
    const auto stop_time  = start_time + (1s * time_requested);
    // Track time and samps between updating the BW summary
    auto last_update                     = start_time;
    unsigned long long last_update_samps = 0;

    // Run this loop until either time expired (if a duration was given), until
    // the requested number of samples were collected (if such a number was
    // given), or until Ctrl-C was pressed.
    while (not stop_signal_called                                                           // 没有被中断（如 Ctrl+C）
           and (num_requested_samples != num_total_samps or num_requested_samples == 0)     // 还没收完请求样本数或者是无限接收模式
           and (time_requested == 0.0 or std::chrono::steady_clock::now() <= stop_time)) {  // 或者还在运行时间范围内
        const auto now = std::chrono::steady_clock::now();

        // 启动接收，将接收到的数据保存在buffs；md为元数据
        size_t num_rx_samps =
            rx_stream->recv(buffs, samps_per_buff, md, 3.0, enable_size_map);   // 是否记录样本数分布（用于分析接收稳定性）

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cout << std::endl
                      << thread_prefix << "Timeout while streaming" << std::endl;
            break;
        }
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            const std::lock_guard<std::mutex> lock(recv_mutex);
            if (overflow_message) {
                overflow_message = false;
                std::cerr
                    << boost::format(
                           "Got an overflow indication. Please consider the following:\n"
                           "  Your write medium must sustain a rate of %0.3fMB/s.\n"
                           "  Dropped samples will not be written to the file.\n"
                           "  Please modify this example for your purposes.\n"
                           "  This message will not appear again.\n")
                           % (usrp->get_rx_rate(channel_nums[0]) * total_num_channels
                               * sizeof(samp_type) / 1e6);
            }
            continue;
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            const std::lock_guard<std::mutex> lock(recv_mutex);
            std::string error = thread_prefix + "Receiver error: " + md.strerror();
            if (continue_on_bad_packet) {
                std::cerr << error << std::endl;
                continue;
            } else
                throw std::runtime_error(error);
        }

        // 分析整个接收过程中，每次 recv() 实际返回了多少个样本，是否稳定、是否抖动等
        if (enable_size_map) {
            const std::lock_guard<std::mutex> lock(recv_mutex);
            SizeMap::iterator it = mapSizes.find(num_rx_samps);
            if (it == mapSizes.end())
                mapSizes[num_rx_samps] = 0;
            mapSizes[num_rx_samps] += 1;
        }

        num_total_samps += num_rx_samps;

        // 写入文件代码
        for (size_t ch = 0; ch < rx_stream->get_num_channels(); ch++) {
            if (outfiles[ch].is_open()) {
                outfiles[ch].write(
                    (const char*)buffs[ch], num_rx_samps * sizeof(samp_type));
            }
        }

        // 每秒计算一次实时带宽（单位是样本/秒）
        last_update_samps += num_rx_samps;
        const auto time_since_last_update = now - last_update;
        if (time_since_last_update > 1s) {
            const std::lock_guard<std::mutex> lock(recv_mutex);
            const double time_since_last_update_s =
                std::chrono::duration<double>(time_since_last_update).count();
            bw                = double(last_update_samps) / time_since_last_update_s;
            last_update_samps = 0;
            last_update       = now;
        }
    }
    const auto actual_stop_time = std::chrono::steady_clock::now();

    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_stream->issue_stream_cmd(stream_cmd);

    for (auto & outfile : outfiles) {
        if (outfile.is_open()) {
            outfile.close();
        }
    }

    for (size_t i = 0; i < rx_stream->get_num_channels(); i++) {
        delete[] buffs[i];
    }

    // 在程序结束时显示平均带宽统计
    if (stats) {
        const std::lock_guard<std::mutex> lock(recv_mutex);
        std::cout << std::endl;
        const double actual_duration_seconds =
            std::chrono::duration<float>(actual_stop_time - start_time).count();
        std::cout << boost::format("%sReceived %d samples in %f seconds") % thread_prefix
                         % num_total_samps % actual_duration_seconds
                  << std::endl;

        if (enable_size_map) {
            std::cout << std::endl;
            std::cout << "Packet size map (bytes: count)" << std::endl;
            for (auto & mapSize : mapSizes)
                std::cout << mapSize.first << ":\t" << mapSize.second << std::endl;
        }
    }
}

// 检测并等待指定的锁定传感器（如 PLL lock、LO lock 等）在一定时间内锁定成功。
typedef std::function<uhd::sensor_value_t(const std::string&)> get_sensor_fn_t;     // sensor_value_t 是 UHD（USRP）框架中描述硬件传感器值的类。

bool check_locked_sensor(std::vector<std::string> sensor_names, // 设备当前支持的所有传感器名
    const char* sensor_name,                                    // 要检查的特定传感器，比如 "lo_locked"
    get_sensor_fn_t get_sensor_fn,
    double setup_time)
{
    if (std::find(sensor_names.begin(), sensor_names.end(), sensor_name)
        == sensor_names.end())
        return false;

    const auto setup_timeout = std::chrono::steady_clock::now() + (setup_time * 1s);
    bool lock_detected       = false;

    std::cout << "Waiting for \"" << sensor_name << "\": ";
    std::cout.flush();

    while (true) {
        if (lock_detected and (std::chrono::steady_clock::now() > setup_timeout)) {
            std::cout << " locked." << std::endl;
            break;
        }
        if (get_sensor_fn(sensor_name).to_bool()) {
            std::cout << "+";
            std::cout.flush();
            lock_detected = true;
        } else {
            if (std::chrono::steady_clock::now() > setup_timeout) {
                std::cout << std::endl;
                throw std::runtime_error(
                    str(boost::format(
                            "timed out waiting for consecutive locks on sensor \"%s\"")
                        % sensor_name));
            }
            std::cout << "_";
            std::cout.flush();
        }
        std::this_thread::sleep_for(100ms);
    }
    std::cout << std::endl;
    return true;
}

int UHD_SAFE_MAIN(int argc, char* argv[])
{
    // variables to be set by po
    std::string args, file, type, ant, subdev, ref, wirefmt, channels;
    size_t total_num_samps, spb;
    double rate, freq, gain, bw, total_time, setup_time, lo_offset;

    std::vector<std::thread> threads;

    std::vector<size_t> channel_list;
    std::vector<std::string> channel_strings;
    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        ("file", po::value<std::string>(&file)->default_value("usrp_samples.dat"), "name of the file to write binary samples to")
        ("type", po::value<std::string>(&type)->default_value("short"), "sample type: double, float, or short")
        ("nsamps", po::value<size_t>(&total_num_samps)->default_value(400000), "total number of samples to receive")
        ("duration", po::value<double>(&total_time)->default_value(0), "total number of seconds to receive")
        ("spb", po::value<size_t>(&spb)->default_value(1000), "samples per buffer")
        ("rate", po::value<double>(&rate)->default_value(10e6), "rate of incoming samples")
        ("freq", po::value<double>(&freq)->default_value(2407e6), "RF center frequency in Hz")
        ("lo-offset", po::value<double>(&lo_offset)->default_value(0.0),
            "Offset for frontend LO in Hz (optional)")
        ("gain", po::value<double>(&gain), "gain for the RF chain")
        ("ant", po::value<std::string>(&ant), "antenna selection")
        ("subdev", po::value<std::string>(&subdev), "subdevice specification")
        ("channels,channel", po::value<std::string>(&channels)->default_value("0"), R"(which channel(s) to use (specify "0", "1", "0,1", etc))")    // 使用 Raw String Literal 语法
        ("bw", po::value<double>(&bw), "analog frontend filter bandwidth in Hz")
        ("ref", po::value<std::string>(&ref), "reference source (internal, external, mimo)")
        ("wirefmt", po::value<std::string>(&wirefmt)->default_value("sc16"), "wire format (sc8, sc16 or s16)")  // sc is "short complex"
        ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
        ("progress", "periodically display short-term bandwidth")
        ("stats", "show average bandwidth on exit")
        ("sizemap", "track packet size and display breakdown on exit. Use with multi_streamer option if CPU limits stream rate.")
        ("null", "run without writing to file")
        ("continue", "don't abort on a bad packet")
        ("skip-lo", "skip checking LO lock status")
        ("int-n", "tune USRP with integer-N tuning")
        ("multi_streamer", "Create a separate streamer per channel.")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help")) {
        std::cout << "UHD RX samples to file " << desc << std::endl;
        std::cout << std::endl
                  << "This application streams data from a single channel of a USRP "
                     "device to a file.\n"
                  << std::endl;
        return ~0;
    }

    bool bw_summary             = vm.count("progress") > 0;
    bool stats                  = vm.count("stats") > 0;
    bool null                   = vm.count("null") > 0;
    bool enable_size_map        = vm.count("sizemap") > 0;
    bool continue_on_bad_packet = vm.count("continue") > 0;
    bool multithread            = vm.count("multi_streamer") > 0;

    if (enable_size_map)
        std::cout << "Packet size tracking enabled - will only recv one packet at a time!"
                  << "已启用数据包大小跟踪 —— 每次只接收一个数据包！"
                  << std::endl;

    // create a usrp device
    std::cout << std::endl;
    std::cout << "Creating the usrp device with: " << args << "..." << std::endl;   // 传输设备地址，为空字符串是表示：用户未指定设备，使用默认连接的设备
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);  // 初始化、加载bin、创建设备等

    // Parse channel selection string
    boost::split(channel_strings, channels, boost::is_any_of("\"',"));
    for (const auto& ch : channel_strings) {
        try {
            int chan = std::stoi(ch);
            if (chan >= static_cast<int>(usrp->get_rx_num_channels()) || chan < 0) {
                throw std::runtime_error("Invalid channel(s) specified.");
            } else {
                channel_list.push_back(static_cast<size_t>(chan));
            }
        } catch (std::invalid_argument const& c) {
            throw std::runtime_error("Invalid channel(s) specified.");
        } catch (std::out_of_range const& c) {
            throw std::runtime_error("Invalid channel(s) specified.");
        }
    }

    // Lock mboard clocks
    if (vm.count("ref")) {
        usrp->set_clock_source(ref);
    }

    // always select the subdevice first, the channel mapping affects the other settings
    // 总是先设置 subdevice，因为通道映射会影响后续的其他配置
    if (vm.count("subdev"))
        usrp->set_rx_subdev_spec(subdev);

    std::cout << "Using Device: " << usrp->get_pp_string() << std::endl;

    // set the sample rate
    if (rate <= 0.0) {
        std::cerr << "Please specify a valid sample rate" << std::endl;
        return ~0;  // 将0按位取反，int的二进制0是32个0，取反后为0xFFFFFFFF，在有符号整形中表示-1.
    }
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rate / 1e6) << std::endl;
    usrp->set_rx_rate(rate, uhd::usrp::multi_usrp::ALL_CHANS);
    std::cout << boost::format("Actual RX Rate: %f Msps...")
                     % (usrp->get_rx_rate(channel_list[0]) / 1e6)
              << std::endl
              << std::endl;

    // set the center frequency
    if (vm.count("freq")) { // with default of 0.0 this will always be true
        std::cout << boost::format("Setting RX Freq: %f MHz...") % (freq / 1e6)
                  << std::endl;
        std::cout << boost::format("Setting RX LO Offset: %f MHz...") % (lo_offset / 1e6)
                  << std::endl;
        uhd::tune_request_t tune_request(freq, lo_offset);  // 将目标频率和手动本震频率配置好
        if (vm.count("int-n"))
            tune_request.args = uhd::device_addr_t("mode_n=integer");
        for (size_t chan : channel_list)
            usrp->set_rx_freq(tune_request, chan);                          // 将上述配置的频率进行设置
        std::cout << boost::format("Actual RX Freq: %f MHz...")
                         % (usrp->get_rx_freq(channel_list[0]) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the rf gain
    if (vm.count("gain")) {
        std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
        usrp->set_rx_gain(gain, uhd::usrp::multi_usrp::ALL_CHANS);
        std::cout << boost::format("Actual RX Gain: %f dB...")
                         % usrp->get_rx_gain(channel_list[0])
                  << std::endl
                  << std::endl;
    }

    // set the IF filter bandwidth
    // 设置(接收端)中频滤波器带宽
    // 对接收信号具有“带通滤波”的作用，过滤掉非目标频段的干扰。
    if (vm.count("bw")) {
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        for (size_t chan : channel_list)
            usrp->set_rx_bandwidth(bw, chan);
        std::cout << boost::format("Actual RX Bandwidth: %f MHz...")
                         % (usrp->get_rx_bandwidth(channel_list[0]) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the antenna
    if (vm.count("ant"))
        for (size_t chan : channel_list)
            usrp->set_rx_antenna(ant, chan);

    std::this_thread::sleep_for(1s * setup_time);

    // check Ref and LO Lock detect
    if (not vm.count("skip-lo")) {
        for (size_t channel : channel_list) {
            std::cout << "Locking LO on channel " << channel << std::endl;
            check_locked_sensor(
                usrp->get_rx_sensor_names(channel),
                "lo_locked",
                [usrp, channel](const std::string& sensor_name) {
                    return usrp->get_rx_sensor(sensor_name, channel);
                },
                setup_time);
        }
        if (ref == "mimo") {
            check_locked_sensor(
                usrp->get_mboard_sensor_names(0),
                "mimo_locked",
                [usrp](const std::string& sensor_name) {
                    return usrp->get_mboard_sensor(sensor_name);
                },
                setup_time);
        }
        if (ref == "external") {
            check_locked_sensor(
                usrp->get_mboard_sensor_names(0),
                "ref_locked",
                [usrp](const std::string& sensor_name) {
                    return usrp->get_mboard_sensor(sensor_name);
                },
                setup_time);
        }
    }

    // 这段代码是一个信号处理设置逻辑，
    // 专门用于在无限接收数据时允许用户通过 Ctrl + C 中断接收过程。
    // SIGINT 就是用户按 Ctrl + C 时系统发送给进程的中断信号。
    // sig_int_handler 是回调函数
    if (total_num_samps == 0) {
        std::signal(SIGINT, &sig_int_handler);  // SIGINT = SIGnal INTerrupt 就是 std::signal C99定义的 Ctrl + C
        std::cout << "Press Ctrl + C to stop streaming..." << std::endl;
    }

#ifdef __linux__
    const double req_disk_rate = usrp->get_rx_rate(channel_list[0]) * channel_list.size()
                                 * uhd::convert::get_bytes_per_item(wirefmt);
    const double disk_rate_meas = disk_rate_check(
        uhd::convert::get_bytes_per_item(wirefmt), channel_list.size(), spb, file);
    if (disk_rate_meas > 0 && req_disk_rate >= disk_rate_meas) {
        std::cerr
            << boost::format(
                   "  Disk write test indicates that an overflow is likely to occur.\n"
                   "  Your write medium must sustain a rate of %0.3fMB/s,\n"
                   "  but write test returned write speed of %0.3fMB/s.\n"
                   "  The disk write rate is also affected by system load\n"
                   "  and OS/disk caching capacity.\n")
                   % (req_disk_rate / 1e6) % (disk_rate_meas / 1e6);
    }
#endif

    std::vector<size_t> chans_in_thread;
    std::vector<double> rates(channel_list.size()); //

#define recv_to_file_args(format)                                                    \
    (usrp,                                                                           \
        format,                                                                      \
        wirefmt,                                                                     \
        chans_in_thread,                                                             \
        channel_list.size(),                                                         \
        multithread ? "ch" + std::to_string(chans_in_thread[0]) + "_" + file : file, \
        spb,                                                                         \
        total_num_samps,                                                             \
        rates[i],                                                                    \
        total_time,                                                                  \
        stats,                                                                       \
        null,                                                                        \
        enable_size_map,                                                             \
        continue_on_bad_packet,                                                      \
        th_prefix)

    for (size_t i = 0; i < channel_list.size(); i++) {
        std::string th_prefix;
        if (multithread) {
            chans_in_thread.clear();
            chans_in_thread.push_back(channel_list[i]);
            th_prefix = "Thread " + std::to_string(i) + ":\n";
        } else {
            chans_in_thread = channel_list;
        }
        // 将新线程对象添加到一个线程容器中
        /*
         * lambda 表达式：
         * [=, &rates]: = 表示“按值捕获所有外部变量”; &rates 表示按引用捕获 rates.
         * (): 代表本线程不接收任何变量传入。
         *
         * !! recv_to_file_args中第九个参数为 rates， 对应了 recv_to_file 的第九个参数 bw.
         */
        threads.emplace_back([=, &rates]() {    // threads.push_back(std::thread([=, &rates]() -> threads.emplace_back([=, &rates]()
            // recv to file
            if (wirefmt == "s16") {
                if (type == "double")
                    recv_to_file<double> recv_to_file_args("f64");
                else if (type == "float")
                    recv_to_file<float> recv_to_file_args("f32");
                else if (type == "short")
                    recv_to_file<short> recv_to_file_args("s16");
                else
                    throw std::runtime_error("Unknown type " + type);
            } else {
                if (type == "double")
                    recv_to_file<std::complex<double>> recv_to_file_args("fc64");
                else if (type == "float")
                    recv_to_file<std::complex<float>> recv_to_file_args("fc32");
                else if (type == "short")
                    recv_to_file<std::complex<short>> recv_to_file_args("sc16");
                else
                    throw std::runtime_error("Unknown type " + type);
            }
        });
        if (!multithread) {
            break;
        }
    }


    if (total_time == 0) {
        if (total_num_samps > 0) {
            total_time = std::ceil(total_num_samps / usrp->get_rx_rate());
        }
    }

    // Wait a bit extra for the first updates from each thread
    std::this_thread::sleep_for(500ms);

    const auto end_time = std::chrono::steady_clock::now() + (total_time - 1) * 1s;

    while (!threads.empty()   // 还有线程没运行完
           && (std::chrono::steady_clock::now() < end_time || total_time == 0)  // 判断是否到了限定时间
           && !stop_signal_called) {    // 信号停止中断是否被触发
        std::this_thread::sleep_for(1s);

        // Remove any threads that are finished
        for (size_t i = 0; i < threads.size(); i++) {
            if (!threads[i].joinable()) {
                // Thread is not joinable, i.e. it has finished and 'joined' already
                // Remove the thread from the list.
                // 当线程不可被 join 时(意思是它已经完成并且被 joined 了)，从列表中将它移除。
                threads.erase(threads.begin() + i);
                // Clear last bandwidth value after thread is finished
                // 在线程结束后清除最后一个带宽的数据。
                rates[i] = 0;
            }
        }
        // Report the bandwidth of remaining threads
        if (bw_summary && !threads.empty()) {
            const std::lock_guard<std::mutex> lock(recv_mutex);
            std::cout << "\t"
                      << (std::accumulate(std::begin(rates), std::end(rates), 0) / 1e6
                             / threads.size())
                      << " Msps" << std::endl;
        }
    }

    // join any remaining threads
    for (auto & thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}
