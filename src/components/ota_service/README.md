# OTA service

The OTA service exposes a raw firmware upload endpoint at:

```text
POST /ota
```

It listens on TCP port `8080` by default.

The request body must be the compiled, signed MCUboot-compatible binary. The image is
written to MCUboot's secondary slot and marked as a test upgrade. About 2 seconds after
replying `202 Accepted`, the device reboots on its own and MCUboot swaps in the new image.
The new image confirms itself (`OTAService::confirmImage()`) once Wi-Fi is up and the OTA
server has started. If it resets before that point (crash, no Wi-Fi), MCUboot rolls back
to the previous image.

## Tutorial: update firmware over the air

All commands below run from the west workspace root (`~/zephyrproject`) with the Python
virtual environment active (`source ~/zephyrproject/venv/bin/activate`). The build
directory is `~/zephyrproject/build`.

### 1. One-time setup over USB

The bootloader on the board and the OTA images must come from the **same sysbuild
configuration** (same MCUboot swap mode and same signing key). Do this once, and again
whenever `sysbuild.conf`, the signing key, or MCUboot settings change:

```bash
west build -p always -b esp32c3_supermini --sysbuild -d build -s projects/aquasort
west flash -d build
```

Note the device IP from the serial log (`Device IP: ...`).

### 2. Build the update

After changing code or `prj.conf`, rebuild incrementally:

```bash
west build -b esp32c3_supermini --sysbuild -d build -s projects/aquasort
```

The image to upload is always:

```text
build/aquasort/zephyr/zephyr.signed.bin
```

Do **not** upload `build/aquasort/zephyr/zephyr.bin` (unsigned) or
`build/zephyr/zephyr.signed.bin` (that path only exists in a non-sysbuild build, which
uses a different MCUboot mode and no signing key, and will be rejected or fail to boot).

### 3. Check you are about to upload the right program

Run all three checks before every upload.

**a) The build is this project, built with sysbuild:**

```bash
grep -A2 "name: 'aquasort'" build/build_info.yml
```

Expected output contains:

```text
   - name: 'aquasort'
     source-dir: '/home/izasoerya/zephyrproject/projects/aquasort'
     type: 'MAIN'
```

If this prints nothing, the build directory is not a sysbuild build of this project.
Redo step 1.

**b) The image is freshly built (newer than your last edit):**

```bash
ls -l --time-style=+%F_%T build/aquasort/zephyr/zephyr.signed.bin
```

The timestamp must be from the build you just ran.

**c) The image is signed with the key the bootloader trusts:**

```bash
imgtool verify -k bootloader/mcuboot/root-rsa-2048.pem \
  build/aquasort/zephyr/zephyr.signed.bin
```

Expected output:

```text
Image was correctly validated
Image version: <your version>
```

If you use your own key (`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` in `sysbuild.conf`), pass
that key to `-k` instead.

> **Tip:** give every release a version so check (c) and the serial log tell builds
> apart. Create `projects/aquasort/VERSION`:
>
> ```text
> VERSION_MAJOR = 1
> VERSION_MINOR = 0
> PATCHLEVEL = 1
> VERSION_TWEAK = 0
> EXTRAVERSION =
> ```
>
> Bump `PATCHLEVEL` before each release. The signed image then reports
> `Image version: 1.0.1+0`.

### 4. Upload

Use the device IP address (WSL2 cannot resolve `.local` mDNS names):

```bash
curl --fail-with-body \
  --request POST \
  --header "Content-Type: application/octet-stream" \
  --data-binary @build/aquasort/zephyr/zephyr.signed.bin \
  http://<device-ip>:8080/ota
```

If curl prints `Couldn't read data from file`, you are not in `~/zephyrproject` or the
image was not built. Stop: curl would send an empty upload.

### 5. Confirm the update on the serial log

Expected sequence:

```text
OTA: image staged (<N> bytes), rebooting in 2000 ms
OTA: rebooting into new image
...                                  (MCUboot swaps images, app boots)
Device IP: <device-ip>
OTA: confirming running image: 0
```

If `OTA: confirming running image` never appears and the device resets, MCUboot rolls
back to the previous firmware on the next boot. Fix the problem and upload again.

## mDNS hostname

The device also advertises its network hostname through mDNS:

```text
aquasort_device.local
```

This works from Windows (`curl.exe` in PowerShell) but **not from WSL2**, which has no
mDNS resolver. Some resolvers also reject the `_` in the name; renaming it to
`aquasort-device` (in `prj.conf` and `task_wifi_context.h`) avoids that.

```powershell
curl.exe --fail-with-body `
  --request POST `
  --header "Content-Type: application/octet-stream" `
  --data-binary "@\\wsl.localhost\<distro>\home\izasoerya\zephyrproject\build\aquasort\zephyr\zephyr.signed.bin" `
  http://aquasort_device.local:8080/ota
```

## Response statuses

| Status | Meaning |
|---|---|
| `200 OK` | Upload fragment received while the transfer is continuing. |
| `202 Accepted` | Complete firmware image staged in the secondary slot; device reboots in ~2 s. |
| `400 Bad Request` | Upload was aborted or contained no firmware bytes. |
| `409 Conflict` | Another firmware upload is already in progress. |
| `413 Payload Too Large` | Firmware exceeds the secondary image-slot capacity. |
| `500 Internal Server Error` | Slot erase, flash write, or MCUboot upgrade marking failed. |
| `405 Method Not Allowed` | The endpoint was contacted with a method other than POST. |

## Requirements

- The HTTP server configuration must include `CONFIG_HTTP_SERVER`.
- MCUboot must manage the application image with `CONFIG_BOOTLOADER_MCUBOOT`.
- Flash map, streaming flash, and image manager support must be enabled.
- The firmware must fit in `slot1_partition` (1.75 MB on the default 4 MB layout).
- The firmware must be built and signed for the same MCUboot configuration as the
  bootloader on the device (see step 1).
