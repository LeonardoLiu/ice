# **********************************************************************
#
# Copyright (c) 2003-2007 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

top_srcdir	= ..\..\..

TARGETS		= publisher.exe consumer.exe server.exe

P_SRCS		= Publisher.vb
C_SRCS		= Consumer.vb
S_SRCS		= QueueI.vb Server.vb

GEN_SRCS	= $(GDIR)\Queue.vb

SLICE_SRCS	= $(SDIR)/Queue.ice

SDIR		= .

GDIR		= generated

SLICE2VBFLAGS	= -I$(slicedir) $(SLICE2VBFLAGS)

!include $(top_srcdir)\config\Make.rules.mak

VBCFLAGS	= $(VBCFLAGS) -target:exe

publisher.exe: $(P_SRCS) $(GEN_SRCS)
	$(VBC) $(VBCFLAGS) -out:$@ -r:$(csbindir)\icecs.dll $(P_SRCS) $(GEN_SRCS)

consumer.exe: $(C_SRCS) $(GEN_SRCS)
	$(VBC) $(VBCFLAGS) -out:$@ -r:$(csbindir)\icecs.dll $(C_SRCS) $(GEN_SRCS)

server.exe: $(S_SRCS) $(GEN_SRCS)
	$(VBC) $(VBCFLAGS) -out:$@ -r:$(csbindir)\icecs.dll $(S_SRCS) $(GEN_SRCS)

!include .depend
