/* Convert string to 64-bit unsigned fixed point.
   Copyright (C) 2006-2013 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Joseph Myers <joseph@codesourcery.com>, 2006.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#define RETURN_TYPE uint64_t
#define UNSIGNED 1
#define RETURN_TYPE_BITS 64
#define SAT_MIN 0
#define SAT_MAX 0xffffffffffffffffLL
#define STRTOFIX strtoufix64

#include "strtofix.c"
