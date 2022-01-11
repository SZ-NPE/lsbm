#!/bin/bash

# Copy leveldb headers
# cp -rf /shunzi/lsbm/include/ /usr/local/include/lsbm/include/lsbm/
cp -rf /shunzi/lsbm/include/lsbm/* /usr/local/include/lsbm/
cp -rf /shunzi/lsbm/lsbm/ /usr/local/include/lsbm/lsbm/
cp -rf /shunzi/lsbm/util/ /usr/local/include/lsbm/util/
cp -rf /shunzi/lsbm/port/ /usr/local/include/lsbm/util/port/
cp -rf /shunzi/lsbm/port/ /usr/local/include/lsbm/port/
cp -rf /shunzi/lsbm/common/ /usr/local/include/lsbm/common/
cp -rf /shunzi/lsbm/table/ /usr/local/include/lsbm/table/

# Copy static lib
cp -rf /shunzi/lsbm/common/libdb_common.a /usr/lib64/lsmb/
cp -rf /shunzi/lsbm/lsbm/libdb_lsmcb.a /usr/lib64/lsmb/
cp -rf /shunzi/lsbm/util/libutil.a /usr/lib64/lsmb/
cp -rf /shunzi/lsbm/table/libtable.a /usr/lib64/lsmb/
cp -rf /shunzi/lsbm/port/libport.a /usr/lib64/lsmb/
