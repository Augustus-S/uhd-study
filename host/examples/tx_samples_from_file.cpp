//
// Copyright 2011-2012,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <complex>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>

namespace po = boost::program_options;

static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

template <typename samp_type>
void send_from_file(
    uhd::tx_streamer::sptr tx_stream, const std::string& file, size_t samps_per_buff)
{
    uhd::tx_metadata_t md;
    md.start_of_burst = false;
    md.end_of_burst   = false;
    std::vector<samp_type> buff(samps_per_buff);
    std::vector<samp_type*> buffs(tx_stream->get_num_channels(), &buff.front());
    std::ifstream infile(file.c_str(), std::ifstream::binary);

    // loop until the entire file has been read
    // 循环读取整个文件

    while (not md.end_of_burst and not stop_signal_called) {
        infile.read((char*)&buff.front(), buff.size() * sizeof(samp_type));
        size_t num_tx_samps = size_t(infile.gcount() / sizeof(samp_type));

        md.end_of_burst = infile.eof();

        const size_t samples_sent = tx_stream->send(buffs, num_tx_samps, md);
        if (samples_sent != num_tx_samps) {
            UHD_LOG_ERROR("TX-STREAM",
                "The tx_stream timed out sending " << num_tx_samps << " samples ("
                                                   << samples_sent << " sent).");
            return;
        }
    }

    infile.close();
}

int UHD_SAFE_MAIN(int argc, char* argv[])
{
    // variables to be set by po
    std::string args, file, type, ant, subdev, ref, wirefmt, channels;
    size_t spb, single_channel;
    double rate, freq, gain, power, bw, delay, lo_offset;

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        ("file", po::value<std::string>(&file)->default_value("usrp_samples.dat"), "name of the file to read binary samples from")
        ("type", po::value<std::string>(&type)->default_value("short"), "sample type: double, float, or short")
        ("spb", po::value<size_t>(&spb)->default_value(10000), "samples per buffer")
        ("rate", po::value<double>(&rate), "rate of outgoing samples")
        ("freq", po::value<double>(&freq), "RF center frequency in Hz")
        ("lo-offset", po::value<double>(&lo_offset)->default_value(0.0),
            "Offset for frontend LO in Hz (optional)")
        ("gain", po::value<double>(&gain), "gain for the RF chain")
        ("power", po::value<double>(&power), "transmit power")
        ("ant", po::value<std::string>(&ant), "antenna selection")
        ("subdev", po::value<std::string>(&subdev), "subdevice specification")
        ("bw", po::value<double>(&bw), "analog frontend filter bandwidth in Hz")
        ("ref", po::value<std::string>(&ref), "clock reference (internal, external, mimo, gpsdo)")
        ("wirefmt", po::value<std::string>(&wirefmt)->default_value("sc16"), "wire format (sc8 or sc16)")
        ("delay", po::value<double>(&delay)->default_value(0.0), "specify a delay between repeated transmission of file (in seconds)")
        ("channel", po::value<size_t>(&single_channel), "which channel to use")
        ("channels", po::value<std::string>(&channels), "which channels to use (specify \"0\", \"1\", \"0,1\", etc)")
        ("repeat", "repeatedly transmit file")
        ("int-n", "tune USRP with integer-n tuning")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help")) {
        std::cout << boost::format("UHD TX samples from file %s") % desc << std::endl;
        return ~0;
    }

    bool repeat = vm.count("repeat") > 0;

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    // Channels
    std::vector<size_t> channel_nums;
    std::vector<std::string> channels_split;
    if (vm.count("channel")) {
        if (vm.count("channels")) {
            std::cout << "ERROR: Cannot specify 'channel' and 'channels'!" << std::endl;
            return EXIT_FAILURE;
        }
        if (single_channel >= usrp->get_tx_num_channels())
            throw std::runtime_error("Invalid channel specified.");
        channel_nums.push_back(single_channel);
    } else {
        // Provide default
        if (!vm.count("channels"))
            channels = "0";
        // Split string into 1 or more channels
        boost::split(channels_split, channels, boost::is_any_of("\"',"));
        for (std::string channel : channels_split) {
            if (boost::lexical_cast<size_t>(channel) >= usrp->get_tx_num_channels())
                throw std::runtime_error("Invalid channel(s) specified.");
            channel_nums.push_back(boost::lexical_cast<size_t>(channel));
        }
    }

    // Lock mboard clocks
    if (vm.count("ref")) {
        usrp->set_clock_source(ref);
    }

    // always select the subdevice first, the channel mapping affects the other settings
    if (vm.count("subdev"))
        usrp->set_tx_subdev_spec(subdev);

    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    // set the sample rate
    if (not vm.count("rate")) {
        std::cerr << "Please specify the sample rate with --rate" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting TX Rate: %f Msps...") % (rate / 1e6) << std::endl;
    for (std::size_t channel : channel_nums) {
        usrp->set_tx_rate(rate, channel);
        std::cout << boost::format("Actual TX Rate: %f Msps...")
                         % (usrp->get_tx_rate(channel) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the center frequency
    if (not vm.count("freq")) {
        std::cerr << "Please specify the center frequency with --freq" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting TX Freq: %f MHz...") % (freq / 1e6) << std::endl;
    std::cout << boost::format("Setting TX LO Offset: %f MHz...") % (lo_offset / 1e6)
              << std::endl;
    uhd::tune_request_t tune_request;
    // 这里的 lo_offset 和 rx 部分一样，但是 set_tx_freq() 函数内部逻辑并不完全一样。
    tune_request = uhd::tune_request_t(freq, lo_offset);    // 应该是格式化的作用
    if (vm.count("int-n"))
        tune_request.args = uhd::device_addr_t("mode_n=integer");
    for (std::size_t channel : channel_nums) {
        usrp->set_tx_freq(tune_request, channel);
        std::cout << boost::format("Actual TX Freq: %f MHz...")
                         % (usrp->get_tx_freq(channel) / 1e6)
                  << std::endl
                  << std::endl;
    }

    // set the rf gain
    if (vm.count("power")) {
        for (std::size_t channel : channel_nums) {
            if (!usrp->has_tx_power_reference(channel)) {
                std::cout << "ERROR: USRP does not have a reference power API on channel "
                          << channel << "!" << std::endl;
                return EXIT_FAILURE;
            }
            std::cout << "Setting TX output power: " << power << " dBm..." << std::endl;
            usrp->set_tx_power_reference(power, channel);
            std::cout << "Actual TX output power: "
                      << usrp->get_tx_power_reference(channel) << " dBm..." << std::endl;
        }

        if (vm.count("gain")) {
            std::cout << "WARNING: If you specify both --power and --gain, "
                         " the latter will be ignored."
                      << std::endl;
        }
    } else if (vm.count("gain")) {
        for (std::size_t channel : channel_nums) {
            std::cout << boost::format("Setting TX Gain: %f dB...") % gain << std::endl;
            usrp->set_tx_gain(gain, channel);
            std::cout << boost::format("Actual TX Gain: %f dB...")
                             % usrp->get_tx_gain(channel)
                      << std::endl
                      << std::endl;
        }
    }

    // set the analog frontend filter bandwidth
    if (vm.count("bw")) {
        std::cout << boost::format("Setting TX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        for (std::size_t channel : channel_nums) {
            usrp->set_tx_bandwidth(bw, channel);
            std::cout << boost::format("Actual TX Bandwidth: %f MHz...")
                             % (usrp->get_tx_bandwidth(channel) / 1e6)
                      << std::endl
                      << std::endl;
        }
    }

    // set the antenna
    if (vm.count("ant")) {
        for (std::size_t channel : channel_nums) {
            usrp->set_tx_antenna(ant, channel);
        }
    }
    // allow for some setup time:
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Check Ref and LO Lock detect
    std::vector<std::string> sensor_names;
    for (std::size_t channel : channel_nums) {
        sensor_names = usrp->get_tx_sensor_names(channel);
        if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked")
            != sensor_names.end()) {
            uhd::sensor_value_t lo_locked = usrp->get_tx_sensor("lo_locked", channel);
            std::cout << boost::format("Checking TX: %s ...") % lo_locked.to_pp_string()
                      << std::endl;
            UHD_ASSERT_THROW(lo_locked.to_bool());
        }
    }
    sensor_names = usrp->get_mboard_sensor_names(0);
    if ((ref == "mimo")
        and (std::find(sensor_names.begin(), sensor_names.end(), "mimo_locked")
             != sensor_names.end())) {
        uhd::sensor_value_t mimo_locked = usrp->get_mboard_sensor("mimo_locked", 0);
        std::cout << boost::format("Checking TX: %s ...") % mimo_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(mimo_locked.to_bool());
    }
    if ((ref == "external")
        and (std::find(sensor_names.begin(), sensor_names.end(), "ref_locked")
             != sensor_names.end())) {
        uhd::sensor_value_t ref_locked = usrp->get_mboard_sensor("ref_locked", 0);
        std::cout << boost::format("Checking TX: %s ...") % ref_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(ref_locked.to_bool());
    }

    // set sigint if user wants to receive
    if (repeat) {
        std::signal(SIGINT, &sig_int_handler);
        std::cout << "Press Ctrl + C to stop streaming..." << std::endl;
    }

    // create a transmit streamer
    std::string cpu_format;
    if (type == "double")
        cpu_format = "fc64";
    else if (type == "float")
        cpu_format = "fc32";
    else if (type == "short")
        cpu_format = "sc16";

    uhd::stream_args_t stream_args(cpu_format, wirefmt);
    stream_args.channels             = channel_nums;
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

    // send from file
    do {
        if (type == "double")
            send_from_file<std::complex<double>>(tx_stream, file, spb);
        else if (type == "float")
            send_from_file<std::complex<float>>(tx_stream, file, spb);
        else if (type == "short")
            send_from_file<std::complex<short>>(tx_stream, file, spb);
        else
            throw std::runtime_error("Unknown type " + type);

        if (repeat and delay > 0.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(int64_t(delay * 1000)));
        }
    } while (repeat and not stop_signal_called);

    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}
