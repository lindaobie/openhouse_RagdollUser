hidapi 0.15.0 (https://github.com/libusb/hidapi), Linux hidraw backend only, unmodified.
Built into lh_usb with hidden symbols: a host process may have another hidapi loaded
(monado-service links the libusb backend), and the dynamic linker would otherwise bind
hidapi's own internal calls to it. Licence: BSD (LICENSE-bsd.txt), as chosen in LICENSE.txt.
