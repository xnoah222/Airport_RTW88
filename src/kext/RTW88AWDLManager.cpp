/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include <IOKit/80211/IO80211Controller.h>
#include <IOKit/80211/IO80211VirtualInterface.h>
#include <IOKit/80211/apple80211_ioctl.h>

#include "RTW88AWDLManager.hpp"
#include "RTW88IEEE80211.hpp"

RTW88AWDLManager::~RTW88AWDLManager()
{
    reset();
}

bool RTW88AWDLManager::init(RTW88IEEE80211 *backend, IOWorkLoop *workLoop)
{
    if (!backend || !workLoop)
        return false;
    _backend = backend;
    _workLoop = workLoop;
    _syncEnabled = true;
    _p2pEnabled = false;
    _electionMetric = 0;
    _electionId = 0;
    _masterChannel = 0;
    _secondaryMasterChannel = 0;
    _presenceMode = 0;
    _syncState = 0;
    _actionFrameTxMode = 0;
    _minRate = 0;
    _deviceCapabilities = 0;
    bzero(_bssid, sizeof(_bssid));
    _peerRegistrations = 0;
    return true;
}

void RTW88AWDLManager::reset()
{
    _awdlInterface = nullptr;
    _p2pInterface = nullptr;
    _backend = nullptr;
    _workLoop = nullptr;
    _syncEnabled = true;
    _p2pEnabled = false;
    _electionMetric = 0;
    _electionId = 0;
    _masterChannel = 0;
    _secondaryMasterChannel = 0;
    _presenceMode = 0;
    _syncState = 0;
    _actionFrameTxMode = 0;
    _minRate = 0;
    _deviceCapabilities = 0;
    bzero(_bssid, sizeof(_bssid));
    _peerRegistrations = 0;
    if (_syncTemplate) {
        IOFree(_syncTemplate, _syncTemplateLength);
        _syncTemplate = nullptr;
        _syncTemplateLength = 0;
    }
}

void RTW88AWDLManager::setVirtualInterface(UInt role, IO80211VirtualInterface *interface)
{
    if (role == APPLE80211_VIF_AWDL)
        _awdlInterface = interface;
    else if (role >= APPLE80211_VIF_P2P_DEVICE && role <= APPLE80211_VIF_P2P_GO)
        _p2pInterface = interface;
}

void RTW88AWDLManager::clearVirtualInterface(IO80211VirtualInterface *interface)
{
    if (_awdlInterface == interface)
        _awdlInterface = nullptr;
    if (_p2pInterface == interface)
        _p2pInterface = nullptr;
}

void RTW88AWDLManager::setBSSID(const uint8_t *bssid)
{
    if (bssid) memcpy(_bssid, bssid, sizeof(_bssid));
}

void RTW88AWDLManager::getBSSID(uint8_t *bssid) const
{
    if (bssid) memcpy(bssid, _bssid, sizeof(_bssid));
}

IOReturn RTW88AWDLManager::setSyncFrameTemplate(const void *payload, uint32_t length)
{
    if (!payload || length == 0 || length > 4096)
        return kIOReturnBadArgument;

    uint8_t *copy = (uint8_t *)IOMalloc(length);
    if (!copy)
        return kIOReturnNoMemory;
    memcpy(copy, payload, length);

    if (_syncTemplate)
        IOFree(_syncTemplate, _syncTemplateLength);
    _syncTemplate = copy;
    _syncTemplateLength = length;
    return kIOReturnSuccess;
}

IOReturn RTW88AWDLManager::copySyncFrameTemplate(void *payload, uint32_t *length) const
{
    if (!payload || !length)
        return kIOReturnBadArgument;
    if (!_syncTemplate || !_syncTemplateLength)
        return kIOReturnNotReady;

    /* Treat *length as the caller-provided capacity.  The old code copied the
     * whole saved template unconditionally, which could overflow an IO80211
     * buffer if a smaller payload was supplied on GET. */
    const uint32_t capacity = *length;
    *length = _syncTemplateLength;
    if (capacity < _syncTemplateLength)
        return kIOReturnNoSpace;

    memcpy(payload, _syncTemplate, _syncTemplateLength);
    return kIOReturnSuccess;
}

void RTW88AWDLManager::notePeerTrafficRegistration(bool active)
{
    if (active)
        ++_peerRegistrations;
    else if (_peerRegistrations)
        --_peerRegistrations;
}
