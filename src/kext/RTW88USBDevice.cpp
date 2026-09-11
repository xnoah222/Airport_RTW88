// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// RTW88USBDevice.cpp — IOEthernetController for USB rtw88 adapters

#include "RTW88USBDevice.hpp"
#include "RTW88IEEE80211.hpp"
#include "RTW88UserClient.hpp"

#include <IOKit/IOLib.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>
#include <IOKit/usb/StandardUSB.h>
#include <string.h>

extern "C" {
#include "../compat/rtw88_compat.h"

/* USB probe entry declared in usb.c */
int  rtw_usb_probe(struct usb_interface *intf, const struct usb_device_id *id);
void rtw_usb_disconnect(struct usb_interface *intf);
}

#define super IOEthernetController
OSDefineMetaClassAndStructors(RTW88USBDevice, IOEthernetController)

/* ------------------------------------------------------------------ */
/*  USB compat ops — routes IOUSBHostPipe calls to compat shim         */
/* ------------------------------------------------------------------ */

static RTW88USBDevice *g_usb_dev = nullptr;

static int compat_usb_bulk_out(struct usb_interface *intf, uint8_t ep,
                                void *buf, int len, int timeout_ms)
{
    return g_usb_dev ? g_usb_dev->usbBulkOut(ep, buf, len, timeout_ms) : -EIO;
}
static int compat_usb_bulk_in(struct usb_interface *intf, uint8_t ep,
                               void *buf, int len, int timeout_ms)
{
    return g_usb_dev ? g_usb_dev->usbBulkIn(ep, buf, len, timeout_ms) : -EIO;
}
static int compat_usb_ctrl(struct usb_interface *intf,
                             uint8_t reqType, uint8_t req,
                             uint16_t value, uint16_t index,
                             void *buf, uint16_t size, int timeout_ms)
{
    return g_usb_dev ? g_usb_dev->usbCtrl(reqType, req, value, index,
                                            buf, size, timeout_ms) : -EIO;
}

static struct rtw88_usb_ops _usb_ops = {
    .bulk_msg_out = compat_usb_bulk_out,
    .bulk_msg_in  = compat_usb_bulk_in,
    .ctrl_msg     = compat_usb_ctrl,
};

/* ------------------------------------------------------------------ */
/*  IOService                                                           */
/* ------------------------------------------------------------------ */

bool RTW88USBDevice::init(OSDictionary *props)
{
    IOLog("rtw88: RTW88USBDevice::init\n");
    return super::init(props);
}

bool RTW88USBDevice::start(IOService *provider)
{
    IOLog("rtw88: RTW88USBDevice::start\n");
    if (!super::start(provider)) return false;

    _usbDev = OSDynamicCast(IOUSBHostDevice, provider);
    if (!_usbDev) {
        IOLog("rtw88: provider is not IOUSBHostDevice\n");
        return false;
    }
    _usbDev->retain();
    _usbDev->open(this);

    /* Get first interface via service plane child iterator */
    {
        OSIterator *iter = _usbDev->getChildIterator(gIOServicePlane);
        if (iter) {
            OSObject *obj;
            while ((obj = iter->getNextObject()) != nullptr) {
                IOUSBHostInterface *candidate = OSDynamicCast(IOUSBHostInterface, obj);
                if (candidate) { _usbIface = candidate; _usbIface->retain(); break; }
            }
            iter->release();
        }
    }
    if (!_usbIface) {
        IOLog("rtw88: no USB interface found\n");
        return false;
    }
    _usbIface->open(this);

    /* Find bulk endpoints using MacKernelSDK StandardUSB helpers */
    {
        const StandardUSB::EndpointDescriptor *ep = nullptr;
        while ((ep = (const StandardUSB::EndpointDescriptor *)
                StandardUSB::getNextAssociatedDescriptorWithType(
                    _usbIface->getConfigurationDescriptor(),
                    _usbIface->getInterfaceDescriptor(),
                    ep, kDescriptorTypeEndpoint)) != nullptr) {
            uint8_t dir  = StandardUSB::getEndpointDirection(ep);
            uint8_t type = StandardUSB::getEndpointType(ep);
            uint8_t addr = StandardUSB::getEndpointAddress(ep);
            if (type == kIOUSBEndpointTypeBulk) {
                if (dir == kIOUSBEndpointDirectionIn  && !_bulkIn)
                    _bulkIn  = _usbIface->copyPipe(addr);
                if (dir == kIOUSBEndpointDirectionOut && !_bulkOut)
                    _bulkOut = _usbIface->copyPipe(addr);
            }
        }
    }
    if (!_bulkIn || !_bulkOut) {
        IOLog("rtw88: missing bulk endpoints\n");
        return false;
    }

    /* Install global ops */
    g_usb_dev      = this;
    rtw88_usb_io_ops = &_usb_ops;
    rtw88_compat_init();

    /* Build compat usb_interface */
    _compatIface = (struct usb_interface *)IOMallocZero(sizeof(*_compatIface));
    if (!_compatIface) return false;

    struct usb_device *udev = (struct usb_device *)IOMallocZero(sizeof(*udev));
    if (!udev) return false;
    udev->idVendor  = _usbDev->getDeviceDescriptor()->idVendor;
    udev->idProduct = _usbDev->getDeviceDescriptor()->idProduct;
    udev->kext_dev  = this;
    _compatIface->usb_dev = udev;

    /* Create 802.11 state machine; pass a fake pci_dev (USB has none) */
    _ieee80211 = RTW88IEEE80211::create(
        reinterpret_cast<RTW88PCIDevice *>(this), nullptr);
    if (!_ieee80211) return false;

    /* Workloop */
    _workLoop = IOWorkLoop::workLoop();
    _cmdGate  = IOCommandGate::commandGate(this);
    if (!_workLoop || !_cmdGate) return false;
    _workLoop->addEventSource(_cmdGate);

    /* Attach Ethernet interface */
    if (!attachInterface((IONetworkInterface **)&_iface)) return false;
    setupMediumDict();
    _iface->registerService();

    IOLog("rtw88: USB device started\n");
    return true;
}

void RTW88USBDevice::stop(IOService *provider)
{
    teardown();
    super::stop(provider);
}

void RTW88USBDevice::free()
{
    super::free();
}

void RTW88USBDevice::teardown()
{
    if (_enabled) disable(_iface);
    if (_ieee80211) { _ieee80211->stop(); _ieee80211->release(); _ieee80211 = nullptr; }
    rtw88_compat_exit();
    if (_compatIface) {
        if (_compatIface->usb_dev) IOFree(_compatIface->usb_dev, sizeof(struct usb_device));
        IOFree(_compatIface, sizeof(*_compatIface));
        _compatIface = nullptr;
    }
    if (_bulkIn)   { _bulkIn->abort();   _bulkIn->release();   _bulkIn = nullptr; }
    if (_bulkOut)  { _bulkOut->abort();  _bulkOut->release();  _bulkOut = nullptr; }
    if (_cmdGate)  { _workLoop->removeEventSource(_cmdGate); _cmdGate->release(); _cmdGate = nullptr; }
    if (_iface)    { detachInterface(_iface); _iface->release(); _iface = nullptr; }
    if (_workLoop) { _workLoop->release(); _workLoop = nullptr; }
    if (_usbIface) { _usbIface->close(this); _usbIface->release(); _usbIface = nullptr; }
    if (_usbDev)   { _usbDev->close(this);   _usbDev->release();   _usbDev = nullptr; }
    g_usb_dev = nullptr;
    rtw88_usb_io_ops = nullptr;
}

/* ------------------------------------------------------------------ */
/*  IOEthernetController                                                */
/* ------------------------------------------------------------------ */

IOReturn RTW88USBDevice::enable(IONetworkInterface *iface)
{
    if (_enabled) return kIOReturnSuccess;
    IOReturn ret = _ieee80211->start();
    if (ret == kIOReturnSuccess) _enabled = true;
    return ret;
}

IOReturn RTW88USBDevice::disable(IONetworkInterface *iface)
{
    if (!_enabled) return kIOReturnSuccess;
    _enabled = false;
    if (_ieee80211) _ieee80211->stop();
    return kIOReturnSuccess;
}

UInt32 RTW88USBDevice::outputPacket(mbuf_t m, void *param)
{
    if (!_enabled || !_ieee80211) { mbuf_freem(m); return kIOReturnOutputDropped; }
    return _ieee80211->outputPacket(m);
}

IOReturn RTW88USBDevice::getHardwareAddress(IOEthernetAddress *addr)
{
    if (!_ieee80211) return kIOReturnNotReady;
    _ieee80211->getMACAddress(addr->bytes);
    return kIOReturnSuccess;
}

IOReturn RTW88USBDevice::setHardwareAddress(const IOEthernetAddress *addr)
{
    memcpy(_macAddr.bytes, addr->bytes, 6);
    return kIOReturnSuccess;
}

IOReturn RTW88USBDevice::getMaxPacketSize(UInt32 *maxSize) const
{
    *maxSize = 2346;
    return kIOReturnSuccess;
}

IOReturn RTW88USBDevice::setMulticastMode(bool active)   { return kIOReturnSuccess; }
IOReturn RTW88USBDevice::setMulticastList(IOEthernetAddress *a, UInt32 n) { return kIOReturnSuccess; }
IOReturn RTW88USBDevice::setPromiscuousMode(bool active) { return kIOReturnSuccess; }

IOReturn RTW88USBDevice::getPacketFilters(const OSSymbol *group,
                                            UInt32 *filters) const
{
    if (group->isEqualTo(kIOEthernetWakeOnLANFilterGroup)) {
        *filters = 0; return kIOReturnSuccess;
    }
    return super::getPacketFilters(group, filters);
}

bool RTW88USBDevice::configureInterface(IONetworkInterface *iface)
{
    return super::configureInterface(iface);
}

IOReturn RTW88USBDevice::selectMedium(const IONetworkMedium *medium)
{
    setCurrentMedium(medium);
    return kIOReturnSuccess;
}

void RTW88USBDevice::injectRxFrame(mbuf_t m)
{
    if (_iface && _enabled) _iface->inputPacket(m);
    else mbuf_freem(m);
}

/* ------------------------------------------------------------------ */
/*  Medium dict                                                         */
/* ------------------------------------------------------------------ */

bool RTW88USBDevice::setupMediumDict()
{
    OSDictionary *mediums = OSDictionary::withCapacity(2);
    if (!mediums) return false;
    addMedium(mediums, kIOMediumEthernetAuto, 0);
    addMedium(mediums, kIOMediumEthernet1000BaseT | kIOMediumOptionFullDuplex, 1000);
    setProperty(kIOMediumDictionary, mediums);
    mediums->release();

    IONetworkMedium *auto_m = IONetworkMedium::getMediumWithType(
        OSDynamicCast(OSDictionary, getProperty(kIOMediumDictionary)),
        kIOMediumEthernetAuto);
    setCurrentMedium(auto_m);
    setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, auto_m);
    return true;
}

void RTW88USBDevice::addMedium(OSDictionary *dict, IOMediumType type, UInt64 speed)
{
    IONetworkMedium *m = IONetworkMedium::medium(type, speed * 1000000ULL);
    if (m) { IONetworkMedium::addMedium(dict, m); m->release(); }
}

/* ------------------------------------------------------------------ */
/*  USB bulk transfer                                                   */
/* ------------------------------------------------------------------ */

int RTW88USBDevice::usbBulkOut(uint8_t ep, void *buf, int len, int timeout_ms)
{
    if (!_bulkOut) return -EIO;
    IOMemoryDescriptor *md = IOMemoryDescriptor::withAddress(buf, len,
                                                              kIODirectionOut);
    if (!md) return -ENOMEM;
    md->prepare(kIODirectionOut);
    uint32_t transferred = 0;
    IOReturn ret = _bulkOut->io(md, (uint32_t)len, transferred,
                                 (uint32_t)timeout_ms);
    md->complete(kIODirectionOut);
    md->release();
    if (ret != kIOReturnSuccess) return -EIO;
    return (int)transferred;
}

int RTW88USBDevice::usbBulkIn(uint8_t ep, void *buf, int len, int timeout_ms)
{
    if (!_bulkIn) return -EIO;
    IOMemoryDescriptor *md = IOMemoryDescriptor::withAddress(buf, len,
                                                              kIODirectionIn);
    if (!md) return -ENOMEM;
    md->prepare(kIODirectionIn);
    uint32_t transferred = 0;
    IOReturn ret = _bulkIn->io(md, (uint32_t)len, transferred,
                                (uint32_t)timeout_ms);
    md->complete(kIODirectionIn);
    md->release();
    if (ret != kIOReturnSuccess) return -EIO;
    return (int)transferred;
}

int RTW88USBDevice::usbCtrl(uint8_t reqType, uint8_t req,
                               uint16_t value, uint16_t index,
                               void *buf, uint16_t size, int timeout_ms)
{
    if (!_usbDev) return -EIO;
    StandardUSB::DeviceRequest request = {};
    request.bmRequestType = reqType;
    request.bRequest      = req;
    request.wValue        = USBToHost16(value);
    request.wIndex        = USBToHost16(index);
    request.wLength       = USBToHost16(size);

    uint32_t transferred  = 0;
    IOReturn ret = _usbDev->deviceRequest(this, request, buf, transferred,
                                           (uint32_t)timeout_ms);
    if (ret != kIOReturnSuccess) return -EIO;
    return (int)transferred;
}

/* ------------------------------------------------------------------ */
/*  DMA (USB: system memory, no hardware constraints)                  */
/* ------------------------------------------------------------------ */

void *RTW88USBDevice::allocCoherent(size_t size, IOPhysicalAddress *phys)
{
    IOBufferMemoryDescriptor *desc = IOBufferMemoryDescriptor::withOptions(
        kIOMemoryKernelUserShared | kIODirectionInOut,
        size, PAGE_SIZE);
    if (!desc) return nullptr;
    desc->prepare();
    if (phys) *phys = desc->getPhysicalAddress();
    return desc->getBytesNoCopy();
}

void RTW88USBDevice::freeCoherent(size_t size, void *virt, IOPhysicalAddress phys)
{
    /* Memory is small; leak is acceptable; proper impl tracks descriptors like PCI */
}

/* ------------------------------------------------------------------ */
/*  IOUserClient                                                        */
/* ------------------------------------------------------------------ */

IOReturn RTW88USBDevice::newUserClient(task_t owningTask, void *securityID,
                                        UInt32 type, IOUserClient **handler)
{
    RTW88UserClient *client = RTW88UserClient::create(
        reinterpret_cast<RTW88PCIDevice *>(this), owningTask);
    if (!client) return kIOReturnNoMemory;
    if (!client->attach(this) || !client->start(this)) {
        client->detach(this); client->release();
        return kIOReturnError;
    }
    *handler = client;
    return kIOReturnSuccess;
}
