/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - util.h                                                  *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2012 CasualJames                                        *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef __UTIL_H__
#define __UTIL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdint.h>

#include <retro_inline.h>

/**********************
   Byte swap utilities
 **********************/
#ifdef _MSC_VER
#include <stdlib.h>
#endif

#if defined(_MSC_VER)
#define BUILTIN_BSWAP16 _byteswap_ushort
#define BUILTIN_BSWAP32 _byteswap_ulong
#define BUILTIN_BSWAP64 _byteswap_uint64
#endif

static INLINE uint16_t m64p_swap16(uint16_t x)
{
#ifdef BUILTIN_BSWAP16
   return BUILTIN_BSWAP16(x);
#elif defined(__GNUC__)
   return __builtin_bswap16(x);
#else
   return ((x & 0x00FF) << 8) |
      ((x & 0xFF00) >> 8);
#endif
}

static INLINE uint32_t m64p_swap32(uint32_t x)
{
#ifdef BUILTIN_BSWAP32
   return BUILTIN_BSWAP32(x); // long is always 32-bit in Windows
#elif defined(__GNUC__)
   return __builtin_bswap32(x);
#else
   return ((x & 0x000000FF) << 24) |
      ((x & 0x0000FF00) << 8) |
      ((x & 0x00FF0000) >> 8) |
      ((x & 0xFF000000) >> 24);
#endif
}

static INLINE uint64_t m64p_swap64(uint64_t x)
{
#ifdef BUILTIN_BSWAP64
   return BUILTIN_BSWAP64(x);
#elif defined(__GNUC__)
   return __builtin_bswap64(x);
#else
   return ((x & 0x00000000000000FFULL) << 56) |
      ((x & 0x000000000000FF00ULL) << 40) |
      ((x & 0x0000000000FF0000ULL) << 24) |
      ((x & 0x00000000FF000000ULL) << 8) |
      ((x & 0x000000FF00000000ULL) >> 8) |
      ((x & 0x0000FF0000000000ULL) >> 24) |
      ((x & 0x00FF000000000000ULL) >> 40) |
      ((x & 0xFF00000000000000ULL) >> 56);
#endif
}

#ifdef MSB_FIRST
#define big16(x) (x)
#define big32(x) (x)
#define big64(x) (x)
#define little16(x) m64p_swap16(x)
#define little32(x) m64p_swap32(x)
#define little64(x) m64p_swap64(x)
#else
#define big16(x) m64p_swap16(x)
#define big32(x) m64p_swap32(x)
#define big64(x) m64p_swap64(x)
#define little16(x) (x)
#define little32(x) (x)
#define little64(x) (x)
#endif

/* Byte swaps, converts to little endian or converts to big endian a buffer,
 * containing 'count' elements, each of size 'length'. */
void swap_buffer(void *buffer, size_t length, size_t count);
void to_little_endian_buffer(void *buffer, size_t length, size_t count);
void to_big_endian_buffer(void *buffer, size_t length, size_t count);

/**********************
     GUI utilities
 **********************/
void countrycodestring(char countrycode, char *string);
void imagestring(unsigned char imagetype, char *string);

/**********************
     Path utilities
 **********************/

/* Extracts the full file name (with extension) from a path string.
 * Returns the same string, advanced until the file name. */
const char* namefrompath(const char* path);

/* Creates a path string by joining two path strings.
 * The given path strings may or may not start or end with a path separator.
 * Returns a malloc'd string with the resulting path. */
char* combinepath(const char* first, const char *second);

/**********************
    String utilities
 **********************/

/** trim
 *    Removes leading and trailing whitespace from str. Function modifies str
 *    and also returns modified string.
 */
char *trim(char *str);

/* Converts an string to an integer.
 * Returns 1 on success, 0 on failure. 'result' is undefined on failure.
 *
 * The following conditions cause this function to fail:
 * - Empty string
 * - Leading characters (including whitespace)
 * - Trailing characters (including whitespace)
 * - Overflow or underflow.
 */
int string_to_int(const char *str, int *result);

/* Converts an string of hexadecimal characters to a byte array.
 * 'output_size' is the number of bytes (hex digraphs) to convert.
 * Returns 1 on success, 0 on failure. 'output' is undefined on failure. */
int parse_hex(const char *str, unsigned char *output, size_t output_size);

/* Formats an string, using the same syntax as printf.
 * Returns the result in a malloc'd string. */
char* formatstr(const char* fmt, ...);

typedef enum _ini_line_type
{
    INI_BLANK,
    INI_COMMENT,
    INI_SECTION,
    INI_PROPERTY,
    INI_TRASH
} ini_line_type;

typedef struct _ini_line
{
    ini_line_type type;
    char *name;
    char *value;
} ini_line;

/* Parses the INI file line pointer by 'lineptr'.
 * The first line pointed by 'lineptr' may be modifed.
 * 'lineptr' will point to the next line after this function runs.
 *
 * Returns a ini_line structure with information about the line.
 * For INI_COMMENT, the value field contains the comment.
 * For INI_SECTION, the name field contains the section name.
 * For INI_PROPERTY, the name and value fields contain the property parameters.
 * The line type is INI_BLANK if the line is blank or invalid.
 *
 * The name and value fields (if any) of ini_line point to 'lineptr'
 * (so their lifetime is associated to that of 'lineptr').
 */
ini_line ini_parse_line(char **lineptr);

#ifdef __cplusplus
}
#endif

#endif // __UTIL_H__

