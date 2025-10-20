# MicroPython Human Interface Device library
# Copyright (C) 2021 H. Groefsema
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Asynchronous composite HID example (mouse + keyboard).

This example merges the mouse and keyboard async samples so both
peripherals run from the same BLE advertisement.  The mouse remains the
primary broadcast device (appearance 962) to satisfy iOS touch
requirements while exposing the keyboard report map as a second HID
service.
"""

import struct
import uasyncio as asyncio
from machine import Pin, SoftSPI

from bluetooth import UUID

from hid_services import (
    Advertiser,
    HumanInterfaceDevice,
    DSC_F_READ,
    F_READ,
    F_READ_NOTIFY,
    F_READ_WRITE_NORESPONSE,
    F_READ_WRITE_NOTIFY_NORESPONSE,
    GATTS,
    IRQ,
)


class CompositeHID(HumanInterfaceDevice):
    """Composite HID service exposing mouse and keyboard reports."""

    def __init__(self, name="uHID"):
        super().__init__(name)

        # Keep the appearance as a mouse so iOS treats the device as a
        # pointing device when advertising.
        self.device_appearance = 962

        # Mouse service definition (copied from Mouse.HIDS).
        self.mouse_hids = (
            UUID(0x1812),
            (
                (UUID(0x2A4A), F_READ),
                (UUID(0x2A4B), F_READ),
                (UUID(0x2A4C), F_READ_WRITE_NORESPONSE),
                (
                    UUID(0x2A4D),
                    F_READ_NOTIFY,
                    (
                        (UUID(0x2908), DSC_F_READ),
                    ),
                ),
                (UUID(0x2A4E), F_READ_WRITE_NORESPONSE),
            ),
        )

        # Keyboard service definition (copied from Keyboard.HIDS).
        self.keyboard_hids = (
            UUID(0x1812),
            (
                (UUID(0x2A4A), F_READ),
                (UUID(0x2A4B), F_READ),
                (UUID(0x2A4C), F_READ_WRITE_NORESPONSE),
                (
                    UUID(0x2A4D),
                    F_READ_NOTIFY,
                    (
                        (UUID(0x2908), DSC_F_READ),
                    ),
                ),
                (
                    UUID(0x2A4D),
                    F_READ_WRITE_NOTIFY_NORESPONSE,
                    (
                        (UUID(0x2908), DSC_F_READ),
                    ),
                ),
                (UUID(0x2A4E), F_READ_WRITE_NORESPONSE),
            ),
        )

        # Mouse and keyboard report descriptors (copied verbatim).
        self.mouse_report = [
            0x05,
            0x01,
            0x09,
            0x02,
            0xA1,
            0x01,
            0x85,
            0x01,
            0x09,
            0x01,
            0xA1,
            0x00,
            0x05,
            0x09,
            0x19,
            0x01,
            0x29,
            0x05,
            0x15,
            0x00,
            0x25,
            0x01,
            0x95,
            0x05,
            0x75,
            0x01,
            0x81,
            0x02,
            0x95,
            0x01,
            0x75,
            0x03,
            0x81,
            0x03,
            0x05,
            0x01,
            0x09,
            0x30,
            0x09,
            0x31,
            0x09,
            0x38,
            0x15,
            0x81,
            0x25,
            0x7F,
            0x75,
            0x08,
            0x95,
            0x03,
            0x81,
            0x06,
            0x05,
            0x0C,
            0x0A,
            0x38,
            0x02,
            0x15,
            0x81,
            0x25,
            0x7F,
            0x75,
            0x08,
            0x95,
            0x01,
            0x81,
            0x06,
            0xC0,
            0xC0,
        ]

        self.keyboard_report = [
            0x05,
            0x01,
            0x09,
            0x06,
            0xA1,
            0x01,
            0x85,
            0x02,
            0x75,
            0x01,
            0x95,
            0x08,
            0x05,
            0x07,
            0x19,
            0xE0,
            0x29,
            0xE7,
            0x15,
            0x00,
            0x25,
            0x01,
            0x81,
            0x02,
            0x95,
            0x01,
            0x75,
            0x08,
            0x81,
            0x01,
            0x95,
            0x05,
            0x75,
            0x01,
            0x05,
            0x08,
            0x19,
            0x01,
            0x29,
            0x05,
            0x91,
            0x02,
            0x95,
            0x01,
            0x75,
            0x03,
            0x91,
            0x01,
            0x95,
            0x06,
            0x75,
            0x08,
            0x15,
            0x00,
            0x25,
            0x65,
            0x05,
            0x07,
            0x19,
            0x00,
            0x29,
            0x65,
            0x81,
            0x00,
            0xC0,
        ]

        self.services.append(self.mouse_hids)
        self.services.append(self.keyboard_hids)

        # Mouse state.
        self.mouse_x = 0
        self.mouse_y = 0
        self.mouse_w = 0
        self.mouse_button1 = 0
        self.mouse_button2 = 0
        self.mouse_button3 = 0

        # Keyboard state.
        self.kb_modifiers = 0
        self.kb_keys = [0x00] * 6
        self.kb_callback = None

    def ble_irq(self, event, data):
        if event == IRQ.GATTS_WRITE:
            conn_handle, attr_handle = data
            if getattr(self, "h_keyboard_repout", None) == attr_handle:
                print("Keyboard changed by client")
                report = self._ble.gatts_read(attr_handle)
                bytes_report = struct.unpack("B", report)
                if self.kb_callback is not None:
                    self.kb_callback(bytes_report)
                return GATTS.NO_ERROR
        return super().ble_irq(event, data)

    def start(self):
        super().start()

        print("Registering services")
        handles = self._ble.gatts_register_services(self.services)
        self.save_service_characteristics(handles)
        self.write_service_characteristics()
        self.adv = Advertiser(self._ble, [UUID(0x1812)], self.device_appearance, self.device_name)
        print("Server started")

    def save_service_characteristics(self, handles):
        super().save_service_characteristics(handles)

        (h_info, h_hid, h_ctrl, self.h_mouse_rep, h_d1, self.h_mouse_proto) = handles[3]
        (
            k_info,
            k_hid,
            k_ctrl,
            self.h_keyboard_rep,
            k_d1,
            self.h_keyboard_repout,
            k_d2,
            self.h_keyboard_proto,
        ) = handles[4]

        mouse_buttons = (
            self.mouse_button1 + self.mouse_button2 * 2 + self.mouse_button3 * 4
        )
        mouse_state = struct.pack("Bbbb", mouse_buttons, self.mouse_x, self.mouse_y, self.mouse_w)

        keyboard_state = struct.pack(
            "8B",
            self.kb_modifiers,
            0,
            self.kb_keys[0],
            self.kb_keys[1],
            self.kb_keys[2],
            self.kb_keys[3],
            self.kb_keys[4],
            self.kb_keys[5],
        )

        self.characteristics[h_info] = ("Mouse HID information", b"\x01\x01\x00\x00")
        self.characteristics[h_hid] = ("Mouse HID input report map", bytes(self.mouse_report))
        self.characteristics[h_ctrl] = ("Mouse HID control point", b"\x00")
        self.characteristics[self.h_mouse_rep] = ("Mouse HID report", mouse_state)
        self.characteristics[h_d1] = ("Mouse HID reference", struct.pack("<BB", 1, 1))
        self.characteristics[self.h_mouse_proto] = ("Mouse HID protocol mode", b"\x01")

        self.characteristics[k_info] = ("Keyboard HID information", b"\x01\x01\x00\x00")
        self.characteristics[k_hid] = ("Keyboard HID input report map", bytes(self.keyboard_report))
        self.characteristics[k_ctrl] = ("Keyboard HID control point", b"\x00")
        self.characteristics[self.h_keyboard_rep] = ("Keyboard HID input report", keyboard_state)
        self.characteristics[k_d1] = ("Keyboard HID input reference", struct.pack("<BB", 2, 1))
        self.characteristics[self.h_keyboard_repout] = ("Keyboard HID output report", keyboard_state)
        self.characteristics[k_d2] = ("Keyboard HID output reference", struct.pack("<BB", 2, 2))
        self.characteristics[self.h_keyboard_proto] = ("Keyboard HID protocol mode", b"\x01")

    def set_mouse_axes(self, x=0, y=0):
        self.mouse_x = max(-127, min(127, x))
        self.mouse_y = max(-127, min(127, y))

    def set_mouse_wheel(self, w=0):
        self.mouse_w = max(-127, min(127, w))

    def set_mouse_buttons(self, b1=0, b2=0, b3=0):
        self.mouse_button1 = b1
        self.mouse_button2 = b2
        self.mouse_button3 = b3

    def set_keyboard_modifiers(
        self,
        right_gui=0,
        right_alt=0,
        right_shift=0,
        right_control=0,
        left_gui=0,
        left_alt=0,
        left_shift=0,
        left_control=0,
    ):
        self.kb_modifiers = (
            (right_gui << 7)
            + (right_alt << 6)
            + (right_shift << 5)
            + (right_control << 4)
            + (left_gui << 3)
            + (left_alt << 2)
            + (left_shift << 1)
            + left_control
        )

    def set_keyboard_keys(self, k0=0x00, k1=0x00, k2=0x00, k3=0x00, k4=0x00, k5=0x00):
        self.kb_keys = [k0, k1, k2, k3, k4, k5]

    def notify_mouse_report(self):
        if self.is_connected():
            mouse_buttons = (
                self.mouse_button1 + self.mouse_button2 * 2 + self.mouse_button3 * 4
            )
            state = struct.pack("Bbbb", mouse_buttons, self.mouse_x, self.mouse_y, self.mouse_w)
            self.characteristics[self.h_mouse_rep] = ("Mouse HID report", state)
            self._ble.gatts_notify(self.conn_handle, self.h_mouse_rep, state)
            print("Notify mouse report:", struct.unpack("Bbbb", state))

    def notify_keyboard_report(self):
        if self.is_connected():
            state = struct.pack(
                "8B",
                self.kb_modifiers,
                0,
                self.kb_keys[0],
                self.kb_keys[1],
                self.kb_keys[2],
                self.kb_keys[3],
                self.kb_keys[4],
                self.kb_keys[5],
            )
            self.characteristics[self.h_keyboard_rep] = ("Keyboard HID input report", state)
            self._ble.gatts_notify(self.conn_handle, self.h_keyboard_rep, state)
            print("Notify keyboard report:", struct.unpack("8B", state))

    def set_kb_callback(self, kb_callback):
        self.kb_callback = kb_callback


class Device:
    def __init__(self, name="uHID"):
        self.axes = (0, 0)
        self.keys = [0x00] * 6
        self.mouse_updated = False
        self.keyboard_updated = False
        self.active = True

        # Define buttons (shared with original examples).
        self.pin_forward = Pin(5, Pin.IN)
        self.pin_reverse = Pin(23, Pin.IN)
        self.pin_right = Pin(19, Pin.IN)
        self.pin_left = Pin(18, Pin.IN)

        self.device = CompositeHID(name)
        self.device.set_state_change_callback(self.device_state_callback)

    def device_state_callback(self):
        state = self.device.get_state()
        if state is CompositeHID.DEVICE_IDLE:
            return
        if state is CompositeHID.DEVICE_ADVERTISING:
            return
        if state is CompositeHID.DEVICE_CONNECTED:
            return

    def advertise(self):
        self.device.start_advertising()

    def stop_advertise(self):
        self.device.stop_advertising()

    async def advertise_for(self, seconds=30):
        self.advertise()
        while seconds > 0 and self.device.get_state() is CompositeHID.DEVICE_ADVERTISING:
            await asyncio.sleep(1)
            seconds -= 1
        if self.device.get_state() is CompositeHID.DEVICE_ADVERTISING:
            self.stop_advertise()

    async def gather_input(self):
        while self.active:
            prev_axes = self.axes
            prev_keys = self.keys[:]

            x = self.pin_right.value() * 127 - self.pin_left.value() * 127
            y = self.pin_forward.value() * 127 - self.pin_reverse.value() * 127
            self.axes = (x, y)

            keys = [0x00] * 6
            if self.pin_forward.value():
                keys[0] = 0x1A  # W
            if self.pin_left.value():
                keys[1] = 0x04  # A
            if self.pin_reverse.value():
                keys[2] = 0x16  # S
            if self.pin_right.value():
                keys[3] = 0x07  # D
            self.keys = keys

            if prev_axes != self.axes:
                self.mouse_updated = True
            if prev_keys != self.keys:
                self.keyboard_updated = True

            await asyncio.sleep_ms(50)

    async def notify(self):
        while self.active:
            state = self.device.get_state()
            if self.mouse_updated or self.keyboard_updated:
                if state is CompositeHID.DEVICE_CONNECTED:
                    if self.mouse_updated:
                        self.device.set_mouse_axes(*self.axes)
                        self.device.notify_mouse_report()
                    if self.keyboard_updated:
                        self.device.set_keyboard_keys(*self.keys)
                        self.device.notify_keyboard_report()
                elif state is CompositeHID.DEVICE_IDLE:
                    await self.advertise_for(30)
                self.mouse_updated = False
                self.keyboard_updated = False

            if state is CompositeHID.DEVICE_CONNECTED:
                await asyncio.sleep_ms(50)
            else:
                await asyncio.sleep(2)

    async def co_start(self):
        if self.device.get_state() is CompositeHID.DEVICE_STOPPED:
            self.device.start()
            self.active = True
            await asyncio.gather(
                self.advertise_for(30),
                self.gather_input(),
                self.notify(),
            )

    async def co_stop(self):
        self.active = False
        self.device.stop()

    def start(self):
        asyncio.run(self.co_start())

    def stop(self):
        asyncio.run(self.co_stop())


if __name__ == "__main__":
    device = Device()
    device.start()
