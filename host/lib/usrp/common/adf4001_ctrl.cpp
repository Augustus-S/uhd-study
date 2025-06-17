//
// Copyright 2013 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// Original ADF4001 driver written by: bistromath
//                                     Mar 1, 2013
//
// Re-used and re-licensed with permission.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// adf4001 是 (整数N型)200 MHz 时钟发生器 PLL。
// 本 cpp 的作用是：
// 在 UHD 驱动层与 ADF4001 PLL 芯片交互，控制 USRP 中的锁相环时钟源。

#include <uhd/utils/log.hpp>
#include <uhdlib/usrp/common/adf4001_ctrl.hpp>
#include <iomanip>
#include <iostream>

using namespace uhd;
using namespace uhd::usrp;

adf4001_regs_t::adf4001_regs_t(void)
{
    // \todo:以下参数的功能为 ChatGPT 生成，后续对照 "adf4001 手册" 检查是否正确。
    ref_counter             = 0;    // 参考计数器 R 值。外部参考频率会被此值除后进入相位比较器。
    n                       = 0;    // 分频值：f_out = (N/R) * f_ref
    charge_pump_current_1   = 0;    // 充电泵电流（低字节位）。调节锁相速度与环路带宽。取值越高，响应越快但抖动增加。
    charge_pump_current_2   = 0;    // 充电泵电流（高字节位）。与 current_1 组合形成总电流设置。
    anti_backlash_width     = ANTI_BACKLASH_WIDTH_2_9NS;        // 防死区宽度（Anti-Backlash Pulse Width）。抑制抖动与锁相迟滞。通常设为最小 2.9ns。
    lock_detect_precision   = LOCK_DETECT_PRECISION_3CYC;       // 锁定检测需要连续几次一致结果才认为锁定，3 个周期容错锁相检测。值越高越精确但响应慢。
    charge_pump_gain        = CHARGE_PUMP_GAIN_1;               // 充电泵增益控制，用于控制充电泵电流倍增。
    counter_reset           = COUNTER_RESET_NORMAL;             // 是否复位分频器计数器。默认 NORMAL = 0（不复位），COUNTER_RESET_RESET = 1 会暂停 PLL 运行。
    power_down              = POWER_DOWN_NORMAL;                // 控制芯片电源状态：正常、异步关闭、同步关闭。
    muxout                  = MUXOUT_TRISTATE_OUT;              // 控制 MUXOUT 引脚的输出内容，比如锁定状态、分频输出等。
    phase_detector_polarity = PHASE_DETECTOR_POLARITY_NEGATIVE; // 相位比较器极性，决定频率调节方向（取决于电路）。
    charge_pump_mode        = CHARGE_PUMP_TRISTATE;             // 充电泵工作模式：正常/三态禁用（用于断开 PLL 控制）。
    fastlock_mode           = FASTLOCK_MODE_DISABLED;           // 快速锁定模式：在切频时快速接近目标频率。
    timer_counter_control   = TIMEOUT_3CYC;                     // 超时时间设置，用于 Fastlock 或其他逻辑等待时间，单位是锁定检测周期数。
}


// 根据地址 addr（取值 0-3）拼装对应的寄存器数据格式。
// 每个 reg 是 24 位（ADF4001 使用 24 位 SPI 写入），低 2 位为地址，高位根据芯片手册进行字段排列。
uint32_t adf4001_regs_t::get_reg(uint8_t addr)
{
    uint32_t reg = 0;
    switch (addr) {
        case 0:
            reg |= (uint32_t(ref_counter) & 0x003FFF) << 2;
            reg |= (uint32_t(anti_backlash_width) & 0x000003) << 16;
            reg |= (uint32_t(lock_detect_precision) & 0x000001) << 20;
            break;
        case 1:
            reg |= (uint32_t(n) & 0x001FFF) << 8;
            reg |= (uint32_t(charge_pump_gain) & 0x000001) << 21;
            break;
        case 2:
            reg |= (uint32_t(counter_reset) & 0x000001) << 2;
            reg |= (uint32_t(power_down) & 0x000001) << 3;
            reg |= (uint32_t(muxout) & 0x000007) << 4;
            reg |= (uint32_t(phase_detector_polarity) & 0x000001) << 7;
            reg |= (uint32_t(charge_pump_mode) & 0x000001) << 8;
            reg |= (uint32_t(fastlock_mode) & 0x000003) << 9;
            reg |= (uint32_t(timer_counter_control) & 0x00000F) << 11;
            reg |= (uint32_t(charge_pump_current_1) & 0x000007) << 15;
            reg |= (uint32_t(charge_pump_current_2) & 0x000007) << 18;
            reg |= (uint32_t(power_down) & 0x000002) << 20;
            break;
        case 3:
            reg |= (uint32_t(counter_reset) & 0x000001) << 2;
            reg |= (uint32_t(power_down) & 0x000001) << 3;
            reg |= (uint32_t(muxout) & 0x000007) << 4;
            reg |= (uint32_t(phase_detector_polarity) & 0x000001) << 7;
            reg |= (uint32_t(charge_pump_mode) & 0x000001) << 8;
            reg |= (uint32_t(fastlock_mode) & 0x000003) << 9;
            reg |= (uint32_t(timer_counter_control) & 0x00000F) << 11;
            reg |= (uint32_t(charge_pump_current_1) & 0x000007) << 15;
            reg |= (uint32_t(charge_pump_current_2) & 0x000007) << 18;
            reg |= (uint32_t(power_down) & 0x000002) << 20;
            break;
        default:
            break;
    }

    reg |= (uint32_t(addr) & 0x03);

    return reg;
}


// 设置 SPI 通信配置，初始化默认寄存器值。
// 调用 program_regs() 将当前配置写入芯片。
adf4001_ctrl::adf4001_ctrl(uhd::spi_iface::sptr _spi, int slaveno)
    : spi_iface(_spi), slaveno(slaveno)
{
    spi_config.mosi_edge = spi_config_t::EDGE_RISE;

    // set defaults
    adf4001_regs.ref_counter           = 1;
    adf4001_regs.n                     = 4;
    adf4001_regs.charge_pump_current_1 = 7;
    adf4001_regs.charge_pump_current_2 = 7;
    adf4001_regs.muxout                = adf4001_regs_t::MUXOUT_DLD;
    adf4001_regs.counter_reset         = adf4001_regs_t::COUNTER_RESET_NORMAL;
    adf4001_regs.phase_detector_polarity =
        adf4001_regs_t::PHASE_DETECTOR_POLARITY_POSITIVE;
    adf4001_regs.charge_pump_mode = adf4001_regs_t::CHARGE_PUMP_TRISTATE;

    // everything else should be defaults

    program_regs();
}

void adf4001_ctrl::set_lock_to_ext_ref(bool external)
{
    if (external) {
        adf4001_regs.charge_pump_mode = adf4001_regs_t::CHARGE_PUMP_NORMAL;
    } else {
        adf4001_regs.charge_pump_mode = adf4001_regs_t::CHARGE_PUMP_TRISTATE;
    }

    program_regs();
}

void adf4001_ctrl::program_regs(void)
{
    // no control over CE, only LE, therefore we use the initialization latch method
    // 无法控制 CE（使能引脚），只能控制 LE（锁存使能引脚），因此我们采用初始化锁存器（Initialization Latch）的方法
    write_reg(3);

    // conduct a function latch (2)
    // 0，1，2，3 只是自定义的类型，实际根据芯片手册已经确定只需要使用这几种情况。
    write_reg(2);

    // write R counter latch (0)
    write_reg(0);

    // write N counter latch (1)
    write_reg(1);
}


// 使用 SPI 接口写寄存器数据(24bits)到芯片
// 根据芯片手册，执行对应的位操作
void adf4001_ctrl::write_reg(uint8_t addr)
{
    uint32_t reg = adf4001_regs.get_reg(addr); // load the reg data

    spi_iface->transact_spi(slaveno, spi_config, reg, 24, false);
}
