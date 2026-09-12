/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * RTW88AWDLManager.hpp — shared AWDL/P2P state for AirPort_RTW88 1.0.1.
 *
 * This class intentionally contains no private Apple ABI layouts. It owns the
 * verified AWDL state that is shared between IO80211 virtual-interface IOCTLs
 * and the Realtek management-frame transport.
 */
#pragma once

#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOReturn.h>

class RTW88IEEE80211;
class IO80211VirtualInterface;

class RTW88AWDLManager {
public:
    RTW88AWDLManager() = default;
    ~RTW88AWDLManager();

    bool init(RTW88IEEE80211 *backend, IOWorkLoop *workLoop);
    void reset();

    void setVirtualInterface(UInt role, IO80211VirtualInterface *interface);
    void clearVirtualInterface(IO80211VirtualInterface *interface);
    IO80211VirtualInterface *awdlInterface() const { return _awdlInterface; }
    IO80211VirtualInterface *p2pInterface() const { return _p2pInterface; }

    bool syncEnabled() const { return _syncEnabled; }
    void setSyncEnabled(bool enabled) { _syncEnabled = enabled; }

    uint32_t electionMetric() const { return _electionMetric; }
    void setElectionMetric(uint32_t metric) { _electionMetric = metric; }

    void setBSSID(const uint8_t *bssid);
    void getBSSID(uint8_t *bssid) const;
    uint32_t electionId() const { return _electionId; }
    void setElectionId(uint32_t v) { _electionId = v; }
    uint32_t masterChannel() const { return _masterChannel; }
    void setMasterChannel(uint32_t v) { _masterChannel = v; }
    uint32_t secondaryMasterChannel() const { return _secondaryMasterChannel; }
    void setSecondaryMasterChannel(uint32_t v) { _secondaryMasterChannel = v; }
    uint8_t minRate() const { return _minRate; }
    void setMinRate(uint8_t v) { _minRate = v; }
    uint32_t presenceMode() const { return _presenceMode; }
    void setPresenceMode(uint32_t v) { _presenceMode = v; }
    uint32_t syncState() const { return _syncState; }
    void setSyncState(uint32_t v) { _syncState = v; }
    uint8_t deviceCapabilities() const { return _deviceCapabilities; }
    void setDeviceCapabilities(uint8_t v) { _deviceCapabilities = v; }
    uint64_t actionFrameTxMode() const { return _actionFrameTxMode; }
    void setActionFrameTxMode(uint64_t v) { _actionFrameTxMode = v; }

    IOReturn setSyncChannelSequence(const void *data, uint32_t length) {
        if (!data || length == 0 || length > sizeof(_syncChannelSequence))
            return kIOReturnBadArgument;
        bzero(_syncChannelSequence, sizeof(_syncChannelSequence));
        memcpy(_syncChannelSequence, data, length);
        _syncChannelSequenceLength = length;
        return kIOReturnSuccess;
    }
    IOReturn copySyncChannelSequence(void *data, uint32_t length) const {
        if (!data || !_syncChannelSequenceLength || length < _syncChannelSequenceLength)
            return kIOReturnNotReady;
        memcpy(data, _syncChannelSequence, _syncChannelSequenceLength);
        return kIOReturnSuccess;
    }

    void setSyncParams(uint32_t awLen, uint32_t awPeriod, uint32_t extLen, uint32_t syncPeriod) {
        _availabilityWindowLength = awLen;
        _availabilityWindowPeriod = awPeriod;
        _extensionLength = extLen;
        _synchronizationFramePeriod = syncPeriod;
        _syncParamsValid = true;
    }
    bool syncParamsValid() const { return _syncParamsValid; }
    uint32_t availabilityWindowLength() const { return _availabilityWindowLength; }
    uint32_t availabilityWindowPeriod() const { return _availabilityWindowPeriod; }
    uint32_t extensionLength() const { return _extensionLength; }
    uint32_t synchronizationFramePeriod() const { return _synchronizationFramePeriod; }

    IOReturn setSyncFrameTemplate(const void *payload, uint32_t length);
    IOReturn copySyncFrameTemplate(void *payload, uint32_t *length) const;

    void setP2PEnabled(bool enabled) { _p2pEnabled = enabled; }
    bool p2pEnabled() const { return _p2pEnabled; }

    void notePeerTrafficRegistration(bool active);
    uint32_t peerRegistrationCount() const { return _peerRegistrations; }

    bool hasVirtualTransport() const {
        return _awdlInterface != nullptr || _p2pInterface != nullptr;
    }

private:
    RTW88IEEE80211 *_backend = nullptr;       // non-retained, controller owns
    IOWorkLoop *_workLoop = nullptr;          // non-retained, controller owns
    IO80211VirtualInterface *_awdlInterface = nullptr;
    IO80211VirtualInterface *_p2pInterface = nullptr;

    uint8_t *_syncTemplate = nullptr;
    uint32_t _syncTemplateLength = 0;
    uint32_t _electionMetric = 0;
    uint32_t _electionId = 0;
    uint32_t _masterChannel = 0;
    uint32_t _secondaryMasterChannel = 0;
    uint32_t _peerRegistrations = 0;
    uint32_t _presenceMode = 0;
    uint32_t _syncState = 0;
    uint64_t _actionFrameTxMode = 0;
    uint8_t _bssid[6] = {};
    uint8_t _minRate = 0;
    uint8_t _deviceCapabilities = 0;
    uint8_t _syncChannelSequence[512] = {};
    uint32_t _syncChannelSequenceLength = 0;
    uint32_t _availabilityWindowLength = 0;
    uint32_t _availabilityWindowPeriod = 0;
    uint32_t _extensionLength = 0;
    uint32_t _synchronizationFramePeriod = 0;
    bool _syncParamsValid = false;
    bool _syncEnabled = true;
    bool _p2pEnabled = false;
};
