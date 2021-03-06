# targets.mk

# Make 'include file' defining targets for Synchronet project

# $Id: targets.mk,v 1.36 2011/10/13 23:37:30 deuce Exp $

# LIBODIR, EXEODIR, DIRSEP, LIBFILE, EXEFILE, and DELETE must be pre-defined

SBBS		= $(LIBODIR)$(DIRSEP)$(LIBPREFIX)sbbs$(SOFILE)
FTPSRVR		= $(LIBODIR)$(DIRSEP)$(LIBPREFIX)ftpsrvr$(SOFILE)
WEBSRVR		= $(LIBODIR)$(DIRSEP)$(LIBPREFIX)websrvr$(SOFILE)
MAILSRVR	= $(LIBODIR)$(DIRSEP)$(LIBPREFIX)mailsrvr$(SOFILE)
SERVICES	= $(LIBODIR)$(DIRSEP)$(LIBPREFIX)services$(SOFILE)
SBBSCON		= $(EXEODIR)$(DIRSEP)sbbs$(EXEFILE)
SBBSMONO	= $(EXEODIR)$(DIRSEP)sbbsmono$(EXEFILE)
JSEXEC		= $(EXEODIR)$(DIRSEP)jsexec$(EXEFILE)
NODE		= $(EXEODIR)$(DIRSEP)node$(EXEFILE)
BAJA		= $(EXEODIR)$(DIRSEP)baja$(EXEFILE)
UNBAJA		= $(EXEODIR)$(DIRSEP)unbaja$(EXEFILE)
FIXSMB		= $(EXEODIR)$(DIRSEP)fixsmb$(EXEFILE)
CHKSMB		= $(EXEODIR)$(DIRSEP)chksmb$(EXEFILE)
SMBUTIL		= $(EXEODIR)$(DIRSEP)smbutil$(EXEFILE)
SBBSECHO	= $(EXEODIR)$(DIRSEP)sbbsecho$(EXEFILE)
ECHOCFG		= $(EXEODIR)$(DIRSEP)echocfg$(EXEFILE)
ADDFILES	= $(EXEODIR)$(DIRSEP)addfiles$(EXEFILE)
FILELIST	= $(EXEODIR)$(DIRSEP)filelist$(EXEFILE)
MAKEUSER	= $(EXEODIR)$(DIRSEP)makeuser$(EXEFILE)
ANS2ASC		= $(EXEODIR)$(DIRSEP)ans2asc$(EXEFILE)
ASC2ANS		= $(EXEODIR)$(DIRSEP)asc2ans$(EXEFILE)
SEXYZ		= $(EXEODIR)$(DIRSEP)sexyz$(EXEFILE)
QWKNODES	= $(EXEODIR)$(DIRSEP)qwknodes$(EXEFILE)
SLOG		= $(EXEODIR)$(DIRSEP)slog$(EXEFILE)
ALLUSERS	= $(EXEODIR)$(DIRSEP)allusers$(EXEFILE)
DELFILES	= $(EXEODIR)$(DIRSEP)delfiles$(EXEFILE)
DUPEFIND	= $(EXEODIR)$(DIRSEP)dupefind$(EXEFILE)
SMBACTIV	= $(EXEODIR)$(DIRSEP)smbactiv$(EXEFILE)
DSTSEDIT	= $(EXEODIR)$(DIRSEP)dstsedit$(EXEFILE)

UTILS		= $(FIXSMB) $(CHKSMB) \
			  $(SMBUTIL) $(BAJA) $(NODE) \
			  $(SBBSECHO) $(ECHOCFG) \
			  $(ADDFILES) $(FILELIST) $(MAKEUSER) \
			  $(ANS2ASC) $(ASC2ANS)  $(UNBAJA) \
			  $(QWKNODES) $(SLOG) $(ALLUSERS) \
			  $(DELFILES) $(DUPEFIND) $(SMBACTIV) \
			  $(SEXYZ) $(DSTSEDIT)

all:	dlls utils console

console:	$(JS_DEPS) xpdev-mt smblib \
		$(MTOBJODIR) $(LIBODIR) $(EXEODIR) \
		dlls \
		$(SBBSCON) $(JSEXEC)

utils:	smblib xpdev-mt xpdev ciolib-mt uifc-mt \
		$(LIBODIR) $(OBJODIR) $(MTOBJODIR) $(EXEODIR) \
		$(UTILS)

dlls:	$(JS_DEPS) smblib xpdev-mt \
		$(MTOBJODIR) $(LIBODIR) \
		$(SBBS) $(FTPSRVR) $(MAILSRVR) $(SERVICES)

mono:	xpdev-mt smblib \
		$(MTOBJODIR) $(EXEODIR) \
		$(SBBSMONO)


# Library dependencies
$(SBBS): 
$(FTPSRVR): 
$(WEBSRVR):
$(MAILSRVR):
$(SERVICES): 
$(SBBSCON): $(XPDEV-MT_LIB) $(SMBLIB)
$(SBBSMONO): $(XPDEV-MT_LIB) $(SMBLIB)
$(JSEXEC): $(XPDEV-MT_LIB) $(SMBLIB)
$(NODE): $(XPDEV_LIB)
$(BAJA): $(XPDEV_LIB) $(SMBLIB)
$(UNBAJA): $(XPDEV_LIB)
$(FIXSMB): $(XPDEV_LIB) $(SMBLIB)
$(CHKSMB): $(XPDEV_LIB) $(SMBLIB)
$(SMBUTIL): $(XPDEV_LIB) $(SMBLIB)
$(SBBSECHO): $(XPDEV_LIB) $(SMBLIB)
$(ECHOCFG): $(XPDEV-MT_LIB) $(SMBLIB) $(UIFCLIB-MT) $(CIOLIB-MT)
$(ADDFILES): $(XPDEV_LIB)
$(FILELIST): $(XPDEV_LIB)
$(MAKEUSER): $(XPDEV_LIB)
$(ANS2ASC):
$(ASC2ANS):
$(SEXYZ): $(XPDEV-MT_LIB) $(SMBLIB)
$(QWKNODES): $(XPDEV_LIB)
$(SLOG): $(XPDEV_LIB)
$(ALLUSERS): $(XPDEV_LIB)
$(DELFILES): $(XPDEV_LIB)
$(DUPEFIND): $(XPDEV_LIB) $(SMBLIB)
$(SMBACTIV): $(XPDEV_LIB) $(SMBLIB)
$(DSTSEDIT): $(XPDEV_LIB)
