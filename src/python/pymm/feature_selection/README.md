# Feature Selection Example

NOTE: Experimental feature
please first read:  [PYMM](../../../../pymm.md) 

Store data to the shelf
In this example:
- We create a shelf of 20GB and store (features, target) 
- We store isynthetic data with features of size 20KB 
  100 features and 10 samples and target with floating number
                          
```bash
$ python3 store_data.py
[LOG]: create_region: path /mnt/pmem0 id 20GBshelf size req 0x500000000 create failed (available 0x0)
Value 'features' has been made available on shelf '20GBshelf'!
Value 'target' has been made available on shelf '20GBshelf'!
items before loading
['features', 'target']
Load target from
Load target from: dataset_20KB_target.csv
Load features from; dataset_20KB_features.csv
items after loading
['target', 'features']
```

Run with DRAM -> copy the data to DRAM 
```bash
$ python3 Feature_selection_DRAM.py
```




Run with NVM   
```
$ python3 Feature_selection_NVM.py
```
