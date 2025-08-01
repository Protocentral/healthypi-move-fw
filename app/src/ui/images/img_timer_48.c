/*
 * HealthyPi Move
 * 
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Author: Ashwin Whitchurch, Protocentral Electronics
 * Contact: ashwin@protocentral.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */



#include <lvgl.h>

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_TIMER_48
#define LV_ATTRIBUTE_IMG_TIMER_48
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_TIMER_48
uint8_t img_timer_48_map[] = {

    0x4c,0x70,0x47,0x00,0x66,0x99,0x34,0x05,0x4e,0xff,0x0d,0x01,0x7e,0xbf,0x40,0x04,
    0x58,0xaa,0x58,0x03,0x00,0x00,0x00,0x01,0x80,0x80,0x00,0x02,0x6d,0x92,0x26,0x07,
    0x60,0xa5,0x36,0xad,0x55,0xaa,0x37,0x06,0x80,0xa7,0x78,0x02,0x60,0xa5,0x36,0xe0,
    0x40,0x80,0x40,0x04,0x60,0xa4,0x37,0x87,0x61,0xa5,0x38,0xf9,0x60,0xa4,0x35,0x8a,
    0x74,0xc2,0x5d,0x05,0x92,0xcf,0xa3,0xfe,0x8a,0xc8,0x95,0xfb,0x89,0xc7,0x92,0xfa,
    0x91,0xce,0xa2,0xfc,0x92,0xd0,0xa4,0xfd,0x6a,0xae,0x50,0x41,0x69,0xab,0x54,0x10,
    0x8e,0xcc,0x9d,0xfb,0x69,0xad,0x4d,0x1e,0x8c,0xca,0x99,0xfb,0x60,0xa3,0x34,0xf9,
    0x60,0xa4,0x35,0x91,0x6a,0xae,0x50,0x59,0x67,0xad,0x4a,0x96,0x8b,0xc9,0x97,0xfb,
    0x83,0xc2,0x88,0xfa,0x67,0xac,0x47,0xbb,0x65,0xa8,0x43,0x4a,0x6c,0xae,0x54,0x2a,
    0x7a,0xba,0x76,0xfb,0x84,0xc4,0x8b,0xfb,0x87,0xc5,0x8e,0xfa,0x74,0xb3,0x60,0x0a,
    0x74,0xb6,0x69,0xfc,0x67,0xac,0x48,0xd7,0x67,0xac,0x49,0xb5,0x6c,0xb0,0x54,0x35,
    0x66,0xab,0x45,0xc4,0x68,0xad,0x4a,0x7b,0x66,0xac,0x45,0xad,0x67,0xad,0x4a,0x8f,
    0x6c,0xaf,0x47,0x07,0x67,0xac,0x4b,0x82,0x68,0xac,0x4b,0x9a,0x61,0xa4,0x35,0x85,
    0x60,0xa4,0x36,0xde,0x61,0xa5,0x37,0xf7,0x60,0xa4,0x35,0x8f,0x60,0xa6,0x37,0xc1,
    0x60,0xa3,0x34,0xcd,0x60,0xa3,0x34,0xc0,0x60,0xa4,0x33,0xd0,0x68,0xad,0x4b,0x89,
    0x8f,0xcc,0x9e,0xfd,0x72,0xb4,0x64,0xfc,0x66,0xac,0x45,0xc8,0x69,0xae,0x4f,0x46,
    0x69,0xad,0x50,0x32,0x65,0xaa,0x47,0x24,0x8d,0xcb,0x9a,0xfb,0x78,0xb9,0x71,0xfd,
    0x6a,0xb1,0x4e,0x0c,0x64,0xaa,0x3e,0xe7,0x67,0xad,0x49,0xa2,0x6a,0xae,0x4f,0x68,
    0x90,0xce,0xa0,0xfd,0x67,0xab,0x45,0xaa,0x64,0xa9,0x3d,0xef,0x7e,0xbd,0x7e,0xfd,
    0x67,0xad,0x49,0xe9,0x69,0xae,0x4e,0x72,0x68,0xad,0x4a,0xbf,0x67,0xac,0x47,0xf0,
    0x64,0xab,0x40,0xcb,0x67,0xac,0x48,0xf8,0x64,0xaa,0x3e,0xe2,0x67,0xad,0x48,0xb8,
    0x60,0xa3,0x34,0xf7,0x61,0xa4,0x36,0xfb,0x5f,0xa2,0x32,0x41,0x60,0xa5,0x36,0xe7,
    0x5f,0xa5,0x36,0xb3,0x60,0xa3,0x34,0xdb,0x65,0xa9,0x43,0x4f,0x65,0xa9,0x44,0x63,
    0x67,0xab,0x43,0x53,0x65,0xab,0x44,0xe8,0x65,0xab,0x40,0xf6,0x70,0xad,0x57,0x19,
    0x66,0xab,0x44,0xd0,0x66,0xac,0x45,0xf9,0x7c,0xbd,0x7c,0xfb,0x7f,0xbf,0x81,0xf9,
    0x63,0xaa,0x3b,0xf9,0x6b,0xae,0x51,0x3b,0x81,0xc0,0x85,0xfc,0x64,0xab,0x3f,0xd7,
    0x65,0xab,0x43,0xef,0x66,0xac,0x48,0xf3,0x64,0xaa,0x3d,0xdf,0x69,0xae,0x4d,0x8d,
    0x69,0xab,0x4b,0x38,0x62,0xa6,0x38,0x72,0x61,0xa4,0x36,0xa5,0x60,0xa3,0x34,0xc8,
    0x60,0xa3,0x35,0xbd,0x61,0xa4,0x34,0xbe,0x60,0xa3,0x34,0x80,0x80,0xc0,0x83,0xfb,
    0x7a,0xbc,0x78,0xfa,0x68,0xae,0x49,0x84,0x67,0xae,0x51,0x2f,0x68,0xae,0x51,0x16,
    0x6a,0xad,0x4d,0x60,0x68,0xac,0x48,0xa0,0x67,0xae,0x4d,0x74,0x63,0xaa,0x3e,0xda,
    0x67,0xac,0x48,0xdc,0x6c,0xaf,0x56,0xfa,0x67,0xab,0x47,0x77,0xff,0xff,0xff,0xff,
    0xfd,0xfe,0xfd,0xff,0xfe,0xfe,0xfe,0xff,0x94,0xd1,0xa7,0xff,0x94,0xd0,0xa6,0xff,
    0x93,0xd0,0xa5,0xff,0xff,0xff,0xfe,0xff,0x96,0xd2,0xa9,0xff,0x95,0xd1,0xa7,0xff,
    0x94,0xd1,0xa6,0xff,0xfd,0xfd,0xfd,0xff,0x95,0xd1,0xa8,0xff,0xfe,0xfe,0xfd,0xff,
    0x92,0xce,0xa2,0xff,0x90,0xcc,0x9e,0xff,0x6c,0xb7,0x46,0xff,0x6b,0xb7,0x40,0xff,
    0x69,0xb0,0x49,0xff,0xa7,0xc7,0x9d,0xff,0xe9,0xf1,0xe8,0xff,0x50,0xa7,0x22,0xff,
    0x88,0xc6,0x91,0xff,0x92,0xcd,0xa0,0xff,0xb8,0xd2,0xb0,0xff,0xd1,0xe1,0xcd,0xff,
    0xbf,0xd7,0xb9,0xff,0xfa,0xfc,0xfa,0xff,0xf6,0xf9,0xf6,0xff,0x78,0xbb,0x76,0xff,
    0x93,0xcf,0xa4,0xff,0x4d,0xa5,0x01,0xff,0x6a,0xb5,0x40,0xff,0x53,0xa8,0x28,0xff,
    0x6c,0xaf,0x50,0xff,0x6d,0xb6,0x4c,0xff,0x7b,0xbd,0x7b,0xff,0x6e,0xb0,0x54,0xff,
    0x69,0xb2,0x44,0xff,0x64,0xab,0x3d,0xff,0x6b,0xb6,0x44,0xff,0x66,0xae,0x3e,0xff,
    0x70,0xb9,0x54,0xff,0x72,0xb9,0x5d,0xff,0x6d,0xb8,0x4a,0xff,0x84,0xb5,0x71,0xff,
    0xdf,0xea,0xdd,0xff,0xf5,0xf8,0xf4,0xff,0xab,0xcb,0xa1,0xff,0x7a,0xb4,0x64,0xff,
    0xc3,0xda,0xbe,0xff,0x4e,0xa6,0x19,0xff,0x59,0xa8,0x2e,0xff,0x49,0xa3,0x00,0xff,
    0x8f,0xcb,0x9b,0xff,0x5e,0xae,0x49,0xff,0x87,0xc5,0x8f,0xff,0x69,0xb3,0x3e,0xff,
    0x8d,0xca,0x98,0xff,0x7f,0xc0,0x83,0xff,0x67,0xb1,0x3e,0xff,0x76,0xb9,0x6b,0xff,
    0x6c,0xb2,0x5b,0xff,0x6c,0xaa,0x4c,0xff,0xd6,0xe5,0xd3,0xff,0x5c,0xa9,0x32,0xff,
    0x65,0xaa,0x43,0xff,0x4e,0xa6,0x11,0xff,0x94,0xcf,0xa4,0xff,0x4e,0xa5,0x0c,0xff,
    0x73,0xb9,0x71,0xff,0x8b,0xc8,0x94,0xff,0x73,0xb7,0x6c,0xff,0x73,0xb7,0x64,0xff,
    0x6c,0xb8,0x42,0xff,0x75,0xba,0x67,0xff,0xb5,0xcf,0xac,0xff,0x7b,0xb1,0x64,0xff,
    0x89,0xba,0x78,0xff,0xfc,0xfd,0xfc,0xff,0xcd,0xdf,0xc9,0xff,0xed,0xf4,0xec,0xff,
    0x83,0xb8,0x71,0xff,0x99,0xc3,0x8c,0xff,0xda,0xe8,0xd7,0xff,0x8d,0xbd,0x7e,0xff,
    0x7f,0xb6,0x6b,0xff,0x9f,0xc5,0x94,0xff,0xef,0xf5,0xef,0xff,0x61,0xaf,0x4d,0xff,
    0x62,0xb0,0x52,0xff,0x71,0xb0,0x57,0xff,0x85,0xc2,0x89,0xff,0x78,0xbc,0x78,0xff,
    0x61,0xaa,0x3b,0xff,0x81,0xc1,0x85,0xff,0x6f,0xb9,0x4f,0xff,0x80,0xc0,0x81,0xff,
    0x67,0xad,0x47,0xff,0x72,0xbe,0x4f,0xff,0x75,0xae,0x5b,0xff,0x64,0xab,0x37,0xff,
    0x70,0xac,0x53,0xff,0x61,0xa6,0x36,0xff,0xbd,0xd4,0xb6,0xff,0xc3,0xd7,0xbd,0xff,
    0x63,0xa8,0x38,0xff,0xe4,0xee,0xe3,0xff,0xaf,0xce,0xa8,0xff,0x53,0xa9,0x32,0xff,
    0x6e,0xb5,0x5f,0xff,0x55,0xab,0x38,0xff,0xa3,0xc8,0x99,0xff,0x77,0xb9,0x70,0xff,
    0x6d,0xb2,0x58,0xff,0xa6,0xca,0x9d,0xff,0x71,0xb5,0x63,0xff,0x6d,0xb4,0x4f,0xff,
    0x71,0xb8,0x5c,0xff,0x6a,0xb5,0x45,0xff,0x78,0xba,0x73,0xff,0x85,0xc4,0x8c,0xff,
    0xc7,0xda,0xc1,0xff,0x9c,0xc0,0x8f,0xff,0x0b,0x94,0x00,0xff,0xf7,0xfb,0xf7,0xff,
    0x7e,0xb5,0x6a,0xff,0x57,0xa9,0x33,0xff,0x7a,0xcc,0x55,0xff,0x78,0xc8,0x54,0xff,

    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x3f,0x90,0xe0,0x69,0x4f,0x90,0xa1,0x5d,0x17,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x40,0x2c,0x53,0x4f,0xe0,0x53,0x21,0x79,0x27,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0a,0x09,0x00,0x00,0x00,0x2a,0x51,0x00,0x00,0x00,0x02,0x04,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x01,0x02,0x00,0x00,0x00,0x00,0x00,0x53,0x51,0x04,0x00,0x00,0x00,0x00,0x00,0x01,0x04,0x00,0x00,0x00,0x02,0x00,0x00,0x06,0x02,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x03,0x00,0x00,0x00,0x19,0x5c,0x31,0x49,0x21,0x68,0xc0,0x2c,0x2e,0x2f,0x78,0x23,0x00,0x00,0x00,0x04,0x04,0x00,0x00,0x17,0x16,0x00,0x00,0x04,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x01,0x00,0x00,0x5f,0x4d,0x3e,0x64,0xb7,0xaa,0xa8,0xa9,0xbc,0xbc,0xa9,0xa8,0xaa,0x9e,0xa5,0x67,0x3b,0x23,0x00,0x00,0x27,0x00,0x7e,0xfe,0x32,0x00,0x00,0x03,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x02,0x00,0x5f,0x1e,0x5e,0x8f,0xa8,0xef,0x20,0x12,0x18,0x14,0x8c,0x8c,0x14,0x18,0x1f,0x25,0x9b,0xf4,0xa6,0xa5,0x2a,0x2b,0x00,0x00,0x19,0x4e,0xe1,0x21,0x27,0x00,0x04,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x4d,0x5e,0xa6,0xf2,0x25,0x3c,0x83,0x82,0x86,0x86,0x86,0x86,0x86,0x86,0x86,0x86,0x86,0x87,0x14,0x13,0xc6,0xf5,0xa7,0x32,0x17,0x00,0x2f,0xaa,0xe1,0x7c,0x41,0x00,0x0a,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x17,0x4e,0x8f,0xf0,0x26,0x11,0x82,0x87,0x87,0xc2,0xb4,0xb6,0xb9,0xa2,0xdb,0xa2,0xb9,0x94,0x8d,0x83,0x8a,0x86,0x84,0x1f,0x28,0xc8,0x29,0x6b,0xe1,0x21,0x31,0xff,0x2d,0x00,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x23,0x4c,0x9e,0x24,0x48,0x82,0x88,0xc2,0xc5,0xc6,0xed,0xb1,0xbf,0xa3,0xaf,0xd4,0xaf,0xa0,0xbf,0xb1,0xb5,0xa2,0x8d,0x82,0x82,0x83,0x20,0x90,0xaa,0x4c,0x30,0x00,0x6c,0x44,0x00,0x05,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x76,0x51,0xa4,0x20,0x82,0x84,0x84,0xb6,0xd7,0xc1,0xd0,0x96,0xbe,0x92,0xad,0x99,0xcd,0x99,0xad,0x92,0x97,0xf1,0xa0,0xc1,0xf6,0x95,0x83,0x82,0x1f,0x7d,0xa5,0x4d,0x00,0x00,0x00,0x02,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x19,0x69,0xa4,0x26,0x87,0x84,0x8d,0xec,0x9d,0xd5,0xac,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0xad,0xb0,0xa0,0xfd,0x94,0x84,0x83,0x3c,0xbc,0x8e,0x47,0x00,0x09,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x0a,0x29,0xa4,0x26,0x87,0x84,0xc5,0xb2,0xaf,0xd2,0x7f,0x7f,0x7f,0x80,0x80,0x81,0x85,0x80,0x80,0x85,0x81,0x81,0x80,0x8b,0x7f,0x7f,0xad,0xee,0xb3,0xdf,0x84,0x84,0x3c,0xa3,0xa4,0x16,0x00,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x03,0x0c,0x0c,0x0c,0x0c,0x0c,0x03,0x04,0x00,0x00,0x03,0x00,0x32,0x8f,0x63,0x87,0x84,0xc5,0x9d,0xd1,0xfb,0x7f,0x81,0x80,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x89,0x7f,0x7f,0x98,0xb3,0xdf,0x84,0x83,0x1a,0x90,0x4c,0x17,0x00,0x0a,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x16,0xa4,0x28,0x9c,0x84,0xb8,0xb1,0xd5,0x7f,0x7f,0xcd,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0xac,0xce,0x7f,0x85,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x81,0x7f,0xb0,0xb3,0xf7,0x84,0x82,0x20,0x8f,0x46,0x00,0x03,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x56,0x6f,0x71,0x39,0x39,0x39,0x70,0x38,0x72,0x00,0x0a,0x00,0x60,0xf3,0x1a,0x83,0x8c,0xb5,0xd3,0x81,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0x91,0xbd,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x85,0x81,0x7f,0xea,0xc3,0xb4,0x9c,0x84,0x28,0xba,0x65,0x00,0x0c,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x22,0x59,0x38,0x3a,0x3a,0x3a,0x38,0x59,0x36,0x00,0x00,0x1d,0x9e,0x62,0x82,0x83,0xdb,0xa5,0xcf,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xca,0xcb,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0xcd,0xd0,0xbc,0x84,0x84,0x1f,0xa1,0x21,0x00,0x04,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,0x3e,0xf3,0x18,0x88,0xb4,0x9d,0xb0,0x7f,0xcd,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xca,0xe2,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xac,0x9d,0xb6,0x83,0x83,0x43,0xa7,0x2b,0x00,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x0c,0x09,0x30,0x07,0x07,0x07,0x07,0x07,0x04,0x09,0x00,0x2b,0xa7,0x24,0x83,0x87,0xc4,0xcb,0x99,0x7f,0x85,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xca,0xe2,0x7f,0x7f,0x85,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0xd1,0xd8,0x83,0x83,0x12,0xa6,0x1e,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x3b,0x8e,0x13,0x82,0x8c,0x9f,0x98,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0x96,0xe4,0x7f,0x7f,0x85,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xd2,0xc3,0xb8,0x83,0x11,0xc7,0x52,0x30,
    0x1c,0x58,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x58,0x1c,0x03,0x00,0x00,0x60,0xf4,0x14,0x87,0xb6,0xb2,0x92,0x7f,0x81,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0x96,0xe4,0x81,0x7f,0x85,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x85,0x7f,0x99,0xaf,0x9b,0x87,0x87,0x4b,0xa7,0x65,
    0x37,0x57,0x34,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x34,0x57,0x37,0x30,0x00,0x19,0x64,0xf6,0x83,0x8a,0xc4,0xd0,0x81,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xe6,0xbd,0x81,0x7f,0x85,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xae,0xb5,0x83,0x88,0x13,0x8e,0x7a,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x3f,0xba,0x66,0x82,0x83,0xd7,0xae,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xe6,0xbd,0x89,0x7f,0x81,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xce,0x93,0x95,0x88,0x18,0xde,0x46,
    0x06,0x00,0x05,0x05,0x05,0x05,0x05,0x02,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x10,0x00,0x47,0xa6,0x13,0x82,0xc2,0xeb,0xb0,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0x98,0xc0,0xcd,0x7f,0x81,0x7f,0x7f,0x7f,0x85,0x81,0x7f,0x7f,0x7f,0x7f,0x8b,0x7f,0xe9,0x9f,0xc5,0x88,0x14,0xa9,0x2c,
    0x00,0x05,0x05,0x05,0x05,0x05,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x75,0xaa,0x1f,0x82,0x95,0x93,0x97,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xe7,0xbd,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0xd6,0xc0,0xf7,0x87,0x11,0xc9,0x67,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x6d,0x36,0x0d,0x0f,0x0f,0x0f,0x0f,0x0f,0x0d,0x1c,0x5b,0x02,0x00,0x1e,0xde,0x42,0x82,0x8d,0x93,0xbe,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xe7,0xfa,0xab,0xab,0xab,0xab,0xab,0xcc,0xcb,0xf8,0x7f,0x81,0x7f,0x7f,0x81,0x7f,0x9a,0xa3,0xdd,0x8a,0x15,0xbb,0x6a,
    0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x44,0x34,0xe5,0x35,0x0e,0x0e,0x0e,0x0e,0x0e,0x35,0xe8,0x37,0x02,0x00,0x1e,0xde,0x42,0x82,0x8d,0x93,0xbe,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0x92,0xae,0x91,0x91,0x91,0x91,0x91,0x91,0xf9,0x97,0x7f,0x81,0x7f,0x7f,0x85,0x7f,0x9a,0xd9,0xb9,0x8a,0x15,0xbb,0x52,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x01,0x09,0x09,0x09,0x09,0x09,0x09,0x07,0x00,0x04,0x00,0x2f,0xaa,0x1a,0x82,0x95,0x93,0xce,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x85,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x85,0x7f,0x7f,0x7f,0x81,0x7f,0xad,0xa0,0xdd,0x8a,0x11,0xc9,0x7b,
    0x00,0x00,0x06,0x01,0x01,0x01,0x01,0x10,0x0c,0x02,0x06,0x06,0x06,0x06,0x06,0x00,0x00,0x00,0x00,0x09,0x00,0x2d,0x8e,0x12,0x88,0xc2,0xed,0x98,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x80,0x8b,0x8b,0x8b,0x8b,0x8b,0x8b,0x80,0x81,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0xcf,0xdc,0xb6,0x87,0x14,0xa9,0x50,
    0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x02,0x00,0x03,0x00,0x1d,0x9e,0x25,0x88,0x87,0xd8,0xee,0x7f,0x81,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xac,0x93,0xb8,0x82,0x18,0xa8,0x2e,
    0x06,0x00,0x41,0x0f,0x33,0x0d,0x0d,0x0d,0x0d,0x0d,0x0d,0x0d,0x0d,0x33,0x36,0x5c,0x00,0x06,0x00,0x04,0x00,0x40,0xa5,0xa2,0x82,0x87,0x9b,0xfc,0xcd,0x7f,0x85,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xb0,0x9f,0x8c,0x82,0x12,0x8e,0x31,
    0x0c,0x00,0x5a,0xe8,0x54,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x35,0xe3,0x6e,0x00,0x04,0x00,0x02,0x00,0x27,0x45,0xc7,0x15,0x83,0x94,0x9f,0xe9,0x7f,0x8b,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0xd1,0xbc,0x87,0x88,0x66,0xba,0x22,
    0x00,0x05,0x00,0x03,0x30,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x09,0x09,0x07,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x49,0xa1,0x1a,0x83,0x8c,0xeb,0x96,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0xd6,0xa5,0xdd,0x87,0x15,0xbb,0x4a,0x17,
    0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x06,0x06,0x01,0x01,0x01,0x01,0x01,0x30,0x00,0x1d,0xb7,0x73,0x88,0x87,0x9b,0xd9,0x9a,0x7f,0x81,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0x98,0x93,0x95,0x88,0x1a,0xa1,0x2e,0x00,
    0x00,0x00,0x00,0x02,0x02,0x02,0x02,0x02,0x02,0x05,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x27,0x5d,0xec,0x11,0x84,0x8d,0xc1,0x98,0x7f,0xcd,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x7f,0x9a,0xa0,0x9b,0x83,0x82,0x62,0xb7,0x5a,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x19,0x33,0x0d,0x0f,0x0f,0x0f,0x0f,0x0d,0x1c,0x22,0x00,0x00,0x31,0x8f,0x20,0x88,0x83,0xa2,0xdc,0xcf,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x80,0x7f,0xea,0x9f,0x95,0x83,0x3c,0xf3,0x29,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x56,0xe5,0x1b,0x1b,0x1b,0x1b,0x1b,0x54,0xe3,0x1c,0x00,0x00,0x17,0x68,0xf0,0x48,0x9c,0x8c,0xb5,0xd3,0x7f,0x7f,0x8b,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0xcd,0x7f,0xbe,0xb3,0xda,0x83,0x82,0x74,0x9e,0x1d,0x00,0x03,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x03,0x09,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x01,0x00,0x47,0x8f,0x24,0x82,0x84,0xb8,0xc3,0xf1,0x7f,0x7f,0x80,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x89,0x7f,0xac,0xbf,0xef,0x84,0x88,0x13,0xf5,0x2c,0x00,0x06,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,0x4e,0x9e,0x25,0x82,0x84,0xb6,0xb3,0xf1,0x81,0x7f,0x89,0x81,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x81,0x89,0x7f,0x7f,0xd2,0xbf,0xf2,0x9c,0x84,0x3c,0xa3,0xc0,0x23,0x00,0x04,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x00,0x00,0x00,0x0a,0x00,0x77,0x4f,0x90,0x12,0x82,0x84,0xb6,0xc1,0xcc,0x92,0x7f,0x7f,0x81,0x80,0x81,0x85,0x7f,0x7f,0x7f,0x7f,0x85,0x81,0x80,0x81,0x7f,0x7f,0x99,0x96,0x9d,0xc6,0x8c,0x84,0x11,0x3d,0xc8,0x5b,0x00,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x6c,0xe0,0x90,0x12,0x82,0x84,0xb8,0xd7,0xb2,0x96,0xd6,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x9a,0xce,0xaf,0xc3,0xdd,0x9c,0x83,0x11,0x28,0xc8,0x6b,0x00,0x03,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x3f,0x90,0x90,0x13,0x82,0x84,0x8c,0xdf,0x9f,0xdc,0xd5,0xce,0x92,0x9a,0xcd,0x85,0x85,0xcd,0x9a,0x92,0x97,0xf1,0xd9,0x9d,0xc6,0xb4,0x83,0x82,0x3c,0x3d,0xc8,0x1e,0x00,0x0a,0x02,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x16,0x61,0xa4,0x63,0x84,0x82,0x82,0x95,0xb9,0xd8,0xb1,0xb2,0xa3,0xd4,0xcc,0xcc,0xd4,0xa3,0xb2,0xb1,0xb5,0xa2,0xb4,0x83,0x82,0x83,0x26,0xa0,0xa6,0x3b,0x00,0x05,0x06,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x09,0x00,0x41,0x29,0x8f,0x3d,0x12,0x84,0x82,0x8a,0x83,0x8d,0x94,0xb9,0x9b,0xc4,0xc4,0xdb,0xb9,0x94,0x8d,0x83,0x8a,0x86,0x88,0x42,0x43,0xf5,0x61,0x1d,0x00,0x0a,0x02,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x0a,0x2f,0xa7,0xf5,0x43,0x13,0x11,0x8a,0x8a,0x86,0x86,0x86,0x86,0x86,0x86,0x86,0x8a,0x8a,0x8a,0x15,0x1f,0x24,0xa1,0xb7,0x2a,0x19,0x00,0x03,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x00,0x40,0x2a,0xa7,0x8e,0xc7,0x4b,0x13,0x18,0x14,0x15,0x84,0x84,0x15,0x14,0x18,0x13,0x4b,0xc7,0x8e,0xba,0x3e,0x22,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0x00,0x00,0x2b,0x32,0x45,0xba,0x8e,0xa8,0xa9,0xc9,0xbb,0xbb,0xc9,0xa9,0xa8,0x8e,0xba,0x45,0x46,0x16,0x00,0x00,0x04,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x00,0x00,0x27,0x16,0x2d,0x2e,0x50,0x6a,0x4a,0x4a,0x6a,0x50,0x49,0x2d,0x16,0x44,0x00,0x00,0x02,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

};

const lv_image_dsc_t img_timer_48 = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_I8,
    .flags = 0,
    .w = 65,
    .h = 48,
    .stride = 65,
    .reserved_2 = 0,
  },
  .data_size = sizeof(img_timer_48_map),
  .data = img_timer_48_map,
  .reserved = NULL,
};

