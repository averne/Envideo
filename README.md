# Envideo
Envideo is a support library designed for low-level interaction with Nvidia's multimedia engines, including NVDEC, NVENC, and OFA. Its primary objective is to provide a unified interface across different platforms (Linux, Horizon OS) and hardware types (discrete and integrated GPUs), enabling the development of high-performance, cross-platform hardware acceleration code.  
The library offers a set of primitives reminiscent of Vulkan objects—such as devices, channels, buffers, fences, and command buffers—that facilitate communication and synchronization with the underlying hardware. It serves as an abstraction layer, handling OS-specific and kernel driver complexities, as well as hardware variations such as host engines.  
In addition, it includes a few helpful utilities such as constraint checking, dynamic frequency scaling routines, or hardware-accelerated data transfer via copy engines.  
For an example of how Envideo integrates into a larger project, see: https://github.com/averne/FFmpeg.

## Support
Currently supported platforms are:
- Nintendo Switch (Horizon OS, nvgpu kernel driver, ARM64, integrated chipset)
- Jetson cards (Linux4Tegra, nvgpu kernel driver, ARM64, integrated chipset)
- Desktop linux (Linux, nvidia & nvidia-open kernel drivers, AMD64, discrete chipsets)
- Future plans include the nouveau/nova open-source kernel drivers, and possibly Windows support

## Building/Testing
Building the tests requires the google-test and xxhash libraries. If not detected on the system, they will be automatically downloaded and built from source.  
For compiling on the Nintendo Switch, a crossfile is [included](misc/switch-crossfile.txt) (pass it to meson at configuration time using with `--crossfile`).
1. Clone the repository recursively.
2. Setup the project using: `meson setup build -D<driver>=enabled -Dtests=true`, where `<driver>` can be `nvidia` or `nvgpu`. Multiple drivers can be specified.
3. Compile the project with: `meson compile -C build`
4. Finally, run the tests with: `meson test -C build`
