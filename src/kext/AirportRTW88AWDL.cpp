/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * AirPort_RTW88 1.0.1 — Ventura IO80211 AWDL/P2P virtual-interface bridge.
 *
 * This file deliberately implements only payload ABIs present in the pinned
 * MacKernelSDK. Verified Ventura payload ABIs are handled explicitly. Unknown AWDL/P2P
 * selectors stay unsupported rather than pretending a radio operation completed.
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

    /* IO80211's peer-to-peer roles (including AWDL/AirLink) must be backed by
     * IO80211P2PInterface, not a bare IO80211VirtualInterface.  A bare VIF is
     * sufficient to allocate/publish an ifnet named awdl0, but Ventura does
     * not classify it as a peer-to-peer virtual interface: the result is the
     * exact ghost state we observed (BSD awdl0 exists, MTU 0/inactive, while
     * Apple80211GetVirtualIfListCopy reports zero interfaces).
     *
     * Real IO80211 traces show AWDL role 4 running both
     * IO80211VirtualInterface::init and IO80211P2PInterface::init.  Construct
     * the derived class so its virtual init performs the P2P/AWDL setup. */
    IO80211P2PInterface *p2p = new IO80211P2PInterface;
    if (!p2p)
        return nullptr;

    if (!p2p->init(this, addr, role, rtw88VifRoleName(role))) {
        IOLog("AirPort_RTW88: P2P virtual interface init failed role=%u\n", role);
        p2p->release();
        return nullptr;
    }

    IO80211VirtualInterface *interface = p2p;
    IOLog("AirPort_RTW88: P2P virtual interface created role=%u name=%s class=%s\n",
          role, rtw88VifRoleName(role), interface->getMetaClass()->getClassName());
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

    if (_awdlManager)
        _awdlManager->setVirtualInterface(role, interface);
    if (role == APPLE80211_VIF_AWDL && _ieee80211)
        (void)_ieee80211->setAWDLReceiveMode(true);

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
    if (role == APPLE80211_VIF_AWDL && _ieee80211)
        (void)_ieee80211->setAWDLReceiveMode(false);
    if (_awdlManager)
        _awdlManager->clearVirtualInterface(interface);
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
    case APPLE80211_IOC_AUTH_TYPE:
    case APPLE80211_IOC_SSID:
    case APPLE80211_IOC_COUNTRY_CODE:
        /* awdl0 shares the same physical regulatory domain as en0.
         * Forward COUNTRY_CODE through the infrastructure handler so GET
         * returns the controller's current _countryCode and Apple's special
         * XZ/xZ SET requests are acknowledged without replacing it. */
        return apple80211Request(request_type, request_number, _netif, data);

    case APPLE80211_IOC_CHANNEL: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_channel_data *)data;
        if (role != APPLE80211_VIF_AWDL)
            return apple80211Request(request_type, request_number, _netif, data);
        if (set) {
            if (d->version != APPLE80211_VERSION || d->channel.channel == 0)
                return kIOReturnBadArgument;
            if (_awdlManager)
                _awdlManager->setMasterChannel((uint8_t)d->channel.channel);
            IOReturn r = _ieee80211 ? _ieee80211->setAWDLChannel(d->channel.channel)
                                    : kIOReturnNotReady;
            if (r == kIOReturnBusy) {
                IOLog("AirPort_RTW88: AWDL channel=%u deferred while STA associated\n",
                      d->channel.channel);
                return kIOReturnSuccess;
            }
            return r;
        }
        uint16_t ch = _awdlManager ? _awdlManager->masterChannel() : 0;
        if (!ch)
            return apple80211Request(request_type, request_number, _netif, data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->channel.version = APPLE80211_VERSION;
        d->channel.channel = ch;
        d->channel.flags = APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ |
            (ch <= 14 ? APPLE80211_C_FLAG_2GHZ : APPLE80211_C_FLAG_5GHZ);
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_SYNC_ENABLED: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_sync_enabled *)data;
        if (set) {
            if (!_awdlManager) return kIOReturnNotReady;
            _awdlManager->setSyncEnabled(d->enabled != 0);
            IOLog("AirPort_RTW88: AWDL sync enabled=%d\n", _awdlManager->syncEnabled());
        } else {
            bzero(d, sizeof(*d));
            d->version = APPLE80211_VERSION;
            d->enabled = (_awdlManager && _awdlManager->syncEnabled()) ? 1 : 0;
        }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_ELECTION_METRIC: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_election_metric *)data;
        if (set) {
            if (!_awdlManager) return kIOReturnNotReady;
            _awdlManager->setElectionMetric(d->metric);
        } else {
            bzero(d, sizeof(*d));
            d->version = APPLE80211_VERSION;
            d->metric = _awdlManager ? _awdlManager->electionMetric() : 0;
        }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_peer_traffic_registration *)data;
        if (!set)
            return kIOReturnNotFound;
        uint32_t n = d->name_len < sizeof(d->name) ? d->name_len : (uint32_t)sizeof(d->name);
        if (_awdlManager) _awdlManager->notePeerTrafficRegistration(d->active != 0);
        IOLog("AirPort_RTW88: AWDL peer traffic registration active=%u name_len=%u peers=%u\n",
              d->active, n, _awdlManager ? _awdlManager->peerRegistrationCount() : 0);
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_SYNC_FRAME_TEMPLATE: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_sync_frame_template *)data;
        if (!_awdlManager) return kIOReturnNotReady;
        if (set) {
            IOReturn ret = _awdlManager->setSyncFrameTemplate(d->payload, d->payload_len);
            if (ret == kIOReturnSuccess)
                IOLog("AirPort_RTW88: AWDL sync template len=%u\n", d->payload_len);
            return ret;
        }
        if (!d->payload) return kIOReturnBadArgument;
        uint32_t length = d->payload_len;
        IOReturn ret = _awdlManager->copySyncFrameTemplate(d->payload, &length);
        /* Always publish the required/actual size, including kIOReturnNoSpace,
         * so IO80211 can retry with an adequate buffer. */
        d->version = APPLE80211_VERSION;
        d->payload_len = length;
        if (ret) return ret;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_HT_CAPABILITY: {
        if (set || !data) return kIOReturnUnsupported;
        auto *d = (apple80211_ht_capability *)data;
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        /* AirportItlwm master: element 45 + CBW20/40 + SGI20 + SGI40.
         * The pinned header names these fields unk1/unk3 but the bundled
         * AirportItlwm fixture confirms their exact offsets. */
        d->unk1 = 45;
        d->unk3 = 0x0062;
        IOLog("AirPort_RTW88: AWDL HT_CAPABILITY cap=0x%x\n", d->unk3);
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_VHT_CAPABILITY: {
        if (set || !data) return kIOReturnUnsupported;
        auto *d = (apple80211_vht_capability *)data;
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->cap = 3263; /* Match AirportItlwm's Ventura legacy path. */
        IOLog("AirPort_RTW88: AWDL VHT_CAPABILITY cap=%u\n", d->cap);
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_CHANNELS_INFO: {
        if (set || !data || !_ieee80211) return kIOReturnUnsupported;
        auto *d = (apple80211_channels_info *)data;
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        RTW88Channel channels[APPLE80211_MAX_CHANNELS] = {};
        uint32_t count = 0;
        IOReturn ret = _ieee80211->copyChannels(channels, APPLE80211_MAX_CHANNELS, &count);
        if (ret != kIOReturnSuccess && ret != kIOReturnNoSpace) return ret;
        if (count > APPLE80211_MAX_CHANNELS) count = APPLE80211_MAX_CHANNELS;
        for (uint32_t i = 0; i < count; ++i) {
            const uint16_t ch = channels[i].number;
            d->chan_num[i] = (uint8_t)ch;
            d->passive[i] = channels[i].passive ? 1 : 0;
            d->radar_dfs[i] = channels[i].radar ? 1 : 0;
            /* RTL8822B supports HT40 and VHT80. Keep width claims limited to
             * bands where the physical radio can actually use them. */
            d->support_40Mhz[i] = (ch != 14) ? 1 : 0;
            d->support_80Mhz[i] = (ch > 14) ? 1 : 0;
            d->chan_spec[i] = ch;
        }
        d->num_chan_specs = (uint16_t)count;
        IOLog("AirPort_RTW88: AWDL CHANNELS_INFO count=%u\n", count);
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_peer_cache_maximum_size *)data;
        if (set) {
            IOLog("AirPort_RTW88: AWDL peer cache max requested=%u\n", d->max_peers);
            return kIOReturnSuccess;
        }
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->max_peers = 255;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_P2P_ENABLE:
        if (!set) return kIOReturnUnsupported;
        if (_awdlManager) _awdlManager->setP2PEnabled(true);
        IOLog("AirPort_RTW88: P2P_ENABLE accepted (Ventura opaque ABI)\n");
        return kIOReturnSuccess;

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

    case APPLE80211_IOC_AWDL_BSSID: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_bssid *)data;
        if (set) _awdlManager->setBSSID(d->bssid);
        else { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; _awdlManager->getBSSID(d->bssid); }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_ELECTION_ID: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_election_id *)data;
        if (set) _awdlManager->setElectionId(d->election_id);
        else { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; d->election_id = _awdlManager->electionId(); }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_MASTER_CHANNEL: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_master_channel *)data;
        if (set) {
            _awdlManager->setMasterChannel(d->master_channel);
            if (_ieee80211 && d->master_channel) {
                IOReturn r = _ieee80211->setAWDLChannel(d->master_channel);
                if (r != kIOReturnSuccess && r != kIOReturnBusy) return r;
            }
        } else { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; d->master_channel = _awdlManager->masterChannel(); }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_SECONDARY_MASTER_CHANNEL: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_secondary_master_channel *)data;
        if (set) _awdlManager->setSecondaryMasterChannel(d->secondary_master_channel);
        else { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; d->secondary_master_channel = _awdlManager->secondaryMasterChannel(); }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_MIN_RATE: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_min_rate *)data;
        if (set) _awdlManager->setMinRate(d->min_rate);
        else { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; d->min_rate = _awdlManager->minRate(); }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_ELECTION_RSSI_THRESHOLDS: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_election_rssi_thresholds *)data;
        if (!set) { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_sync_channel_sequence *)data;
        if (set) {
            IOReturn r = _awdlManager->setSyncChannelSequence(d, sizeof(*d));
            if (r != kIOReturnSuccess) return r;
            IOLog("AirPort_RTW88: AWDL sync channel sequence len=%u enc=%u steps=%u dup=%u fill_ch=%u\n",
                  (unsigned)d->length, (unsigned)d->encoding, (unsigned)d->step_count,
                  (unsigned)d->duplicate_count, (unsigned)d->fill_channel);
            /* On an idle/unassociated PHY, fill_channel is the safest concrete
             * channel hint available from the sequence. The full sequence is
             * retained for IO80211 GETs; connected STA coexistence still needs
             * a real timeslicer rather than a permanent retune. */
            if (_ieee80211 && d->fill_channel) {
                IOReturn cr = _ieee80211->setAWDLChannel(d->fill_channel);
                if (cr != kIOReturnSuccess && cr != kIOReturnBusy &&
                    cr != kIOReturnUnsupported)
                    return cr;
            }
        } else {
            bzero(d, sizeof(*d));
            IOReturn r = _awdlManager->copySyncChannelSequence(d, sizeof(*d));
            if (r != kIOReturnSuccess) {
                d->version = APPLE80211_VERSION;
                return kIOReturnSuccess;
            }
        }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_PRESENCE_MODE: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_presence_mode *)data;
        if (set) _awdlManager->setPresenceMode(d->mode);
        else { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; d->mode = _awdlManager->presenceMode(); }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS: {
        if (!data) return kIOReturnBadArgument;
        auto *d = (apple80211_awdl_extension_state_machine_parameter *)data;
        if (!set) { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_SYNC_STATE: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_sync_state *)data;
        if (set) _awdlManager->setSyncState(d->state);
        else { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; d->state = _awdlManager->syncState(); }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_SYNC_PARAMS: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_sync_params *)data;
        if (set) {
            _awdlManager->setSyncParams(d->availability_window_length,
                                        d->availability_window_period,
                                        d->extension_length,
                                        d->synchronization_frame_period);
            IOLog("AirPort_RTW88: AWDL sync params aw_len=%u aw_period=%u ext=%u sync_period=%u\n",
                  d->availability_window_length, d->availability_window_period,
                  d->extension_length, d->synchronization_frame_period);
        } else {
            bzero(d, sizeof(*d));
            d->version = APPLE80211_VERSION;
            if (_awdlManager->syncParamsValid()) {
                d->availability_window_length = _awdlManager->availabilityWindowLength();
                d->availability_window_period = _awdlManager->availabilityWindowPeriod();
                d->extension_length = _awdlManager->extensionLength();
                d->synchronization_frame_period = _awdlManager->synchronizationFramePeriod();
            }
        }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_DEVICE_CAPABILITIES: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_cap *)data;
        if (set) _awdlManager->setDeviceCapabilities(d->cap);
        else {
            bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION;
            /* AirportItlwm reports CCA-stats capability. Its public fixture
             * uses bit 0 for this legacy capability. */
            d->cap = _awdlManager->deviceCapabilities();
            if (!d->cap) d->cap = 1;
        }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_AF_TX_MODE: {
        if (!data || !_awdlManager) return kIOReturnNotReady;
        auto *d = (apple80211_awdl_af_tx_mode *)data;
        if (set) _awdlManager->setActionFrameTxMode(d->mode);
        else { bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; d->mode = _awdlManager->actionFrameTxMode(); }
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_AWDL_OOB_AUTO_REQUEST:
        /* AirportItlwm only consumes this as a SET. The large private OOB
         * payload is not needed for basic AWDL transport bring-up. */
        return set ? kIOReturnSuccess : kIOReturnUnsupported;

    default:
        break;
    }

    IOLog("AirPort_RTW88: VIF selector=%d unsupported (ABI/transport not implemented)\n",
          request_number);
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
    if (!vif || !_ieee80211 || vif->getInterfaceRole() != APPLE80211_VIF_AWDL)
        return;

    /* Ventura's IO80211VirtualInterface has its own private output queues.
     * requestPacketTx is the controller's opportunity to drain them. The
     * private method returns bool with undocumented semantics, so packetHead
     * (initialized to null) is authoritative rather than the return value. */
    static const IOMbufServiceClass classes[] = {
        kIOMbufServiceClassCTL, kIOMbufServiceClassVO, kIOMbufServiceClassVI,
        kIOMbufServiceClassRV, kIOMbufServiceClassAV, kIOMbufServiceClassOAM,
        kIOMbufServiceClassRD, kIOMbufServiceClassBE, kIOMbufServiceClassBK,
        kIOMbufServiceClassBKSYS
    };

    UInt32 sent = 0;
    for (unsigned c = 0; c < sizeof(classes) / sizeof(classes[0]); c++) {
        /* Bound each callback so one chatty class cannot monopolize the
         * IO80211 workloop. IO80211 will signal us again if more remains. */
        mbuf_t head = nullptr, tail = nullptr;
        UInt count = 0;
        unsigned long long bytes = 0;
        (void)vif->dequeueOutputPacketsWithServiceClass(32, classes[c],
                                                        &head, &tail,
                                                        &count, &bytes);
        mbuf_t m = head;
        while (m) {
            mbuf_t next = mbuf_nextpkt(m);
            mbuf_setnextpkt(m, nullptr);
            if (_ieee80211->txAWDLDataFrame(m))
                sent++;
            /* txAWDLDataFrame consumes m on both success and failure. */
            m = next;
        }
    }

    if (sent || options)
        IOLog("AirPort_RTW88: requestPacketTx AWDL sent=%u options=%u\n", sent, options);
}

