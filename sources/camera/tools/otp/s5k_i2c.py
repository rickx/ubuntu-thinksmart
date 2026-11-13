#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Mei Tao
"""Minimal 16-bit-address / 8-bit-data I2C access to the S5KC505A on the Qualcomm CCI bus via I2C_RDWR.
Writes are restricted by a whitelist so that the OTP *program* command can never be issued."""
import ctypes, fcntl, os, time
I2C_RDWR = 0x0707; I2C_M_RD = 0x0001
class Msg(ctypes.Structure):
    _fields_ = [('addr', ctypes.c_uint16), ('flags', ctypes.c_uint16), ('len', ctypes.c_uint16), ('buf', ctypes.POINTER(ctypes.c_uint8))]
class RdWr(ctypes.Structure):
    _fields_ = [('msgs', ctypes.POINTER(Msg)), ('nmsgs', ctypes.c_uint32)]
# whitelist: register -> allowed values (OTP page select, OTP control read/clear/stop). 0x03 (= program) is NOT allowed.
ALLOWED_WRITES = {0x0A02: set(range(0x00, 0x40)), 0x0A00: {0x00, 0x01, 0x04}, 0x0B00: {0x00, 0x01}, 0x0101: {0x00, 0x01, 0x02, 0x03},
                  # Samsung GAS/LSC SRAM loader (volatile): 20-bit value = [0x3214 low nibble | 0x3215 | 0x3216], address 0x3217 (0..143), command 0x3218 1->0
                  0x3214: set(range(0x00, 0x10)), 0x3215: set(range(0x100)), 0x3216: set(range(0x100)), 0x3217: set(range(0x00, 0x90)), 0x3218: {0x00, 0x01}}
# Note: 0x0A00 = 0x03 (OTP program) is deliberately absent.
class S5K:
    def __init__(self, bus=3, addr=0x37):
        self.fd = os.open('/dev/i2c-%d' % bus, os.O_RDWR); self.addr = addr
    def _xfer(self, msgs):
        arr = (Msg * len(msgs))(*msgs); data = RdWr(arr, len(msgs))
        fcntl.ioctl(self.fd, I2C_RDWR, data)
    def read(self, reg, n):
        wb = (ctypes.c_uint8 * 2)(reg >> 8, reg & 0xff); rb = (ctypes.c_uint8 * n)()
        self._xfer([Msg(self.addr, 0, 2, wb), Msg(self.addr, I2C_M_RD, n, rb)])
        return bytes(rb)
    def write8(self, reg, val):
        if reg not in ALLOWED_WRITES or val not in ALLOWED_WRITES[reg]:
            raise PermissionError('write 0x%04x=0x%02x is not whitelisted' % (reg, val))
        wb = (ctypes.c_uint8 * 3)(reg >> 8, reg & 0xff, val)
        self._xfer([Msg(self.addr, 0, 3, wb)])
    def close(self): os.close(self.fd)
