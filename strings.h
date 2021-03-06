/*
 * strings.h
 *
 * Copyright (c) 2020, Peter Eriksson <pen@lysator.liu.se>
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef STRINGS_H
#define STRINGS_H 1

#include <sys/types.h>

typedef struct slist {
  size_t c;
  size_t s;
  char **v;
} SLIST;

extern SLIST *
slist_new(size_t size);

extern int
slist_add(SLIST *sp,
	  char *s);

extern void
slist_free(SLIST *sp);

extern char *
slist_join(SLIST *sp,
	   const char *delim);


extern char *
s_ndup(const char *s,
       size_t len);

extern char *
s_dup(const char *s);

extern int
s_match(const char *a,
	const char *b);

extern int
s_nmatch(const char *a,
	 const char *b,
	 size_t len);

extern int
s_trim(char *s);

extern char *
s_dupcat(const char *str,
	 ...);

extern int
s_getint(int *ip,
	 char **spp);

extern int
s_sepint(int *ip,
	 char **spp,
	 char *delim);


extern int
s_cpy(char *dst,
      size_t dstsize,
      const char *src);

extern int
s_ncpy(char *dst,
       size_t dstsize,
       const char *src,
       size_t len);

extern int
s_cat(char *dst,
      size_t dstsize,
      const char *src);

extern int
s_ncat(char *dst,
       size_t dstsize,
       const char *src,
       size_t len);

#endif
