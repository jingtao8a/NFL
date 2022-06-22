#include "benchmark/workload.h"
#include "util/common.h"
#include "util/zipf.h"

using namespace kvevaluator;

template<typename KT, typename VT>
void GenerateRequests(std::string output_path, std::string data_path, 
                      std::string dist_name, int batch_size, double init_frac, 
                      double read_frac, double kks_frac) {
  // Load synthetic data
  std::vector<std::pair<KT, VT>> kvs;
  LoadData(data_path, kvs);
  std::sort(kvs.begin(), kvs.end(), 
    [](auto const& a, auto const& b) {
      return a.first < b.first;
  });
  int tot_num = kvs.size();
  int init_idx = int(tot_num * init_frac);
  // Verify unique data
  for (int i = 1; i < kvs.size(); ++ i) {
    if (EQ(kvs[i].first, kvs[i - 1].first)) {
      std::cout << std::fixed << "Duplicated data" << std::endl
                << i - 1 << "th key [" << kvs[i - 1].first << "]" << std::endl
                << i << "th key [" << kvs[i].first << "]" << std::endl;
      exit(-1);
    }
  }
  // Generate the read-write workloads
  // Prepare the out-of-bound data
  int kks_idx = static_cast<int>(tot_num * kks_frac);
  int oob_num = tot_num - kks_idx;
  std::cout << "Total Number\t[" << tot_num << "]\nInitial Number\t[" 
            << init_idx << "]\nKKS Number\t[" << kks_idx << "]\nOOB Number\t[" 
            << oob_num << "]" << std::endl;
  std::vector<std::pair<KT, VT>> oob_data;
  oob_data.reserve(oob_num);
  for (int i = kks_idx; i < tot_num; ++ i) {
    oob_data.push_back(kvs[i]);
  }
  // Shuffle the known-key-space data
  if (kks_idx > 2) {
    Shuffle(kvs, 1, kks_idx - 1);
    std::swap(kvs[init_idx - 1], kvs[kks_idx - 1]);
  }
  // Prepare the data for bulk loading
  std::vector<std::pair<KT, VT>> existing_data;
  std::vector<Request<KT, VT>> reqs;
  reqs.reserve(tot_num);
  existing_data.reserve(init_idx);
  for (int i = 0; i < init_idx; ++ i) {
    reqs.push_back({kBulkLoad, kvs[i]});
    existing_data.push_back(kvs[i]);
  }
  std::sort(reqs.begin(), reqs.end(), 
    [](auto const& a, auto const& b) {
      return a.kv.first < b.kv.first;
  });
  std::sort(existing_data.begin(), existing_data.end(), 
    [](auto const& a, auto const& b) {
      return a.first < b.first;
  });
  // Generate the requests based on the read-fraction
  for (int i = init_idx, j = init_idx, k = 0; i < tot_num; i += batch_size) {
    int batch_num = std::min(tot_num - i, batch_size);
    int num_read_per_batch = static_cast<int>(batch_num * read_frac);
    int num_write_per_batch = batch_num - num_read_per_batch;
    if (dist_name == "zipf") {
      ScrambledZipfianGenerator zipf_gen(existing_data.size());
      for (int u = 0; u < num_read_per_batch; ++ u) {
        int idx = zipf_gen.nextValue();
        reqs.push_back({kQuery, existing_data[idx]});
      }
    } else if (dist_name == "uniform") {
      std::mt19937_64 gen(kSEED);
      std::uniform_int_distribution<> uniform_gen(0, existing_data.size() - 1);
      for (int u = 0; u < num_read_per_batch; ++ u) {
        int idx = uniform_gen(gen);
        reqs.push_back({kQuery, existing_data[idx]});
      }      
    }
    int num_kks_write = static_cast<int>(num_write_per_batch * kks_frac);
    int num_oob_write = num_write_per_batch - num_kks_write;
    for (int u = 0; j < kks_idx && u < num_kks_write; ++ j, ++ u) {
      reqs.push_back({kInsert, kvs[j]});
      existing_data.push_back(kvs[j]);
    }
    for (int u = 0; k < oob_data.size() && u < num_oob_write; ++ k, ++ u) {
      reqs.push_back({kInsert, oob_data[k]});
      existing_data.push_back(oob_data[k]);
    }
  }
  WriteRequests(output_path, reqs);
}

template<typename KT, typename VT>
void GenerateRequestsBasedYCSB(std::string output_path, std::string workload_base_path) {
  std::string load_path = workload_base_path + "-load.out";
  std::string run_path = workload_base_path + "-run.out";
  std::vector<Request<KT, VT>> reqs;
  std::mt19937_64 gen(kSEED);
  auto load_reqs = [&reqs, &gen](std::string path) mutable {
    std::fstream in(path, std::ios::in);
    if (!in.is_open()) {
      std::cout << "File [" << path << "] doesn't exist" << std::endl;
      exit(-1);
    }
    int num_ops;
    in >> num_ops;
    reqs.reserve(reqs.capacity() + num_ops);
    for (int i = 0; i < num_ops; ++ i) {
      std::string op;
      KT key;
      in >> op >> key;
      VT val = static_cast<VT>(gen());
      if (op == "INSERT") {
        reqs.push_back({kInsert, {key, val}});
      } else if (op == "READ") {
        reqs.push_back({kQuery, {key, val}});
      } else if (op == "UPDATE") {
        reqs.push_back({kUpdate, {key, val}});
      } else if (op == "BULKLOAD") {
        reqs.push_back({kBulkLoad, {key, val}});
      } else {
        std::cout << "Unsupported operation [" << op << "]" << std::endl;
        exit(-1);
      }
    }
    in.close();
  };
  load_reqs(load_path);
  std::sort(reqs.begin(), reqs.end(), [](auto const& a, auto const&b ) {
    return a.kv.first < b.kv.first;
  });
  load_reqs(run_path);
  WriteRequests(output_path, reqs);
}

int GetNumKeys(std::string name) {
  if (name == "wiki-ts-200M") {
    return 90437011;
  }
  int tot_num = 0;
  for (int i = name.size() - 1; i >= 0; -- i) {
    if (name[i] == 'M') {
      int p = 1;
      for (int j = i - 1; j >= 0 && name[j] != '-'; j --, p *= 10) {
        tot_num = tot_num + (name[j] - '0') * p;
      }
      tot_num = tot_num * 1000000;
      break;
    }
  }
  return tot_num;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "No enough parameters" << std::endl;
    std::cout << "Please input: gen [dataset | workload] " 
              << std::endl;
    exit(-1);
  }
  std::string gen_type = std::string(argv[1]);
  // For scalability
  if (gen_type == "dataset") {
    if (argc < 5) {
      std::cout << "No enough parameters for generating dataset" << std::endl;
      std::cout << "Please input: gen dataset (data directory)" 
                << "(distribution name) (number millions of keys) " << std::endl;
      exit(-1);
    }
    std::vector<double> keys;
    std::string data_dir = std::string(argv[2]);
    std::string distribution_name = std::string(argv[3]);
    int num_keys = STRTONUM<char*, int>(argv[4]) * 1000000;
    if (distribution_name == "lognormal") {
      if (argc < 7) {
        std::cout << "No enough parameters for generating dataset based on the " 
                  << "lognormal distribution\n" 
                  << "Please input: gen dataset (data directory) lognormal "
                  << "(number millions of keys) (mean) (variance)" << std::endl;
        exit(-1);
      }
      double mean = STRTONUM<char*, double>(argv[5]);
      double var = STRTONUM<char*, double>(argv[6]);
      std::lognormal_distribution<double> dist(mean, var);
      std::string workload_path = PathJoin(data_dir, distribution_name + "-" +
                                  STR<int>(num_keys / 1000000) + "M-var(" + 
                                  STR<double>(var) + ").bin");
      GenerateSyntheticKeys<std::lognormal_distribution<double>, double>(
                            dist, num_keys, keys, workload_path);
    } else if (distribution_name == "uniform") {
      if (argc < 7) {
        std::cout << "No enough parameters for generating dataset based on the " 
                  << "uniform distribution\n" 
                  << "Please input: gen dataset (data directory) uniform "
                  << "(number millions of keys) (begin) (end)" << std::endl;
        exit(-1);
      }
      double begin = STRTONUM<char*, double>(argv[5]);
      double end = STRTONUM<char*, double>(argv[6]);
      std::uniform_real_distribution<double> dist(begin, end);
      std::string workload_path = PathJoin(data_dir, distribution_name + "-" +
                                  STR<int>(num_keys / 1000000) + "M.bin");
      GenerateSyntheticKeys<std::uniform_real_distribution<double>, double>(
                            dist, num_keys, keys, workload_path);
    } else {
      std::cout << "Unsupported distribution name [" << distribution_name << "]"
                << std::endl;
      exit(-1);
    }
  } else if (gen_type == "workload") {
    if (argc < 6) {
      std::cout << "No enough parameters for generating workloads\n"
                << "Please input: gen workload (data directory) (workload directory) (workload type) (workload name)"
                << std::endl;
      exit(-1);
    }
    std::string data_dir = std::string(argv[2]);
    std::string workload_dir = std::string(argv[3]);
    std::string workload_type = std::string(argv[4]);
    std::string workload_name = std::string(argv[5]);
    if (workload_type == "keyset") {
      if (argc < 12) {
        std::cout << "No enough parameters for generating workloads based on the key set\n"
                  << "Please input: gen workload (data directory) (workload directory) keyset (workload name) (key type) (distribution name) (batch size) " 
                  << "(ratio of data for bulk loading) (read ratio) (known-key-space write ratio)" << std::endl;
        exit(-1);
      }
      std::string key_type = std::string(argv[6]);
      std::string dist_name = std::string(argv[7]);
      int batch_size = STRTONUM<char*, int>(argv[8]);
      double init_frac = STRTONUM<char*, double>(argv[9]);
      double read_frac = STRTONUM<char*, double>(argv[10]) / 100;
      double kks_frac = STRTONUM<char*, double>(argv[11]);
      std::string output_path = PathJoin(workload_dir, workload_name + "-" + STR<int>(read_frac * 100) + "R-" + dist_name + ".bin");
      // For distshift and updates
      // std::string output_path = PathJoin(workload_dir, workload_name + "-" + STR<int>(read_frac * 100) + "R-" + STR<int>(kks_frac * 100) + "K-" + dist_name + ".bin");
      // For scalability
      // std::string output_path = PathJoin(workload_dir, workload_name + "-" + STR<int>(init_frac * 200) + "I-" + STR<int>(read_frac * 100) + "R-" + STR<int>(kks_frac * 100) + "K-" + dist_name + ".bin");
      std::string source_path = PathJoin(data_dir, workload_name + ".bin");
      if (key_type == "float64") {
        GenerateRequests<double, long long>(output_path, source_path, dist_name, batch_size, init_frac, read_frac, kks_frac);
      } else {
        std::cout << "Unsupported key type [" << key_type << "]" << std::endl;
        exit(-1);
      }
    } else if (workload_type == "ycsb-gen") {
      std::string output_path = PathJoin(workload_dir, workload_name + ".bin");
      std::string source_path = PathJoin(data_dir, workload_name);
      GenerateRequestsBasedYCSB<double, long long>(output_path, source_path);
    } else {
      std::cout << "Unsupported workload type [" << workload_type << "]" << std::endl;
      exit(-1);
    }
  } else if (gen_type == "category") {
    if (argc < 4) {
      std::cout << "No enough parameters for generating workloads\n"
                << "Please input: gen category (workload directory) (workload name)"
                << std::endl;
      exit(-1);
    }
    std::string workload_dir = std::string(argv[2]);
    std::string workload_name = std::string(argv[3]);
    std::string workload_path = PathJoin(workload_dir, workload_name + ".bin");
    std::string output_path = PathJoin(workload_dir, "cate_" + workload_name + ".bin");
    std::vector<std::pair<double, long long>> load_data;
    std::vector<Request<double, long long>> reqs;
    std::cout << "Loading Data" << std::endl;
    LoadRequests(workload_path, load_data, reqs);
    std::cout << "Building Key Set" << std::endl;
    std::set<double> key_set;
    for (uint32_t i = 0; i < load_data.size(); ++ i) {
      key_set.insert(load_data[i].first);
    }
    for (uint32_t i = 0; i < reqs.size(); ++ i) {
      key_set.insert(reqs[i].kv.first);
    }
    std::cout << "Building Key Array" << std::endl;
    std::mt19937_64 gen(kSEED);
    std::vector<double> key_array;
    std::vector<double> cum_dis;
    key_array.reserve(key_set.size());
    cum_dis.resize(key_set.size());
    key_array.insert(key_array.end(), key_set.begin(), key_set.end());
    std::cout << "Building Distance Array" << std::endl;
    cum_dis[0] = 0;
    for (uint32_t i = 1; i < key_array.size(); ++ i) {
      double dis = static_cast<long long>(std::fabs(gen())) % 20;
      cum_dis[i] = dis + cum_dis[i - 1];
    }
    std::cout << "Building Categorical Keys" << std::endl;
    for (uint32_t i = 0; i < load_data.size(); ++ i) {
      uint32_t idx = std::lower_bound(key_array.begin(), key_array.end(), load_data[i].first) - key_array.begin();
      double opt_key = idx + cum_dis[idx];
      load_data[i] = {opt_key, load_data[i].second};
    }
    for (uint32_t i = 0; i < reqs.size(); ++ i) {
      uint32_t idx = std::lower_bound(key_array.begin(), key_array.end(), reqs[i].kv.first) - key_array.begin();
      double opt_key = idx + cum_dis[idx];
      reqs[i].kv = {opt_key, reqs[i].kv.second};
    }
    std::cout << "Merging All Requests" << std::endl;
    std::vector<Request<double, long long>> all_reqs;
    all_reqs.reserve(load_data.size() + reqs.size());
    for (uint32_t i = 0; i < load_data.size(); ++ i) {
      all_reqs.insert(all_reqs.end(), {kBulkLoad, load_data[i]});
    }
    all_reqs.insert(all_reqs.end(), reqs.begin(), reqs.end());
    std::cout << "Writing All Requests" << std::endl;
    WriteRequests(output_path, all_reqs);
  } else {
    std::cout << "Unsupported generator type [" << gen_type << "]" << std::endl;
    exit(-1);
  }
  return 0;
}