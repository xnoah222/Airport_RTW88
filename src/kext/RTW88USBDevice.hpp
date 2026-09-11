/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * RTW88USBDevice.hpp — IOEthernetController for USB rtw88 adapters
 *
 * Mirrors RTW88PCIDevice but attaches to IOUSBHostDevice instead of
 * IOPCIDevice.  All 802.11 logic is shared via RTW88IEEE80211.
 */
#pragma once

#include <sys/kernel_types.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class RTW88IEEE80211;
class RTW88UserClient;

class RTW88USBDevice : public IOEthernetController {
    OSDeclareDefaultStructors(RTW88USBDevice)

    friend class RTW88UserClient;
    friend class RTW88IEEE80211;

public:
    bool     init(OSDictionary *props) override;
    bool     start(IOService *provider) override;
    void     stop(IOService *provider) override;
    void     free() override;

    /* IONetworkController */
    IOReturn enable(IONetworkInterface *iface) override;
    IOReturn disable(IONetworkInterface *iface) override;
    IOReturn getMaxPacketSize(UInt32 *maxSize) const override;
    UInt32   outputPacket(mbuf_t m, void *param) override;

    /* IOEthernetController */
    IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    IOReturn setHardwareAddress(const IOEthernetAddress *addr) override;
    IOReturn setMulticastMode(bool active) override;
    IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) override;
    IOReturn setPromiscuousMode(bool active) override;
    IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;
    bool     configureInterface(IONetworkInterface *iface) override;
    IOReturn selectMedium(const IONetworkMedium *medium) override;

    IOReturn newUserClient(task_t owningTask, void *securityID,
                           UInt32 type, IOUserClient **handler) override;

    /* Called from RTW88IEEE80211 */
    void injectRxFrame(mbuf_t m);

    /* DMA helpers (USB uses system memory, no special DMA constraints) */
    void *allocCoherent(size_t size, IOPhysicalAddress *phys);
    void  freeCoherent(size_t size, void *virt, IOPhysicalAddress phys);

    /* USB bulk transfer — called from compat usb.c */
    int usbBulkOut(uint8_t ep, void *buf, int len, int timeout_ms);
    int usbBulkIn(uint8_t ep, void *buf, int len, int timeout_ms);
    int usbCtrl(uint8_t reqType, uint8_t req, uint16_t value, uint16_t index,
                void *buf, uint16_t size, int timeout_ms);

    RTW88IEEE80211 *get80211() { return _ieee80211; }

private:
    bool setupMediumDict();
    void addMedium(OSDictionary *mediums, IOMediumType type, UInt64 speed);
    void teardown();

    IOUSBHostDevice     *_usbDev      = nullptr;
    IOUSBHostInterface  *_usbIface    = nullptr;
    IOUSBHostPipe       *_bulkIn      = nullptr;
    IOUSBHostPipe       *_bulkOut     = nullptr;

    IOWorkLoop          *_workLoop    = nullptr;
    IOCommandGate       *_cmdGate     = nullptr;
    IOEthernetInterface *_iface       = nullptr;

    RTW88IEEE80211      *_ieee80211   = nullptr;
    RTW88UserClient     *_userClient  = nullptr;

    IOEthernetAddress    _macAddr;
    bool                 _enabled     = false;

    /* usb_interface for compat layer */
    struct usb_interface *_compatIface = nullptr;
};
