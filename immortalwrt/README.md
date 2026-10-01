# ImmortalWrt 24.10 package

ImmortalWrt 24.10.4 uses the shared package definition in `openwrt/trafficmon`. Select the SDK
that exactly matches the target router. For the validated x86/64 target that is:

```text
immortalwrt-sdk-24.10.4-x86-64_gcc-13.3.0_musl.Linux-x86_64.tar.zst
SHA256 4f5de9da8674d4acdb6bd2afe594ea079c9983690d9e2eb1661a99b8fcd7d270
```

Build with the official SDK package infrastructure:

```sh
./openwrt/prepare-sdk-package.sh /path/to/immortalwrt-sdk
make -C /path/to/immortalwrt-sdk defconfig
make -C /path/to/immortalwrt-sdk package/toolchain/compile V=s
make -C /path/to/immortalwrt-sdk package/trafficmon/compile NO_DEPS=1 V=s
```

`NO_DEPS=1` is appropriate after the toolchain metadata exists because the target router supplies
the exact-kernel-ABI `kmod-nf-conntrack-netlink`. It also avoids rebuilding unrelated kernel
packages. Never copy a kmod from an OpenWrt SDK or a different ImmortalWrt target/kernel.

The generated IPK depends only on `libc` and `kmod-nf-conntrack-netlink`; the collector continues
to use raw `NETLINK_NETFILTER` and has no libnl/libnl-tiny dependency.

Run `tests/smoke.sh` for opkg installation and basic service checks. The platform-independent
runtime gates remain in `../openwrt/tests/service.sh` and `../openwrt/tests/reboot-persistence.sh`.
Use `tests/upgrade-preservation.sh` for an actual lower-to-higher package upgrade and
`tests/removal-preservation.sh` to verify that removing the package leaves protocol configuration
and traffic databases intact.
