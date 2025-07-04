//
// Copyright 2010-2012,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/exception.hpp>
#include <uhd/transport/udp_simple.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <complex>
#include <iostream>
#include <thread>

namespace po = boost::program_options;

int UHD_SAFE_MAIN(int argc, char* argv[])
{
    // variables to be set by po
    std::string args, file, ant, subdev, ref;
    size_t total_num_samps;
    double rate, freq, gain, bw;
    std::string addr, port;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        ("nsamps", po::value<size_t>(&total_num_samps)->default_value(1000), "total number of samples to receive")
        ("rate", po::value<double>(&rate)->default_value(100e6/16), "rate of incoming samples")
        ("freq", po::value<double>(&freq)->default_value(0), "rf center frequency in Hz")
        ("gain", po::value<double>(&gain)->default_value(0), "gain for the RF chain")
        /*
         * ant 参考值
         * tx-ant  //发射天线，b210设置为 “TX/RX or TX2”
         * rx-ant  //接收天线，b210设置为 “RX1 or RX2”
         */
        ("ant", po::value<std::string>(&ant), "antenna selection")
        ("subdev", po::value<std::string>(&subdev), "subdevice specification")
        ("bw", po::value<double>(&bw), "analog frontend filter bandwidth in Hz")
        ("port", po::value<std::string>(&port)->default_value("7124"), "server udp port")
        ("addr", po::value<std::string>(&addr)->default_value("192.168.1.10"), "resolvable server address")
        ("ref", po::value<std::string>(&ref), "clock reference (internal, external, mimo, gpsdo)")  // 参考时钟。
        ("int-n", "tune USRP with integer-N tuning")    // \todo:这是什么意思？
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help")) {
        std::cout << boost::format("UHD RX to UDP %s") % desc << std::endl;
        return ~0;
    }

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    // Lock mboard clocks
    if (vm.count("ref")) {
        usrp->set_clock_source(ref); // \todo:分析 ref 的作用和赋值过程。
    }

    // always select the subdevice first, the channel mapping affects the other settings
    // 始终应先选择子设备，因为通道映射会影响其他设置
    // subdevice（子设备）：USRP
    // 设备可以有多个子设备（例如多个射频前端），必须明确指定。(什么是射频前端？)
    if (vm.count("subdev")) {
        usrp->set_rx_subdev_spec(subdev);
    }

    // set the rx sample rate
    std::cout << boost::format("Setting RX Rate: %f Msps...") % (rate / 1e6) << std::endl;
    usrp->set_rx_rate(rate);
    std::cout << boost::format("Actual RX Rate: %f Msps...") % (usrp->get_rx_rate() / 1e6)
              << std::endl
              << std::endl;

    // set the rx center frequency
    std::cout << boost::format("Setting RX Freq: %f MHz...") % (freq / 1e6) << std::endl;
    uhd::tune_request_t tune_request(freq);
    if (vm.count("int-n"))
        tune_request.args = uhd::device_addr_t("mode_n=integer");
    usrp->set_rx_freq(tune_request);
    std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq() / 1e6)
              << std::endl
              << std::endl;

    // set the rx rf gain
    std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
    usrp->set_rx_gain(gain);
    std::cout << boost::format("Actual RX Gain: %f dB...") % usrp->get_rx_gain()
              << std::endl
              << std::endl;

    // set the analog frontend filter bandwidth
    if (vm.count("bw")) {
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        usrp->set_rx_bandwidth(bw);
        std::cout << boost::format("Actual RX Bandwidth: %f MHz...")
                         % (usrp->get_rx_bandwidth() / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the antenna
    if (vm.count("ant"))
        usrp->set_rx_antenna(ant); // ant's type is std::string.

    std::this_thread::sleep_for(std::chrono::seconds(1)); // allow for some setup time

    // Check Ref and LO Lock detect
    std::vector<std::string> sensor_names;
    sensor_names = usrp->get_rx_sensor_names(0);
    if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked")
        != sensor_names.end()) { // 从 sensor 中查找 lo_locked。
        uhd::sensor_value_t lo_locked = usrp->get_rx_sensor("lo_locked", 0);
        std::cout << boost::format("Checking RX: %s ...") % lo_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(lo_locked.to_bool());
    }
    sensor_names = usrp->get_mboard_sensor_names(0);
    if ((ref == "mimo")
        and (std::find(sensor_names.begin(), sensor_names.end(), "mimo_locked")
             != sensor_names.end())) {
        uhd::sensor_value_t mimo_locked = usrp->get_mboard_sensor("mimo_locked", 0);
        std::cout << boost::format("Checking RX: %s ...") % mimo_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(mimo_locked.to_bool());
    }
    if ((ref == "external")
        and (std::find(sensor_names.begin(), sensor_names.end(), "ref_locked")
             != sensor_names.end())) {
        uhd::sensor_value_t ref_locked = usrp->get_mboard_sensor("ref_locked", 0);
        std::cout << boost::format("Checking RX: %s ...") % ref_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(ref_locked.to_bool());
    }

    // create a receive streamer
    uhd::stream_args_t stream_args("fc32"); // complex floats
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    // setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
    stream_cmd.num_samps  = total_num_samps;
    stream_cmd.stream_now = true;
    rx_stream->issue_stream_cmd(stream_cmd);

    // loop until total number of samples reached
    size_t num_acc_samps = 0; // number of accumulated samples
    uhd::rx_metadata_t md;
    std::vector<std::complex<float>> buff(rx_stream->get_max_num_samps());
    uhd::transport::udp_simple::sptr udp_xport =
        uhd::transport::udp_simple::make_connected(addr, port); // 配置 UDP 传输

    while (num_acc_samps < total_num_samps) {
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md); // 开始接收

        // handle the error codes
        switch (md.error_code) {
            case uhd::rx_metadata_t::ERROR_CODE_NONE:
                break;

            case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
                if (num_acc_samps == 0)
                    continue;
                std::cout << boost::format("Got timeout before all samples received, "
                                           "possible packet loss, exiting loop...")
                          << std::endl;
                goto done_loop;

            default:
                std::cout << boost::format("Got error code 0x%x, exiting loop...")
                                 % md.error_code
                          << std::endl;
                goto done_loop;
        }

        // send complex single precision floating point samples over udp
        // udp 发送
        // buff.front() 表示访问 std::vector<std::complex<float>> buff 中的第一个元素.
        udp_xport->send(boost::asio::buffer(buff, num_rx_samps * sizeof(buff.front())));

        num_acc_samps += num_rx_samps;
    }
done_loop:

    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}
