// Copyright (C) 2017 - 2026 Vasily Evseenko <svpcom@p2ptech.org>

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

#include <stdlib.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <memory>
#include <stdexcept>

#include "wifibroadcast.hpp"

using namespace std;

string string_format(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // Extra space for '\0'
    size_t size = vsnprintf(nullptr, 0, format, args) + 1; // NOLINT(clang-analyzer-valist.Uninitialized)
    va_end(args);

    unique_ptr<char[]> buf(new char[size]);

    va_start(args, format);
    vsnprintf(buf.get(), size, format, args);
    va_end(args);

    // We don't want the '\0' inside
    return string(buf.get(), buf.get() + size - 1);
}

uint64_t get_time_ms(void) // in milliseconds
{
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (rc < 0) throw runtime_error(string_format("Error getting time: %s", strerror(errno)));
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

uint64_t get_time_us(void) // in microseconds
{
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (rc < 0) throw runtime_error(string_format("Error getting time: %s", strerror(errno)));
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int open_udp_socket_for_rx(int port, int rcv_buf_size, uint32_t bind_addr, int socket_type, int socket_protocol)
{
    struct sockaddr_in saddr;
    int fd = socket(AF_INET, socket_type, socket_protocol);
    if (fd < 0) throw runtime_error(string_format("Error opening socket: %s", strerror(errno)));

    const int optval = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(optval)) !=0)
    {
        close(fd);
        throw runtime_error(string_format("Unable to set SO_REUSEADDR: %s", strerror(errno)));
    }

    if(setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, (const void *)&optval , sizeof(optval)) != 0)
    {
        close(fd);
        throw runtime_error(string_format("Unable to set SO_RXQ_OVFL: %s", strerror(errno)));
    }

    if (rcv_buf_size > 0)
    {
        if(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const void *)&rcv_buf_size , sizeof(rcv_buf_size)) !=0)
        {
            close(fd);
            throw runtime_error(string_format("Unable to set SO_RCVBUF: %s", strerror(errno)));
        }
    }

    memset(&saddr, '\0', sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(bind_addr);
    saddr.sin_port = htons((unsigned short)port);

    if (::bind(fd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0)
    {
        close(fd);
        throw runtime_error(string_format("Unable to bind to %s:%d : %s", inet_ntoa(saddr.sin_addr), port, strerror(errno)));
    }
    return fd;
}


int open_unix_socket_for_rx(const char *socket_path, int rcv_buf_size, int socket_type, int socket_protocol)
{
    struct sockaddr_un saddr;

    int fd = socket(AF_UNIX, socket_type, socket_protocol);
    if (fd < 0) throw runtime_error(string_format("Error opening socket: %s", strerror(errno)));

    const int optval = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(optval)) !=0)
    {
        close(fd);
        throw runtime_error(string_format("Unable to set SO_REUSEADDR: %s", strerror(errno)));
    }

    if(setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, (const void *)&optval , sizeof(optval)) != 0)
    {
        close(fd);
        throw runtime_error(string_format("Unable to set SO_RXQ_OVFL: %s", strerror(errno)));
    }

    if (rcv_buf_size > 0)
    {
        if(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const void *)&rcv_buf_size , sizeof(rcv_buf_size)) !=0)
        {
            close(fd);
            throw runtime_error(string_format("Unable to set SO_RCVBUF: %s", strerror(errno)));
        }
    }

    memset(&saddr, '\0', sizeof(saddr));
    saddr.sun_family = AF_UNIX;
    strncpy(saddr.sun_path + 1, socket_path, sizeof(saddr.sun_path) - 2);

    if (::bind(fd, (struct sockaddr *) &saddr, sizeof(sa_family_t) + strlen(saddr.sun_path + 1) + 1) < 0)
    {
        close(fd);
        throw runtime_error(string_format("Unable to bind to @%s : %s", saddr.sun_path + 1, strerror(errno)));
    }
    return fd;
}


static uint8_t subch_width(uint8_t subch)
{
    switch(subch)
    {
    case SUBCH_NONE:
        return 0;
    case SUBCH_20L:
    case SUBCH_20U:
        return 40;
    case SUBCH_20LL:
    case SUBCH_20LU:
    case SUBCH_20UL:
    case SUBCH_20UU:
        return 80;
    default:
        throw runtime_error(string_format("Unsupported sub-channel: %d", subch));
    }
}


radiotap_header_t init_radiotap_header(uint8_t stbc,
                                       bool ldpc,
                                       bool short_gi,
                                       uint8_t bandwidth,
                                       uint8_t mcs_index,
                                       bool vht_mode,
                                       uint8_t vht_nss,
                                       uint8_t subch)
{
    radiotap_header_t res = {
        .header = {},
        .stbc = stbc,
        .ldpc = ldpc,
        .short_gi = short_gi,
        .bandwidth = bandwidth,
        .mcs_index = mcs_index,
        .vht_mode = vht_mode,
        .vht_nss = vht_nss,
        .subch = subch,
    };

    if (subch != SUBCH_NONE && subch_width(subch) != bandwidth)
    {
        throw runtime_error(string_format("Sub-channel %d is not in %d MHz", subch, bandwidth));
    }

    if (!vht_mode)
    {
        // Set flags in HT radiotap header
        uint8_t flags = 0;

        switch(bandwidth)
        {
        case 10:
        case 20:
            flags |= IEEE80211_RADIOTAP_MCS_BW_20;
            break;
        case 40:
            // 2 and 3 are the radiotap 20L/20U codes
            flags |= (subch == SUBCH_NONE) ? IEEE80211_RADIOTAP_MCS_BW_40 : subch;
            break;
        default:
            throw runtime_error(string_format("Unsupported HT bandwidth: %d", bandwidth));
        }

        if (short_gi)
        {
            flags |= IEEE80211_RADIOTAP_MCS_SGI;
        }

        switch(stbc)
        {
        case 0:
            break;
        case 1:
            flags |= (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
            break;
        case 2:
            flags |= (IEEE80211_RADIOTAP_MCS_STBC_2 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
            break;
        case 3:
            flags |= (IEEE80211_RADIOTAP_MCS_STBC_3 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
            break;
        default:
            throw runtime_error(string_format("Unsupported HT STBC type: %d", stbc));
        }

        if (ldpc)
        {
            flags |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
        }

        copy(radiotap_header_ht, radiotap_header_ht + sizeof(radiotap_header_ht), back_inserter(res.header));

        res.header[MCS_FLAGS_OFF] = flags;
        res.header[MCS_IDX_OFF] = mcs_index;
    }
    else
    {
        // Set flags in VHT radiotap header
        uint8_t flags = 0;

        copy(radiotap_header_vht, radiotap_header_vht + sizeof(radiotap_header_vht), back_inserter(res.header));

        if (short_gi)
        {
            flags |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
        }

        if (stbc)
        {
            flags |= IEEE80211_RADIOTAP_VHT_FLAG_STBC;
        }

        switch(bandwidth)
        {
        case 10:
        case 20:
            res.header[VHT_BW_OFF] = IEEE80211_RADIOTAP_VHT_BW_20M;
            break;
        case 40:
            res.header[VHT_BW_OFF] = IEEE80211_RADIOTAP_VHT_BW_40M;
            break;
        case 80:
            res.header[VHT_BW_OFF] = IEEE80211_RADIOTAP_VHT_BW_80M;
            break;
        case 160:
            res.header[VHT_BW_OFF] = IEEE80211_RADIOTAP_VHT_BW_160M;
            break;
        default:
            throw runtime_error(string_format("Unsupported VHT bandwidth: %d", bandwidth));
        }

        if (subch != SUBCH_NONE)
        {
            // the sub-channel code is a VHT bandwidth code
            res.header[VHT_BW_OFF] = subch;
        }

        if (ldpc)
        {
            res.header[VHT_CODING_OFF] = IEEE80211_RADIOTAP_VHT_CODING_LDPC_USER0;
        }

        res.header[VHT_FLAGS_OFF] = flags;
        res.header[VHT_MCSNSS0_OFF] |= ((mcs_index << IEEE80211_RADIOTAP_VHT_MCS_SHIFT) & IEEE80211_RADIOTAP_VHT_MCS_MASK);
        res.header[VHT_MCSNSS0_OFF] |= ((vht_nss << IEEE80211_RADIOTAP_VHT_NSS_SHIFT) & IEEE80211_RADIOTAP_VHT_NSS_MASK);
    }

    return res;
}
