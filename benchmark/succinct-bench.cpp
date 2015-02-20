#include <iostream>
#include <fstream>
#include <unistd.h>

#include "../include/succinct/SuccinctShard.hpp"
#include "succinct/bench/SuccinctBenchmark.hpp"

void print_usage(char *exec) {
    fprintf(stderr, "Usage: %s [-m mode] [-i isa_sampling_rate] [-n npa_sampling_rate] [-t type] [-l len] [file]\n", exec);
}

int main(int argc, char **argv) {
    if(argc < 2 || argc > 12) {
        print_usage(argv[0]);
        return -1;
    }

    int c;
    uint32_t mode = 0;
    uint32_t isa_sampling_rate = 32;
    uint32_t npa_sampling_rate = 128;
    std::string type = "latency-get";
    int32_t len = 100;
    while((c = getopt(argc, argv, "m:i:n:t:l:")) != -1) {
        switch(c) {
        case 'm':
            mode = atoi(optarg);
            break;
        case 'i':
            isa_sampling_rate = atoi(optarg);
            break;
        case 'n':
            npa_sampling_rate = atoi(optarg);
            break;
        case 't':
            type = std::string(optarg);
            break;
        case 'l':
            len = atoi(optarg);
            break;
        default:
            mode = 0;
            isa_sampling_rate = 32;
            npa_sampling_rate = 128;
            type = "latency-get";
            len = 100;
        }
    }

    if(optind == argc) {
        print_usage(argv[0]);
        return -1;
    }

    std::string inputpath = std::string(argv[optind]);
    std::ifstream input(inputpath);

    SuccinctShard *fd;
    if(mode == 0) {
        fd = new SuccinctShard(0, inputpath, true, isa_sampling_rate, npa_sampling_rate);

        // Serialize and save to file
        std::ofstream s_out(inputpath + ".succinct");
        fd->serialize(s_out);
        s_out.close();
    } else if(mode == 1) {
        fd = new SuccinctShard(0, inputpath, false, isa_sampling_rate, npa_sampling_rate);
    } else {
        // Only modes 0, 1 supported for now
        assert(0);
    }

    SuccinctBenchmark s_bench(fd);
    if(type == "latency-get") {
        s_bench.benchmark_get_latency("latency_results_get");
    } else if(type == "throughput-access") {
        s_bench.benchmark_access_throughput(len);
    } else {
        // Not supported
        assert(0);
    }

    return 0;
}
