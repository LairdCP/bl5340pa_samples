# Sample applications for the BL5340PA DVK

These sample applications showcase the abilities of the BL5340PA. They can also be built for other Laird Connectivity development kits (DVKs) for comparison and interoperability testing.

These samples can also be built for the nrf21540dk_nrf52840 (single core) and the nrf5340dk_nrf5340_cpuapp (dual core) with nrf21540_ek shield. The Nordic development kits can be used to test the Laird FEM driver. The Nordic DKs allow access to the the FEM GPIO and SPI signals. The BL5340PA module does not.

## Throughput

This application can be used to test throughput or perform range tests.

Modified from nrf/samples/bluetooth/throughput to support the BL5340PA and other Laird Connectivity boards. It also supports extended advertisements, power control, and logging the RSSI.

### Known Issues

The throughput example uses 'prj_<board>.conf' files which were deprecated in NCS 2.4.0. The 'prj_<board>.conf' method generates less warnings for dual core projects because certain Kconfig should only be applied to the network core.
