/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * AirPort_RTW88 1.0.0 — Ventura IO80211 AWDL/P2P virtual-interface bridge.
 *
 * This file deliberately implements only payload ABIs present in the pinned
 * MacKernelSDK. Unknown AWDL/P2P SET selectors are accepted without touching
 * their opaque payload so wifid can continue service initialization safely;
 * unknown GET selectors remain unsupported until their ABI is verified.
 */
#include "AirportRTW88.hpp"
#include "AirportRTW88Interface.hpp"
#include <net/bpf.h>
#include <sys/kpi_mbuf.h>

#define super IO80211Controller

static const char *rtw88VifRoleName(UInt role)
{
    return role == APPLE80211_VIF_AWDL ? "awdl" : "p2p";
}

IO80211VirtualInterface *AirportRTW88::createVirtualInterface(ether_addr *addr, UInt role)
{
    IOLog("AirPort_RTW88: createVirtualInterface role=%u\n", role);

    if (role < APPLE80211_VIF_P2P_DEVICE || role > APPLE80211_VIF_AWDL)
        return super::createVirtualInterface(addr, role);

    IO80211VirtualInterface *interface = new IO80211VirtualInterface;
    if (!interface)
        return nullptr;

    if (!interface->init(this, addr, role, rtw88VifRoleName(role))) {
        IOLog("AirPort_RTW88: virtual interface init failed role=%u\n", role);
        interface->release();
        return nullptr;
    }

    IOLog("AirPort_RTW88: virtual interface created role=%u name=%s\n",
          role, rtw88VifRoleName(role));
    return interface;
}

SInt32 AirportRTW88::enableVirtualInterface(IO80211VirtualInterface *interface)
{
    if (!interface)
        return kIOReturnBadArgument;

    UInt role = (UInt)interface->getInterfaceRole();
    IOLog("AirPort_RTW88: enableVirtualInterface role=%u bsd=%s\n",
          role, interface->getBSDName() ? interface->getBSDName() : "?");

    SInt32 ret = super::enableVirtualInterface(interface);
    if (ret != kIOReturnSuccess)
        return ret;

    if (role == APPLE80211_VIF_AWDL)
        _awdlInterface = interface;
    else if (role >= APPLE80211_VIF_P2P_DEVICE && role <= APPLE80211_VIF_P2P_GO)
        _p2pInterface = interface;

#if __IO80211_TARGET >= __MAC_13_0
    interface->setEnabledBySystem(true);
#endif
    interface->setLinkState(kIO80211NetworkLinkUp, 0);
    interface->postMessage(APPLE80211_M_LINK_CHANGED);
    return kIOReturnSuccess;
}

SInt32 AirportRTW88::disableVirtualInterface(IO80211VirtualInterface *interface)
{
    if (!interface)
        return kIOReturnBadArgument;

    UInt role = (UInt)interface->getInterfaceRole();
    IOLog("AirPort_RTW88: disableVirtualInterface role=%u bsd=%s\n",
          role, interface->getBSDName() ? interface->getBSDName() : "?");

    /* Match AirportItlwm: let IO80211 tear the VIF down first, then publish
     * link-down only after the superclass accepted the transition. */
    SInt32 ret = super::disableVirtualInterface(interface);
    if (ret != kIOReturnSuccess)
        return ret;

    interface->setLinkState(kIO80211NetworkLinkDown, 0);
    interface->postMessage(APPLE80211_M_LINK_CHANGED);
    if (_awdlInterface == interface) _awdlInterface = nullptr;
    if (_p2pInterface == interface)  _p2pInterface = nullptr;
    return kIOReturnSuccess;
}

SInt32 AirportRTW88::apple80211VirtualRequest(UInt request_type, int request_number,
                                               IO80211VirtualInterface *interface,
                                               void *data)
{
    if (request_type != SIOCGA80211 && request_type != SIOCSA80211)
        return kIOReturnBadArgument;
    bool set = request_type == SIOCSA80211;
    UInt role = interface ? (UInt)interface->getInterfaceRole() : 0;

    IOLog("AirPort_RTW88: VIF IOCTL %s selector=%d role=%u\n",
          set ? "SET" : "GET", request_number, role);

    /* Common control-plane selectors describe the same physical radio. */
    switch (request_number) {
    case APPLE80211_IOC_CARD_CAPABILITIES:
    case APPLE80211_IOC_POWER:
    case APPLE80211_IOC_SUPPORTED_CHANNELS:
    case APPLE80211_IOC_DRIVER_VERSION:
    case APPLE80211_IOC_OP_MODE:
    case APPLE80211_IOC_PHY_MODE:
    case APPLE80211_IOC_RSSI:
    case APPLE80211_IOC_STATE:
    case APPLE80211_IOC_BSSID:
    case APPLE80211_IOC_CHANNEL:
    case APPLE80211_IOC_AUTH_TYPE:
    case APPLE80211_IOC_SSID:
        return apple80211Request(request_type, request_number, _netif, data);

    case APPLE80211_IOC_AWDL_SYNC_ENABLED: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_sync_enabled *)data;
        if (set) {
            _awdlSyncEnabled = d->enabled != 0;
            IOLog("AirPort_RTW88: AWDL sync enabled=%d\n", _awdlSyncEnabled);
        } else {
            bzero(d, sizeof(*d));
            d->version = APPLE80211_VERSION;
            d->enabled = _awdlSyncEnabled ? 1 : 0;
        }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_ELECTION_METRIC: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_election_metric *)data;
        if (set)
            _awdlElectionMetric = d->metric;
        else {
            bzero(d, sizeof(*d));
            d->version = APPLE80211_VERSION;
            d->metric = _awdlElectionMetric;
        }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_peer_traffic_registration *)data;
        if (!set)
            return kIOReturnNotFound;
        uint32_t n = d->name_len < sizeof(d->name) ? d->name_len : (uint32_t)sizeof(d->name);
        IOLog("AirPort_RTW88: AWDL peer traffic registration active=%u name_len=%u\n",
              d->active, n);
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_SYNC_FRAME_TEMPLATE: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_sync_frame_template *)data;
        if (set) {
            if (!d->payload || d->payload_len == 0 || d->payload_len > 4096)
                return kIOReturnBadArgument;
            uint8_t *copy = (uint8_t *)IOMalloc(d->payload_len);
            if (!copy) return kIOReturnNoMemory;
            memcpy(copy, d->payload, d->payload_len);
            if (_awdlSyncTemplate)
                IOFree(_awdlSyncTemplate, _awdlSyncTemplateLength);
            _awdlSyncTemplate = copy;
            _awdlSyncTemplateLength = d->payload_len;
            IOLog("AirPort_RTW88: AWDL sync template len=%u\n", d->payload_len);
            return kIOReturnSuccess;
        }
        if (!_awdlSyncTemplate || !_awdlSyncTemplateLength || !d->payload)
            return kIOReturnNotReady;
        d->version = APPLE80211_VERSION;
        d->payload_len = _awdlSyncTemplateLength;
        memcpy(d->payload, _awdlSyncTemplate, _awdlSyncTemplateLength);
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_HT_CAPABILITY: {
        /* AirportItlwm exposes HT capability on the virtual interface.
         * Ventura's pinned struct differs from older headers, so advertise
         * only fields whose ABI is actually published here. */
        if (set || !data) return kIOReturnUnsupported;
        auto *d = (apple80211_ht_capability *)data;
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        IOLog("AirPort_RTW88: AWDL HT_CAPABILITY advertised\n");
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_P2P_SCAN: {
        /* AirportItlwm accepts/logs this as part of virtual-interface service
         * initialization. Use Ventura's published apple80211_scan_data ABI,
         * but do not start a second concurrent STA scan from the VIF path. */
        if (!set || !data) return kIOReturnUnsupported;
        auto *d = (apple80211_scan_data *)data;
        if (d->version != APPLE80211_VERSION ||
            d->ssid_len > APPLE80211_MAX_SSID_LEN ||
            d->num_channels > APPLE80211_MAX_CHANNELS)
            return kIOReturnBadArgument;
        IOLog("AirPort_RTW88: P2P_SCAN ssid_len=%u channels=%u type=%u phy=0x%x\n",
              d->ssid_len, d->num_channels, d->scan_type, d->phy_mode);
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_P2P_LISTEN:
        /* AirportItlwm accepts this request. The pinned Ventura SDK does not
         * publish apple80211_p2p_listen_data, so keep the payload opaque. */
        if (!set) return kIOReturnUnsupported;
        IOLog("AirPort_RTW88: P2P_LISTEN accepted (opaque Ventura ABI)\n");
        return kIOReturnSuccess;

    case APPLE80211_IOC_P2P_GO_CONF:
        if (!set) return kIOReturnUnsupported;
        IOLog("AirPort_RTW88: P2P_GO_CONF accepted (opaque Ventura ABI)\n");
        return kIOReturnSuccess;

    case APPLE80211_IOC_AWDL_BSSID:
    case APPLE80211_IOC_AWDL_ELECTION_ID:
    case APPLE80211_IOC_AWDL_MASTER_CHANNEL:
    case APPLE80211_IOC_AWDL_SECONDARY_MASTER_CHANNEL:
    case APPLE80211_IOC_AWDL_MIN_RATE:
    case APPLE80211_IOC_AWDL_ELECTION_RSSI_THRESHOLDS:
    case APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE:
    case APPLE80211_IOC_AWDL_PRESENCE_MODE:
    case APPLE80211_IOC_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS:
    case APPLE80211_IOC_AWDL_SYNC_STATE:
    case APPLE80211_IOC_AWDL_SYNC_PARAMS:
    case APPLE80211_IOC_AWDL_DEVICE_CAPABILITIES:
    case APPLE80211_IOC_AWDL_AF_TX_MODE:
    case APPLE80211_IOC_AWDL_OOB_AUTO_REQUEST:
        /* AirportItlwm dispatches all of these explicitly. Our Ventura SDK
         * exposes their selector numbers but omits the payload layouts. SET
         * can safely pass service initialization; GET must not fabricate a
         * kernel ABI, so keep it unsupported until the exact layout is known. */
        if (set) {
            IOLog("AirPort_RTW88: AirportItlwm AWDL selector=%d SET accepted (opaque ABI)\n",
                  request_number);
            return kIOReturnSuccess;
        }
        IOLog("AirPort_RTW88: AirportItlwm AWDL selector=%d GET needs Ventura ABI\n",
              request_number);
        return kIOReturnUnsupported;

    default:
        break;
    }

    /* The pinned Ventura headers expose selector numbers for more AWDL/P2P
     * operations than payload layouts. Accept SET requests opaquely without
     * dereferencing unknown structures; never fabricate GET payloads. */
    bool isP2PControl = request_number >= APPLE80211_IOC_P2P_ENABLE &&
                        request_number <= APPLE80211_IOC_P2P_CT_WINDOW;
    bool isAWDLControl = request_number >= APPLE80211_IOC_AWDL_SYNC_PARAMS &&
                         request_number <= APPLE80211_IOC_AWDL_DFSP_UCSA_CONFIG;
    if (set && (isP2PControl || isAWDLControl)) {
        IOLog("AirPort_RTW88: accepting opaque VIF SET selector=%d (ABI not declared in pinned SDK)\n",
              request_number);
        return kIOReturnSuccess;
    }

    return kIOReturnUnsupported;
}

static int rtw88SendActionFrame(RTW88IEEE80211 *backend, mbuf_t m)
{
    if (!m) return kIOReturnBadArgument;
    if (!backend) { mbuf_freem(m); return kIOReturnNotReady; }

    size_t len = mbuf_pkthdr_len(m);
    if (!len) len = mbuf_len(m);
    if (len < 24 || len > 4096) {
        mbuf_freem(m);
        return kIOReturnBadArgument;
    }

    uint8_t *frame = (uint8_t *)IOMalloc(len);
    if (!frame) { mbuf_freem(m); return kIOReturnNoMemory; }
    errno_t err = mbuf_copydata(m, 0, len, frame);
    mbuf_freem(m);
    if (err) { IOFree(frame, len); return kIOReturnError; }

    bool ok = backend->txRawManagementFrame(frame, (uint32_t)len);
    IOFree(frame, len);
    return ok ? kIOReturnSuccess : kIOReturnError;
}

int AirportRTW88::outputActionFrame(IO80211Interface *interface, mbuf_t m)
{
    (void)interface;
    IOLog("AirPort_RTW88: infrastructure action TX len=%zu\n", m ? mbuf_pkthdr_len(m) : 0);
    return rtw88SendActionFrame(_ieee80211, m);
}

int AirportRTW88::bpfOutputPacket(OSObject *object, UInt dltType, mbuf_t m)
{
    IO80211VirtualInterface *vif = OSDynamicCast(IO80211VirtualInterface, object);
    IOLog("AirPort_RTW88: VIF bpfOutput dlt=%u role=%d len=%zu\n",
          dltType, vif ? vif->getInterfaceRole() : 0, m ? mbuf_pkthdr_len(m) : 0);

    if (dltType == DLT_RAW || dltType == DLT_IEEE802_11)
        return rtw88SendActionFrame(_ieee80211, m);

    if (dltType == DLT_IEEE802_11_RADIO && m) {
        /* Minimal radiotap decapsulation: bytes 2..3 are the little-endian
         * radiotap header length.  AWDL action frames are then ordinary
         * native 802.11 management frames for the rtw88 MGMT queue. */
        uint8_t hdr[4] = {};
        if (mbuf_pkthdr_len(m) < sizeof(hdr) ||
            mbuf_copydata(m, 0, sizeof(hdr), hdr) != 0) {
            mbuf_freem(m);
            return kIOReturnBadArgument;
        }
        uint16_t rtlen = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
        size_t total = mbuf_pkthdr_len(m);
        if (rtlen < 4 || rtlen >= total) {
            mbuf_freem(m);
            return kIOReturnBadArgument;
        }
        mbuf_adj(m, (int)rtlen);
        return rtw88SendActionFrame(_ieee80211, m);
    }

    if (m) mbuf_freem(m);
    return kIOReturnUnsupported;
}

void AirportRTW88::requestPacketTx(void *object, UInt options)
{
    IO80211VirtualInterface *vif = OSDynamicCast(IO80211VirtualInterface, (OSObject *)object);
    IOLog("AirPort_RTW88: requestPacketTx role=%d options=%u\n",
          vif ? vif->getInterfaceRole() : 0, options);
    /* The pinned header does not expose a dequeue API for queued VIF data.
     * Do not re-signal the output thread here (that can recurse). Raw/action
     * traffic enters through bpfOutputPacket(), which is implemented above. */
}
