# Bench OTA recovery evidence, 29 October 2026

The source/build capsule attributed to the running `.202` image `a0452be9`
has `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` unset in its saved `sdkconfig`;
the current HELIO candidate's configuration is the same on this option. This
is build provenance, not an independent read of the device's bootloader or
partition table. A fresh read-only Firmware endpoint identifies `helio1`,
build `Oct 29 2026 06:11:31`, application size 2,692,592 bytes. It does not
report a tested automatic return to the prior app. The existing MoonBase path
and a second app slot alone do not establish that a failed candidate will
automatically recover, especially because the last reported USB path is
disconnected. Keep the broad new catalogue/UI image as a local build until a
real recovery route and its behavior are established. Do not describe ESP-IDF
rollback as active merely because a candidate builds or uploads successfully.

At 2026-10-29 09:04 UTC a fresh read-only `/api/modules/Firmware` response
still identifies `helio1`, runtime build `Oct 29 2026 06:11:31`, and an app
partition of 3,145,728 bytes containing a 2,692,592-byte application. The
response exposes no recovery-image control. That observation does not prove
the partition table or the absence of a recovery image; the installed
bootloader, recovery behavior and physical USB path remain unverified. No
update, reboot, configuration change or light command was sent by this check.
