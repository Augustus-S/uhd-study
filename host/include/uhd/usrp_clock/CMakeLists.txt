#
# Copyright 2014-2015 Ettus Research LLC
# Copyright 2018 Ettus Research, a National Instruments Company
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

UHD_INSTALL(FILES
    octoclock_eeprom.hpp
    multi_usrp_clock.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/uhd/usrp_clock
    COMPONENT headers
)

if(ENABLE_C_API)
    UHD_INSTALL(FILES
    usrp_clock.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/uhd/usrp_clock
    COMPONENT headers
    )
endif(ENABLE_C_API)
