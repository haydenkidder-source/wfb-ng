// -*- C++ -*-
//
// Copyright (C) 2026 Vasily Evseenko <svpcom@p2ptech.org>

/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 3.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "wifibroadcast.hpp"


static radiotap_header_t ht(uint8_t bandwidth, uint8_t subch)
{
    return init_radiotap_header(0, false, false, bandwidth, 1, false, 1, subch);
}

static radiotap_header_t vht(uint8_t bandwidth, uint8_t subch)
{
    return init_radiotap_header(0, false, false, bandwidth, 1, true, 1, subch);
}


TEST_CASE("Sub-channel code lands in the MCS bandwidth bits of an HT header", "[radiotap]")
{
    radiotap_header_t h = ht(40, SUBCH_NONE);
    REQUIRE(h.header.size() == sizeof(radiotap_header_ht));
    REQUIRE((h.header[MCS_FLAGS_OFF] & 3) == IEEE80211_RADIOTAP_MCS_BW_40);
    REQUIRE(h.header[MCS_IDX_OFF] == 1);

    REQUIRE((ht(40, SUBCH_20L).header[MCS_FLAGS_OFF] & 3) == IEEE80211_RADIOTAP_MCS_BW_20L);
    REQUIRE((ht(40, SUBCH_20U).header[MCS_FLAGS_OFF] & 3) == IEEE80211_RADIOTAP_MCS_BW_20U);
    REQUIRE((ht(20, SUBCH_NONE).header[MCS_FLAGS_OFF] & 3) == IEEE80211_RADIOTAP_MCS_BW_20);
    REQUIRE(ht(40, SUBCH_20U).subch == SUBCH_20U);
}

TEST_CASE("Sub-channel code replaces the VHT bandwidth byte", "[radiotap]")
{
    radiotap_header_t h = vht(80, SUBCH_NONE);
    REQUIRE(h.header.size() == sizeof(radiotap_header_vht));
    REQUIRE(h.header[VHT_BW_OFF] == IEEE80211_RADIOTAP_VHT_BW_80M);

    REQUIRE(vht(80, SUBCH_20LL).header[VHT_BW_OFF] == 7);
    REQUIRE(vht(80, SUBCH_20LU).header[VHT_BW_OFF] == 8);
    REQUIRE(vht(80, SUBCH_20UL).header[VHT_BW_OFF] == 9);
    REQUIRE(vht(80, SUBCH_20UU).header[VHT_BW_OFF] == 10);
    REQUIRE(vht(40, SUBCH_20L).header[VHT_BW_OFF] == 2);
    REQUIRE(vht(40, SUBCH_20U).header[VHT_BW_OFF] == 3);

    // everything but the bandwidth byte is the plain header
    radiotap_header_t q = vht(80, SUBCH_20LU);
    for (size_t i = 0; i < h.header.size(); i++)
    {
        if (i != VHT_BW_OFF) REQUIRE(q.header[i] == h.header[i]);
    }
}

TEST_CASE("Sub-channel outside the channel width is refused", "[radiotap]")
{
    REQUIRE_THROWS_AS(ht(20, SUBCH_20U), std::runtime_error);
    REQUIRE_THROWS_AS(ht(40, SUBCH_20LL), std::runtime_error);
    REQUIRE_THROWS_AS(vht(80, SUBCH_20U), std::runtime_error);
    REQUIRE_THROWS_AS(vht(40, SUBCH_20UU), std::runtime_error);
    REQUIRE_THROWS_AS(vht(80, 5), std::runtime_error);   // 40L of 80: not a 20 MHz sub-channel
    REQUIRE_THROWS_AS(vht(80, 11), std::runtime_error);
}
