#!/usr/bin/python

import sys

ns = float(sys.argv[1])
size = float(sys.argv[2])

def ns_to_MBs(ns, size):
    return ((1e9 / ns)*size)/(1024*1024)

print(ns_to_MBs(ns,size))




