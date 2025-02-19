#	$NetBSD: Makefile,v 1.25.6.3 2024/04/20 13:26:35 martin Exp $

LIBISPRIVATE=yes

LIB=ntp

.include <bsd.own.mk>

.include "${.CURDIR}/../Makefile.inc"

DIST=	${IDIST}/libntp 

.PATH.c:	${DIST}

SRCS= \
a_md5encrypt.c \
adjtime.c \
atoint.c \
atolfp.c \
atouint.c \
audio.c \
authkeys.c \
authreadkeys.c \
authusekey.c \
bsd_strerror.c \
buftvtots.c \
caljulian.c \
caltontp.c \
calyearstart.c \
clocktime.c \
clocktypes.c \
decodenetnum.c \
dofptoa.c \
dolfptoa.c \
emalloc.c \
findconfig.c \
getopt.c \
hextoint.c \
hextolfp.c \
humandate.c \
icom.c \
iosignal.c \
is_ip_address.c \
lib_strbuf.c \
libssl_compat.c \
machines.c \
mktime.c \
modetoa.c \
mstolfp.c \
msyslog.c \
netof.c \
ntp_calendar.c \
ntp_calgps.c \
ntp_crypto_rnd.c \
ntp_intres.c \
ntp_libopts.c \
ntp_lineedit.c \
ntp_random.c \
ntp_rfc2553.c \
ntp_worker.c \
numtoa.c \
numtohost.c \
octtoint.c \
prettydate.c \
recvbuff.c \
refnumtoa.c \
snprintf.c \
socket.c \
socktoa.c \
socktohost.c \
ssl_init.c \
statestr.c \
strdup.c \
syssignal.c \
systime.c \
timespecops.c \
timetoa.c \
timexsup.c \
uglydate.c \
vint64ops.c \
work_fork.c \
work_thread.c \
xsbprintf.c \
ymd2yd.c

CPPFLAGS+= -I${IDIST}/sntp/libopts

# For MKREPRO, avoid using __DATE__ and __TIME__.
# Instead, use the date and time from ${MKREPRO_TIMESTAMP}
.if ${MKREPRO:Uno} == "yes"
.if ${MKREPRO_TIMESTAMP:Uundefined} == "undefined"
.error MKREPRO_TIMESTAMP is undefined with MKREPRO active
.endif
MKREPRO_DATE != env LC_ALL=C ${TOOL_DATE} -u -r "${MKREPRO_TIMESTAMP}" "+%b %e %Y"
MKREPRO_TIME != env LC_ALL=C ${TOOL_DATE} -u -r "${MKREPRO_TIMESTAMP}" "+%T"
.if ${MKREPRO_DATE} == "" || ${MKREPRO_TIME} == ""
.error empty MKREPRO_DATE or MKREPRO_TIME
.endif
CPPFLAGS.ntp_calendar.c += -DMKREPRO_DATE=\"${MKREPRO_DATE:Q}\"
CPPFLAGS.ntp_calendar.c += -DMKREPRO_TIME=\"${MKREPRO_TIME:Q}\"
.endif

COPTS.timetoa.c+=	${GCC_NO_FORMAT_TRUNCATION}
COPTS.socktoa.c+=	${${ACTIVE_CC} == "gcc" && ${HAVE_GCC:U0} >= 10:? -Wno-restrict :}
COPTS.socktohost.c+=	${${ACTIVE_CC} == "gcc" && ${HAVE_GCC:U0} >= 10:? -Wno-restrict :}

COPTS.a_md5encrypt.c+= -Wno-error=deprecated-declarations

.include <bsd.lib.mk>
