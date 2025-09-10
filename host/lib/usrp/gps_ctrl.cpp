//
// Copyright 2010-2011,2014-2016 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/exception.hpp>
#include <uhd/types/sensors.hpp>
#include <uhd/utils/log.hpp>
#include <uhdlib/usrp/gps_ctrl.hpp>
#include <stdint.h>
#include <boost/algorithm/string.hpp>
#include <boost/date_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/thread/thread_time.hpp>
#include <boost/tokenizer.hpp>
#include <chrono>
#include <ctime>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <tuple>

using namespace uhd;
using namespace boost::posix_time;
using namespace boost::algorithm;

namespace {
constexpr int GPS_COMM_TIMEOUT_MS       = 1300;
constexpr int GPS_NMEA_NORMAL_FRESHNESS = 1000;
constexpr int GPS_SERVO_FRESHNESS       = 1000;
constexpr int GPS_LOCK_FRESHNESS        = 2500;
constexpr int GPS_TIMEOUT_DELAY_MS      = 200;
constexpr int GPSDO_COMMAND_DELAY_MS    = 200;
} // namespace

/*!
 * A control for GPSDO devices
 */

gps_ctrl::~gps_ctrl(void)
{
    /* NOP */
}

class gps_ctrl_impl : public gps_ctrl
{
private:
    // tuple 是元组类型，作用是把多个不同类型的值组合成一个对象。
    // 如果要绕过 get_sentence 函数及其 used 机制进行 gps 串口数据监听，可以考虑直接遍历 sentences
    std::map<std::string, std::tuple<std::string, boost::system_time, bool>> sentences;

    std::mutex cache_mutex;
    boost::system_time _last_cache_update;

    /*!
     * \brief 从缓存中获取指定类型的 GPS 语句
     *
     * 该函数会尝试从 GPS 缓存中获取类型为 \p which 的语句。
     * 如果缓存中存在且语句的时间戳未超过 \p max_age_ms 毫秒，则立即返回。
     * 否则，函数会等待，直到：
     *   - 收到所需类型的新语句，或
     *   - 等待时间超过 \p timeout 毫秒。
     *
     * 当 \p wait_for_next 为 true 时，函数只会返回在调用之后新接收到的语句，
     * 已经在缓存中存在的语句会被忽略（并标记为已使用）。
     *
     * \param which         要获取的语句类型（例如："GPGGA"）。
     * \param max_age_ms    语句的最大允许时长（毫秒）。
     * \param timeout       最大等待时间（毫秒）。
     * \param wait_for_next 若为 true，则必须等待新语句到达后才返回。
     *
     * \return 返回指定类型的 GPS 语句字符串。
     *
     * \throws uhd::value_error 如果在超时时间内未找到有效的语句。
     *
     * \note 该函数在需要时会更新 GPS 缓存，并在访问缓存时使用互斥锁保证线程安全。
     */
    std::string get_sentence(const std::string which,
        const int max_age_ms,
        const int timeout,
        const bool wait_for_next = false)
    {
        std::string sentence;
        boost::system_time now       = boost::get_system_time();
        boost::system_time exit_time = now + milliseconds(timeout);
        boost::posix_time::time_duration age;

        if (wait_for_next) {
            update_cache();
            std::lock_guard<std::mutex> lock(cache_mutex);
            // mark sentence as touched
            if (sentences.find(which) != sentences.end())
                std::get<2>(sentences[which]) = true;
        }
        while (1) {
            try {
                // update cache if older than a millisecond
                if (now - _last_cache_update > milliseconds(1)) {
                    update_cache();
                }

                std::lock_guard<std::mutex> lock(cache_mutex);
                if (sentences.find(which) == sentences.end()) {
                    age = milliseconds(max_age_ms);
                } else {
                    age = boost::get_system_time() - std::get<1>(sentences[which]);
                }
                if (age < milliseconds(max_age_ms)
                    and (not(wait_for_next and std::get<2>(sentences[which])))) {
                    sentence                      = std::get<0>(sentences[which]);
                    std::get<2>(sentences[which]) = true;
                }
            } catch (std::exception& e) {
                UHD_LOGGER_DEBUG("GPS") << "get_sentence: " << e.what();
            }

            if (not sentence.empty() or now > exit_time) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            now = boost::get_system_time();
        }

        if (sentence.empty()) {
            throw uhd::value_error("gps ctrl: No " + which + " message found");
        }

        return sentence;
    }

    // NMEA 是 GPS 模块输出的标准串口协议，该函数用于检查串口输出的消息是否符合标准。
    static bool is_nmea_checksum_ok(std::string nmea)
    {
        if (nmea.length() < 5 || nmea[0] != '$' || nmea[nmea.length() - 3] != '*')
            return false;

        std::stringstream ss;
        uint32_t string_crc;
        uint32_t calculated_crc = 0;

        // get crc from string
        // todo:后续可以学习一下这个crc校验机制
        ss << std::hex << nmea.substr(nmea.length() - 2, 2);
        ss >> string_crc;

        // calculate crc
        // 计算 crc
        // 从 $ 之后的第一个字符开始（即 nmea[1]），一直到 * 之前的最后一个字符。
        // 对每个字符逐个做 XOR（异或），得到最终的 calculated_crc。
        for (size_t i = 1; i < nmea.length() - 3; i++)
            calculated_crc ^= nmea[i];

        // return comparison
        return (string_crc == calculated_crc);
    }

    // Read all outstanding messages and put them in the cache
    //
    // Outside of the ctor, this is the only function that actually reads
    // messages from the GPS. It will read all outstanding messages and put them
    // into the sentences cache, together with a timestamp, so we know how old
    // the messages are.
    //
    // To pull messages out of the cache, use get_sentence().
    //
    // \param msg_key_hint If not empty, this will be used as the key for the
    //                    message in the cache. This is useful when we know that
    //                    a message is arriving that does not conform to the
    //                    message patterns for a SERVO or GP* message.
    /*!
     * 读取所有未处理的消息并放入缓存
     *
     * 除了构造函数以外，这是唯一一个从GPS实际读取消息的函数。
     * 它会读取所有未处理的消息并连同时间戳一起放入缓存(sentences cache)，
     * 这样我们就可以知道这些消息的时间了。
     *
     * 如果要从缓存中读取消息，请使用 get_sentence() 函数。
     *
     * \param msg_key_hint 如果该参数非空，将会用它作为缓存中的消息键。
     *                     当我们知道有某条消息即将到达，
     *                     但是它不符合 SERVO or GP* 消息模式时，
     *                     这个参数就非常有用。
     */
    void update_cache(const std::string& msg_key_hint = "")
    {
        if (not gps_detected()) {
            return;
        }

        static const std::regex servo_regex("^\\d\\d-\\d\\d-\\d\\d.*$");
        static const std::regex gp_msg_regex("^\\$GP.*,\\*[0-9A-F]{2}$");
        std::map<std::string, std::string> msgs;

        // Get all GPSDO messages available
        // Creating a map here because we only want the latest of each message type
        // 获取所有可用的 GPSDO 消息
        // 在这里创建一个 map，因为我们只需要每种消息类型的最新一条
        for (std::string msg = _recv(0); not msg.empty(); msg = _recv(0)) {
            // Strip any end of line characters
            // 去掉所有行尾字符
            erase_all(msg, "\r");
            erase_all(msg, "\n");

            if (msg.empty()) {
                // Ignore empty strings
                continue;
            }

            if (msg.length() < 6) {
                UHD_LOGGER_WARNING("GPS")
                    << UHD_FUNCTION << "(): Short GPSDO string: " << msg;
                continue;
            }

            // Look for SERVO message
            if (std::regex_search(
                    msg, servo_regex, std::regex_constants::match_continuous)) {
                UHD_LOG_TRACE("GPS", "Received new SERVO message: " << msg);
                msgs["SERVO"] = msg;
            } else if (std::regex_match(msg, gp_msg_regex) and is_nmea_checksum_ok(msg)) {
                UHD_LOG_TRACE(
                    "GPS", "Received new " << msg.substr(1, 5) << " message: " << msg);
                msgs[msg.substr(1, 5)] = msg;
            } else {
                if (!msg_key_hint.empty()) {
                    UHD_LOG_DEBUG(
                        "GPS", "Received " << msg_key_hint << " message: " << msg);
                    msgs[msg_key_hint] = msg;
                } else {
                    UHD_LOGGER_WARNING("GPS") << "Unexpected GPSDO string: " << msg;
                }
            }
        }

        boost::system_time time = boost::get_system_time();

        // Update sentences with newly read data
        // 使用新读到的数据更新 sentences
        for (auto& msg : msgs) {
            if (!msg.second.empty()) {
                std::lock_guard<std::mutex> lock(cache_mutex);
                sentences[msg.first] = std::make_tuple(msg.second, time, false);
            }
        }

        _last_cache_update = time;
    }

public:
    gps_ctrl_impl(uart_iface::sptr uart) : _uart(uart), _gps_type(GPS_TYPE_NONE)
    {
        std::string reply;
        bool i_heard_some_nmea = false, i_heard_something_weird = false;

        // first we look for an internal GPSDO

        // 读取接收缓冲区中当前的所有数据，然后丢弃（清空当前的串口缓存区）
        _flush(); // get whatever junk is in the rx buffer right now, and throw it away

        // 请求当前GPSDO的身份
        _send("*IDN?\r\n"); // request identity from the GPSDO

        // then we loop until we either timeout, or until we get a response that indicates
        // we're a JL device maximum response time was measured at ~320ms, so we set the
        // timeout at 650ms
        // 然后我们进入循环，直到发生超时，或者直到收到一个表明我们是 JL 设备的响应为止。
        // 测得的最大响应时间大约为 320ms，所以我们将超时时间设定为 650ms。
        //! 注意：JL device 很可能是 UHD 官方板载的GPS设备，
        //       如果使用外部GPS，发生问题时或许可以考虑修改延迟时间，
        //       但是这个超时时间已经相当大了。
        const boost::system_time comm_timeout =
            boost::get_system_time() + milliseconds(650);
        while (boost::get_system_time() < comm_timeout) {
            reply = _recv();
            // known devices are JL "FireFly", "GPSTCXO", and "LC_XO"
            if (reply.find("FireFly") != std::string::npos
                or reply.find("LC_XO") != std::string::npos
                or reply.find("GPSTCXO") != std::string::npos) {
                _gps_type = GPS_TYPE_INTERNAL_GPSDO;
                break;
            } else if (reply.substr(0, 3) == "$GP") {
                i_heard_some_nmea = true; // but keep looking
            } else if (not reply.empty()) {
                // wrong baud rate or firmware still initializing
                i_heard_something_weird = true;
                _send("*IDN?\r\n"); // re-send identity request
            } else {
                // _recv timed out
                _send("*IDN?\r\n"); // re-send identity request
            }
        }

        if (_gps_type == GPS_TYPE_NONE) {
            if (i_heard_some_nmea) {
                _gps_type = GPS_TYPE_GENERIC_NMEA;
            } else if (i_heard_something_weird) {
                UHD_LOGGER_ERROR("GPS")
                    << "GPS invalid reply \"" << reply << "\", assuming none available";
            }
        }

        switch (_gps_type) {
            case GPS_TYPE_INTERNAL_GPSDO:
                erase_all(reply, "\r");
                erase_all(reply, "\n");
                UHD_LOGGER_INFO("GPS") << "Found an internal GPSDO: " << reply;
                init_gpsdo();
                break;

            case GPS_TYPE_GENERIC_NMEA:
                UHD_LOGGER_INFO("GPS") << "Found a generic NMEA GPS device";
                break;

            case GPS_TYPE_NONE:
            default:
                UHD_LOGGER_INFO("GPS") << "No GPSDO found";
                break;
        }

        // initialize cache
        update_cache();
    }

    ~gps_ctrl_impl(void) override
    {
        /* NOP */
    }

    // return a list of supported sensors
    std::vector<std::string> get_sensors(void) override
    {
        return {"gps_gpgga", "gps_gprmc", "gps_time", "gps_locked", "gps_servo"};
    }

    // 使用了上文的 get_sentence() 函数来接收GPS消息。
    uhd::sensor_value_t get_sensor(std::string key) override
    {
        if (key == "gps_gpgga" or key == "gps_gprmc") {
            return sensor_value_t(boost::to_upper_copy(key),
                get_sentence(boost::to_upper_copy(key.substr(4, 8)),
                    GPS_NMEA_NORMAL_FRESHNESS,
                    GPS_TIMEOUT_DELAY_MS),
                "");
        } else if (key == "gps_time") {
            return sensor_value_t("GPS epoch time", int(get_epoch_time()), "seconds");
        } else if (key == "gps_locked") {
            return sensor_value_t("GPS lock status", locked(), "locked", "unlocked");
        } else if (key == "gps_servo") {
            return sensor_value_t("GPS_SERVO",
                get_sentence("SERVO", GPS_SERVO_FRESHNESS, GPS_TIMEOUT_DELAY_MS),
                "");
        } else {
            throw uhd::value_error("gps ctrl get_sensor unknown key: " + key);
        }
    }

    std::string send_cmd(std::string cmd) override
    {
        // Strip any end of line characters, then append them: This way we can
        // be sure the command is terminated correctly.
        erase_all(cmd, "\r");
        erase_all(cmd, "\n");
        const bool cmd_is_query = cmd.find("?") != std::string::npos;
        _send(cmd + "\r\n");
        if (!cmd_is_query) {
            return "";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(GPSDO_COMMAND_DELAY_MS));
        update_cache(cmd);
        try {
            return get_sentence(cmd, GPS_SERVO_FRESHNESS, GPS_TIMEOUT_DELAY_MS);
        } catch (const std::exception& ex) {
            UHD_LOGGER_WARNING("GPS")
                << UHD_FUNCTION
                << "(): Caught exception while attempting to parse response to command '"
                << cmd << "': " << ex.what();
        }
        return "";
    }

private:
    void init_gpsdo(void)
    {
        // issue some setup stuff so it spits out the appropriate data
        // none of these should issue replies so we don't bother looking for them
        // we have to sleep between commands because the JL device, despite not
        // acking, takes considerable time to process each command.
        const std::vector<std::string> init_cmds = {// Turn off serial echo
            "SYST:COMM:SER:ECHO OFF\r\n",
            "SYST:COMM:SER:PRO OFF\r\n",
            // Send NMEA string $GPGGA every 1 second
            "GPS:GPGGA 1\r\n",
            // Do not send modified $GPGGA string
            "GPS:GGAST 0\r\n",
            // Send NMEA string $GPRMC every 1 second
            "GPS:GPRMC 1\r\n",
            // Send SERVO updates every 1 second
            "SERV:TRAC 1\r\n"};

        for (const auto& cmd : init_cmds) {
            _send(cmd);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(GPSDO_COMMAND_DELAY_MS));
        }
    }

    // helper function to retrieve a field from an NMEA sentence
    std::string get_token(std::string sentence, size_t offset)
    {
        boost::tokenizer<boost::escaped_list_separator<char>> tok(sentence);
        std::vector<std::string> toked;
        tok.assign(sentence); // this can throw
        toked.assign(tok.begin(), tok.end());

        if (toked.size() <= offset) {
            throw uhd::value_error(std::string("Invalid response \"") + sentence + "\"");
        }
        return toked[offset];
    }

    ptime get_time(void)
    {
        int error_cnt = 0;
        ptime gps_time;
        while (error_cnt < 2) {
            try {
                // wait for next GPRMC string
                std::string reply = get_sentence(
                    "GPRMC", GPS_NMEA_NORMAL_FRESHNESS, GPS_COMM_TIMEOUT_MS, true);

                std::string datestr = get_token(reply, 9);
                std::string timestr = get_token(reply, 1);

                if (datestr.empty() or timestr.empty()) {
                    throw uhd::value_error(
                        std::string("Invalid response \"") + reply + "\"");
                }

                struct tm raw_date;
                raw_date.tm_year =
                    std::stoi(datestr.substr(4, 2)) + 2000 - 1900; // years since 1900
                raw_date.tm_mon =
                    std::stoi(datestr.substr(2, 2)) - 1; // months since january (0-11)
                raw_date.tm_mday = std::stoi(datestr.substr(0, 2)); // dom (1-31)
                raw_date.tm_hour = std::stoi(timestr.substr(0, 2));
                raw_date.tm_min  = std::stoi(timestr.substr(2, 2));
                raw_date.tm_sec  = std::stoi(timestr.substr(4, 2));
                gps_time         = boost::posix_time::ptime_from_tm(raw_date);

                UHD_LOG_TRACE(
                    "GPS", "GPS time: " + boost::posix_time::to_simple_string(gps_time));
                return gps_time;

            } catch (std::exception& e) {
                UHD_LOGGER_DEBUG("GPS") << "get_time: " << e.what();
                error_cnt++;
            }
        }
        throw uhd::value_error("get_time: Timeout after no valid message found");

        return gps_time; // keep gcc from complaining
    }

    int64_t get_epoch_time(void)
    {
        return (get_time() - from_time_t(0)).total_seconds();
    }

    bool gps_detected(void) override
    {
        return (_gps_type != GPS_TYPE_NONE);
    }

    bool locked(void)
    {
        int error_cnt = 0;
        while (error_cnt < 3) {
            try {
                std::string reply =
                    get_sentence("GPGGA", GPS_LOCK_FRESHNESS, GPS_COMM_TIMEOUT_MS);
                if (reply.empty())
                    error_cnt++;
                else
                    return (get_token(reply, 6) != "0");
            } catch (std::exception& e) {
                UHD_LOGGER_DEBUG("GPS") << "locked: " << e.what();
                error_cnt++;
            }
        }
        throw uhd::value_error("locked(): unable to determine GPS lock status");
    }

    uart_iface::sptr _uart;

    void _flush(void)
    {
        while (not _uart->read_uart(0.0).empty()) {
            // NOP
        }
    }

    std::string _recv(double timeout = GPS_TIMEOUT_DELAY_MS / 1000.)
    {
        return _uart->read_uart(timeout);
    }

    void _send(const std::string& buf)
    {
        return _uart->write_uart(buf);
    }

    enum { GPS_TYPE_INTERNAL_GPSDO, GPS_TYPE_GENERIC_NMEA, GPS_TYPE_NONE } _gps_type;
};

/***********************************************************************
 * Public make function for the GPS control
 **********************************************************************/
gps_ctrl::sptr gps_ctrl::make(uart_iface::sptr uart)
{
    return sptr(new gps_ctrl_impl(uart));
}
