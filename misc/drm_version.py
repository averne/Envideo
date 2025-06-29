#!/usr/bin/env python3

import sys, ctypes, fcntl

DRM_IOWR = lambda c, t: (3 << 30) | (ctypes.sizeof(t) << 16) | (ord('d') << 8) | c

class drm_version(ctypes.Structure):
    _fields_ = [
        ("version_major",      ctypes.c_int),
        ("version_minor",      ctypes.c_int),
        ("version_patchlevel", ctypes.c_int),
        ("name_len",           ctypes.c_size_t),
        ("name",               ctypes.POINTER(ctypes.c_char)),
        ("date_len",           ctypes.c_size_t),
        ("date",               ctypes.POINTER(ctypes.c_char)),
        ("desc_len",           ctypes.c_size_t),
        ("desc",               ctypes.POINTER(ctypes.c_char)),
    ]

def get_version(fd):
    v = drm_version()
    fcntl.ioctl(fd, DRM_IOWR(0, v), v)

    v.name = ctypes.create_string_buffer(v.name_len)
    v.date = ctypes.create_string_buffer(v.date_len)
    v.desc = ctypes.create_string_buffer(v.desc_len)
    fcntl.ioctl(fd, DRM_IOWR(0, v), v)

    return f"{v.version_major}.{v.version_minor}.{v.version_patchlevel}",\
        v.name[:v.name_len].decode(), v.date[:v.date_len].decode(), v.desc[:v.desc_len].decode()

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} /dev/dri/cardN")
        sys.exit(1)
    try:
        with open(sys.argv[1], "rb") as drm_fd:
            ver, name, date, desc = get_version(drm_fd.fileno())
            print(f"DRM Version Info:")
            print(f" Version: {ver}")
            print(f" Name: {name}")
            print(f" Date: {date}")
            print(f" Description: {desc}")
    except FileNotFoundError:
        print(f"DRM device not found. Try checking {sys.argv[1]} or run as root.")
    except PermissionError:
        print("Permission denied. Try running as root.")
