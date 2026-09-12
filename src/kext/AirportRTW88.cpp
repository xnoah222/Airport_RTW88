/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Native IO80211 controller for the RTL8822BE port.
 */
#include "AirportRTW88.hpp"
#include "AirportRTW88Interface.hpp"
#include "RTW88UserClient.hpp"

extern "C" {
#include "../compat/rtw88_compat.h"
}
extern "C" boolean_t preemption_enabled(void);

extern "C" void rtw88_trigger_interrupt(void);  /* esta si es la unica con extern "C" real, ver RTW88PCIDevice.cpp */  /* struct RTW88StateResult vive aca */
#include <IOKit/network/IOOutputQueue.h>  /* kIOReturnOutputDropped */

#define super IO80211Controller

OSDefineMetaClassAndStructors(AirportRTW88, IO80211Controller)

bool AirportRTW88::init(OSDictionary *props)
{
    return super::init(props);
}

/* Match AirportItlwm's IO80211Controller lifecycle.  IONetworkController::
 * start() calls this virtual before AirportRTW88::start() continues; using an
 * IO80211WorkLoop keeps controller IOCTL/VIF callbacks, our interrupt source
 * and RX injection on the same controller-owned workloop. */
bool AirportRTW88::createWorkLoop()
{
    if (_workLoop)
        return true;
    _workLoop = IO80211WorkLoop::workLoop();
    IOLog("AirPort_RTW88: createWorkLoop IO80211WorkLoop=%p\n", _workLoop);
    return _workLoop != nullptr;
}

IOWorkLoop *AirportRTW88::getWorkLoop() const
{
    return _workLoop;
}

bool AirportRTW88::start(IOService *provider)
{
    IOLog("AirportRTW88: start\n");
    _pciDev = OSDynamicCast(IOPCIDevice, provider);
    if (!_pciDev) {
        IOLog("AirportRTW88: provider is not IOPCIDevice\n");
        return false;
    }
    if (!super::start(provider)) {
        _pciDev = nullptr;
        return false;
    }
    _superStarted = true;
    _pciDev->retain();

    /* Locks para tracking DMA -- antes faltaban por completo */
    _dmaLock = IOSimpleLockAlloc();
    _pendingFreeLock = IOSimpleLockAlloc();
    if (!_dmaLock || !_pendingFreeLock)
        return failStart(provider, "failed to allocate DMA locks");

    /* Habilitar bus mastering y espacio de memoria */
    _pciDev->setBusMasterEnable(true);
    _pciDev->setMemoryEnable(true);

    /* Mapear BAR2 -- rtw88 hardcodea bar_id=2 en pci.c */
    _mmioMap = _pciDev->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if (!_mmioMap) {
        return failStart(provider, "failed to map BAR2");
    }
    _mmioBase = (volatile void *)_mmioMap->getVirtualAddress();
    IOLog("AirportRTW88: BAR2 mapped at %p, size 0x%llx\n",
          (void *)_mmioBase, (unsigned long long)_mmioMap->getLength());

    /* Instalar callbacks compartidos de compat (mismos que usa RTW88PCIDevice,
     * ahora apuntando a esta instancia via el puntero generico RTW88HwOps*) */
    g_pci_dev_instance = this;
    rtw88_pci_io_ops    = &_pci_io_ops;
    rtw88_dma_ops       = &_dma_ops;

    if (rtw88_compat_init() != 0)
        return failStart(provider, "compat initialization failed");
    _compatInitialized = true;

    _compatPciDev = (struct pci_dev *)IOMallocZero(sizeof(struct pci_dev));
    if (!_compatPciDev)
        return failStart(provider, "failed to allocate compat PCI device");

    _compatPciDev->vendor  = _pciDev->configRead16(0x00);
    _compatPciDev->device  = _pciDev->configRead16(0x02);
    _compatPciDev->kext_dev = this;
    _compatPciDev->resource[2]     = (resource_size_t)_mmioBase;
    _compatPciDev->resource_len[2] = (resource_size_t)_mmioMap->getLength();

    IOLog("AirportRTW88: PCI device %04x:%04x\n",
          _compatPciDev->vendor, _compatPciDev->device);

    /* super::start() has already called our createWorkLoop().  AirportItlwm
     * relies on that controller-owned IO80211WorkLoop; do not create a second
     * parallel loop here. */
    if (!_workLoop)
        return failStart(provider, "IO80211 workloop unavailable after super::start");

    if (!setupInterrupt()) {
        return failStart(provider, "failed to set up interrupt");
    }

    rtw88_find_fw_dir();

    /* Crear la maquina de estados 802.11 -- AHORA con el pci_dev real,
     * en vez de nullptr como antes */
    _ieee80211 = RTW88IEEE80211::createAirport(this, _compatPciDev);
    if (!_ieee80211) {
        return failStart(provider, "failed to create IEEE80211 state machine");
    }
    _ieee80211->setEventDelegate(this);
    _ieee80211->setRxDelegate(this);

    rtw88_force_wifi_only();

    IOReturn probeRet = _ieee80211->start();
    if (probeRet != kIOReturnSuccess) {
        IOLog("AirportRTW88: probe failed (0x%08x)\n", probeRet);
        return failStart(provider, "IEEE80211 start failed");
    }
    _ieeeStarted = true;

    _awdlManager = new RTW88AWDLManager;
    if (!_awdlManager || !_awdlManager->init(_ieee80211, _workLoop))
        return failStart(provider, "failed to initialize AWDL/P2P manager");
    IOLog("AirPort_RTW88: AWDL/P2P manager initialized (1.0.1 partial support)\n");

    /* Let IO80211/IONetworkController prepare and configure the client.
     * Calling init/attach directly bypasses the controller's lifecycle. */
    IOLog("AirportRTW88: calling attachInterface (AirportItlwm lifecycle, attach=true)\n");
    /* AirportItlwm passes its member pointer directly.  This matters when
     * attach=true because matching/registration can be synchronous: make the
     * primary interface visible to our callbacks before attachInterface()
     * returns instead of assigning it afterwards from a temporary. */
    if (!attachInterface((IONetworkInterface **)&_netif, true) || !_netif)
        return failStart(provider, "controller attachInterface failed");
    if (!OSDynamicCast(AirportRTW88Interface, _netif)) {
        detachInterface(_netif, true);
        _netif->release();
        _netif = nullptr;
        return failStart(provider, "controller returned unexpected interface type");
    }
    _netifAttached = true;

    /* NOTA: se omite rtw88_set_tx_resume_cb -- esa optimizacion depende de
     * _txQueue (IOBasicOutputQueue), especifico de IOEthernetController.
     * AirportRTW88 usa la cola de salida de IO80211Controller; sin esto
     * el peor caso es que un TX stall se resuelva por timeout natural
     * en vez de al instante, no rompe funcionalidad basica. */
    if (_intrSrc) _intrSrc->enable();

    /* AirportItlwm publishes a valid-but-not-yet-associated link, then both
     * the controller service and the interface.  The controller publication
     * is important for IO80211 clients that own virtual-interface lifecycle. */
    IO80211Controller::setLinkStatus(kIONetworkLinkValid);
    registerService();
    _netif->registerService();
    IOLog("AirPort_RTW88: controller and network interface registered\n");

    /* Ventura no longer drives the old selector-94 VIF creation sequence.
     * Ask IO80211 itself to attach the AWDL role once the primary service is
     * registered. Failure is non-fatal so a VIF quirk can never break STA. */
    if (!ensureAWDLVirtualInterface())
        IOLog("AirPort_RTW88: AWDL VIF was not attached; STA remains available\n");

    IOLog("AirportRTW88: device started successfully\n");
    return true;
}

bool AirportRTW88::failStart(IOService *provider, const char *reason)
{
    IOLog("AirportRTW88: start failed: %s\n", reason ? reason : "unknown error");

    /* Match AirportItlwm/IONetworkController ownership ordering: IO80211 must
     * stop while the controller workloop, interface and backend still exist.
     * Releasing those first can leave super::stop() touching freed state. */
    if (_superStarted) {
        super::stop(provider);
        _superStarted = false;
    }
    teardown();
    return false;
}

bool AirportRTW88::setupInterrupt()
{
    _intrSrc = IOInterruptEventSource::interruptEventSource(
        this,
        OSMemberFunctionCast(IOInterruptEventSource::Action,
                             this, &AirportRTW88::handleInterrupt),
        _pciDev, 0);
    if (!_intrSrc) {
        IOLog("AirportRTW88: failed to create interrupt event source\n");
        return false;
    }
    if (_workLoop->addEventSource(_intrSrc) != kIOReturnSuccess) {
        _intrSrc->release();
        _intrSrc = nullptr;
        return false;
    }
    return true;
}

void AirportRTW88::handleInterrupt(IOInterruptEventSource *src, int count)
{
    if (_ieee80211) rtw88_trigger_interrupt();
}

UInt8 AirportRTW88::pciReadByte(int offset)   { return _pciDev->configRead8((UInt8)offset); }
UInt16 AirportRTW88::pciReadWord(int offset)  { return _pciDev->configRead16((UInt8)offset); }
UInt32 AirportRTW88::pciReadDword(int offset) { return _pciDev->configRead32((UInt8)offset); }
void AirportRTW88::pciWriteByte(int offset, UInt8 val)   { _pciDev->configWrite8((UInt8)offset, val); }
void AirportRTW88::pciWriteWord(int offset, UInt16 val)  { _pciDev->configWrite16((UInt8)offset, val); }
void AirportRTW88::pciWriteDword(int offset, UInt32 val) { _pciDev->configWrite32((UInt8)offset, val); }

int AirportRTW88::pciFindCapability(int cap)
{
    if (!_pciDev || cap < 0 || cap > 0xff)
        return 0;

    UInt8 offset = 0;
    UInt32 value = _pciDev->findPCICapability((UInt8)cap, &offset);
    return value ? (int)offset : 0;
}

void *AirportRTW88::allocCoherent(size_t size, IOPhysicalAddress *phys)
{
    IOBufferMemoryDescriptor *desc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIOMemoryPhysicallyContiguous | kIODirectionInOut | kIOMemoryKernelUserShared,
        size,
        0x00000000FFFFFFF0ULL);
    if (!desc) { IOLog("AirportRTW88: dma alloc failed, size=%zu\n", size); return nullptr; }
    if (desc->prepare() != kIOReturnSuccess) { desc->release(); return nullptr; }

    IOPhysicalAddress pa = desc->getPhysicalAddress();
    void *va = desc->getBytesNoCopy();
    if (!va || !pa) { desc->complete(); desc->release(); return nullptr; }
    memset(va, 0, size);
    if (phys) *phys = pa;

    RTW88DMAEntry *entry = (RTW88DMAEntry *)IOMallocZero(sizeof(RTW88DMAEntry));
    if (!entry) { desc->complete(); desc->release(); return nullptr; }
    entry->desc = desc; entry->virt = va; entry->phys = pa; entry->size = size;

    IOSimpleLockLock(_dmaLock);
    entry->next = _dmaList;
    _dmaList = entry;
    IOSimpleLockUnlock(_dmaLock);
    return va;
}

void AirportRTW88::freeCoherent(size_t size, void *virt, IOPhysicalAddress phys)
{
    IOSimpleLockLock(_dmaLock);
    RTW88DMAEntry **prev = &_dmaList;
    for (RTW88DMAEntry *e = _dmaList; e; e = e->next) {
        if (e->virt == virt) {
            *prev = e->next;
            IOSimpleLockUnlock(_dmaLock);
            if (preemption_enabled()) {
                e->desc->complete(); e->desc->release(); IOFree(e, sizeof(*e));
            } else {
                IOSimpleLockLock(_pendingFreeLock);
                e->next = _dmaPendingFree; _dmaPendingFree = e;
                IOSimpleLockUnlock(_pendingFreeLock);
            }
            return;
        }
        prev = &e->next;
    }
    IOSimpleLockUnlock(_dmaLock);
    IOLog("AirportRTW88: freeCoherent: virt %p not found\n", virt);
}

void AirportRTW88::freeCoherentByPhys(IOPhysicalAddress phys)
{
    IOSimpleLockLock(_dmaLock);
    RTW88DMAEntry **prev = &_dmaList;
    for (RTW88DMAEntry *e = _dmaList; e; e = e->next) {
        if (e->phys == phys) {
            *prev = e->next;
            IOSimpleLockUnlock(_dmaLock);
            if (preemption_enabled()) {
                e->desc->complete(); e->desc->release(); IOFree(e, sizeof(*e));
            } else {
                IOSimpleLockLock(_pendingFreeLock);
                e->next = _dmaPendingFree; _dmaPendingFree = e;
                IOSimpleLockUnlock(_pendingFreeLock);
            }
            return;
        }
        prev = &e->next;
    }
    IOSimpleLockUnlock(_dmaLock);
}

void AirportRTW88::setBounceOrigVA(IOPhysicalAddress phys, void *orig_va)
{
    IOSimpleLockLock(_dmaLock);
    for (RTW88DMAEntry *e = _dmaList; e; e = e->next) {
        if (e->phys == phys) { e->orig_va = orig_va; break; }
    }
    IOSimpleLockUnlock(_dmaLock);
}

void AirportRTW88::syncBounceForCpu(IOPhysicalAddress dma, size_t size)
{
    IOSimpleLockLock(_dmaLock);
    for (RTW88DMAEntry *e = _dmaList; e; e = e->next) {
        if (e->phys == dma && e->orig_va && e->virt) {
            size_t copy_len = (size <= e->size) ? size : e->size;
            IOSimpleLockUnlock(_dmaLock);
            memcpy(e->orig_va, e->virt, copy_len);
            return;
        }
    }
    IOSimpleLockUnlock(_dmaLock);
}

void AirportRTW88::resumeTxIfStalled()
{
    /* AirportRTW88 no usa IOBasicOutputQueue (eso es de IOEthernetController);
     * IO80211Controller maneja su propia cola. Sin efecto propio por ahora --
     * peor caso, un stall se resuelve por timeout natural en vez de al instante. */
}

void AirportRTW88::drainPendingFree()
{
    if (!_pendingFreeLock) return;
    IOSimpleLockLock(_pendingFreeLock);
    RTW88DMAEntry *list = _dmaPendingFree;
    _dmaPendingFree = nullptr;
    IOSimpleLockUnlock(_pendingFreeLock);
    for (RTW88DMAEntry *e = list; e; ) {
        RTW88DMAEntry *next = e->next;
        e->desc->complete(); e->desc->release(); IOFree(e, sizeof(*e));
        e = next;
    }
}

void AirportRTW88::releaseDMAEntries()
{
    if (!_dmaLock) return;

    IOSimpleLockLock(_dmaLock);
    RTW88DMAEntry *list = _dmaList;
    _dmaList = nullptr;
    IOSimpleLockUnlock(_dmaLock);

    for (RTW88DMAEntry *entry = list; entry; ) {
        RTW88DMAEntry *next = entry->next;
        if (entry->desc) {
            entry->desc->complete();
            entry->desc->release();
        }
        IOFree(entry, sizeof(*entry));
        entry = next;
    }
}

void AirportRTW88::teardown()
{
    if (_intrSrc)
        _intrSrc->disable();

    if (_awdlManager) {
        _awdlManager->reset();
        delete _awdlManager;
        _awdlManager = nullptr;
    }

    if (_netif) {
        if (_netifAttached)
            detachInterface(_netif, true);
        _netifAttached = false;
        _netif->release();
        _netif = nullptr;
    }

    if (_ieee80211) {
        if (_ieeeStarted)
            _ieee80211->stop();
        _ieeeStarted = false;
        _ieee80211->release();
        _ieee80211 = nullptr;
    }

    drainPendingFree();
    releaseDMAEntries();

    if (_compatInitialized) {
        rtw88_compat_exit();
        _compatInitialized = false;
    }

    if (_intrSrc) {
        if (_workLoop)
            _workLoop->removeEventSource(_intrSrc);
        _intrSrc->release();
        _intrSrc = nullptr;
    }
    if (_workLoop) {
        _workLoop->release();
        _workLoop = nullptr;
    }
    if (_compatPciDev) {
        IOFree(_compatPciDev, sizeof(*_compatPciDev));
        _compatPciDev = nullptr;
    }
    if (_mmioMap) {
        _mmioMap->release();
        _mmioMap = nullptr;
        _mmioBase = nullptr;
    }

    if (g_pci_dev_instance == this) {
        g_pci_dev_instance = nullptr;
        rtw88_pci_io_ops = nullptr;
        rtw88_dma_ops = nullptr;
    }

    if (_pciDev) {
        _pciDev->setBusMasterEnable(false);
        _pciDev->release();
        _pciDev = nullptr;
    }

    if (_pendingFreeLock) {
        IOSimpleLockFree(_pendingFreeLock);
        _pendingFreeLock = nullptr;
    }
    if (_dmaLock) {
        IOSimpleLockFree(_dmaLock);
        _dmaLock = nullptr;
    }
}

void AirportRTW88::stop(IOService *provider)
{
    /* AirportItlwm calls IO80211Controller::stop() before releasing its
     * workloop/HAL/interface state. Keep the same ordering here so superclass
     * teardown cannot observe a freed IO80211WorkLoop or Realtek backend. */
    if (_superStarted) {
        super::stop(provider);
        _superStarted = false;
    }
    teardown();
}

void AirportRTW88::free()
{
    teardown();
    super::free();
}

const OSString *AirportRTW88::newVendorString() const { return OSString::withCString("Realtek"); }
const OSString *AirportRTW88::newModelString() const  { return OSString::withCString("802.11ac"); }

IOReturn AirportRTW88::enable(IONetworkInterface *iface)
{
    if (!_ieee80211) return kIOReturnNotReady;
    /* Ventura can enable its queues without issuing POWER ON. Ensure the
     * hardware is running before handing interface state to IO80211. */
    IOReturn ret = _ieee80211->powerOn();
    if (ret != kIOReturnSuccess) return ret;
    ret = super::enable(iface);
    if (ret != kIOReturnSuccess) _ieee80211->powerOff();
    IOLog("AirportRTW88: IO80211 enable result=0x%x\n", ret);
    return ret;
}

IOReturn AirportRTW88::disable(IONetworkInterface *iface)
{
    IOReturn ret = super::disable(iface);
    IOLog("AirportRTW88: IO80211 disable result=0x%x\n", ret);
    return ret;
}

IOReturn AirportRTW88::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    if (!group || !filters) return kIOReturnBadArgument;
    *filters = group->isEqualTo(gIONetworkFilterGroup) ?
        kIOPacketFilterUnicast | kIOPacketFilterBroadcast | kIOPacketFilterMulticast : 0;
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::setMulticastMode(bool active)
{
    IOReturn ret = _ieee80211 ? _ieee80211->setReceiveMulticast(active) : kIOReturnNotReady;
    IOLog("AirportRTW88: multicast active=%d result=0x%x\n", active, ret);
    return ret;
}

IOReturn AirportRTW88::setMulticastList(IOEthernetAddress *addrs, UInt32 count)
{
    if (count && !addrs) return kIOReturnBadArgument;
    /* rtw88 configure_filter supports all-multicast, not an address table.
     * Use that documented fallback; the network stack handles membership. */
    return setMulticastMode(count != 0);
}

IOReturn AirportRTW88::setPromiscuousMode(bool active)
{
    /* Promiscuous reception is not advertised or enabled by this driver. */
    return active ? kIOReturnUnsupported : kIOReturnSuccess;
}

IONetworkInterface *AirportRTW88::createInterface()
{
    IOLog("AirportRTW88: createInterface entered (controller workloop=%p)\n",
          getWorkLoop());
    AirportRTW88Interface *interface = OSTypeAlloc(AirportRTW88Interface);
    if (!interface) {
        IOLog("AirportRTW88: createInterface allocation failed\n");
        return nullptr;
    }
    if (!interface->init(this)) {
        IOLog("AirportRTW88: createInterface IO80211Interface::init failed\n");
        interface->release();
        return nullptr;
    }
    IOLog("AirportRTW88: createInterface initialized\n");
    return interface;
}

bool AirportRTW88::configureInterface(IONetworkInterface *iface)
{
    if (!super::configureInterface(iface)) return false;
    OSDictionary *media = OSDictionary::withCapacity(1);
    if (!media) return false;
    IONetworkMedium *automatic = IONetworkMedium::medium(kIOMediumIEEE80211Auto, 0);
    bool ok = automatic && IONetworkMedium::addMedium(media, automatic);
    if (ok) ok = publishMediumDictionary(media);
    if (ok) ok = setCurrentMedium(automatic);
    if (ok) ok = setSelectedMedium(automatic);
    if (automatic) automatic->release();
    media->release();
    IOLog("AirportRTW88: Wi-Fi automatic medium published=%d\n", ok);
    return ok;
}

IOReturn AirportRTW88::selectMedium(const IONetworkMedium *medium)
{
    if (!medium || medium->getType() != kIOMediumIEEE80211Auto)
        return kIOReturnUnsupported;
    return setSelectedMedium(medium) ? kIOReturnSuccess : kIOReturnError;
}

UInt32 AirportRTW88::getFeatures() const
{
    /* Preserve IONetworkController feature flags. IO80211-specific 802.11n
     * negotiation is handled by enableFeature(), as in AirportItlwm. */
    return super::getFeatures();
}

UInt32 AirportRTW88::outputPacket(mbuf_t m, void *param)
{
    if (_ieee80211) return _ieee80211->outputPacket(m);
    if (m) mbuf_freem(m);
    return kIOReturnOutputDropped;
}

IOReturn AirportRTW88::getHardwareAddress(IOEthernetAddress *addr)
{
    if (!addr || !_ieee80211) return kIOReturnNotReady;
    _ieee80211->getMACAddress(addr->bytes);
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::getHardwareAddressForInterface(IO80211Interface *iface,
                                                       IOEthernetAddress *addr)
{
    (void)iface;
    /* AirportItlwm maps the infrastructure-interface query to the physical
     * controller address.  Virtual interfaces get their own address through
     * IO80211's attachVirtualInterface lifecycle. */
    return getHardwareAddress(addr);
}

/* Native IO80211 request payloads; compared against AirportItlwm v2.3.0. */
SInt32 AirportRTW88::apple80211Request(unsigned int request_type,
                                        int request_number,
                                        IO80211Interface *interface,
                                        void *data)
{
    if (!_ieee80211) return kIOReturnNotReady;
    if (request_type != SIOCGA80211 && request_type != SIOCSA80211)
        return kIOReturnBadArgument;
    bool isSet = (request_type == SIOCSA80211);
    if (!data && request_number != APPLE80211_IOC_DISASSOCIATE)
        return kIOReturnBadArgument;
    IOLog("AirportRTW88: IOCTL %s selector=%d\n", isSet ? "SET" : "GET", request_number);

    switch (request_number) {
    case APPLE80211_IOC_SSID:
        return handleSSID(isSet, (struct apple80211_ssid_data *)data);

    case APPLE80211_IOC_AUTH_TYPE:
        return handleAUTH_TYPE(isSet, (struct apple80211_authtype_data *)data);

    case APPLE80211_IOC_ASSOCIATE:
        return isSet ? handleASSOCIATE((struct apple80211_assoc_data *)data) : kIOReturnUnsupported;

    case APPLE80211_IOC_RSN_IE:
        return handleRSN_IE(isSet, (struct apple80211_rsn_ie_data *)data);

    case APPLE80211_IOC_AP_IE_LIST:
        return isSet ? kIOReturnUnsupported : handleAP_IE_LIST((struct apple80211_ap_ie_data *)data);

    case APPLE80211_IOC_CIPHER_KEY:
        return isSet ? handleCIPHER_KEY((struct apple80211_key *)data) : kIOReturnUnsupported;

    case APPLE80211_IOC_SCAN_REQ:
        return isSet ? handleSCAN_REQ(data) : kIOReturnUnsupported;

    case APPLE80211_IOC_SCAN_REQ_MULTIPLE:
        /* Ventura may use the multi-SSID scan request even when one network is
         * requested.  The Realtek backend scans the full channel set, so the
         * request filters are advisory for now. */
        if (!isSet) return kIOReturnUnsupported;
        /* CoreWiFi can submit another scan while the hardware scan is still
         * running.  Do not surface EBUSY: keep the active scan and let its
         * SCAN_DONE satisfy the coalesced request. */
        if (_scanInProgress) {
            IOLog("AirPort_RTW88: SCAN_REQ_MULTIPLE coalesced with active scan\n");
            return kIOReturnSuccess;
        }
        {
            RTW88StateResult st = {};
            if (_ieee80211->cmdGetState(&st) == kIOReturnSuccess &&
                st.state == RTW88_STATE_CONNECTED) {
                /* Do not channel-hop a live STA.  The manual backend scan
                 * currently scans the complete channel table rather than the
                 * CoreWiFi-requested subset, which can keep us off-channel
                 * long enough for the AP to drop the association.  Satisfy
                 * connected background scans from the already-maintained BSS
                 * cache; disconnected scans still perform real RF scanning. */
                _scanCursor = 0;
                UInt32 result = 0;
                IOLog("AirPort_RTW88: SCAN_REQ_MULTIPLE connected cache-only completion\n");
                _netif->postMessage(APPLE80211_M_SCAN_DONE, &result, sizeof(result));
                return kIOReturnSuccess;
            }
        }
        _scanInProgress = true;
        _scanCursor = 0;
        {
            IOReturn ret = _ieee80211->cmdScan();
            if (ret) _scanInProgress = false;
            return ret;
        }

    case APPLE80211_IOC_SCANCACHE_CLEAR:
        /* Do not destroy the backend BSS tree while a scan may still be using
         * it.  Reset only the Apple-side enumeration cursor; the next hardware
         * scan refreshes/deduplicates entries by BSSID. */
        if (!isSet) return kIOReturnUnsupported;
        _scanCursor = 0;
        return kIOReturnSuccess;

    case APPLE80211_IOC_SCAN_RESULT:
        return isSet ? kIOReturnUnsupported : handleSCAN_RESULT((struct apple80211_scan_result **)data);

    case APPLE80211_IOC_RSSI: {
        if (isSet) return kIOReturnUnsupported;
        RTW88StateResult st = {};
        IOReturn ret = _ieee80211->cmdGetState(&st);
        if (ret) return ret;
        if (st.state != RTW88_STATE_CONNECTED) return 6;
        auto *d = static_cast<apple80211_rssi_data *>(data);
        bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION;
        d->num_radios = 1; d->rssi_unit = APPLE80211_UNIT_DBM;
        d->rssi[0] = d->aggregate_rssi = st.rssi;
        d->rssi_ext[0] = d->aggregate_rssi_ext = st.rssi;
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_DISASSOCIATE:
        return isSet ? handleDISASSOCIATE() : kIOReturnUnsupported;
    case APPLE80211_IOC_OP_MODE: {
        if (isSet) return kIOReturnUnsupported;
        auto *d = static_cast<apple80211_opmode_data *>(data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->op_mode = APPLE80211_M_STA;
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_PHY_MODE: {
        if (isSet) return kIOReturnUnsupported;
        auto *d = static_cast<apple80211_phymode_data *>(data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->phy_mode = APPLE80211_MODE_11A | APPLE80211_MODE_11B |
            APPLE80211_MODE_11G | APPLE80211_MODE_11N | APPLE80211_MODE_11AC;
        d->active_phy_mode = APPLE80211_MODE_AUTO;
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_CARD_CAPABILITIES: {
        if (isSet) return kIOReturnUnsupported;
        auto *d = static_cast<apple80211_capability_data *>(data);
        bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION;

        /* Follow current AirportItlwm's legacy IO80211 capability layout.
         * Keep the low feature bits truthful for rtw88, then use the exact
         * opaque high-byte pattern AirportItlwm currently exposes. Do not
         * invent extra AWDL bits: IO80211 probes the virtual-interface path
         * separately. */
        const unsigned caps[] = {
            APPLE80211_CAP_TKIP, APPLE80211_CAP_AES_CCM,
            APPLE80211_CAP_WPA2, APPLE80211_CAP_TKIPMIC,
            APPLE80211_CAP_SHSLOT, APPLE80211_CAP_SHPREAMBLE
        };
        for (unsigned cap : caps)
            d->capabilities[cap / 8] |= 1U << (cap % 8);

        /* AirportItlwm master (Ventura legacy IO80211 path) publishes these
         * high bytes. Keep them byte-for-byte aligned with the reference; the
         * previous 1.0.1 draft had drifted to a different experimental mask. */
        d->capabilities[2] = 0xFF;
        d->capabilities[3] = 0x2B;
        d->capabilities[4] = 0xAD;
        d->capabilities[5] = 0x8C;
        d->capabilities[6] = 0x8C;
        d->capabilities[7] = 0x84;
        IOLog("AirPort_RTW88: CARD_CAPABILITIES AirportItlwm master profile advertised\n");
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_BSSID: {
        if (isSet) return kIOReturnSuccess;
        RTW88StateResult st = {}; IOReturn ret = _ieee80211->cmdGetState(&st);
        if (ret) return ret;
        // AirportItlwm returns BSD ENXIO (6) until associated. Success with
        // an empty BSSID can make airportd treat an idle interface as joined.
        if (st.state != RTW88_STATE_CONNECTED) return 6;
        auto *d = static_cast<apple80211_bssid_data *>(data);
        bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION;
        memcpy(d->bssid.octet, st.bssid, 6);
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_DEAUTH: {
        if (isSet) return kIOReturnUnsupported;
        auto *d = static_cast<apple80211_deauth_data *>(data);
        bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION;
        d->deauth_reason = _ieee80211->deauthReason();
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_ASSOCIATION_STATUS: {
        if (isSet) return kIOReturnUnsupported;
        RTW88StateResult st = {}; IOReturn ret = _ieee80211->cmdGetState(&st);
        if (ret) return ret;
        auto *d = static_cast<apple80211_assoc_status_data *>(data);
        bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION;
        // Query success is distinct from successful association (AirportItlwm).
        d->status = st.state == RTW88_STATE_CONNECTED ?
            APPLE80211_STATUS_SUCCESS : APPLE80211_STATUS_UNAVAILABLE;
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_ASSOCIATE_RESULT: {
        if (isSet) return kIOReturnUnsupported;
        RTW88StateResult st = {}; IOReturn ret = _ieee80211->cmdGetState(&st);
        if (ret) return ret;
        if (st.state != RTW88_STATE_CONNECTED) return kIOReturnNotReady;
        auto *d = static_cast<apple80211_assoc_result_data *>(data);
        d->version = APPLE80211_VERSION; d->result = APPLE80211_RESULT_SUCCESS;
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_SUPPORTED_CHANNELS:
    case APPLE80211_IOC_HW_SUPPORTED_CHANNELS: {
        if (isSet) return kIOReturnUnsupported;
        RTW88Channel channels[APPLE80211_MAX_CHANNELS] = {};
        uint32_t count = 0;
        IOReturn ret = _ieee80211->copyChannels(channels, APPLE80211_MAX_CHANNELS, &count);
        if (ret) return ret;
        auto *d = static_cast<apple80211_sup_channel_data *>(data);
        bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION; d->num_channels = count;
        for (uint32_t i = 0; i < count; i++) {
            d->supported_channels[i].version = APPLE80211_VERSION;
            d->supported_channels[i].channel = channels[i].number;
            d->supported_channels[i].flags = APPLE80211_C_FLAG_ACTIVE |
                APPLE80211_C_FLAG_20MHZ |
                (channels[i].number <= 14 ? APPLE80211_C_FLAG_2GHZ : APPLE80211_C_FLAG_5GHZ);
        }
        return kIOReturnSuccess;
    }
    case APPLE80211_IOC_DRIVER_VERSION:
    case APPLE80211_IOC_HARDWARE_VERSION: {
        if (isSet) return kIOReturnUnsupported;
        auto *d = static_cast<apple80211_version_data *>(data);
        bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION;
        const char *v = request_number == APPLE80211_IOC_DRIVER_VERSION ? "AirPort_RTW88 1.0.1" : "RTL8822BE";
        d->string_len = (uint16_t)strlcpy(d->string, v, sizeof(d->string));
        return kIOReturnSuccess;
    }


    case APPLE80211_IOC_VIRTUAL_IF_CREATE:
        return isSet ? handleVIRTUAL_IF_CREATE((struct apple80211_virt_if_create_data *)data)
                     : kIOReturnUnsupported;

    case APPLE80211_IOC_VIRTUAL_IF_DELETE:
        return isSet ? handleVIRTUAL_IF_DELETE((struct apple80211_virt_if_delete_data *)data)
                     : kIOReturnUnsupported;

    case APPLE80211_IOC_CURRENT_NETWORK:
        if (isSet) return kIOReturnUnsupported;
        IOLog("AirPort_RTW88: CURRENT_NETWORK dispatcher entry\n");
        return handleCURRENT_NETWORK(static_cast<apple80211_scan_result *>(data));

    case APPLE80211_IOC_STATE:
        return isSet ? kIOReturnUnsupported : handleSTATE((struct apple80211_state_data *)data);

    case APPLE80211_IOC_CHANNEL:
        return isSet ? kIOReturnUnsupported : handleCHANNEL((struct apple80211_channel_data *)data);

    case APPLE80211_IOC_RADIO_INFO: {
        if (isSet) return kIOReturnUnsupported;
        if (!data) return kIOReturnBadArgument;
        auto *radio = static_cast<apple80211_radio_info_data *>(data);
        bzero(radio, sizeof(*radio));
        radio->version = APPLE80211_VERSION;
        radio->count = 1;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_POWER:
        return handlePOWER(isSet, (struct apple80211_power_data *)data);

    case APPLE80211_IOC_NOISE: {
        if (isSet) return kIOReturnUnsupported;
        RTW88StateResult st = {};
        IOReturn ret = _ieee80211->cmdGetState(&st);
        if (ret) return ret;
        if (st.state != RTW88_STATE_CONNECTED) return 6;
        auto *d = static_cast<apple80211_noise_data *>(data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->num_radios = 1;
        d->noise_unit = APPLE80211_UNIT_DBM;
        d->noise[0] = d->aggregate_noise = -95;
        d->noise_ext[0] = d->aggregate_noise_ext = -95;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_INT_MIT: {
        if (isSet) return kIOReturnUnsupported;
        auto *d = static_cast<apple80211_intmit_data *>(data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->int_mit = APPLE80211_INT_MIT_AUTO;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_PROTMODE: {
        if (isSet) return kIOReturnUnsupported;
        RTW88StateResult st = {};
        IOReturn ret = _ieee80211->cmdGetState(&st);
        if (ret) return ret;
        if (st.state != RTW88_STATE_CONNECTED) return 6;
        auto *d = static_cast<apple80211_protmode_data *>(data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->protmode = APPLE80211_PROTMODE_OFF;
        d->threshold = 0;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_TXPOWER: {
        if (isSet) return kIOReturnUnsupported;
        RTW88StateResult st = {};
        IOReturn ret = _ieee80211->cmdGetState(&st);
        if (ret) return ret;
        if (st.state != RTW88_STATE_CONNECTED) return 6;
        auto *d = static_cast<apple80211_txpower_data *>(data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->txpower_unit = APPLE80211_UNIT_PERCENT;
        d->txpower = 100;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_POWERSAVE: {
        if (isSet) return kIOReturnUnsupported;
        auto *d = static_cast<apple80211_powersave_data *>(data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->powersave_level = APPLE80211_POWERSAVE_MODE_DISABLED;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_LOCALE: {
        if (isSet) return kIOReturnUnsupported;
        auto *d = static_cast<apple80211_locale_data *>(data);
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->locale = APPLE80211_LOCALE_ROW;
        return kIOReturnSuccess;
    }

    case APPLE80211_IOC_COUNTRY_CODE: {
        auto *d = static_cast<apple80211_country_code_data *>(data);
        if (isSet) {
            if (d->version != APPLE80211_VERSION) return kIOReturnBadArgument;
            /* Keep Apple's special x/X requests from replacing the usable code. */
            if (d->cc[0] != 'x' && d->cc[0] != 'X') {
                memcpy(_countryCode, d->cc, sizeof(_countryCode));
                _countryCode[APPLE80211_MAX_CC_LEN - 1] = 0;
                if (_netif) _netif->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED);
            }
            return kIOReturnSuccess;
        }
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        memcpy(d->cc, _countryCode, sizeof(d->cc));
        return kIOReturnSuccess;
    }

    default:
        return kIOReturnUnsupported;
    }
}

bool AirportRTW88::ensureAWDLVirtualInterface()
{
    if (!_awdlManager || !_netif)
        return false;
    if (_awdlManager->awdlInterface())
        return true;

    ether_addr addr = {};
    memcpy(addr.octet, _macAddr.bytes, sizeof(addr.octet));
    /* AWDL uses a distinct locally administered unicast address. Keep it
     * deterministic for this boot and distinct from en0. */
    addr.octet[0] = (uint8_t)((addr.octet[0] | 0x02u) & 0xFEu);
    addr.octet[5] ^= 0x80u;

    IO80211VirtualInterface *created = nullptr;
    IOLog("AirPort_RTW88: proactively attaching AWDL VIF mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
          addr.octet[0], addr.octet[1], addr.octet[2], addr.octet[3], addr.octet[4], addr.octet[5]);
    bool ok = attachVirtualInterface(&created, &addr, APPLE80211_VIF_AWDL, true);
    if (!ok || !created) {
        IOLog("AirPort_RTW88: proactive attachVirtualInterface(AWDL) failed\n");
        return false;
    }
    _awdlManager->setVirtualInterface(APPLE80211_VIF_AWDL, created);
    IOLog("AirPort_RTW88: proactive AWDL VIF attached bsd=%s role=%u\n",
          created->getBSDName() ? created->getBSDName() : "?",
          (unsigned)created->getInterfaceRole());

    /* Ventura can publish the BSD awdl0 object without driving the legacy
     * enableVirtualInterface callback.  In that state ifconfig shows awdl0
     * with mtu 0 / inactive and CoreWLAN still reports no usable VIF.  Drive
     * the controller's normal enable path once, explicitly, after attach.
     * This is intentionally non-fatal: STA must remain usable even if the
     * private IO80211 VIF lifecycle rejects the transition. */
    SInt32 enableRet = enableVirtualInterface(created);
    IOLog("AirPort_RTW88: proactive AWDL enable result=0x%x bsd=%s\n",
          (unsigned)enableRet, created->getBSDName() ? created->getBSDName() : "?");
    if (enableRet != kIOReturnSuccess) {
        /* Keep the object attached for diagnostics, but do not fake success:
         * a rejected superclass transition means we do not yet own a valid
         * data path. */
        return false;
    }

    return true;
}

IOReturn AirportRTW88::handleVIRTUAL_IF_CREATE(struct apple80211_virt_if_create_data *d)
{
    if (!d || d->version != APPLE80211_VERSION)
        return kIOReturnBadArgument;
    if (d->role < APPLE80211_VIF_P2P_DEVICE || d->role > APPLE80211_VIF_AWDL)
        return kIOReturnUnsupported;

    ether_addr addr = {};
    memcpy(addr.octet, d->mac, APPLE80211_ADDR_LEN);

    if (!_awdlManager)
        return kIOReturnNotReady;

    IO80211VirtualInterface *existing =
        d->role == APPLE80211_VIF_AWDL ? _awdlManager->awdlInterface()
                                        : _awdlManager->p2pInterface();

    if (existing) {
        const char *name = existing->getBSDName();
        bzero(d->bsd_name, sizeof(d->bsd_name));
        if (name) strlcpy((char *)d->bsd_name, name, sizeof(d->bsd_name));
        IOLog("AirPort_RTW88: VIRTUAL_IF_CREATE role=%u already exists bsd=%s\n",
              d->role, name ? name : "?");
        return kIOReturnSuccess;
    }

    IOLog("AirPort_RTW88: VIRTUAL_IF_CREATE role=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
          d->role, d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);

    /* Match AirportItlwm's lifecycle: let IO80211 perform the full
     * attach/configure/name sequence. This calls createVirtualInterface(),
     * then enableVirtualInterface(), and eventually yields p2p0/awdl0. */
    IO80211VirtualInterface *created = nullptr;
    if (!attachVirtualInterface(&created, &addr, d->role, true) || !created) {
        IOLog("AirPort_RTW88: attachVirtualInterface failed role=%u\n", d->role);
        return kIOReturnError;
    }
    _awdlManager->setVirtualInterface(d->role, created);

    const char *name = created->getBSDName();
    bzero(d->bsd_name, sizeof(d->bsd_name));
    if (name) strlcpy((char *)d->bsd_name, name, sizeof(d->bsd_name));

    IOLog("AirPort_RTW88: VIRTUAL_IF_CREATE success role=%u bsd=%s\n",
          d->role, name ? name : "?");
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handleVIRTUAL_IF_DELETE(struct apple80211_virt_if_delete_data *d)
{
    if (!d || d->version != APPLE80211_VERSION)
        return kIOReturnBadArgument;

    char requested[sizeof(d->bsd_name) + 1] = {};
    memcpy(requested, d->bsd_name, sizeof(d->bsd_name));

    IO80211VirtualInterface *target = nullptr;
    IO80211VirtualInterface *awdl = _awdlManager ? _awdlManager->awdlInterface() : nullptr;
    IO80211VirtualInterface *p2p  = _awdlManager ? _awdlManager->p2pInterface() : nullptr;
    if (awdl && awdl->getBSDName() &&
        strncmp(awdl->getBSDName(), requested, sizeof(d->bsd_name)) == 0)
        target = awdl;
    else if (p2p && p2p->getBSDName() &&
             strncmp(p2p->getBSDName(), requested, sizeof(d->bsd_name)) == 0)
        target = p2p;

    if (!target) {
        IOLog("AirPort_RTW88: VIRTUAL_IF_DELETE bsd=%s not found\n", requested);
        return kIOReturnNotFound;
    }

    UInt role = (UInt)target->getInterfaceRole();
    IOLog("AirPort_RTW88: VIRTUAL_IF_DELETE role=%u bsd=%s\n", role, requested);
    bool ok = detachVirtualInterface(target, true);
    if (ok) {
        if (_awdlManager) _awdlManager->clearVirtualInterface(target);
        return kIOReturnSuccess;
    }
    return kIOReturnError;
}

IOReturn AirportRTW88::handleSSID(bool set, struct apple80211_ssid_data *d)
{
    if (!d) return kIOReturnBadArgument;
    if (set) return kIOReturnSuccess; // AirportItlwm accepts this; ASSOCIATE carries the target.
    RTW88StateResult st = {}; IOReturn r = _ieee80211->cmdGetState(&st);
    if (r) return r;
    // Match the classic IO80211 contract: no current SSID before RUN.
    if (st.state != RTW88_STATE_CONNECTED) return 6;
    bzero(d, sizeof(*d)); d->version = APPLE80211_VERSION;
    d->ssid_len = (uint32_t)strnlen(st.ssid, sizeof(d->ssid_bytes));
    memcpy(d->ssid_bytes, st.ssid, d->ssid_len);
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handleAUTH_TYPE(bool set, struct apple80211_authtype_data *d)
{
    if (!d) return kIOReturnBadArgument;
    if (set) {
        if (d->version != APPLE80211_VERSION) return kIOReturnBadArgument;
        /* AirportItlwm stores the Apple auth request verbatim.  Do not reject
         * mixed WPA/WPA2 masks before the actual ASSOCIATE request arrives. */
        _authLower = d->authtype_lower;
        _authUpper = d->authtype_upper;
    } else {
        bzero(d, sizeof(*d));
        d->version = APPLE80211_VERSION;
        d->authtype_lower = _authLower;
        d->authtype_upper = _authUpper;
    }
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handleASSOCIATE(struct apple80211_assoc_data *d)
{
    if (!d || d->version != APPLE80211_VERSION ||
        !d->ad_ssid_len || d->ad_ssid_len > APPLE80211_MAX_SSID_LEN)
        return kIOReturnBadArgument;

    char ssid[APPLE80211_MAX_SSID_LEN + 1] = {};
    memcpy(ssid, d->ad_ssid, d->ad_ssid_len);

    const uint8_t emptyBssid[APPLE80211_ADDR_LEN] = {};
    const uint8_t *bssid = memcmp(d->ad_bssid.octet, emptyBssid,
                                  APPLE80211_ADDR_LEN) ? d->ad_bssid.octet : nullptr;

    /* Match AirportItlwm's Apple80211 contract: AUTH_TYPE and RSN IE from the
     * ASSOCIATE payload become the connection parameters.  Ventura's local
     * apple80211_assoc_data has no ad_rsn_ie_len, so derive the TLV length. */
    _authLower = d->ad_auth_lower;
    _authUpper = d->ad_auth_upper;

    if (d->ad_rsn_ie[0] == 48) {
        uint16_t rsnLen = (uint16_t)d->ad_rsn_ie[1] + 2;
        if (rsnLen <= APPLE80211_MAX_RSN_IE_LEN)
            _ieee80211->cmdSetAssocRsnIE(d->ad_rsn_ie, rsnLen);
    }

    const uint32_t personalMask =
        APPLE80211_AUTHTYPE_WPA_PSK |
        APPLE80211_AUTHTYPE_WPA2_PSK |
        APPLE80211_AUTHTYPE_SHA256_PSK;
    bool secure = (d->ad_auth_upper & personalMask) != 0;

    /* RTL backend currently implements WPA2/CCMP PSK.  Mixed WPA/WPA2 and
     * SHA256-PSK profiles are accepted when Apple supplies a 32-byte PMK; a
     * pure WPA1 profile is still rejected because the association builder is
     * intentionally WPA2/RSN-only. */
    if (d->ad_auth_lower != APPLE80211_AUTHTYPE_OPEN)
        return kIOReturnUnsupported;
    if (!secure && d->ad_auth_upper != APPLE80211_AUTHTYPE_NONE)
        return kIOReturnUnsupported;
    if (!(d->ad_auth_upper & (APPLE80211_AUTHTYPE_WPA2_PSK |
                              APPLE80211_AUTHTYPE_SHA256_PSK)) &&
        d->ad_auth_upper != APPLE80211_AUTHTYPE_NONE)
        return kIOReturnUnsupported;

    const uint8_t *pmk = nullptr;
    if (secure) {
        /* AirportItlwm consumes ad_key.key directly.  Do not gate on
         * key_cipher_type here; Ventura variants are not consistent about
         * tagging the 32-byte association key as APPLE80211_CIPHER_PMK. */
        if (d->ad_key.key_len != 32)
            return kIOReturnUnsupported;
        pmk = d->ad_key.key;
    } else if (d->ad_key.key_len != 0) {
        return kIOReturnUnsupported;
    }

    IOReturn ret = _ieee80211->cmdConnect(ssid, nullptr, pmk, bssid, false);
    IOLog("AirportRTW88: ASSOCIATE ssid=%s secure=%d keylen=%u ret=0x%x\n",
          ssid, secure, d->ad_key.key_len, ret);
    return ret;
}

IOReturn AirportRTW88::handleRSN_IE(bool set, struct apple80211_rsn_ie_data *d)
{
    if (!d) return kIOReturnBadArgument;
    if (set) {
        if (d->version != APPLE80211_VERSION || d->len < 2 ||
            d->len > APPLE80211_MAX_RSN_IE_LEN)
            return kIOReturnBadArgument;
        return _ieee80211->cmdSetAssocRsnIE(d->ie, d->len);
    }
    uint16_t len = 0;
    IOReturn ret = _ieee80211->copyTargetRsnIE(d->ie, sizeof(d->ie), &len);
    if (ret) return ret;
    d->version = APPLE80211_VERSION;
    d->len = len;
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handleAP_IE_LIST(struct apple80211_ap_ie_data *d)
{
    if (!d || !d->ie_data || d->len == 0) return kIOReturnBadArgument;
    uint16_t len = 0;
    uint8_t rsn[APPLE80211_MAX_RSN_IE_LEN] = {};
    IOReturn ret = _ieee80211->copyTargetRsnIE(rsn, sizeof(rsn), &len);
    if (ret) return ret;
    if (len > d->len) return kIOReturnNoSpace;
    memcpy(d->ie_data, rsn, len);
    d->version = APPLE80211_VERSION;
    d->len = len;
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handleCIPHER_KEY(struct apple80211_key *key)
{
    if (!key || key->version != APPLE80211_VERSION || key->key_len > APPLE80211_KEY_BUFF_LEN)
        return kIOReturnBadArgument;

    if (key->key_cipher_type == APPLE80211_CIPHER_NONE)
        return kIOReturnSuccess;

    uint32_t cipher = 0;
    switch (key->key_cipher_type) {
    case APPLE80211_CIPHER_AES_CCM:
        cipher = WLAN_CIPHER_SUITE_CCMP;
        break;
    case APPLE80211_CIPHER_TKIP:
        cipher = WLAN_CIPHER_SUITE_TKIP;
        break;
    default:
        return kIOReturnUnsupported;
    }

    /* Match AirportItlwm semantics strictly:
     * key_flags == 4 -> PTK
     * key_flags == 0 -> GTK
     */
    bool pairwise;
    if (key->key_flags == 4) {
        pairwise = true;
    } else if (key->key_flags == 0) {
        pairwise = false;
    } else {
        IOLog("AirportRTW88: unexpected CIPHER_KEY flags=%u\n",
              key->key_flags);
        return kIOReturnUnsupported;
    }
    IOReturn ret = _ieee80211->cmdInstallExternalKey(pairwise,
                    (uint8_t)key->key_index, cipher, key->key, (uint8_t)key->key_len);
    IOLog("AirportRTW88: CIPHER_KEY %s cipher=%u idx=%u len=%u ret=0x%x\n",
          pairwise ? "PTK" : "GTK", key->key_cipher_type, key->key_index,
          key->key_len, ret);
    if (!ret && _netif)
        _netif->postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE);
    return ret;
}

IOReturn AirportRTW88::handleDISASSOCIATE()
{
    return _ieee80211->cmdDisconnect();
}

IOReturn AirportRTW88::handleSCAN_REQ(void *data)
{
    if (!data) return kIOReturnBadArgument;
    auto *d = static_cast<apple80211_scan_data *>(data);
    if (d->version != APPLE80211_VERSION || d->ssid_len > APPLE80211_MAX_SSID_LEN ||
        d->num_channels > APPLE80211_MAX_CHANNELS) return kIOReturnBadArgument;
    if (_scanInProgress) {
        IOLog("AirPort_RTW88: SCAN_REQ coalesced with active scan\n");
        return kIOReturnSuccess;
    }
    RTW88StateResult st = {};
    if (_ieee80211->cmdGetState(&st) == kIOReturnSuccess &&
        st.state == RTW88_STATE_CONNECTED) {
        _scanCursor = 0;
        UInt32 result = 0;
        IOLog("AirPort_RTW88: SCAN_REQ connected cache-only completion\n");
        _netif->postMessage(APPLE80211_M_SCAN_DONE, &result, sizeof(result));
        return kIOReturnSuccess;
    }
    _scanInProgress = true; _scanCursor = 0;
    IOReturn ret = _ieee80211->cmdScan();
    if (ret) _scanInProgress = false;
    return ret;
}

void AirportRTW88::fillScanResultFromBSS(const RTW88BSS &b, struct apple80211_scan_result *d, bool fullIEs)
{
    if (!d) return;
    bzero(d, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->asr_channel.version = APPLE80211_VERSION;
    d->asr_channel.channel = b.channel;
    /* Match AirportItlwm's Ventura behavior: keep scan/current-network
     * channel flags deliberately conservative so CoreWiFi accepts them. */
    d->asr_channel.flags = APPLE80211_C_FLAG_ACTIVE |
                           APPLE80211_C_FLAG_20MHZ |
        (b.channel <= 14 ? APPLE80211_C_FLAG_2GHZ : APPLE80211_C_FLAG_5GHZ);

    d->asr_noise = -95;
    d->asr_rssi = b.rssi;
    d->asr_snr = (int16_t)(b.rssi - d->asr_noise);
    d->asr_beacon_int = (int16_t)b.beacon_interval;
    d->asr_cap = (int16_t)b.capabilities;
    d->asr_age = 0;
    memcpy(d->asr_bssid, b.bssid, APPLE80211_ADDR_LEN);
    d->asr_ssid_len = b.ssid_len > APPLE80211_MAX_SSID_LEN ?
                      APPLE80211_MAX_SSID_LEN : b.ssid_len;
    if (d->asr_ssid_len)
        memcpy(d->asr_ssid, b.ssid, d->asr_ssid_len);

    const size_t ieLength = b.ies_len > sizeof(b.ies) ? sizeof(b.ies) : b.ies_len;
    d->asr_ie_len = 0;
    if (fullIEs) {
        /* AirportItlwm's CURRENT_NETWORK is a scan-result-style object. Give
         * CoreWiFi/locationd the complete BSS IE blob for the associated AP. */
        const size_t ieCopy = ieLength > sizeof(d->asr_ie_data) ? sizeof(d->asr_ie_data) : ieLength;
        if (ieCopy) {
            memcpy(d->asr_ie_data, b.ies, ieCopy);
            d->asr_ie_len = (int16_t)ieCopy;
        }
    } else {
        /* Preserve the already-working Ventura SCAN_RESULT contract: expose
         * only the first RSN TLV, matching the audited AirportItlwm behavior. */
        for (size_t pos = 0; pos + 2 <= ieLength;) {
            const size_t n = (size_t)b.ies[pos + 1] + 2;
            if (n > ieLength - pos) break;
            if (b.ies[pos] == 48) { /* RSN TLV */
                const size_t copyLen = n > sizeof(d->asr_ie_data) ? sizeof(d->asr_ie_data) : n;
                memcpy(d->asr_ie_data, b.ies + pos, copyLen);
                d->asr_ie_len = (int16_t)copyLen;
                break;
            }
            pos += n;
        }
    }

    /* Populate real supported rates from beacon/probe IEs. */
    for (size_t pos = 0; pos + 2 <= ieLength;) {
        const size_t n = (size_t)b.ies[pos + 1] + 2;
        if (n > ieLength - pos) break;
        if (b.ies[pos] == 1 || b.ies[pos] == 50) {
            for (size_t j = 0; j < n - 2 && d->asr_nrates < APPLE80211_MAX_RATES; j++)
                d->asr_rates[d->asr_nrates++] = b.ies[pos + 2 + j];
        }
        pos += n;
    }
}

IOReturn AirportRTW88::handleSCAN_RESULT(struct apple80211_scan_result **out)
{
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    if (_scanInProgress) return kIOReturnBusy;

    RTW88BSS b = {};
    IOReturn ret = _ieee80211->copyScanBSS(_scanCursor, &b);
    if (ret == kIOReturnNotFound) {
        _scanCursor = 0;
        return 5; /* AirportItlwm end-of-list ABI */
    }
    if (ret) return ret;

    fillScanResultFromBSS(b, &_scanResult, false);
    _scanCursor++;
    *out = &_scanResult;
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handleCURRENT_NETWORK(struct apple80211_scan_result *out)
{
    static_assert(sizeof(apple80211_scan_result) == 1164, "Ventura CURRENT_NETWORK ABI changed");
    if (!out) return kIOReturnBadArgument;

    RTW88BSS current = {};
    IOReturn ret = _ieee80211->copyCurrentBSS(&current);
    if (ret != kIOReturnSuccess) {
        /* CoreWiFi/locationd asks selector 103 aggressively.  Do not turn a
         * valid RUN state into -3903 merely because the scan-cache copy was
         * evicted.  Reconstruct the minimum current BSS from the authoritative
         * connection state exposed by the Realtek backend. */
        RTW88StateResult st = {};
        IOReturn sr = _ieee80211->cmdGetState(&st);
        if (sr != kIOReturnSuccess || st.state != RTW88_STATE_CONNECTED ||
            st.channel == 0 || st.ssid[0] == '\0') {
            IOLog("AirPort_RTW88: CURRENT_NETWORK unavailable bss=0x%x state=0x%x run=%u ch=%u ssid0=%u\n",
                  ret, sr, st.state, st.channel, (unsigned)(uint8_t)st.ssid[0]);
            return ret != kIOReturnSuccess ? ret : kIOReturnNotReady;
        }

        current.ssid_len = (uint8_t)strnlen(st.ssid, 32);
        memcpy(current.ssid, st.ssid, current.ssid_len);
        current.ssid[current.ssid_len] = '\0';
        memcpy(current.bssid, st.bssid, sizeof(current.bssid));
        current.channel = (uint8_t)st.channel;
        current.rssi = (int16_t)st.rssi;
        current.beacon_interval = 100;
        IOLog("AirPort_RTW88: CURRENT_NETWORK reconstructed from connected state\n");
    }

    fillScanResultFromBSS(current, out, true);
    IOLog("AirPort_RTW88: CURRENT_NETWORK ssid_len=%u channel=%u rssi=%d ies=%d\n",
          out->asr_ssid_len, out->asr_channel.channel,
          out->asr_rssi, out->asr_ie_len);
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handleSTATE(struct apple80211_state_data *out)
{
    if (!out) return kIOReturnBadArgument;
    struct RTW88StateResult st;
    IOReturn r = _ieee80211->cmdGetState(&st);
    if (r != kIOReturnSuccess) return r;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    switch (st.state) {
    case RTW88_STATE_SCANNING: out->state = APPLE80211_S_SCAN; break;
    case RTW88_STATE_AUTHENTICATING: out->state = APPLE80211_S_AUTH; break;
    case RTW88_STATE_ASSOCIATING:
    case RTW88_STATE_HANDSHAKING: out->state = APPLE80211_S_ASSOC; break;
    case RTW88_STATE_CONNECTED: out->state = APPLE80211_S_RUN; break;
    default: out->state = APPLE80211_S_INIT; break;
    }
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handleCHANNEL(struct apple80211_channel_data *out)
{
    if (!out) return kIOReturnBadArgument;
    RTW88StateResult st = {};
    IOReturn r = _ieee80211->cmdGetState(&st);
    if (r != kIOReturnSuccess) return r;
    if (st.state != RTW88_STATE_CONNECTED || st.channel == 0) return 6;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->channel.version = APPLE80211_VERSION;
    out->channel.channel = st.channel;
    out->channel.flags = APPLE80211_C_FLAG_ACTIVE |
                         APPLE80211_C_FLAG_20MHZ |
        (st.channel <= 14 ? APPLE80211_C_FLAG_2GHZ : APPLE80211_C_FLAG_5GHZ);
    return kIOReturnSuccess;
}

IOReturn AirportRTW88::handlePOWER(bool set, struct apple80211_power_data *d)
{
    if (!d) return kIOReturnBadArgument;
    if (set) {
        if (d->version != APPLE80211_VERSION || !d->num_radios ||
            d->num_radios > APPLE80211_MAX_RADIO)
            return kIOReturnBadArgument;
        UInt32 requested = d->power_state[0];
        if (requested != APPLE80211_POWER_ON && requested != APPLE80211_POWER_OFF)
            return kIOReturnUnsupported;
        IOReturn ret = requested == APPLE80211_POWER_ON ?
            _ieee80211->cmdPowerOn() : _ieee80211->cmdPowerOff();
        if (ret == kIOReturnSuccess && _netif)
            _netif->postMessage(APPLE80211_M_POWER_CHANGED);
        return ret;
    }
    RTW88StateResult st = {};
    IOReturn ret = _ieee80211->cmdGetState(&st);
    if (ret != kIOReturnSuccess) return ret;
    bzero(d, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->num_radios = APPLE80211_MAX_RADIO;
    for (UInt32 i = 0; i < APPLE80211_MAX_RADIO; i++)
        d->power_state[i] = st.powered ? APPLE80211_POWER_ON : APPLE80211_POWER_OFF;
    return kIOReturnSuccess;
}

SInt32 AirportRTW88::stopDMA()
{
    /* No hay traspaso de DMA en caliente implementado todavia; devolver
     * exito optimista para no bloquear el resto del arranque. Revisar
     * si algun caller real depende de que esto pare el hardware de verdad. */
    return kIOReturnSuccess;
}

UInt32 AirportRTW88::hardwareOutputQueueDepth(IO80211Interface *interface)
{
    /* AirportItlwm reports 0 here; IO80211 owns the queueing policy. */
    return 0;
}

SInt32 AirportRTW88::performCountryCodeOperation(IO80211Interface *interface, IO80211CountryCodeOp op)
{
    /* Match AirportItlwm: acknowledge IO80211's country-code operation. */
    return kIOReturnSuccess;
}

SInt32 AirportRTW88::enableFeature(IO80211FeatureCode feature, void *data)
{
    /* Match AirportItlwm: AWDL service initialization does not depend on
     * acknowledging undocumented feature codes. Keep this truthful and
     * conservative now that the diagnostic feature-gate test is complete. */
    if (feature == kIO80211Feature80211n)
        return kIOReturnSuccess;
    return 102;
}

SInt32 AirportRTW88::monitorModeSetEnabled(IO80211Interface *interface,
                                            bool enabled,
                                            UInt32 mode)
{
    /* No monitor backend yet, but AirportItlwm returns success for this SPI. */
    return kIOReturnSuccess;
}

mbuf_t AirportRTW88::allocateInputPacket(uint32_t len)
{
    return allocatePacket(len);  /* helper heredado de IONetworkController */
}

void AirportRTW88::injectRxFrame(mbuf_t m)
{
    if (!m)
        return;

    if (!_netif) {
        mbuf_freem(m);
        return;
    }

    /* Match the proven itlwm/legacy rtw88 RX submission path.
     * Queue the packet first, then flush the interface input queue.
     * Direct inputPacket(..., 0) can bypass the delivery behaviour
     * expected by the macOS network/EAPOL stack. */
    size_t plen = mbuf_pkthdr_len(m);
    size_t mlen = mbuf_len(m);

    if (plen < 14 || plen > 4096 || mlen < 14 || mlen > 4096) {
        IOLog("AirportRTW88: dropping bogus RX mbuf pkthdr=%zu mlen=%zu\n",
              plen, mlen);
        mbuf_freem(m);
        return;
    }

    _netif->inputPacket(m, 0, IONetworkInterface::kInputOptionQueuePacket);
    _netif->flushInputQueue();
}

namespace {
/* AWDL management frames use an IEEE 802.11 Action header followed by the
 * Apple vendor header.  Do not feed arbitrary infrastructure action frames
 * (BlockAck, SA Query, etc.) to the AWDL peer manager. */
static bool rtw88IsAWDLActionFrame(const uint8_t *frame, uint32_t len,
                                   uint8_t *subtypeOut)
{
    static const uint8_t kAWDLBSSID[6] = {0x00, 0x25, 0x00, 0xff, 0x94, 0x73};
    static const uint8_t kAppleOUI[3] = {0x00, 0x17, 0xf2};
    constexpr uint32_t kHdrLen = 24;
    constexpr uint32_t kFixedAWDLActionLen = 16; /* category+OUI+12-byte fixed body */

    if (!frame || len < kHdrLen + kFixedAWDLActionLen)
        return false;

    /* Management/Action subtype (little-endian frame-control low byte). */
    if ((frame[0] & 0xfcU) != 0xd0U)
        return false;

    /* addr3/BSSID must be Apple's well-known AWDL BSSID. */
    if (memcmp(frame + 16, kAWDLBSSID, sizeof(kAWDLBSSID)) != 0)
        return false;

    const uint8_t *a = frame + kHdrLen;
    if (a[0] != 0x7f || memcmp(a + 1, kAppleOUI, sizeof(kAppleOUI)) != 0 ||
        a[4] != 8)
        return false;

    /* byte 5 is the AWDL version; byte 6 is PSF(0) or MIF(3). */
    const uint8_t subtype = a[6];
    if (subtype != 0 && subtype != 3)
        return false;

    /* Reject impossible source addresses before creating peer state. */
    const uint8_t *sa = frame + 10;
    bool any = false;
    for (unsigned i = 0; i < 6; ++i) any |= sa[i] != 0;
    if (!any || (sa[0] & 0x01U))
        return false;

    if (subtypeOut)
        *subtypeOut = subtype;
    return true;
}

/* MacKernelSDK only carries an opaque/empty declaration for packet_info_tag.
 * Reserve a reasonably sized, zeroed backing store so IO80211 never reads
 * beyond a one-byte empty C++ placeholder if Ventura consults private fields. */
static packet_info_tag *rtw88ZeroPacketInfo(uint8_t (&storage)[64])
{
    bzero(storage, sizeof(storage));
    return reinterpret_cast<packet_info_tag *>(storage);
}
}

void AirportRTW88::injectRxActionFrame(const uint8_t *frame, uint32_t len,
                                       int8_t rssi, uint16_t channel)
{
    IO80211VirtualInterface *awdl =
        _awdlManager ? _awdlManager->awdlInterface() : nullptr;
    if (!awdl || !frame || len > 4096)
        return;

    uint8_t subtype = 0xff;
    if (!rtw88IsAWDLActionFrame(frame, len, &subtype))
        return;

    /* IO80211's AWDL peer manager needs peer presence before multicast/data
     * packets can be associated with an AWDL peer.  The public SDK strips
     * the private parameter names, but this ABI is the one exposed by
     * IO80211P2PInterface on Ventura.  Supply conservative radio metadata
     * and let the raw PSF/MIF below provide the authoritative AWDL TLVs. */
    IO80211P2PInterface *p2p = OSDynamicCast(IO80211P2PInterface, awdl);
    const uint8_t *sa = frame + 10;
    if (p2p) {
        ether_addr peer = {};
        memcpy(peer.octet, sa, sizeof(peer.octet));
        IOReturn presence = p2p->postPeerPresence(&peer, (int)rssi,
                                                  (int)channel,
                                                  (int)subtype, nullptr);
        IOLog("AirPort_RTW88: AWDL peer presence sa=%02x:%02x:%02x:%02x:%02x:%02x rssi=%d ch=%u subtype=%u ret=0x%x\n",
              sa[0], sa[1], sa[2], sa[3], sa[4], sa[5],
              (int)rssi, (unsigned)channel, (unsigned)subtype, presence);
    }

    mbuf_t m = allocatePacket(len);
    if (!m)
        return;
    if (mbuf_copyback(m, 0, len, frame, MBUF_DONTWAIT) != 0) {
        mbuf_freem(m);
        return;
    }
    mbuf_pkthdr_setlen(m, len);

    alignas(8) uint8_t tagStorage[64];
    UInt32 ret = awdl->inputPacket(m, rtw88ZeroPacketInfo(tagStorage));
    IOLog("AirPort_RTW88: AWDL action RX sa=%02x:%02x:%02x:%02x:%02x:%02x subtype=%u rssi=%d ch=%u input=0x%x len=%u\n",
          sa[0], sa[1], sa[2], sa[3], sa[4], sa[5],
          (unsigned)subtype, (int)rssi, (unsigned)channel, ret, len);
    if (ret != kIOReturnSuccess && ret != kIOReturnOutputSuccess) {
        /* IO80211VirtualInterface owns the mbuf on accepted paths. On a
         * failure return it is safer not to double-free an ambiguously
         * consumed packet; the trace above records the Ventura result. */
    }
}

void AirportRTW88::injectRxAWDLFrame(mbuf_t m)
{
    if (!m) return;
    IO80211VirtualInterface *awdl = _awdlManager ? _awdlManager->awdlInterface() : nullptr;
    if (!awdl) {
        mbuf_freem(m);
        return;
    }
    const unsigned long packetLen = (unsigned long)mbuf_pkthdr_len(m);
    alignas(8) uint8_t tagStorage[64];
    UInt32 ret = awdl->inputPacket(m, rtw88ZeroPacketInfo(tagStorage));
    if (ret != kIOReturnSuccess && ret != 0)
        IOLog("AirPort_RTW88: AWDL data RX inputPacket result=0x%x len=%lu\n",
              ret, packetLen);
}

IOWorkLoop *AirportRTW88::getRxWorkLoop()
{
    return _workLoop;   /* miembro ya declarado en AirportRTW88.hpp */
}

void AirportRTW88::setLinkStatus(UInt32 status)
{
    IO80211Controller::setLinkStatus(status);
    // IONetwork link flags alone do not update the IO80211 interface state.
    if (getCommandGate()) getCommandGate()->runAction(
        [](OSObject *owner, void *value, void *, void *, void *) -> IOReturn {
            auto *self = static_cast<AirportRTW88 *>(owner);
            bool up = ((uintptr_t)value & kIONetworkLinkActive) != 0;
            if (self->_netif && self->_linkUp != up) {
                self->_linkUp = up;
                self->_netif->setLinkState(up ? kIO80211NetworkLinkUp : kIO80211NetworkLinkDown, 0U);
                if (up) self->_netif->postMessage(APPLE80211_M_ASSOC_DONE);
            }
            /* AWDL has an independent IO80211 virtual-interface lifecycle.
             * Do not mirror the infrastructure association state onto awdl0:
             * when en0 is unassociated the single PHY may still be available
             * for AWDL discovery/channel operation, and forcing LinkDown here
             * prevents IO80211's AWDL peer manager from using that VIF.
             * enableVirtualInterface()/disableVirtualInterface() own the AWDL
             * enabled/link state instead. */
            return kIOReturnSuccess;
        }, (void *)(uintptr_t)status);
}

void AirportRTW88::rtw88Event(RTW88Event ev, void *data)
{
    if (!_netif) return;
    switch (ev) {
    case kRTW88EventScanDone: {
        _scanInProgress = false; _scanCursor = 0; UInt32 result = data ? *(UInt32 *)data : 0;
        _netif->postMessage(APPLE80211_M_SCAN_DONE, &result, sizeof(result)); break;
    }
    case kRTW88EventAssocDone:     _netif->postMessage(APPLE80211_M_ASSOC_DONE); break;
    case kRTW88EventDeauth:        _netif->postMessage(APPLE80211_M_DEAUTH_RECEIVED); break;
    case kRTW88EventDisconnected:  _netif->postMessage(APPLE80211_M_LINK_CHANGED); break;
    case kRTW88EventRSSIChanged:   _netif->postMessage(APPLE80211_M_LINK_QUALITY); break;
    default: break;
    }
}

