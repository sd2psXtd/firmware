"""Run the built ARM GUI initialization and first splash/USB draws in Unicorn.

Usage: python test_gui_startup.py build/sd2psx.elf
Requires unicorn and pyelftools. Hardware, settings, and RP2040 ROM helpers are
stubbed; LVGL, its allocator, and rendering execute from the actual ELF.
This catches GUI heap exhaustion, not physical device startup failures.
"""
import sys,struct
from collections import deque
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import *
f=ELFFile(open(sys.argv[1],'rb')); syms={s.name:s['st_value']&~1 for s in f.get_section_by_name('.symtab').iter_symbols() if s['st_value']}
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
u.mem_map(0x10000000,0x1000000);u.mem_map(0x20000000,0x50000);u.mem_map(0x30000000,0x1000)
for s in f.iter_segments():
 if s['p_type']=='PT_LOAD' and s['p_filesz']:u.mem_write(s['p_vaddr'],s.data())
u.reg_write(UC_ARM_REG_SP,0x20042000-16);u.reg_write(UC_ARM_REG_LR,0x30000001)
R=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]
def reg(i):return u.reg_read(R[i])
def ret(v=0):u.reg_write(UC_ARM_REG_R0,v&0xffffffff);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
def cstr(a):
 b=bytearray()
 while a and len(b)<500:
  c=u.mem_read(a,1)[0];a+=1
  if not c:break
  b.append(c)
 return b.decode(errors='replace')
hooks={}
def put(names,fn):
 for n in names:
  if n in syms:hooks[syms[n]]=fn
put(['__wrap___clzl','__clzsi2'],lambda:ret(32-reg(0).bit_length()))
put(['__wrap___ctzsi2'],lambda:ret((reg(0)&-reg(0)).bit_length()-1 if reg(0) else 32))
def signed_div():
 a,b=reg(0),reg(1)
 a=a if a<2**31 else a-2**32;b=b if b<2**31 else b-2**32
 q=int(a/b);u.reg_write(UC_ARM_REG_R1,(a-q*b)&0xffffffff);ret(q)
put(['__wrap___aeabi_idiv'],signed_div)
put(['oled_init','oled_is_powered_on'],lambda:ret(1))
put(['oled_clear','oled_show','input_flip','splash_init','buffered_printf','oled_update_last_action_time'],lambda:ret())
def time_return():
 u.reg_write(UC_ARM_REG_R1, 0)
 ret(1)
put(['time_us_64','get_absolute_time'],time_return)
for n in syms:
 if n.startswith('settings_get_'):put([n],lambda:ret(0))
mca_enabled = False
mca_selections = []
def set_mca():
 global mca_enabled
 mca_enabled = bool(reg(0))
 mca_selections.append(mca_enabled)
 ret()
put(['settings_set_mca_enabled'],set_mca)
put(['settings_get_mca_enabled'],lambda:ret(mca_enabled))
hooks.pop(syms['settings_get_usb_mode'], None)
put(['settings_get_ps2_cardsize'],lambda:ret(8))
put(['sd_get_CID'],lambda:(u.mem_write(reg(0),bytes(16)),ret(reg(0))))
put(['__wrap_memset','memset'],lambda:(u.mem_write(reg(0),bytes([reg(1)&255])*reg(2)),ret(reg(0))))
put(['__wrap_memcpy','memcpy','__wrap___aeabi_memcpy'],lambda:(u.mem_write(reg(0),bytes(u.mem_read(reg(1),reg(2)))),ret(reg(0))))
put(['__wrap___aeabi_uidiv','__aeabi_uidiv'],lambda:ret(reg(0)//reg(1)))
def divmod_hook():
 a,b=reg(0),reg(1)
 u.reg_write(UC_ARM_REG_R1,a%b);ret(a//b)
put(['__wrap___aeabi_uidivmod','__aeabi_uidivmod'],divmod_hook)
def log():
    fmt = struct.unpack('<I', u.mem_read(u.reg_read(UC_ARM_REG_SP), 4))[0]
    print('LVGL:', cstr(reg(3)), cstr(fmt), flush=True)
    ret()
put(['_lv_log_add'], log)
trace = deque(maxlen=20)
last = None
repeat = 0
returned = False
rev = {v:k for k,v in syms.items()}
def step(uc, address, size, data):
    global last, repeat, returned
    if address == 0x30000000:
        returned = True
        u.emu_stop()
        return
    if address in hooks:
        hooks[address]()
        return
    if address in rev:
        trace.append(rev[address])
    repeat = repeat + 1 if address == last else 0
    last = address
    if repeat > 100:
        raise AssertionError(f'Firmware halted at {address:#x}: {list(trace)}')
u.hook_add(UC_HOOK_CODE, step)
def call(name, *args):
    global returned
    returned = False
    for i, value in enumerate(args):
        u.reg_write(R[i], value)
    u.reg_write(UC_ARM_REG_LR, 0x30000001)
    u.emu_start(syms[name] | 1, 0, count=90000000)
    assert returned, f'{name} did not return: {list(trace)}'
    return reg(0)
def heap(label):
    call('lv_mem_monitor', 0x20044000)
    values = struct.unpack('<6I2B', u.mem_read(0x20044000, 26))
    print(f'{label}: free={values[2]} largest={values[3]} bytes', flush=True)

call('gui_init')
heap('GUI initialized')
widget = struct.unpack('<I', u.mem_read(syms['splash_image'], 4))[0]
source = call('lv_img_get_src', widget)
screen = call('lv_obj_get_screen', widget)
pixels = bytes(u.mem_read(syms['splash_img'], 1032))
call('lv_tick_inc', 1000)
call('lv_timer_handler')
heap('Splash rendered')
# Select through real LVGL event handlers, including reselecting PS3 MCA.
menu_screen = struct.unpack('<I', u.mem_read(syms['scr_menu'], 4))[0]
labels = {}
def collect_labels(obj):
    if call('lv_obj_check_type', obj, syms['lv_label_class']):
        text = cstr(call('lv_label_get_text', obj))
        labels.setdefault(text, []).append(call('lv_obj_get_parent', obj))
    count = call('lv_obj_get_child_cnt', obj)
    for index in range(count):
        collect_labels(call('lv_obj_get_child', obj, index))
collect_labels(menu_screen)
usb_row = next(row for row in labels['USB Mode'] if call('lv_obj_get_child_cnt', row) == 2)
uart_row = next(row for row in labels['Serial'] if call('lv_obj_get_child_cnt', row) == 1)
mca_row = labels['PS3 MCA'][0]
mass_row = labels['Mass'][0]
call('lv_disp_load_scr', menu_screen)
for row, enabled in [(uart_row, False), (mca_row, True), (mca_row, True), (uart_row, False), (mca_row, True)]:
    call('lv_event_send', usb_row, 7, 0)  # LV_EVENT_CLICKED
    call('lv_event_send', row, 7, 0)
    assert mca_enabled == enabled
assert mca_selections == [False, True, True, False, True]
call('lv_event_send', usb_row, 7, 0)
call('lv_event_send', mass_row, 7, 0)
assert call('settings_get_usb_mode', 1) == 2
assert call('settings_get_usb_mode', 0) == 1
assert mca_enabled and mca_selections == [False, True, True, False, True]
heap('USB icon rendered')
assert call('lv_disp_get_scr_act', 0) == screen
assert call('lv_img_get_src', widget) == source
assert bytes(u.mem_read(syms['splash_img'], 1032)) != pixels
assert bytes(u.mem_read(syms['splash_img'], 1032)) == bytes(u.mem_read(syms['usb_splash_img'], 1032))
call('settings_set_usb_mode', call('settings_get_usb_mode', 0))
call('gui_usb_storage', 0)
# Normal GUI refresh reloads the card image into the same buffer after remount.
def read_card_image():
    u.mem_write(reg(0), pixels)
    ret(1)
put(['card_config_read_image'], read_card_image)
call('splash_update_current', 0, 0, 1)
assert bytes(u.mem_read(syms['splash_img'], 1032)) == pixels
assert call('lv_img_get_src', widget) == source
call('lv_refr_now', 0)
heap('Normal screen restored')
call('settings_set_usb_mode', 2)
call('gui_usb_storage', 1)
assert call('lv_disp_get_scr_act', 0) == screen
call('settings_set_usb_mode', call('settings_get_usb_mode', 0))
call('gui_usb_storage', 0)
call('splash_update_current', 0, 0, 1)
assert call('lv_img_get_src', widget) == source
assert bytes(u.mem_read(syms['splash_img'], 1032)) == pixels
print('GUI startup, USB menu selection, shared screen and image restoration passed')
