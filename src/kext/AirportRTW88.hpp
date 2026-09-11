/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * AirportRTW88.hpp — IO80211Controller subclass; native Wi-Fi menu support
 * for the rtw88 macOS port (Feixiao), mirroring OpenIntelWireless/itlwm's
 * AirportItlwm.
 *
 * This is a SEPARATE kext target from rtw88.kext (which stays IOEthernet-
 * based, like itlwm.kext), same as AirportItlwm.kext is separate from
 * itlwm.kext. Do not load both against the same PCI device.
 *
 * IOClass in this target's Info.plist should be AirportRTW88, IOProviderClass
 * stays IOPCIDevice with the same IOPCIMatch entries as rtw88.kext.
 */
#pragma once

#include <IOKit/80211/IO80211Controller.h>
#include <IOKit/80211/IO80211Interface.h>
#include <IOKit/80211/IO80211VirtualInterface.h>
#include <IOKit/80211/IO80211P2PInterface.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>

#include "RTW88IEEE80211.hpp"   /* for RTW88EventDelegate, RTW88BSS, RTW88State */
#include "RTW88UserClient.hpp"  /* for struct RTW88StateResult (definida ahi, no en RTW88IEEE80211.hpp) */

class AirportRTW88Interface;

class AirportRTW88 : public IO80211Controller, public RTW88EventDelegate, public RTW88RxDelegate, public RTW88HwOps {
    OSDeclareDefaultStructors(AirportRTW88)

public:
    /* IOService */
    bool     init(OSDictionary *props) override;
    bool     start(IOService *provider) override;
    void     stop(IOService *provider) override;
    void     free() override;

    /* IONetworkController (required minimum set) */
    const OSString *newVendorString() const override;
    const OSString *newModelString() const override;
    IOReturn enable(IONetworkInterface *iface) override;
    IOReturn disable(IONetworkInterface *iface) override;
    IONetworkInterface *createInterface() override;
    bool     configureInterface(IONetworkInterface *iface) override;
    bool     createWorkLoop() override;
    IOWorkLoop *getWorkLoop() const override;
    IOReturn selectMedium(const IONetworkMedium *medium) override;
    bool useAppleRSNSupplicant(IO80211Interface *) override { return false; }
    UInt32   outputPacket(mbuf_t m, void *param) override;
    IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    IOReturn getHardwareAddressForInterface(IO80211Interface *iface,
                                            IOEthernetAddress *addr) override;
    IOReturn getPacketFilters(const OSSymbol *, UInt32 *) const override;
    IOReturn setMulticastMode(bool active) override;
    IOReturn setMulticastList(IOEthernetAddress *, UInt32 count) override;
    IOReturn setPromiscuousMode(bool active) override;

    /* IO80211Controller — the actual Airport-style API surface.
     * Selector names/signatures must match the IO80211Family SDK headers
     * (IO80211Controller.h) exactly — check MacKernelSDK version pinned
     * in the repo, this list is representative, not exhaustive. */
    virtual SInt32 apple80211Request(unsigned int request_type,
                                      int request_number,
                                      IO80211Interface *interface,
                                      void *data) override;

    /* AWDL / P2P virtual interface ABI (Ventura IO80211FamilyLegacy). */
    virtual SInt32 apple80211VirtualRequest(UInt request_type, int request_number,
                                            IO80211VirtualInterface *interface,
                                            void *data) override;
    virtual IO80211VirtualInterface *createVirtualInterface(ether_addr *addr, UInt role) override;
    virtual SInt32 enableVirtualInterface(IO80211VirtualInterface *interface) override;
    virtual SInt32 disableVirtualInterface(IO80211VirtualInterface *interface) override;
    virtual int outputActionFrame(IO80211Interface *interface, mbuf_t m) override;
    virtual int bpfOutputPacket(OSObject *object, UInt dltType, mbuf_t m) override;
    virtual void requestPacketTx(void *object, UInt options) override;

    /* Puros virtuales de IO80211Controller que hay que implementar si o si
     * para que la clase no quede abstracta (encontrados por el compilador,
     * no los tenia contemplados en el primer intento). */
    virtual SInt32 stopDMA() override;
    virtual UInt32 hardwareOutputQueueDepth(IO80211Interface*) override;
    virtual SInt32 performCountryCodeOperation(IO80211Interface*, IO80211CountryCodeOp) override;
    virtual SInt32 enableFeature(IO80211FeatureCode, void*) override;
    virtual SInt32 monitorModeSetEnabled(IO80211Interface*, bool, UInt32) override;

    /* RTW88EventDelegate — async notifications from RTW88IEEE80211 */
    virtual void rtw88Event(RTW88Event ev, void *data) override;

    /* RTW88RxDelegate — antes RTW88IEEE80211 asumia un RTW88PCIDevice*
     * para esto; ahora cualquiera de los dos kexts puede implementarlo. */
    virtual mbuf_t allocateInputPacket(uint32_t len) override;
    virtual void injectRxFrame(mbuf_t m) override;
    virtual void injectRxActionFrame(const uint8_t *frame, uint32_t len) override;
    virtual IOWorkLoop *getRxWorkLoop() override;
    virtual void setLinkStatus(UInt32 status) override;

private:
    /* IO80211 payload adapters. WPA2-PSK handshake remains in the Realtek backend. */
    IOReturn handleSSID(bool set, struct apple80211_ssid_data *data);
    IOReturn handleAUTH_TYPE(bool set, struct apple80211_authtype_data *data);
    IOReturn handleASSOCIATE(struct apple80211_assoc_data *data);
    IOReturn handleRSN_IE(bool set, struct apple80211_rsn_ie_data *data);
    IOReturn handleAP_IE_LIST(struct apple80211_ap_ie_data *data);
    IOReturn handleCIPHER_KEY(struct apple80211_key *key);
    IOReturn handleDISASSOCIATE();
    IOReturn handleSCAN_REQ(void *data);
    IOReturn handleSCAN_RESULT(struct apple80211_scan_result **data);
    IOReturn handleCURRENT_NETWORK(struct apple80211_scan_result *data);
    void fillScanResultFromBSS(const RTW88BSS &b, struct apple80211_scan_result *data, bool fullIEs);
    IOReturn handleSTATE(struct apple80211_state_data *data);
    IOReturn handleCHANNEL(struct apple80211_channel_data *data);
    IOReturn handlePOWER(bool set, struct apple80211_power_data *data);
    IOReturn handleVIRTUAL_IF_CREATE(struct apple80211_virt_if_create_data *data);
    IOReturn handleVIRTUAL_IF_DELETE(struct apple80211_virt_if_delete_data *data);

    apple80211_scan_result _scanResult = {};
    uint32_t _scanCursor = 0;
    uint32_t _authLower = APPLE80211_AUTHTYPE_OPEN;
    uint32_t _authUpper = APPLE80211_AUTHTYPE_NONE;
    uint8_t  _countryCode[APPLE80211_MAX_CC_LEN] = {'Z', 'Z', 0};

    /* Virtual-interface/AWDL control state. IO80211 owns interface lifetime;
     * these are non-retained observation pointers used only while enabled. */
    IO80211VirtualInterface *_awdlInterface = nullptr;
    IO80211VirtualInterface *_p2pInterface  = nullptr;
    uint8_t  *_awdlSyncTemplate = nullptr;
    uint32_t  _awdlSyncTemplateLength = 0;
    uint32_t  _awdlElectionMetric = 0;
    bool      _awdlSyncEnabled = true;

    IOPCIDevice           *_pciDev      = nullptr;
    AirportRTW88Interface  *_netif       = nullptr;
    RTW88IEEE80211         *_ieee80211   = nullptr;  /* reused as-is from rtw88 core */

    IO80211WorkLoop       *_workLoop    = nullptr;

    IOEthernetAddress       _macAddr;
    bool                    _scanInProgress = false;
    bool                    _linkUp = false;

    /* Acceso real a hardware -- antes esto se pasaba como nullptr a
     * RTW88IEEE80211::create(), causando "pci bus timeout" en cada
     * acceso a registro. Replica lo que ya hace RTW88PCIDevice. */
    IOMemoryMap             *_mmioMap      = nullptr;
    volatile void           *_mmioBase     = nullptr;
    IOInterruptEventSource  *_intrSrc      = nullptr;
    struct pci_dev          *_compatPciDev = nullptr;

    bool setupInterrupt();
    bool failStart(IOService *provider, const char *reason);
    void teardown();
    void releaseDMAEntries();
    void handleInterrupt(IOInterruptEventSource *src, int count);

    /* RTW88HwOps -- acceso real a hardware, antes simulado con nullptr */
    UInt8  pciReadByte(int offset) override;
    UInt16 pciReadWord(int offset) override;
    UInt32 pciReadDword(int offset) override;
    void   pciWriteByte(int offset, UInt8 val) override;
    void   pciWriteWord(int offset, UInt16 val) override;
    void   pciWriteDword(int offset, UInt32 val) override;
    int    pciFindCapability(int cap) override;
    volatile void *mmioBase() const override { return _mmioBase; }
    void  *allocCoherent(size_t size, IOPhysicalAddress *phys) override;
    void   freeCoherent(size_t size, void *virt, IOPhysicalAddress phys) override;
    void   freeCoherentByPhys(IOPhysicalAddress phys) override;
    void   setBounceOrigVA(IOPhysicalAddress phys, void *orig_va) override;
    void   syncBounceForCpu(IOPhysicalAddress dma, size_t size) override;
    void   resumeTxIfStalled() override;
    void   drainPendingFree();

    RTW88DMAEntry *_dmaList        = nullptr;
    IOSimpleLock  *_dmaLock        = nullptr;
    RTW88DMAEntry *_dmaPendingFree = nullptr;
    IOSimpleLock  *_pendingFreeLock = nullptr;
    bool _superStarted      = false;
    bool _compatInitialized = false;
    bool _ieeeStarted       = false;
    bool _netifAttached     = false;
};
