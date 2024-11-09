#!/bin/bash

set -e  # fail and exit on any command erroring

# Path
root_dir=$(cd `dirname $0`/..; pwd)
build_dir=${root_dir}/cmake-build-debug
lib_dir=${root_dir}/lib
data_dir=${root_dir}/data
workload_dir=${root_dir}/workloads
scripts_dir=${root_dir}/scripts
pys_dir=${scripts_dir}/scripts
gen_exec=${build_dir}/gen
format_exec=${build_dir}/format

# Configurations
req_dist='zipf'
num_keys=190 # 190 million
batch_size=256
gen_source=true
declare -A key_type
key_type=([longitudes-200M]='float64'
          [longlat-200M]='float64'
          [ycsb-200M]='float64'
          [books-200M]='float64'
          [fb-200M]='float64'
          [wiki-ts-200M]='float64'
          [lognormal-200M]='float64')

init_frac_list=(0.5)
read_frac_list=(100 80 20 0)
kks_frac_list=(1)
#float64_workloads=('longlat-200M' 'longitudes-200M')
int64_workloads=('ycsb-200M')
#uint64_workloads=('fb-200M' 'wiki-ts-200M')

for kks_frac in ${kks_frac_list[*]}
do
  for init_frac in ${init_frac_list[*]}
  do
    echo 'Request Distribution ['${req_dist}']'

    for workload in ${uint64_workloads[*]}
    do
      if [ ${gen_source} = true ];
      then
        echo 'Format '${workload}
        echo `${format_exec} ${root_dir} ${workload//'-'/'_'} uint64`
      fi
      for read_frac in ${read_frac_list[*]}
      do
        echo 'Generate workload ['${workload}'] with the initial fraction ['${init_frac}'] and the read fraction ['${read_frac}']'
        echo `${gen_exec} workload ${data_dir} ${workload_dir} keyset ${workload} ${key_type[$workload]} ${req_dist} ${batch_size} ${init_frac} ${read_frac} ${kks_frac}`
      done
    done

    # Generate workloads based on realistic workloads
    for workload in ${float64_workloads[*]}
    do
      if [ ${gen_source} = true ];
      then
        echo 'Format '${workload}
        echo `${format_exec} ${root_dir} ${workload} float64`
      fi
      for read_frac in ${read_frac_list[*]}
      do
        echo 'Generate workload ['${workload}'] with the initial fraction ['${init_frac}'] and the read fraction ['${read_frac}'] and the kks fraction ['${kks_frac}']'
        echo `${gen_exec} workload ${data_dir} ${workload_dir} keyset ${workload} ${key_type[$workload]} ${req_dist} ${batch_size} ${init_frac} ${read_frac} ${kks_frac}`
      done
    done

    # Generate workloads based on realistic workloads
    for workload in ${int64_workloads[*]}
    do
      if [ ${gen_source} = true ];
      then
        echo 'Format '${workload}
        echo `${format_exec} ${root_dir} ${workload} int64`
      fi
      for read_frac in ${read_frac_list[*]}
      do
        echo 'Generate workload ['${workload}'] with the initial fraction ['${init_frac}'] and the read fraction ['${read_frac}'] and the kks fraction ['${kks_frac}']'
        echo `${gen_exec} workload ${data_dir} ${workload_dir} keyset ${workload} ${key_type[$workload]} ${req_dist} ${batch_size} ${init_frac} ${read_frac} ${kks_frac}`
      done
    done
  done
done