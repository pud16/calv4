# JC3248W535C diagnostic firmware v2

Target: Guition JC3248W535C ESP32-S3 N16R8, 320x480 AXS15231B QSPI LCD, touch 0x3B.

This is **source code, not a tested/compiled firmware**. The build workflow creates a merged image (bootloader, partition table, app) for web flashing at **0x0**. It must first build successfully. No device-level test has been performed.

Expected result: green screen with DISPLAY OK and coordinates on touch, and USB serial lines at 115200 starting with DIAG V2.

To build through GitHub: create a new private repository, upload the contents of this folder (including `.github/workflows/build.yml`), then open Actions > Build JC3248W535C diagnostic firmware > Run workflow. After the build succeeds, download the artifact `JC3248W535C_DIAG_V2_fullflash` and extract the .bin. Use a browser flasher with an explicit 0x0 offset. If the build fails, do not flash anything; share the Actions log.

Safety: make a full backup of the original flash if possible. Earlier watchdog resets indicate a boot problem. The hardware compatibility and flash memory mode are not yet verified on the user's board. Beware that the vendor storefront incorrectly mixes JC4832 and JC3248 models.
