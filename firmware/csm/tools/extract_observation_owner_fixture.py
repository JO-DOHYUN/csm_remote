"""Compile the current main.cpp owner body verbatim, not a rewritten state model.

The fixture replaces only outer board globals/clock/transport adapter. Real
HostRealtimeAuthority, ControlSourceManager, decode and mailbox are linked.
"""
from pathlib import Path
import sys
import re

root=Path(__file__).resolve().parents[1]
tcp=len(sys.argv)>2 and sys.argv[2]=='tcp'
source=(root/('src/board/uplink/WifiSocketWorker.cpp' if tcp else 'src/main.cpp')).read_text(encoding='utf-8')
output=[]
names=('serviceControlReceive','serviceControlTransmit','closeControlClient',
       'serviceReceive','serviceTransmit','serviceSessionAnchor','notePumpResult',
       'applyPendingConsume','closePendingIsolationBeforeSocketSend',
       'closePendingIsolationAfterPositiveSend','refreshAdmissionSnapshotForSettlement',
       'closeClient','beginCall','endCall','noteSocketError') if tcp else ('stage_realtime_proof','service_host_realtime')
for name in names:
    start=(re.search(r'^\w+ WifiSocketWorker::'+name+r'\(',source,re.M).start()
           if tcp else source.index('static void '+name+'('))
    opening=source.index('{',start)
    depth=1
    end=opening+1
    while depth:
        if source[end]=='{': depth+=1
        if source[end]=='}': depth-=1
        end+=1
    output.append(source[start:end])
Path(sys.argv[1]).write_text('// Generated verbatim owner body fixture.\n'+'\n'.join(output),encoding='utf-8')
