#!/usr/bin/python3
import random
import math

# assume following cache params
assoc=1
numSets=256
cacheLineSize=64 # in bytes
addrWidth=48 # in bits
tagBits = addrWidth - (int(math.log(numSets, 2)) + int(math.log(cacheLineSize, 2)))

for core in range (0, 6):
    fileName="trace"+str(core)+".trc"
    f = open(fileName, "a")
    for i in range (25000):
        # generate random byte offset
        offset = random.randint(0, cacheLineSize-1)
        #print ("Offset: "+str(offset))

        # generate random set id
        setID = random.randint(0, numSets-1) << int(math.log(cacheLineSize, 2))
        #print ("Set: "+str(setID))

        # generate random tag
        #tagID = random.randint(0, pow(tagBits, 2)) << (int(math.log(numSets, 2)) + int(math.log(cacheLineSize, 2)))
        tagID = random.randint(0, 1) << (int(math.log(numSets, 2)) + int(math.log(cacheLineSize, 2)))
        #tagID = 0
        #print ("Tag: "+str(tagID))

        # construct the final address 
        addr = offset + setID + tagID

        #print ("Address: "+hex(addr))
        #rd = random.randint(0, 2)
        #if (rd > 0):
        #    f.write(hex(addr)+",RD,1\n")
        #    if (rd == 1):
        #        f.write(hex(addr)+",WR,1\n")
        #else:
        #    f.write(hex(addr)+",WR,1\n")
        
        f.write(hex(addr)+",RD,1\n")
        f.write(hex(addr)+",WR,1\n")
    
    f.close()

for core in range (6, 8):
    fileName="trace"+str(core)+".trc"
    f = open(fileName, "a")
    for i in range (25000):
        # generate random byte offset
        offset = random.randint(0, cacheLineSize-1)
        #print ("Offset: "+str(offset))

        # generate random set id
        setID = random.randint(0, numSets-1) << int(math.log(cacheLineSize, 2))
        #print ("Set: "+str(setID))

        # generate random tag
        #tagID = random.randint(0, pow(tagBits, 2)) << (int(math.log(numSets, 2)) + int(math.log(cacheLineSize, 2)))
        #tagID = random.randint(0, 1) << (int(math.log(numSets, 2)) + int(math.log(cacheLineSize, 2)))
        tagID = 0
        #print ("Tag: "+str(tagID))

        # construct the final address 
        addr = offset + setID + tagID
        f.write(hex(addr)+",RD,1\n")
    
    f.close()
