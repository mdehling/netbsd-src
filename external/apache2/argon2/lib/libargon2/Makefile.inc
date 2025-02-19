# $NetBSD: Makefile.inc,v 1.1.2.1 2023/10/08 14:35:20 martin Exp $

ARGON2DIR=	${NETBSDSRCDIR}/external/apache2/argon2/dist/phc-winner-argon2

.PATH.c: ${ARGON2DIR}/src ${ARGON2DIR}/src/blake2

SRCS+=	argon2.c core.c blake2b.c encoding.c ref.c

CPPFLAGS+=-I${ARGON2DIR}/include

.if defined(ARGON2_NO_THREADS)
CPPFLAGS+=	-DARGON2_NO_THREADS
.else
SRCS+=	thread.c
CFLAGS+=	-pthread
LDADD+=-lpthread
DPADD+=${LIBPTHREAD}
.endif
