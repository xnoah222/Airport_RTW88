from pathlib import Path
h = (Path(__file__).parents[1] / 'MacKernelSDK/Headers/IOKit/80211/IO80211VirtualInterface.h').read_text()
required = [
    'void startOutputQueues(void);',
    'void stopOutputQueues(void);',
    'void postMessage(unsigned int, void* data = NULL, unsigned long dataLen = 0);',
    'UInt getInterfaceRole(void);',
    'const char *getBSDName(void);',
]
for decl in required:
    assert decl in h, f'missing ABI declaration: {decl}'
for bad in ['static void postMessage', 'static int getInterfaceRole', 'static void startOutputQueues', 'static void stopOutputQueues']:
    assert bad not in h, f'ABI-invalid declaration remains: {bad}'
print('IO80211VirtualInterface instance-method ABI declarations OK')
