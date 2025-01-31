/*
 * ps3mca-tool - PlayStation 3 Memory Card Adaptor Software
 * Copyright (C) 2011 - jimmikaelkael <jimmikaelkael@wanadoo.fr>
 * Copyright (C) 2011 - "someone who wants to stay anonymous"
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <memory.h>
#include <stdlib.h>
#include <inttypes.h>

void memrcpy(void *dst, void *src, size_t len);
void memxor(const void *a, const void *b, void *Result, size_t Length);
void append_le_uint16(uint8_t *buf, uint16_t val);
void append_le_uint32(uint8_t *buf, uint32_t val);
void append_le_uint64(uint8_t *buf, uint64_t val);
uint16_t read_le_uint16(const uint8_t *buf);
uint32_t read_le_uint32(const uint8_t *buf);
uint64_t read_le_uint64(const uint8_t *buf);

#endif

