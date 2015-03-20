#!/usr/bin/env python
# **********************************************************************
#
# Copyright (c) 2003-2015 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

import sys, os

path = [ ".", "..", "../..", "../../..", "../../../.." ]
head = os.path.dirname(sys.argv[0])
if len(head) > 0:
    path = [os.path.join(head, p) for p in path]
path = [os.path.abspath(p) for p in path if os.path.exists(os.path.join(p, "demoscript")) ]
if len(path) == 0:
    raise RuntimeError("can't find toplevel directory!")
sys.path.append(path[0])
sys.path.append(os.path.join(path[0], "scripts"))

from demoscript import Util
from demoscript.Ice import async

server = Util.spawn('Server.py --Ice.PrintAdapterReady')
server.expect('.* ready')
client = Util.spawn('Client.py')
client.expect('.*==>')

async.run(client, server)