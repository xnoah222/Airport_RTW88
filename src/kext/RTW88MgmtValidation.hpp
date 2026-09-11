// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#pragma once
#include <stdint.h>
#include <stddef.h>
// Copy response fields while the RX buffer is still owned by the caller.
static inline bool rtw88ResponsePeer(const uint8_t *f, size_t n,
                                    const uint8_t *mac, const uint8_t *bssid) {
    if (!f || !mac || !bssid || n < 30) return false;
    for (unsigned i = 0; i < 6; ++i)
        if (f[4+i] != mac[i] || f[10+i] != bssid[i] || f[16+i] != bssid[i]) return false;
    return true;
}
static inline bool rtw88AuthResponse(const uint8_t *f, size_t n,
    const uint8_t *mac, const uint8_t *bssid, uint16_t *status) {
    if (!status || !rtw88ResponsePeer(f,n,mac,bssid) || (f[0] & 0xfc) != 0xb0 ||
        f[24] != 0 || f[25] != 0 || f[26] != 2 || f[27] != 0) return false;
    *status = (uint16_t)(f[28] | ((uint16_t)f[29] << 8)); return true;
}
static inline bool rtw88AssocResponse(const uint8_t *f, size_t n,
    const uint8_t *mac, const uint8_t *bssid, uint16_t *status, uint16_t *aid) {
    if (!status || !aid || !rtw88ResponsePeer(f,n,mac,bssid) ||
        ((f[0] & 0xfc) != 0x10 && (f[0] & 0xfc) != 0x30)) return false;
    *status = (uint16_t)(f[26] | ((uint16_t)f[27] << 8));
    *aid = (uint16_t)((f[28] | ((uint16_t)f[29] << 8)) & 0x3fff);
    return *status != 0 || (*aid >= 1 && *aid <= 2007);
}
