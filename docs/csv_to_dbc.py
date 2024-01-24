import csv
import sys
from canlib import kvadblib
import ctypes as ct

msgDict = {} # index by id (ids are stored as a hexadecimal string), values are signals
busNodes = {} # all bus nodes, id as strings, val as nodes

def csv_to_dbc(csvFileName):
    # load csv file
    with open(csvFileName) as csvFile:
        csvreader = csv.reader(csvFile, delimiter=',') # make a reader for the csv file
        ii = 0
        for row in csvreader:
            if(ii > 0):
                SigToDict(row)
            ii += 1
    # all messages in list

    # print everything for debugging
    #for key,value in msgDict.items():
    #    for val in value:
    #        print("MsgID: " + val.msgID + ", Signal Name: " + val.signalName)
    #    print("amt of signals: " + str(len(value)))
    #print("amt of messages: " + str(len(msgDict)))

    # make a dbc file
    db = kvadblib .Dbc(name='NFR24_CAN')   
    i = 0
    addNodes(db)
    for msg,sigs in msgDict.items():
        msgid = 0
        msgname = ""
        dlcC = 0
        sender = ""
        for sig in sigs:
            msgname = sig.msgName
            msgid = int("0x" + sig.msgID, 0)
            dlcC += int(sig.size) # get the size of each signal, and add it to the dlcC
            sender = sig.sender
        dlcC = (int)(dlcC/8) #convert from bits to bytes
        DicttoDBCMessages(db, msgname, msgid, dlcC, db.get_node_by_name(sender))  # create each message
    
    db . write_file('full_bus_converted.dbc')
    db . close()

     

def SigToDict(row):
    sig = CsvSignal(row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7], row[8], row[9], row[10], row[11])
    if(sig.msgID not in msgDict.keys()): # if the message does not exist yet
        msgDict[sig.msgID] = []
    msgDict[sig.msgID].append(sig) # append the signal

def DicttoDBCMessages(dbc, msgName, msgID, dlcCount, sendNode):
    msg = dbc.new_message(name=msgName,
                          id=msgID,
                          dlc=dlcCount
    )
    msg.send_node = sendNode
    FillMessageSignals(dbc, msg, msgID)

def FillMessageSignals(dbc, msg, msgID):
    dmsg = msgDict.get( (str(hex(msgID))[2:] )) # get all of the signals stored
    for val in dmsg:
        if(val.min != None):
            msg . new_signal(
                name = val.signalName,
                type = kvadblib . SignalType . FLOAT,
                byte_order = kvadblib . SignalByteOrder .INTEL,
                mode = kvadblib . SignalMultiplexMode .MUX_INDEPENDENT,
                size = kvadblib . ValueSize(startbit = val.startBit, length = val.size),
                scaling = kvadblib . ValueScaling(factor = val.factor, offset = val.offset),
                limits = kvadblib . ValueLimits(min = val.min, max = val.max),
                unit = val.unit,
                comment = val.cycleTime
            )
        else:
            msg . new_signal(
                name = val.signalName,
                type = kvadblib . SignalType . FLOAT,
                byte_order = kvadblib . SignalByteOrder .INTEL,
                mode = kvadblib . SignalMultiplexMode .MUX_INDEPENDENT,
                size = kvadblib . ValueSize(startbit = val.startBit, length = val.size),
                scaling = kvadblib . ValueScaling(factor = val.factor, offset = val.offset),
                unit = val.unit,
                comment = val.cycleTime
            )

def addNodes(dbc):
    for msg,sigs in msgDict.items():
        if( sigs[0].sender not in busNodes.keys()):
            node = dbc.new_node(sigs[0].sender)
            busNodes[sigs[0].sender] = node

    


class CsvSignal:
    def __init__(self, msgID, msgName, sender, signalName, startBit, size, factor, offset, min, max, unit, cycleTime):
        self.msgID = msgID # string
        self.msgName = msgName # string
        self.sender = sender[2:len(sender)-2] # string
        self.signalName = signalName # string
        self.startBit = int(startBit) # int
        self.size = int(size) # int
        self.factor = float(factor) # float
        self.offset = int(offset) # int
        if(min != ''):
            self.min = int(min) # int
            self.max = int(max) # int
        else:
            self.min = None
            self.max = None
        self.unit = unit # string
        self.cycleTime = cycleTime # string

if __name__ == "__main__":
    # run main
    csv_to_dbc("./full_bus.csv")