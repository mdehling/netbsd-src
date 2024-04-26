#	$NetBSD$

.include <bsd.own.mk>

SUBDIR=
SUBDIR+=	ramdisk
SUBDIR+=	ramdisk-cgdroot
.if ${MKZFS} != "no"
SUBDIR+=	ramdisk-cgdzfsroot
.endif
SUBDIR+=	ramdisk-zfsroot

TARGETS+=	release

.include <bsd.subdir.mk>
