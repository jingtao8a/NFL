#!/bin/bash

set -e # fail and exit on any command erroring

root_dir=$(cd `dirname $0`/..; pwd)
build_dir=${root_dir}/build
exec=${build_dir}/evaluate
config_dir=${root_dir}/configs
workload_dir=${root_dir}/workloads
weights_dir=${root_dir}/flow_weights

# Configurations
algorithms=('nfl')
batch_size_list=(256)
req_dists=('zipf')
flow_para='2D2H2L'
repeat=3
declare -A key_type
key_type=([ycsb-200M]='float64')
# Configurations for workloads
read_frac_list=(100 80 20 0)
test_workloads=('ycsb-200M')

if [ ! -d ${config_dir} ];
then
  mkdir ${config_dir}
fi

# Generate config for nfl
for req_dist in ${req_dists[*]}
do
  for read_frac in ${read_frac_list[*]}
  do
    for workload in ${test_workloads[*]}
    do
      workload_name=${workload}'-'${read_frac}'R-'${req_dist}
      config_path=${config_dir}'/nfl_'${workload_name}'.in'
      weights_path=${weights_dir}'/'${workload_name}'_'${flow_para}'_weights.txt'
      echo 'weights_path='${weights_path} > ${config_path}
    done
  done
done
