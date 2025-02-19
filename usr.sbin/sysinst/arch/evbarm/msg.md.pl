/*	$NetBSD: msg.md.pl,v 1.1.38.1 2023/12/26 05:58:02 snj Exp $	*/
/* Based on english version: */
/*	NetBSD: msg.md.en,v 1.2 2002/04/02 17:02:54 thorpej Exp */

/*
 * Copyright 1997 Piermont Information Systems Inc.
 * All rights reserved.
 *
 * Based on code written by Philip A. Nelson for Piermont Information
 * Systems Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Piermont Information Systems Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PIERMONT INFORMATION SYSTEMS INC. ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL PIERMONT INFORMATION SYSTEMS INC. BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* evbarm machine dependent messages, Polish */


message md_hello
{
}

message md_may_remove_boot_medium
{Jesli uruchomiles komputer z dyskietki, mozesz ja teraz wyciagnac.
}

message badreadbb
{Nie moge przeczytac bootbloku filecore
}

message badreadriscix
{Nie moge przeczytac tablicy partycji RISCiX
}

message notnetbsdriscix
{Nie znalazlem partycji NetBSD w tablicy partycji RISCiX - Nie moge nazwac
}

message notnetbsd
{Nie znalazlem partycji NetBSD (dysk filecore?) - Nie moge nazwac
}

message dobootblks
{Instalowanie bootblokow na %s....
}

message arm32fspart
{Partycje NetBSD na dysku %s wygladaja teraz tak (Rozmiary i Przesuniecia w %s):
}

message set_kernel_1
{Kernel}

message nomsdospart
{There is no MSDOS boot partition in the MBR partition table.}

message rpikernelmissing
{The RPI kernel was not installed, perhaps you didn't select it?}
