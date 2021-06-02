import pymm
import numpy as np
from numpy import genfromtxt
shelf_size_GB = 20
shelf_name = str(shelf_size_GB) + "GBshelf" # 20GBshelf
s = pymm.shelf(shelf_name,size_mb=shelf_size_GB*1024,pmem_path='/mnt/pmem0')

#dataset_name = "dataset_2GB"
#dataset_name = "dataset_200KB"
dataset_name = "dataset_20KB"
print ("items before loading")
print (s.get_item_names())
items = s.get_item_names()
if 'features' in items:
    s.erase('features')
if 'target' in items:
    s.erase('target')

print ("Load target from")
target_name = dataset_name + "_target.csv"
print ("Load target from: " + target_name)
s.target = genfromtxt(target_name, delimiter=',')
features_name = dataset_name + "_features.csv"
print ("Load features from; " + features_name) 
s.features = genfromtxt(features_name, delimiter=',')
print ("items after loading")
print (s.get_item_names())
