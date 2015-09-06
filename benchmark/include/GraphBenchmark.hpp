#ifndef SUCCINCT_GRAPH_BENCHMARK_H
#define SUCCINCT_GRAPH_BENCHMARK_H

#include <random>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TSocket.h>

#include "succinct-graph/SuccinctGraph.hpp"
#include "rpc/ports.h"
#include "succinct-graph/utils.h"
#include "thrift/GraphQueryAggregatorService.h"

using boost::shared_ptr;
using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;

class GraphBenchmark {
private:

   // Read workload distribution; from ATC 13 Bronson et al.
    constexpr static double ASSOC_RANGE_PERC = 0.409;
    constexpr static double OBJ_GET_PERC = 0.289;
    constexpr static double ASSOC_GET_PERC = 0.157;
    constexpr static double ASSOC_COUNT_PERC = 0.117;
    constexpr static double ASSOC_TIME_RANGE_PERC = 0.028;

    // Timings for throughput benchmarks.
    constexpr static int64_t WARMUP_MICROSECS = 60 * 1000 * 1000; // 1 min
    constexpr static int64_t MEASURE_MICROSECS = 120 * 1000 * 1000; // 2 min
    constexpr static int64_t COOLDOWN_MICROSECS = 5 * 1000 * 1000; // 5 sec

    typedef enum {
        NHBR = 0,
        NHBR_ATYPE = 1,
        NHBR_NODE = 2,
        NODE = 3,
        NODE2 = 4,
        MIX = 5,
        TAO_MIX = 6
    } BenchType;

    inline int choose_query(double rand_r) {
        if (rand_r < ASSOC_RANGE_PERC) {
            return 0;
        } else if (rand_r < ASSOC_RANGE_PERC + OBJ_GET_PERC) {
            return 1;
        } else if (rand_r < ASSOC_RANGE_PERC + OBJ_GET_PERC + ASSOC_GET_PERC) {
            return 2;
        } else if (rand_r < ASSOC_RANGE_PERC + OBJ_GET_PERC +
            ASSOC_GET_PERC + ASSOC_COUNT_PERC)
        {
            return 3;
        }
        return 4;
    }

    void bench_throughput(
        const int num_threads,
        const std::string& master_hostname,
        const BenchType type)
    {
        std::vector<shared_ptr<benchmark_thread_data_t>> thread_datas;
        for (int i = 0; i < num_threads; ++i) {
            try {
                shared_ptr<TSocket> socket(
                    new TSocket(master_hostname, QUERY_HANDLER_PORT));
                shared_ptr<TTransport> transport(
                    new TBufferedTransport(socket));
                shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
                shared_ptr<GraphQueryAggregatorServiceClient> client(
                    new GraphQueryAggregatorServiceClient(protocol));
                transport->open();
                client->init();

                shared_ptr<benchmark_thread_data_t> thread_data(
                    new benchmark_thread_data_t);
                thread_data->client = client;
                thread_data->client_id = i;

                thread_datas.push_back(thread_data);

            } catch (std::exception& e) {
                LOG_E("Exception opening clients: %s\n", e.what());
            }
        }

        std::vector<shared_ptr<std::thread>> threads;
        for (auto thread_data : thread_datas) {
            switch (type) {
            case NHBR:
                threads.push_back(shared_ptr<std::thread>(new std::thread(
                    &GraphBenchmark::benchmark_neighbor_throughput_helper,
                    this, thread_data)));
                break;
            case NHBR_ATYPE:
                threads.push_back(shared_ptr<std::thread>(new std::thread(
                    &GraphBenchmark::benchmark_neighbor_atype_throughput_helper,
                    this, thread_data)));
                break;
            case NHBR_NODE:
                threads.push_back(shared_ptr<std::thread>(new std::thread(
                    &GraphBenchmark::benchmark_neighbor_node_throughput_helper,
                    this, thread_data)));
                break;
            case NODE:
                threads.push_back(shared_ptr<std::thread>(new std::thread(
                    &GraphBenchmark::benchmark_node_throughput_helper,
                    this, thread_data)));
                break;
            case NODE2:
                threads.push_back(shared_ptr<std::thread>(new std::thread(
                    &GraphBenchmark::benchmark_node_node_throughput_helper,
                    this, thread_data)));
                break;
            case MIX:
                threads.push_back(shared_ptr<std::thread>(new std::thread(
                    &GraphBenchmark::benchmark_mix_throughput_helper,
                    this, thread_data)));
                break;
            case TAO_MIX:
                threads.push_back(shared_ptr<std::thread>(new std::thread(
                    &GraphBenchmark::benchmark_tao_mix_throughput_helper,
                    this, thread_data)));
                break;
            default:
                assert(false);
            }
        }

        for (auto thread : threads) {
            thread->join();
        }
    }

    template<typename T>
    inline T mod_get(const std::vector<T>& xs, int64_t i) {
        return xs[i % xs.size()];
    }

    typedef struct {
        shared_ptr<GraphQueryAggregatorServiceClient> client;
        int client_id; // for seeding
    } benchmark_thread_data_t;

public:

    GraphBenchmark(SuccinctGraph *graph, const std::string& master_hostname) {
        graph_ = graph;

        if (graph_ == nullptr) {
            // sharded bench
            init_sharded_benchmark(master_hostname);

            get_neighbors_f_ = [this](std::vector<int64_t>& nhbrs, int64_t id) {
                aggregator_->get_neighbors(nhbrs, id);
            };

            get_neighbors_atype_f_ = [this](
                std::vector<int64_t>& nhbrs,
                int64_t id,
                int64_t atype)
            {
                aggregator_->get_neighbors_atype(nhbrs, id, atype);
            };

            get_neighbors_attr_f_ = [this](
                std::vector<int64_t>& nhbrs,
                int64_t id,
                int attr,
                const std::string& key)
            {
                aggregator_->get_neighbors_attr(nhbrs, id, attr, key);
            };

            get_nodes_f_ = [this](
                std::set<int64_t>& nodes,
                int attr,
                const std::string& key)
            {
                aggregator_->get_nodes(nodes, attr, key);
            };

            get_nodes2_f_ = [this](
                std::set<int64_t>& nodes,
                int attr1,
                const std::string& key1,
                int attr2,
                const std::string& key2)
            {
                aggregator_->get_nodes2(nodes, attr1, key1, attr2, key2);
            };

            obj_get_f_ = [this](
                std::vector<std::string>& result,
                int64_t obj_id)
            {
                aggregator_->obj_get(result, obj_id);
            };

            assoc_range_f_ = [this](
                std::vector<ThriftAssoc>& _return,
                int64_t src,
                int64_t atype,
                int32_t off,
                int32_t len)
            {
                aggregator_->assoc_range(_return, src, atype, off, len);
            };

            assoc_get_f_ = [this](
                std::vector<ThriftAssoc>& _return,
                int64_t src,
                int64_t atype,
                const std::set<int64_t>& dst_id_set,
                int64_t t_low,
                int64_t t_high)
            {
                aggregator_->assoc_get(_return,
                    src, atype, dst_id_set, t_low, t_high);
            };

            assoc_count_f_ = [this](int64_t src, int64_t atype) {
                return aggregator_->assoc_count(src, atype);
            };

            assoc_time_range_f_ = [this](
                std::vector<ThriftAssoc>& _return,
                int64_t src,
                int64_t atype,
                int64_t t_low,
                int64_t t_high,
                int32_t len)
            {
                aggregator_->assoc_time_range(
                    _return, src, atype, t_low, t_high, len);
            };

        } else {
            // TODO: too lazy to add assignments for TAO functions in this case

            // not sharded, so call graph
            get_neighbors_f_ = [this](std::vector<int64_t>& nhbrs, int64_t id) {
                graph_->get_neighbors(nhbrs, id);
            };

            get_neighbors_atype_f_ = [this](
                std::vector<int64_t>& nhbrs,
                int64_t id,
                int64_t atype)
            {
                graph_->get_neighbors(nhbrs, id, atype);
            };

            get_neighbors_attr_f_ = [this](
                std::vector<int64_t>& nhbrs,
                int64_t id,
                int attr,
                const std::string& key)
            {
                graph_->get_neighbors(nhbrs, id, attr, key);
            };

            get_nodes_f_ = [this](
                std::set<int64_t>& nodes,
                int attr,
                const std::string& key)
            {
                graph_->get_nodes(nodes, attr, key);
            };

            get_nodes2_f_ = [this](
                std::set<int64_t>& nodes,
                int attr1,
                const std::string& key1,
                int attr2,
                const std::string& key2)
            {
                graph_->get_nodes(nodes, attr1, key1, attr2, key2);
            };
        }
    }

    void init_sharded_benchmark(const std::string& master_hostname) {
        try {
            LOG_E("Connecting to server '%s'...\n", master_hostname.c_str());
            shared_ptr<TSocket> socket(
                new TSocket(master_hostname, QUERY_HANDLER_PORT));
            shared_ptr<TTransport> transport(
                    new TBufferedTransport(socket));
            shared_ptr<TProtocol> protocol(
                    new TBinaryProtocol(transport));
            aggregator_ = shared_ptr<GraphQueryAggregatorServiceClient>(
                new GraphQueryAggregatorServiceClient(protocol));
            transport->open();
            LOG_E("Connected to aggregator!\n");

            int ret = aggregator_->init();
            LOG_E("Aggregator has init()'d cluster, return code = %d\n", ret);
        } catch (std::exception& e) {
            LOG_E("Exception in benchmark client: %s\n", e.what());
        }
    }

    // BENCHMARKING NEIGHBOR QUERIES
    void benchmark_neighbor_latency(
        std::string res_path,
        uint64_t WARMUP_N,
        uint64_t MEASURE_N,
        std::string warmup_query_file,
        std::string query_file)
    {
        time_t t0, t1;
        LOG_E("Benchmarking getNeighbor latency\n");
        read_neighbor_queries(warmup_query_file, query_file,
            warmup_neighbor_indices, neighbor_indices);
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif

        // Warmup
        LOG_E("Warming up for %" PRIu64 " queries...\n", WARMUP_N);
        std::vector<int64_t> result;
        for (uint64_t i = 0; i < WARMUP_N; ++i) {
            get_neighbors_f_(result, mod_get(warmup_neighbor_indices, i));
        }
        LOG_E("Warmup complete.\n");

        // Measure
        LOG_E("Measuring for %" PRIu64 " queries...\n", MEASURE_N);
        for (uint64_t i = 0; i < MEASURE_N; ++i) {
            t0 = get_timestamp();
            get_neighbors_f_(result, mod_get(neighbor_indices, i));
            t1 = get_timestamp();
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            // correctness validation
            query_res_stream << "node id: " << mod_get(neighbor_indices, i)
                << "\n";
            std::sort(result.begin(), result.end());
            for (auto it = result.begin(); it != result.end(); ++it) {
                query_res_stream << *it << " ";
            }
            query_res_stream << "\n";
#endif
        }
        LOG_E("Measure complete.\n");
    }

    void benchmark_neighbor_atype_throughput(
        const int num_threads,
        const std::string& master_hostname,
        std::string warmup_query_file,
        std::string query_file)
    {
        read_neighbor_atype_queries(warmup_query_file, query_file,
            warmup_nhbrAtype_indices, nhbrAtype_indices,
            warmup_atypes, atypes);
        bench_throughput(num_threads, master_hostname, BenchType::NHBR_ATYPE);
    }

    // get_neighbor(nodeId, atype)
    void benchmark_neighbor_atype_latency(
        std::string res_path,
        uint64_t WARMUP_N,
        uint64_t MEASURE_N,
        std::string warmup_query_file,
        std::string query_file)
    {
        time_t t0, t1;
        LOG_E("Benchmarking getNeighborAtype latency\n");
        read_neighbor_atype_queries(warmup_query_file, query_file,
            warmup_nhbrAtype_indices, nhbrAtype_indices,
            warmup_atypes, atypes);
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif

        // Warmup
        LOG_E("Warming up for %" PRIu64 " queries...\n", WARMUP_N);
        std::vector<int64_t> result;
        for (uint64_t i = 0; i < WARMUP_N; ++i) {
            get_neighbors_atype_f_(
                result,
                mod_get(warmup_nhbrAtype_indices, i),
                mod_get(warmup_atypes, i));
        }
        LOG_E("Warmup complete.\n");

        // Measure
        LOG_E("Measuring for %" PRIu64 " queries...\n", MEASURE_N);
        for (uint64_t i = 0; i < MEASURE_N; ++i) {
            t0 = get_timestamp();
            get_neighbors_atype_f_(
                result,
                mod_get(nhbrAtype_indices, i),
                mod_get(atypes, i));
            t1 = get_timestamp();
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            // correctness validation
            query_res_stream << "node id: " << mod_get(neighbor_indices, i)
                << "\n";
            query_res_stream << "atype:  " << mod_get(atypes, i) << "\n";
            std::sort(result.begin(), result.end());
            for (auto it = result.begin(); it != result.end(); ++it) {
                query_res_stream << *it << " ";
            }
            query_res_stream << "\n";
#endif
        }
        LOG_E("Measure complete.\n");
    }

    std::pair<double, double> benchmark_neighbor_throughput_helper(
        shared_ptr<benchmark_thread_data_t> thread_data)
    {
        double query_thput = 0;
        double edges_thput = 0;
        LOG_E("About to start querying on this thread...\n");

        size_t warmup_size = warmup_neighbor_indices.size();
        size_t measure_size = neighbor_indices.size();
        std::srand(1618 + thread_data->client_id);

        try {
            std::vector<int64_t> result;

            // Warmup phase
            int64_t i = 0;
            time_t start = get_timestamp();
            while (get_timestamp() - start < WARMUP_MICROSECS) {
                thread_data->client->get_neighbors(
                    result,
                    mod_get(warmup_neighbor_indices, rand() % warmup_size));
            }
            LOG_E("Warmup done: served %" PRId64 " queries\n", i);

            // Measure phase
            i = 0;
            int64_t edges = 0;
            start = get_timestamp();
            while (get_timestamp() - start < MEASURE_MICROSECS) {
                thread_data->client->get_neighbors(
                    result, mod_get(neighbor_indices, rand() % measure_size));
                edges += result.size();
                ++i;
            }
            time_t end = get_timestamp();
            double total_secs = (end - start) * 1. / 1e6;
            query_thput = i * 1. / total_secs;
            edges_thput = edges * 1. / total_secs;
            LOG_E("Query done: served %" PRId64 " queries\n", i);

            // Cooldown
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_MICROSECS) {
                thread_data->client->get_neighbors(
                    result, mod_get(neighbor_indices, i));
                ++i;
            }

            std::ofstream ofs("throughput_get_nhbrs.txt",
                std::ofstream::out | std::ofstream::app);
            ofs << query_thput << " " << edges_thput << std::endl;

        } catch (std::exception &e) {
            LOG_E("Throughput test ends...: '%s'\n", e.what());
        }
        return std::make_pair(query_thput, edges_thput);
    }

    std::pair<double, double> benchmark_neighbor_atype_throughput_helper(
        shared_ptr<benchmark_thread_data_t> thread_data)
    {
        double query_thput = 0;
        double edges_thput = 0;
        LOG_E("About to start querying on this thread...\n");

        size_t warmup_size = warmup_nhbrAtype_indices.size();
        size_t measure_size = nhbrAtype_indices.size();
        std::srand(1618 + thread_data->client_id);

        try {
            std::vector<int64_t> result;

            // Warmup phase
            int64_t i = 0;
            int query_idx;
            time_t start = get_timestamp();
            while (get_timestamp() - start < WARMUP_MICROSECS) {
                query_idx = rand() % warmup_size;
                thread_data->client->get_neighbors_atype(
                    result,
                    mod_get(warmup_nhbrAtype_indices, query_idx),
                    mod_get(warmup_atypes, query_idx));
            }
            LOG_E("Warmup done: served %" PRId64 " queries\n", i);

            // Measure phase
            i = 0;
            int64_t edges = 0;
            start = get_timestamp();
            while (get_timestamp() - start < MEASURE_MICROSECS) {
                query_idx = rand() % measure_size;
                thread_data->client->get_neighbors_atype(
                    result,
                    mod_get(nhbrAtype_indices, query_idx),
                    mod_get(atypes, query_idx));
                edges += result.size();
                ++i;
            }
            time_t end = get_timestamp();
            double total_secs = (end - start) * 1. / 1e6;
            query_thput = i * 1. / total_secs;
            edges_thput = edges * 1. / total_secs;
            LOG_E("Query done: served %" PRId64 " queries\n", i);

            // Cooldown
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_MICROSECS) {
                thread_data->client->get_neighbors_atype(
                    result,
                    mod_get(nhbrAtype_indices, query_idx),
                    mod_get(atypes, query_idx));
                ++query_idx;
            }

            std::ofstream ofs("throughput_get_nhbrsAtype.txt",
                std::ofstream::out | std::ofstream::app);
            ofs << query_thput << " " << edges_thput << std::endl;

        } catch (std::exception &e) {
            LOG_E("Throughput test ends...: '%s'\n", e.what());
        }
        return std::make_pair(query_thput, edges_thput);
    }

    std::pair<double, double> benchmark_neighbor_node_throughput_helper(
        shared_ptr<benchmark_thread_data_t> thread_data)
    {
        double query_thput = 0;
        double edges_thput = 0;
        LOG_E("About to start querying on this thread...\n");

        size_t warmup_size = warmup_nhbrNode_indices.size();
        size_t measure_size = nhbrNode_indices.size();
        std::srand(1618 + thread_data->client_id);

        try {
            std::vector<int64_t> result;

            // Warmup phase
            int64_t i = 0;
            int query_idx;
            time_t start = get_timestamp();
            while (get_timestamp() - start < WARMUP_MICROSECS) {
                query_idx = rand() % warmup_size;
                thread_data->client->get_neighbors_attr(
                    result,
                    mod_get(warmup_nhbrNode_indices, query_idx),
                    mod_get(warmup_nhbrNode_attr_ids, query_idx),
                    mod_get(warmup_nhbrNode_attrs, query_idx));
            }
            LOG_E("Warmup done: served %" PRId64 " queries\n", i);

            // Measure phase
            i = 0;
            int64_t edges = 0;
            start = get_timestamp();
            while (get_timestamp() - start < MEASURE_MICROSECS) {
                query_idx = rand() % measure_size;
                thread_data->client->get_neighbors_attr(
                    result,
                    mod_get(nhbrNode_indices, query_idx),
                    mod_get(nhbrNode_attr_ids, query_idx),
                    mod_get(nhbrNode_attrs, query_idx));
                edges += result.size();
                ++i;
            }
            time_t end = get_timestamp();
            double total_secs = (end - start) * 1. / 1e6;
            query_thput = i * 1. / total_secs;
            edges_thput = edges * 1. / total_secs;
            LOG_E("Query done: served %" PRId64 " queries\n", i);

            // Cooldown
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_MICROSECS) {
                thread_data->client->get_neighbors_attr(
                    result,
                    mod_get(nhbrNode_indices, query_idx),
                    mod_get(nhbrNode_attr_ids, query_idx),
                    mod_get(nhbrNode_attrs, query_idx));
                ++query_idx;
            }

            std::ofstream ofs("throughput_get_nhbrsNode.txt",
                std::ofstream::out | std::ofstream::app);
            ofs << query_thput << " " << edges_thput << std::endl;

        } catch (std::exception &e) {
            LOG_E("Throughput test ends...: '%s'\n", e.what());
        }
        return std::make_pair(query_thput, edges_thput);
    }

    std::pair<double, double> benchmark_node_throughput_helper(
        shared_ptr<benchmark_thread_data_t> thread_data)
    {
        double query_thput = 0;
        double edges_thput = 0;
        LOG_E("About to start querying on this thread...\n");

        size_t warmup_size = warmup_node_attributes.size();
        size_t measure_size = node_attributes.size();
        std::srand(1618 + thread_data->client_id);

        try {
            std::set<int64_t> result;

            // Warmup phase
            int64_t i = 0;
            int query_idx;
            time_t start = get_timestamp();
            while (get_timestamp() - start < WARMUP_MICROSECS) {
                query_idx = rand() % warmup_size;
                thread_data->client->get_nodes(
                    result, mod_get(warmup_node_attributes, query_idx),
                    mod_get(warmup_node_queries, query_idx));
            }
            LOG_E("Warmup done: served %" PRId64 " queries\n", i);

            // Measure phase
            i = 0;
            int64_t edges = 0;
            start = get_timestamp();
            while (get_timestamp() - start < MEASURE_MICROSECS) {
                query_idx = rand() % measure_size;
                thread_data->client->get_nodes(
                    result, mod_get(node_attributes, query_idx),
                    mod_get(node_queries, query_idx));
                edges += result.size();
                ++i;
            }
            time_t end = get_timestamp();
            double total_secs = (end - start) * 1. / 1e6;
            query_thput = i * 1. / total_secs;
            edges_thput = edges * 1. / total_secs;
            LOG_E("Query done: served %" PRId64 " queries\n", i);

            // Cooldown
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_MICROSECS) {
                thread_data->client->get_nodes(
                    result, mod_get(node_attributes, query_idx),
                    mod_get(node_queries, query_idx));
                ++query_idx;
            }

            std::ofstream ofs("throughput_get_nodes.txt",
                std::ofstream::out | std::ofstream::app);
            ofs << query_thput << " " << edges_thput << std::endl;

        } catch (std::exception &e) {
            LOG_E("Throughput test ends...: '%s'\n", e.what());
        }
        return std::make_pair(query_thput, edges_thput);
    }

    std::pair<double, double> benchmark_node_node_throughput_helper(
        shared_ptr<benchmark_thread_data_t> thread_data)
    {
        double query_thput = 0;
        double edges_thput = 0;
        LOG_E("About to start querying on this thread...\n");

        size_t warmup_size = warmup_node_attributes.size();
        size_t measure_size = node_attributes.size();
        std::srand(1618 + thread_data->client_id);

        try {
            std::set<int64_t> result;

            // Warmup phase
            int64_t i = 0;
            int query_idx;
            time_t start = get_timestamp();
            while (get_timestamp() - start < WARMUP_MICROSECS) {
                query_idx = rand() % warmup_size;
                thread_data->client->get_nodes2(
                    result,
                    mod_get(warmup_node_attributes, query_idx),
                    mod_get(warmup_node_queries, query_idx),
                    mod_get(warmup_node_attributes2, query_idx),
                    mod_get(warmup_node_queries2, query_idx));
            }
            LOG_E("Warmup done: served %" PRId64 " queries\n", i);

            // Measure phase
            i = 0;
            int64_t edges = 0;
            start = get_timestamp();
            while (get_timestamp() - start < MEASURE_MICROSECS) {
                query_idx = rand() % measure_size;
                thread_data->client->get_nodes2(
                    result,
                    mod_get(node_attributes, query_idx),
                    mod_get(node_queries, query_idx),
                    mod_get(node_attributes2, query_idx),
                    mod_get(node_queries2, query_idx));
                edges += result.size();
                ++i;
            }
            time_t end = get_timestamp();
            double total_secs = (end - start) * 1. / 1e6;
            query_thput = i * 1. / total_secs;
            edges_thput = edges * 1. / total_secs;
            LOG_E("Query done: served %" PRId64 " queries\n", i);

            // Cooldown
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_MICROSECS) {
                thread_data->client->get_nodes2(
                    result,
                    mod_get(node_attributes, query_idx),
                    mod_get(node_queries, query_idx),
                    mod_get(node_attributes2, query_idx),
                    mod_get(node_queries2, query_idx));
                ++query_idx;
            }

            std::ofstream ofs("throughput_get_nodes2.txt",
                std::ofstream::out | std::ofstream::app);
            ofs << query_thput << " " << edges_thput << std::endl;

        } catch (std::exception &e) {
            LOG_E("Throughput test ends...: '%s'\n", e.what());
        }
        return std::make_pair(query_thput, edges_thput);
    }

    std::pair<double, double> benchmark_mix_throughput_helper(
        shared_ptr<benchmark_thread_data_t> thread_data)
    {
        double query_thput = 0;
        double edges_thput = 0;
        LOG_E("About to start querying on this thread...\n");

        std::srand(1618 + thread_data->client_id);

        size_t warmup_nhbr_size = warmup_neighbor_indices.size();
        size_t warmup_nhbr_node_size = warmup_nhbrNode_indices.size();
        size_t warmup_node_size = warmup_node_attributes.size();
        size_t warmup_nhbr_atype_size = warmup_nhbrAtype_indices.size();
        size_t nhbr_size = neighbor_indices.size();
        size_t nhbr_node_size = nhbrNode_indices.size();
        size_t node_size = node_attributes.size();
        size_t nhbr_atype_size = nhbrAtype_indices.size();

        try {
            std::vector<int64_t> result;
            std::set<int64_t> result_set;

            // Warmup phase
            int64_t i = 0;
            int query_idx, rand_query;
            time_t start = get_timestamp();
            while (get_timestamp() - start < WARMUP_MICROSECS) {
                rand_query = rand() % 5;
                switch (rand_query) {
                case 0:
                    query_idx = rand() % warmup_nhbr_size;
                    thread_data->client->get_neighbors(result,
                        mod_get(warmup_neighbor_indices, query_idx));
                    break;
                case 1:
                    query_idx = rand() % warmup_nhbr_node_size;
                    thread_data->client->get_neighbors_attr(result,
                        mod_get(warmup_nhbrNode_indices, query_idx),
                        mod_get(warmup_nhbrNode_attr_ids, query_idx),
                        mod_get(warmup_nhbrNode_attrs, query_idx));
                    break;
                case 2:
                    query_idx = rand() % warmup_node_size;
                    thread_data->client->get_nodes(result_set,
                        mod_get(warmup_node_attributes, query_idx),
                        mod_get(warmup_node_queries, query_idx));
                    break;
                case 3:
                    query_idx = rand() % warmup_nhbr_atype_size;
                    thread_data->client->get_neighbors_atype(result,
                        mod_get(warmup_nhbrAtype_indices, query_idx),
                        mod_get(warmup_atypes, query_idx));
                    break;
                case 4:
                    query_idx = rand() % warmup_node_size;
                    thread_data->client->get_nodes2(result_set,
                        mod_get(warmup_node_attributes, query_idx),
                        mod_get(warmup_node_queries, query_idx),
                        mod_get(warmup_node_attributes2, query_idx),
                        mod_get(warmup_node_queries2, query_idx));
                    break;
                default:
                    assert(false);
                }
            }
            LOG_E("Warmup done: served %" PRId64 " queries\n", i);

            // Measure phase
            i = 0;
            int64_t edges = 0;
            start = get_timestamp();
            while (get_timestamp() - start < MEASURE_MICROSECS) {
#ifndef RUN_MIX_THPUT_BODY
#define RUN_MIX_THPUT_BODY
                rand_query = rand() % 5; \
                switch (rand_query) { \
                case 0: \
                    query_idx = rand() % nhbr_size; \
                    thread_data->client->get_neighbors(result, \
                        mod_get(neighbor_indices, query_idx)); \
                    break; \
                case 1:                                     \
                    query_idx = rand() % nhbr_node_size; \
                    thread_data->client->get_neighbors_attr(result, \
                        mod_get(nhbrNode_indices, query_idx), \
                        mod_get(nhbrNode_attr_ids, query_idx), \
                        mod_get(nhbrNode_attrs, query_idx)); \
                    break; \
                case 2:                                             \
                    query_idx = rand() % node_size; \
                    thread_data->client->get_nodes(result_set, \
                        mod_get(node_attributes, query_idx), \
                        mod_get(node_queries, query_idx)); \
                    break; \
                case 3:                               \
                    query_idx = rand() % nhbr_atype_size; \
                    thread_data->client->get_neighbors_atype(result, \
                        mod_get(nhbrAtype_indices, query_idx), \
                        mod_get(atypes, query_idx)); \
                    break; \
                case 4:                                  \
                    query_idx = rand() % node_size; \
                    thread_data->client->get_nodes2(result_set, \
                        mod_get(node_attributes, query_idx), \
                        mod_get(node_queries, query_idx), \
                        mod_get(node_attributes2, query_idx), \
                        mod_get(node_queries2, query_idx)); \
                    break; \
                default: \
                    assert(false); \
                }
#endif
                RUN_MIX_THPUT_BODY
                edges += result.size();
                ++i;
            }
            time_t end = get_timestamp();
            double total_secs = (end - start) * 1. / 1e6;
            query_thput = i * 1. / total_secs;
            edges_thput = edges * 1. / total_secs;
            LOG_E("Query done: served %" PRId64 " queries\n", i);

            // Cooldown
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_MICROSECS) {
                RUN_MIX_THPUT_BODY
                ++query_idx;
            }

            std::ofstream ofs("throughput_mix.txt",
                std::ofstream::out | std::ofstream::app);
            ofs << query_thput << " " << edges_thput << std::endl;

        } catch (std::exception &e) {
            LOG_E("Throughput test ends...: '%s'\n", e.what());
        }
        return std::make_pair(query_thput, edges_thput);
    }

    void benchmark_neighbor_throughput(
        int num_threads,
        const std::string& master_hostname,
        const std::string& warmup_neighbor_query_file,
        const std::string& neighbor_query_file)
    {
        read_neighbor_queries(warmup_neighbor_query_file, neighbor_query_file,
            warmup_neighbor_indices, neighbor_indices);
        bench_throughput(num_threads, master_hostname, BenchType::NHBR);
    }

    std::pair<double, double> benchmark_tao_mix_throughput_helper(
        shared_ptr<benchmark_thread_data_t> thread_data)
    {
        double query_thput = 0;
        double edges_thput = 0;
        LOG_E("About to start querying on this thread...\n");

        std::srand(1618 + thread_data->client_id);
        std::mt19937 rng(1618 + thread_data->client_id);
        std::uniform_int_distribution<int> dist_query(0, 4);
        int query, query_idx;

        size_t warmup_assoc_range_size = warmup_assoc_range_nodes.size();
        size_t warmup_obj_get_size = warmup_obj_get_nodes.size();
        size_t warmup_assoc_count_size = warmup_assoc_count_nodes.size();
        size_t warmup_assoc_time_range_size = warmup_assoc_time_range_nodes.size();
        size_t warmup_assoc_get_size = warmup_assoc_get_nodes.size();
        size_t assoc_range_size = assoc_range_nodes.size();
        size_t obj_get_size = obj_get_nodes.size();
        size_t assoc_count_size = assoc_count_nodes.size();
        size_t assoc_time_range_size = assoc_time_range_nodes.size();
        size_t assoc_get_size = assoc_get_nodes.size();

        std::vector<ThriftAssoc> result;
        std::vector<std::string> attrs;
        int64_t i = 0;

        try {
            // Warmup phase
            time_t start = get_timestamp();
            while (get_timestamp() - start < WARMUP_MICROSECS) {
                query = choose_query((double) rand() / RAND_MAX);
                switch (query) {
                case 0:
                    query_idx = std::rand() % warmup_assoc_range_size;
                    thread_data->client->assoc_range(result,
                        this->warmup_assoc_range_nodes.at(query_idx),
                        this->warmup_assoc_range_atypes.at(query_idx),
                        this->warmup_assoc_range_offs.at(query_idx),
                        this->warmup_assoc_range_lens.at(query_idx));
                    break;
                case 1:
                    query_idx = std::rand() % warmup_obj_get_size;
                    thread_data->client->obj_get(attrs,
                        this->warmup_obj_get_nodes.at(query_idx));
                    break;
                case 2:
                    query_idx = std::rand() % warmup_assoc_get_size;
                    thread_data->client->assoc_get(result,
                        this->warmup_assoc_get_nodes.at(query_idx),
                        this->warmup_assoc_get_atypes.at(query_idx),
                        this->warmup_assoc_get_dst_id_sets.at(query_idx),
                        this->warmup_assoc_get_lows.at(query_idx),
                        this->warmup_assoc_get_highs.at(query_idx));
                    break;
                case 3:
                    query_idx = std::rand() % warmup_assoc_count_size;
                    thread_data->client->assoc_count(
                        this->warmup_assoc_count_nodes.at(query_idx),
                        this->warmup_assoc_count_atypes.at(query_idx));
                    break;
                case 4:
                    query_idx = std::rand() % warmup_assoc_time_range_size;
                    thread_data->client->assoc_time_range(result,
                        this->warmup_assoc_time_range_nodes.at(query_idx),
                        this->warmup_assoc_time_range_atypes.at(query_idx),
                        this->warmup_assoc_time_range_lows.at(query_idx),
                        this->warmup_assoc_time_range_highs.at(query_idx),
                        this->warmup_assoc_time_range_limits.at(query_idx));
                    break;
                default:
                    assert(false);
                }
                ++i;
            }
            LOG_E("Warmup done: served %" PRId64 " queries\n", i);

            // Measure phase
            i = 0;
            int64_t edges = 0;
            start = get_timestamp();
            while (get_timestamp() - start < MEASURE_MICROSECS) {
#ifndef RUN_TAO_MIX_THPUT_BODY
#define RUN_TAO_MIX_THPUT_BODY
                query = choose_query((double) rand() / RAND_MAX); \
                switch (query) { \
                case 0: \
                  query_idx = std::rand() % assoc_range_size; \
                  thread_data->client->assoc_range(result, \
                      this->assoc_range_nodes.at(query_idx), \
                      this->assoc_range_atypes.at(query_idx), \
                      this->assoc_range_offs.at(query_idx), \
                      this->assoc_range_lens.at(query_idx)); \
                  break; \
                case 1: \
                  query_idx = std::rand() % obj_get_size; \
                  thread_data->client->obj_get(attrs, \
                      this->obj_get_nodes.at(query_idx)); \
                  break; \
                case 2: \
                  query_idx = std::rand() % assoc_get_size; \
                  thread_data->client->assoc_get(result, \
                      this->assoc_get_nodes.at(query_idx), \
                      this->assoc_get_atypes.at(query_idx), \
                      this->assoc_get_dst_id_sets.at(query_idx), \
                      this->assoc_get_lows.at(query_idx), \
                      this->assoc_get_highs.at(query_idx)); \
                  break; \
                case 3: \
                  query_idx = std::rand() % assoc_count_size; \
                  thread_data->client->assoc_count( \
                      this->assoc_count_nodes.at(query_idx), \
                      this->assoc_count_atypes.at(query_idx)); \
                  break; \
                case 4: \
                  query_idx = std::rand() % assoc_time_range_size; \
                  thread_data->client->assoc_time_range(result, \
                      this->assoc_time_range_nodes.at(query_idx), \
                      this->assoc_time_range_atypes.at(query_idx), \
                      this->assoc_time_range_lows.at(query_idx), \
                      this->assoc_time_range_highs.at(query_idx), \
                      this->assoc_time_range_limits.at(query_idx)); \
                  break; \
                default: \
                  assert(false); \
                } \
                edges += result.size(); \
                ++i;
#endif
                RUN_TAO_MIX_THPUT_BODY // actually run
            }
            time_t end = get_timestamp();
            double total_secs = (end - start) * 1. / 1e6;
            query_thput = i * 1. / total_secs;
            edges_thput = edges * 1. / total_secs;
            LOG_E("Query done: served %" PRId64 " queries\n", i);

            // Cooldown
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_MICROSECS) {
                RUN_TAO_MIX_THPUT_BODY
            }

            std::ofstream ofs("throughput_tao_mix.txt",
                std::ofstream::out | std::ofstream::app);
            ofs << query_thput << " " << edges_thput << std::endl;

        } catch (std::exception &e) {
            LOG_E("Throughput test ends...: '%s'\n", e.what());
        }
        return std::make_pair(query_thput, edges_thput);
    }

    void benchmark_tao_mix_throughput(
        const int num_threads,
        const std::string& master_hostname,
        const std::string& warmup_assoc_range_file,
        const std::string& assoc_range_file,
        const std::string& warmup_assoc_count_file,
        const std::string& assoc_count_file,
        const std::string& warmup_obj_get_file,
        const std::string& obj_get_file,
        const std::string& warmup_assoc_get_file,
        const std::string& assoc_get_file,
        const std::string& warmup_assoc_time_range_file,
        const std::string& assoc_time_range_file)
    {
        // assoc_range
        read_assoc_range_queries(warmup_assoc_range_file, assoc_range_file);
        // assoc_count
        read_neighbor_atype_queries(warmup_assoc_count_file, assoc_count_file,
            warmup_assoc_count_nodes, assoc_count_nodes,
            warmup_assoc_count_atypes, assoc_count_atypes);
        // obj_get
        read_neighbor_queries(warmup_obj_get_file, obj_get_file,
            warmup_obj_get_nodes, obj_get_nodes);
        // assoc_get
        read_assoc_get_queries(warmup_assoc_get_file, assoc_get_file);
        // assoc_time_range
        read_assoc_time_range_queries(
            warmup_assoc_time_range_file, assoc_time_range_file);

        bench_throughput(num_threads, master_hostname, BenchType::TAO_MIX);
    }

    void benchmark_node_throughput(
        const int num_threads,
        const std::string& master_hostname,
        std::string warmup_query_file,
        std::string query_file)
    {
        read_node_queries(warmup_query_file, query_file);
        bench_throughput(num_threads, master_hostname, BenchType::NODE);
    }

    void benchmark_node_latency(
        std::string res_path,
        uint64_t WARMUP_N,
        uint64_t MEASURE_N,
        std::string warmup_query_file,
        std::string query_file)
    {
        time_t t0, t1;
        read_node_queries(warmup_query_file, query_file);
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif

        LOG_E("Benchmarking getNode latency\n");

        // Warmup
        LOG_E("Warming up for %" PRIu64 " queries...\n", WARMUP_N);
        std::set<int64_t> result;
        for(uint64_t i = 0; i < WARMUP_N; ++i) {
            get_nodes_f_(
                result, mod_get(warmup_node_attributes, i),
                mod_get(warmup_node_queries, i));
            assert(result.size() != 0 && "No result found in"
                " benchmarking node latency");
        }
        LOG_E("Warmup complete.\n");

        // Measure
        LOG_E("Measuring for %" PRIu64 " queries...\n", MEASURE_N);
        for(uint64_t i = 0; i < MEASURE_N; ++i) {
            t0 = get_timestamp();
            get_nodes_f_(result, mod_get(node_attributes, i),
                mod_get(node_queries, i));
            t1 = get_timestamp();
            assert(result.size() != 0 && "No result found in"
                " benchmarking node latency");
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            // correctness validation
            query_res_stream << "attr " << mod_get(node_attributes, i) << ": "
                << mod_get(node_queries, i) << "\n";
            for (auto it = result.begin(); it != result.end(); ++it)
                query_res_stream << *it << " "; // sets are sorted
            query_res_stream << "\n";
#endif
        }
        LOG_E("Measure complete.\n");
    }

    void benchmark_node_node_throughput(
        const int num_threads,
        const std::string& master_hostname,
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        read_node_queries(warmup_query_file, query_file);
        bench_throughput(num_threads, master_hostname, BenchType::NODE2);
    }

    void benchmark_node_node_latency(
        std::string res_path,
        uint64_t WARMUP_N,
        uint64_t MEASURE_N,
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        read_node_queries(warmup_query_file, query_file);
        time_t t0, t1;
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif

        LOG_E("Benchmarking getNode with two attributes latency\n");

        // Warmup
        LOG_E("Warming up for %" PRIu64 " queries...\n", WARMUP_N);
        std::set<int64_t> result;
        for(uint64_t i = 0; i < WARMUP_N; ++i) {
            get_nodes2_f_(result,
                mod_get(warmup_node_attributes, i),
                mod_get(warmup_node_queries, i),
                mod_get(warmup_node_attributes2, i),
                mod_get(warmup_node_queries2, i));
            assert(result.size() != 0 && "No result found in benchmarking node"
                " two attributes latency");
        }
        LOG_E("Warmup complete.\n");

        // Measure
        LOG_E("Measuring for %" PRIu64 " queries...\n", MEASURE_N);
        for(uint64_t i = 0; i < MEASURE_N; ++i) {
            t0 = get_timestamp();
            get_nodes2_f_(result, mod_get(node_attributes, i),
                mod_get(node_queries, i), mod_get(node_attributes2, i),
                mod_get(node_queries2, i));
            t1 = get_timestamp();
            assert(result.size() != 0 && "No result found in benchmarking node"
                " two attributes latency");
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            // correctness
            query_res_stream << "attr1 " << mod_get(node_attributes, i) << ": "
                << mod_get(node_queries, i) << "; ";
            query_res_stream << "attr2 " << mod_get(node_attributes2, i) << ": "
                << mod_get(node_queries2, i) << "\n";
            for (auto it = result.begin(); it != result.end(); ++it)
                query_res_stream << *it << " "; // sets are sorted
            query_res_stream << "\n";
#endif
        }
        LOG_E("Measure complete.\n");
    }

    void benchmark_tao_mix_latency(
        const std::string& assoc_range_res_file,
        const std::string& assoc_count_res_file,
        const std::string& obj_get_res_file,
        const std::string& assoc_get_res_file,
        const std::string& assoc_time_range_res_file,
        int warmup_n, int measure_n,
        const std::string& warmup_assoc_range_file,
        const std::string& assoc_range_file,
        const std::string& warmup_assoc_count_file,
        const std::string& assoc_count_file,
        const std::string& warmup_obj_get_file,
        const std::string& obj_get_file,
        const std::string& warmup_assoc_get_file,
        const std::string& assoc_get_file,
        const std::string& warmup_assoc_time_range_file,
        const std::string& assoc_time_range_file)
    {
        std::ofstream assoc_range_res(assoc_range_res_file);
        std::ofstream assoc_count_res(assoc_count_res_file);
        std::ofstream obj_get_res(obj_get_res_file);
        std::ofstream assoc_get_res(assoc_get_res_file);
        std::ofstream assoc_time_range_res(assoc_time_range_res_file);

        // assoc_range
        read_assoc_range_queries(warmup_assoc_range_file, assoc_range_file);
        // assoc_count
        read_neighbor_atype_queries(warmup_assoc_count_file, assoc_count_file,
            warmup_assoc_count_nodes, assoc_count_nodes,
            warmup_assoc_count_atypes, assoc_count_atypes);
        // obj_get
        read_neighbor_queries(warmup_obj_get_file, obj_get_file,
            warmup_obj_get_nodes, obj_get_nodes);
        // assoc_get
        read_assoc_get_queries(warmup_assoc_get_file, assoc_get_file);
        // assoc_time_range
        read_assoc_time_range_queries(
            warmup_assoc_time_range_file, assoc_time_range_file);

        std::mt19937 rng(1618);
        std::uniform_int_distribution<int> uni(0, 4);

        std::vector<ThriftAssoc> result;
        int64_t cnt;
        std::vector<std::string> attrs;
        time_t t0, t1;

        LOG_E("Benchmarking TAO mixed query latency\n");
        try {
            LOG_E("Warming up for %d queries...\n", warmup_n);
            for (int i = 0; i < warmup_n; ++i) {
                int rand_query = uni(rng);
                switch (rand_query) {
                case 0:
                    assoc_range_f_(result,
                        mod_get(warmup_assoc_range_nodes, i),
                        mod_get(warmup_assoc_range_atypes, i),
                        mod_get(warmup_assoc_range_offs, i),
                        mod_get(warmup_assoc_range_lens, i));
                    break;
                case 1:
                    assoc_count_f_(
                        mod_get(warmup_assoc_count_nodes, i),
                        mod_get(warmup_assoc_count_atypes, i));
                    break;
                case 2:
                    obj_get_f_(attrs, mod_get(warmup_obj_get_nodes, i));
                    break;
                case 3:
                    assoc_get_f_(result,
                        mod_get(warmup_assoc_get_nodes, i),
                        mod_get(warmup_assoc_get_atypes, i),
                        mod_get(warmup_assoc_get_dst_id_sets, i),
                        mod_get(warmup_assoc_get_lows, i),
                        mod_get(warmup_assoc_get_highs, i));
                    break;
                case 4:
                    assoc_time_range_f_(result,
                        mod_get(warmup_assoc_time_range_nodes, i),
                        mod_get(warmup_assoc_time_range_atypes, i),
                        mod_get(warmup_assoc_time_range_lows, i),
                        mod_get(warmup_assoc_time_range_highs, i),
                        mod_get(warmup_assoc_time_range_limits, i));
                    break;
                default:
                    assert(false);
                }
            }
            LOG_E("Warmup complete.\n");

            rng.seed(1618);

            // Measure phase
            LOG_E("Measuring for %d queries...\n", measure_n);
            for (int i = 0; i < measure_n; ++i) {
                int rand_query = uni(rng);
                switch (rand_query) {
                case 0:
                    t0 = get_timestamp();
                    assoc_range_f_(result,
                        mod_get(assoc_range_nodes, i),
                        mod_get(assoc_range_atypes, i),
                        mod_get(assoc_range_offs, i),
                        mod_get(assoc_range_lens, i));
                    t1 = get_timestamp();
                    assoc_range_res << result.size() << "," << t1 - t0 << '\n';
                    break;
                case 1:
                    t0 = get_timestamp();
                    cnt = assoc_count_f_(
                        mod_get(assoc_count_nodes, i),
                        mod_get(assoc_count_atypes, i));
                    t1 = get_timestamp();
                    assoc_count_res << cnt << "," << t1 - t0 << "\n";
                    break;
                case 2:
                    t0 = get_timestamp();
                    obj_get_f_(attrs, mod_get(obj_get_nodes, i));
                    t1 = get_timestamp();
                    obj_get_res << attrs.size() << "," << t1 - t0 << "\n";
                    break;
                case 3:
                    t0 = get_timestamp();
                    assoc_get_f_(result,
                        mod_get(assoc_get_nodes, i),
                        mod_get(assoc_get_atypes, i),
                        mod_get(assoc_get_dst_id_sets, i),
                        mod_get(assoc_get_lows, i),
                        mod_get(assoc_get_highs, i));
                    t1 = get_timestamp();
                    assoc_get_res << result.size() << "," << t1 - t0 << "\n";
                    break;
                case 4:
                    t0 = get_timestamp();
                    assoc_time_range_f_(result,
                        mod_get(assoc_time_range_nodes, i),
                        mod_get(assoc_time_range_atypes, i),
                        mod_get(assoc_time_range_lows, i),
                        mod_get(assoc_time_range_highs, i),
                        mod_get(assoc_time_range_limits, i));
                    t1 = get_timestamp();
                    assoc_time_range_res << result.size() << "," << t1 - t0
                        << "\n";
                    break;
                default:
                    assert(false);
                }
            }
            LOG_E("Measure complete.\n");
        } catch (std::exception &e) {
            LOG_E("Exception: %s\n", e.what());
        }
    }

    void benchmark_mix_throughput(
        const int num_threads,
        const std::string& master_hostname,
        const std::string& warmup_neighbor_query_file,
        const std::string& neighbor_query_file,
        const std::string& warmup_nhbr_atype_file,
        const std::string& nhbr_atype_file,
        const std::string& warmup_nhbr_node_file,
        const std::string& nhbr_node_file,
        const std::string& warmup_node_query_file,
        const std::string& node_query_file)
    {
        read_neighbor_queries(warmup_neighbor_query_file, neighbor_query_file,
            warmup_neighbor_indices, neighbor_indices);
        read_neighbor_atype_queries(warmup_nhbr_atype_file, nhbr_atype_file,
            warmup_nhbrAtype_indices, nhbrAtype_indices,
            warmup_atypes, atypes);
        read_neighbor_node_queries(warmup_nhbr_node_file, nhbr_node_file);
        read_node_queries(warmup_node_query_file, node_query_file);

        bench_throughput(num_threads, master_hostname, BenchType::MIX);
    }

    void benchmark_mix_latency(
        const std::string& nhbr_res_file,
        const std::string& nhbr_atype_res_file,
        const std::string& nhbr_node_res_file,
        const std::string& node_res_file,
        const std::string& node_node_res_file,
        uint64_t WARMUP_N, uint64_t MEASURE_N,
        const std::string& warmup_neighbor_query_file,
        const std::string& neighbor_query_file,
        const std::string& warmup_nhbr_atype_file,
        const std::string& nhbr_atype_file,
        const std::string& warmup_nhbr_node_file,
        const std::string& nhbr_node_file,
        const std::string& warmup_node_query_file,
        const std::string& node_query_file)
    {
        std::ofstream nhbr_res(nhbr_res_file);
        std::ofstream nhbr_atype_res(nhbr_atype_res_file);
        std::ofstream nhbr_node_res(nhbr_node_res_file);
        std::ofstream node_res(node_res_file);
        std::ofstream node_node_res(node_node_res_file);

        read_neighbor_queries(warmup_neighbor_query_file, neighbor_query_file,
            warmup_neighbor_indices, neighbor_indices);
        read_neighbor_atype_queries(warmup_nhbr_atype_file, nhbr_atype_file,
            warmup_nhbrAtype_indices, nhbrAtype_indices,
            warmup_atypes, atypes);
        read_neighbor_node_queries(warmup_nhbr_node_file, nhbr_node_file);
        read_node_queries(warmup_node_query_file, node_query_file);

        std::mt19937 rng(1618);
        std::uniform_int_distribution<int> uni(0, 4);

        std::vector<int64_t> result;
        std::set<int64_t> result_set;

        LOG_E("Benchmarking mixQuery latency\n");
        try {
            // Warmup phase
            LOG_E("Warming up for %" PRIu64 " queries...\n", WARMUP_N);
            for (int i = 0; i < WARMUP_N; ++i) {
                int rand_query = uni(rng);
                switch (rand_query) {
                case 0:
                    get_neighbors_f_(result,
                        mod_get(warmup_neighbor_indices, i));
                    break;
                case 1:
                    get_neighbors_attr_f_(result,
                        mod_get(warmup_nhbrNode_indices, i),
                        mod_get(warmup_nhbrNode_attr_ids, i),
                        mod_get(warmup_nhbrNode_attrs, i));
                    break;
                case 2:
                    get_nodes_f_(result_set,
                        mod_get(warmup_node_attributes, i),
                        mod_get(warmup_node_queries, i));
                    break;
                case 3:
                    get_neighbors_atype_f_(result,
                        mod_get(warmup_nhbrAtype_indices, i),
                        mod_get(warmup_atypes, i));
                    break;
                case 4:
                    get_nodes2_f_(result_set,
                        mod_get(warmup_node_attributes, i),
                        mod_get(warmup_node_queries, i),
                        mod_get(warmup_node_attributes2, i),
                        mod_get(warmup_node_queries2, i));
                    break;
                default:
                    assert(false);
                }
            }
            LOG_E("Warmup complete.\n");

            rng.seed(1618);

            // Measure phase
            LOG_E("Measuring for %" PRIu64 " queries...\n", MEASURE_N);
            int64_t latency = 0;
            for (int i = 0; i < MEASURE_N; ++i) {
                int rand_query = uni(rng);
                switch (rand_query) {
                case 0:
                {
                    scoped_timer t(&latency);
                    get_neighbors_f_(result, mod_get(neighbor_indices, i));
                }
                    nhbr_res << result.size() << "," << latency << std::endl;
                    break;
                case 1:
                {
                    scoped_timer t(&latency);
                    get_neighbors_attr_f_(result,
                        mod_get(nhbrNode_indices, i),
                        mod_get(nhbrNode_attr_ids, i),
                        mod_get(nhbrNode_attrs, i));
                }
                    nhbr_node_res << result.size() << "," << latency << "\n";
                    break;
                case 2:
                {
                    scoped_timer t(&latency);
                    get_nodes_f_(result_set,
                        mod_get(node_attributes, i),
                        mod_get(node_queries, i));
                }
                    node_res << result_set.size() << "," << latency << "\n";
                    break;
                case 3:
                {
                    scoped_timer t(&latency);
                    get_neighbors_atype_f_(result,
                        mod_get(nhbrAtype_indices, i),
                        mod_get(atypes, i));
                }
                    nhbr_atype_res << result.size() << "," << latency << "\n";
                    break;
                case 4:
                {
                    scoped_timer t(&latency);
                    get_nodes2_f_(result_set,
                        mod_get(node_attributes, i),
                        mod_get(node_queries, i),
                        mod_get(node_attributes2, i),
                        mod_get(node_queries2, i));
                }
                    node_node_res << result_set.size() << "," << latency
                    << "\n";
                    break;
                default:
                    assert(false);
                }
            }
            LOG_E("Measure complete.\n");
        } catch (std::exception &e) {
            LOG_E("Exception: %s\n", e.what());
        }
    }

    void benchmark_neighbor_node_throughput(
        const int num_threads,
        const std::string& master_hostname,
        std::string warmup_query_file,
        std::string query_file)
    {
        read_neighbor_node_queries(warmup_query_file, query_file);
        bench_throughput(num_threads, master_hostname, BenchType::NHBR_NODE);
    }

    void benchmark_neighbor_node_latency(
        std::string res_path,
        uint64_t WARMUP_N,
        uint64_t MEASURE_N,
        std::string warmup_query_file,
        std::string query_file)
    {
        read_neighbor_node_queries(warmup_query_file, query_file);
        time_t t0, t1;
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif

        LOG_E("Benchmarking getNeighborOfNode latency\n");

        // Warmup
        LOG_E("Warming up for %" PRIu64 " queries...\n", WARMUP_N);
        std::vector<int64_t> result;
        for(uint64_t i = 0; i < WARMUP_N; ++i) {
            get_neighbors_attr_f_(result,
                mod_get(warmup_nhbrNode_indices, i),
                mod_get(warmup_nhbrNode_attr_ids, i),
                mod_get(warmup_nhbrNode_attrs, i));
        }
        LOG_E("Warmup complete.\n");

        // Measure
        LOG_E("Measuring for %" PRIu64 " queries...\n", MEASURE_N);
        for (uint64_t i = 0; i < MEASURE_N; ++i) {
            t0 = get_timestamp();
            get_neighbors_attr_f_(result,
                mod_get(nhbrNode_indices, i),
                mod_get(nhbrNode_attr_ids, i),
                mod_get(nhbrNode_attrs, i));
            t1 = get_timestamp();
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            // correctness
            query_res_stream << "id " << mod_get(nhbrNode_indices, i)
                << " attr " << mod_get(nhbrNode_attr_ids, i);
            query_res_stream << " query " << mod_get(nhbrNode_attrs, i) << "\n";
            std::sort(result.begin(), result.end());
            for (auto it = result.begin(); it != result.end(); ++it)
                query_res_stream << *it << " ";
            query_res_stream << "\n";
#endif
        }

        LOG_E("Measure complete.\n");
    }

    void benchmark_assoc_range_latency(
        std::string res_path,
        uint64_t warmup_n,
        uint64_t measure_n,
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        read_assoc_range_queries(warmup_query_file, query_file);
        time_t t0, t1;
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif

        LOG_E("Benchmarking assoc_range() latency\n");

        LOG_E("Warming up for %" PRIu64 " queries...\n", warmup_n);
        std::vector<ThriftAssoc> result;
        for (uint64_t i = 0; i < warmup_n; ++i) {
            assoc_range_f_(result,
                mod_get(warmup_assoc_range_nodes, i),
                mod_get(warmup_assoc_range_atypes, i),
                mod_get(warmup_assoc_range_offs, i),
                mod_get(warmup_assoc_range_lens, i));
        }
        LOG_E("Warmup complete.\n");

        LOG_E("Measuring for %" PRIu64 " queries...\n", measure_n);
        for (uint64_t i = 0; i < measure_n; ++i) {
            t0 = get_timestamp();
            assoc_range_f_(result,
                mod_get(assoc_range_nodes, i),
                mod_get(assoc_range_atypes, i),
                mod_get(assoc_range_offs, i),
                mod_get(assoc_range_lens, i));
            t1 = get_timestamp();
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            for (const auto& assoc : result) {
                query_res_stream
                    << "[src=" << assoc.srcId
                    << ",dst=" << assoc.dstId
                    << ",atype=" << assoc.atype
                    << ",time=" << assoc.timestamp
                    << ",attr='" << assoc.attr << "'] ";
            }
            query_res_stream << std::endl;
#endif
        }
        LOG_E("Measure complete.\n");
    }

    void benchmark_assoc_count_latency(
        std::string res_path,
        uint64_t warmup_n,
        uint64_t measure_n,
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        read_neighbor_atype_queries(warmup_query_file, query_file,
            warmup_assoc_count_nodes, assoc_count_nodes,
            warmup_assoc_count_atypes, assoc_count_atypes);
        time_t t0, t1;
        std::ofstream res_stream(res_path);
        LOG_E("Benchmarking assoc_count() latency\n");

        LOG_E("Warming up for %" PRIu64 " queries...\n", warmup_n);
        std::vector<ThriftAssoc> result;
        for (uint64_t i = 0; i < warmup_n; ++i) {
            assoc_count_f_(
                mod_get(warmup_assoc_count_nodes, i),
                mod_get(warmup_assoc_count_atypes, i));
        }
        LOG_E("Warmup complete.\n");

        int64_t cnt;
        LOG_E("Measuring for %" PRIu64 " queries...\n", measure_n);
        for (uint64_t i = 0; i < measure_n; ++i) {
            t0 = get_timestamp();
            cnt = assoc_count_f_(
                mod_get(assoc_count_nodes, i),
                mod_get(assoc_count_atypes, i));
            t1 = get_timestamp();
            res_stream << cnt << "," << t1 - t0 << "\n";
        }
        LOG_E("Measure complete.\n");
    }

    void benchmark_assoc_get_latency(
        std::string res_path,
        int64_t warmup_n,
        int64_t measure_n,
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        read_assoc_get_queries(warmup_query_file, query_file);
        time_t t0, t1;
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif
        LOG_E("Benchmarking assoc_get() latency\n");

        LOG_E("Warming up for %" PRIu64 " queries...\n", warmup_n);
        std::vector<ThriftAssoc> result;
        for (int64_t i = 0; i < warmup_n; ++i) {
            assoc_get_f_(result,
                mod_get(warmup_assoc_get_nodes, i),
                mod_get(warmup_assoc_get_atypes, i),
                mod_get(warmup_assoc_get_dst_id_sets, i),
                mod_get(warmup_assoc_get_lows, i),
                mod_get(warmup_assoc_get_highs, i));
        }
        LOG_E("Warmup complete.\n");

        LOG_E("Measuring for %" PRIu64 " queries...\n", measure_n);
        for (int64_t i = 0; i < measure_n; ++i) {
            t0 = get_timestamp();
            assoc_get_f_(result,
                mod_get(assoc_get_nodes, i),
                mod_get(assoc_get_atypes, i),
                mod_get(assoc_get_dst_id_sets, i),
                mod_get(assoc_get_lows, i),
                mod_get(assoc_get_highs, i));
            t1 = get_timestamp();
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            for (const auto& assoc : result) {
                query_res_stream
                    << "[src=" << assoc.srcId
                    << ",dst=" << assoc.dstId
                    << ",atype=" << assoc.atype
                    << ",time=" << assoc.timestamp
                    << ",attr='" << assoc.attr << "'] ";
            }
            query_res_stream << std::endl;
#endif
        }
        LOG_E("Measure complete.\n");
    }

    void benchmark_obj_get_latency(
        std::string res_path,
        uint64_t warmup_n,
        uint64_t measure_n,
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        read_neighbor_queries(warmup_query_file, query_file,
            warmup_obj_get_nodes, obj_get_nodes);
        time_t t0, t1;
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif

        LOG_E("Benchmarking obj_get() latency\n");

        LOG_E("Warming up for %" PRIu64 " queries...\n", warmup_n);
        std::vector<std::string> result;
        for (uint64_t i = 0; i < warmup_n; ++i) {
            obj_get_f_(result, mod_get(warmup_obj_get_nodes, i));
        }
        LOG_E("Warmup complete.\n");

        LOG_E("Measuring for %" PRIu64 " queries...\n", measure_n);
        for (uint64_t i = 0; i < measure_n; ++i) {
            t0 = get_timestamp();
            obj_get_f_(result, mod_get(obj_get_nodes, i));
            t1 = get_timestamp();
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            for (const auto& attr : result) {
                query_res_stream << "'" << attr << "', ";
            }
            query_res_stream << std::endl;
#endif
        }
        LOG_E("Measure complete.\n");
    }

    void benchmark_assoc_time_range_latency(
        std::string res_path,
        int64_t warmup_n,
        int64_t measure_n,
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        read_assoc_time_range_queries(warmup_query_file, query_file);
        time_t t0, t1;
        std::ofstream res_stream(res_path);

#ifdef BENCH_PRINT_RESULTS
        std::ofstream query_res_stream(res_path + ".succinct_result");
#endif
        LOG_E("Benchmarking assoc_time_range() latency\n");

        LOG_E("Warming up for %" PRIu64 " queries...\n", warmup_n);
        std::vector<ThriftAssoc> result;
        for (int64_t i = 0; i < warmup_n; ++i) {
            assoc_time_range_f_(result,
                mod_get(warmup_assoc_time_range_nodes, i),
                mod_get(warmup_assoc_time_range_atypes, i),
                mod_get(warmup_assoc_time_range_lows, i),
                mod_get(warmup_assoc_time_range_highs, i),
                mod_get(warmup_assoc_time_range_limits, i));
        }
        LOG_E("Warmup complete.\n");

        LOG_E("Measuring for %" PRIu64 " queries...\n", measure_n);
        for (int64_t i = 0; i < measure_n; ++i) {
            t0 = get_timestamp();
            assoc_time_range_f_(result,
                mod_get(assoc_time_range_nodes, i),
                mod_get(assoc_time_range_atypes, i),
                mod_get(assoc_time_range_lows, i),
                mod_get(assoc_time_range_highs, i),
                mod_get(assoc_time_range_limits, i));
            t1 = get_timestamp();
            res_stream << result.size() << "," << t1 - t0 << "\n";

#ifdef BENCH_PRINT_RESULTS
            for (const auto& assoc : result) {
                query_res_stream
                    << "[src=" << assoc.srcId
                    << ",dst=" << assoc.dstId
                    << ",atype=" << assoc.atype
                    << ",time=" << assoc.timestamp
                    << ",attr='" << assoc.attr << "'] ";
            }
            query_res_stream << std::endl;
#endif
        }
        LOG_E("Measure complete.\n");
    }

protected:

    SuccinctGraph * graph_;
    shared_ptr<GraphQueryAggregatorServiceClient> aggregator_;

    std::function<void(std::vector<int64_t>&, int64_t)> get_neighbors_f_;

    std::function<void(
        std::vector<int64_t>&,
        int64_t,
        int64_t)> get_neighbors_atype_f_;

    std::function<void(
        std::vector<int64_t>&,
        int64_t,
        int,
        const std::string&)> get_neighbors_attr_f_;

    std::function<void(
        std::set<int64_t>&,
        int,
        const std::string&)> get_nodes_f_;

    std::function<void(
        std::set<int64_t>&,
        int,
        const std::string&,
        int,
        const std::string&)> get_nodes2_f_;

    // TAO functions

    std::function<void(std::vector<std::string>&, int64_t)> obj_get_f_;

    std::function<void(std::vector<ThriftAssoc>&,
        int64_t, int64_t, int32_t, int32_t)> assoc_range_f_;

    std::function<void(std::vector<ThriftAssoc>&,
        int64_t, int64_t,
        const std::set<int64_t>&, int64_t, int64_t)> assoc_get_f_;

    std::function<int64_t(int64_t, int64_t)> assoc_count_f_;

    std::function<void(std::vector<ThriftAssoc>&,
        int64_t, int64_t, int64_t, int64_t, int32_t)> assoc_time_range_f_;

    uint64_t WARMUP_N; uint64_t MEASURE_N;
    static const uint64_t COOLDOWN_N = 500;

    // get_nhbrs(n)
    std::vector<int64_t> warmup_neighbor_indices, neighbor_indices;

    // get_nhbrs(n, atype)
    std::vector<int64_t> warmup_nhbrAtype_indices, nhbrAtype_indices;
    std::vector<int64_t> warmup_atypes, atypes;

    // get_nhbrs(n, attr)
    std::vector<int64_t> warmup_nhbrNode_indices, nhbrNode_indices;
    std::vector<int> warmup_nhbrNode_attr_ids, nhbrNode_attr_ids;
    std::vector<std::string> warmup_nhbrNode_attrs, nhbrNode_attrs;

    // 2 get_nodes()
    std::vector<int> warmup_node_attributes, node_attributes;
    std::vector<std::string> warmup_node_queries, node_queries;
    std::vector<int> warmup_node_attributes2, node_attributes2;
    std::vector<std::string> warmup_node_queries2, node_queries2;

    // assoc_range()
    std::vector<int64_t> warmup_assoc_range_nodes, assoc_range_nodes;
    std::vector<int64_t> warmup_assoc_range_atypes, assoc_range_atypes;
    std::vector<int32_t> warmup_assoc_range_offs, assoc_range_offs;
    std::vector<int32_t> warmup_assoc_range_lens, assoc_range_lens;

    // assoc_count()
    std::vector<int64_t> warmup_assoc_count_nodes, assoc_count_nodes;
    std::vector<int64_t> warmup_assoc_count_atypes, assoc_count_atypes;

    // obj_get
    std::vector<int64_t> warmup_obj_get_nodes, obj_get_nodes;

    // assoc_get()
    std::vector<int64_t> warmup_assoc_get_nodes, assoc_get_nodes;
    std::vector<int64_t> warmup_assoc_get_atypes, assoc_get_atypes;
    std::vector<std::set<int64_t>> warmup_assoc_get_dst_id_sets;
    std::vector<std::set<int64_t>> assoc_get_dst_id_sets;
    std::vector<int64_t> warmup_assoc_get_highs, assoc_get_highs;
    std::vector<int64_t> warmup_assoc_get_lows, assoc_get_lows;

    // assoc_time_range()
    std::vector<int64_t> warmup_assoc_time_range_nodes, assoc_time_range_nodes;
    std::vector<int64_t> warmup_assoc_time_range_atypes;
    std::vector<int64_t> assoc_time_range_atypes;
    std::vector<int64_t> warmup_assoc_time_range_highs, assoc_time_range_highs;
    std::vector<int64_t> warmup_assoc_time_range_lows, assoc_time_range_lows;
    std::vector<int32_t> warmup_assoc_time_range_limits;
    std::vector<int32_t> assoc_time_range_limits;

    void read_assoc_range_queries(
        const std::string& warmup_file, const std::string& file)
    {
        auto read = [](
            const std::string& file,
            std::vector<int64_t>& nodes, std::vector<int64_t>& atypes,
            std::vector<int32_t>& offs, std::vector<int32_t>& lens)
        {
            std::ifstream ifs(file);
            std::string line, token;
            while (std::getline(ifs, line)) {
                std::stringstream ss(line);

                std::getline(ss, token, ',');
                nodes.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                atypes.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                offs.push_back(std::stoi(token));

                std::getline(ss, token);
                lens.push_back(std::stoll(token));
            }
        };
        read(warmup_file, warmup_assoc_range_nodes, warmup_assoc_range_atypes,
            warmup_assoc_range_offs, warmup_assoc_range_lens);

        read(file, assoc_range_nodes, assoc_range_atypes,
            assoc_range_offs, assoc_range_lens);
    }

    void read_assoc_get_queries(
        const std::string& warmup_file, const std::string& file)
    {
        auto read = [](
            const std::string& file,
            std::vector<int64_t>& nodes, std::vector<int64_t>& atypes,
            std::vector<int64_t>& lows, std::vector<int64_t>& highs,
            std::vector<std::set<int64_t>>& dst_id_sets)
        {
            std::ifstream ifs(file);
            std::string line, token;
            while (std::getline(ifs, line)) {
                std::stringstream ss(line);

                std::getline(ss, token, ',');
                nodes.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                atypes.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                lows.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                highs.push_back(std::stoll(token));

                std::set<int64_t> dst_id_set;
                while (std::getline(ss, token, ',')) {
                    dst_id_set.insert(std::stoll(token));
                }
                dst_id_sets.push_back(dst_id_set);
            }
        };
        read(warmup_file, warmup_assoc_get_nodes, warmup_assoc_get_atypes,
            warmup_assoc_get_lows, warmup_assoc_get_highs,
            warmup_assoc_get_dst_id_sets);

        read(file, assoc_get_nodes, assoc_get_atypes,
            assoc_get_lows, assoc_get_highs, assoc_get_dst_id_sets);
    }

    void read_assoc_time_range_queries(
        const std::string& warmup_file, const std::string& file)
    {
        auto read = [](
            const std::string& file,
            std::vector<int64_t>& nodes, std::vector<int64_t>& atypes,
            std::vector<int64_t>& lows, std::vector<int64_t>& highs,
            std::vector<int32_t>& limits)
        {
            std::ifstream ifs(file);
            std::string line, token;
            while (std::getline(ifs, line)) {
                std::stringstream ss(line);

                std::getline(ss, token, ',');
                nodes.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                atypes.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                lows.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                highs.push_back(std::stoll(token));

                std::getline(ss, token, ',');
                limits.push_back(std::stoi(token));
            }
        };
        read(warmup_file, warmup_assoc_time_range_nodes,
            warmup_assoc_time_range_atypes,
            warmup_assoc_time_range_lows, warmup_assoc_time_range_highs,
            warmup_assoc_time_range_limits);

        read(file, assoc_time_range_nodes, assoc_time_range_atypes,
            assoc_time_range_lows, assoc_time_range_highs,
            assoc_time_range_limits);
    }

    void read_neighbor_queries(
        const std::string& warmup_neighbor_file,
        const std::string& query_neighbor_file,
        std::vector<int64_t>& warmup_neighbor_indices,
        std::vector<int64_t>& neighbor_indices)
    {
        std::ifstream warmup_input(warmup_neighbor_file);
        std::ifstream query_input(query_neighbor_file);

        std::string line;
        while (getline(warmup_input, line)) {
            warmup_neighbor_indices.push_back(std::atoi(line.c_str()));
        }

        while (getline(query_input, line)) {
            neighbor_indices.push_back(std::atoi(line.c_str()));
        }
    }

    void read_neighbor_atype_queries(
        const std::string& warmup_file, const std::string& query_file,
        std::vector<int64_t>& warmup_nhbrAtype_indices,
        std::vector<int64_t>& nhbrAtype_indices,
        std::vector<int64_t>& warmup_atypes,
        std::vector<int64_t>& atypes)
    {
        std::ifstream warmup_input(warmup_file);
        std::ifstream query_input(query_file);
        std::string line;
        while (getline(warmup_input, line)) {
            std::vector<std::string> toks = split(line, ',');
            warmup_nhbrAtype_indices.push_back(std::stoll(toks[0]));
            warmup_atypes.push_back(std::stoi(toks[1]));
        }
        while (getline(query_input, line)) {
            std::vector<std::string> toks = split(line, ',');
            nhbrAtype_indices.push_back(std::stoll(toks[0]));
            atypes.push_back(std::stoi(toks[1]));
        }
    }

    void read_node_queries(
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        std::ifstream warmup_input(warmup_query_file);
        std::ifstream query_input(query_file);

        std::string line;
        while (getline(warmup_input, line)) {
            std::vector<std::string> toks = split(
                line, GraphFormatter::QUERY_FILED_DELIM);
            warmup_node_attributes.push_back(std::atoi(toks[0].c_str()));
            warmup_node_queries.push_back(toks[1]);
            warmup_node_attributes2.push_back(std::atoi(toks[2].c_str()));
            warmup_node_queries2.push_back(toks[3]);
        }
        while (getline(query_input, line)) {
            std::vector<std::string> toks = split(
                line, GraphFormatter::QUERY_FILED_DELIM);
            node_attributes.push_back(std::atoi(toks[0].c_str()));
            node_queries.push_back(toks[1]);
            node_attributes2.push_back(std::atoi(toks[2].c_str()));
            node_queries2.push_back(toks[3]);
        }
    }

    void read_neighbor_node_queries(
        const std::string& warmup_query_file,
        const std::string& query_file)
    {
        std::ifstream warmup_input(warmup_query_file);
        std::ifstream query_input(query_file);

        std::string line;
        while (getline(warmup_input, line)) {
            // Format: nodeId,attrId,[everything to EOL is attr]
            // Since attr can contain ',', we don't use split() to parse
            int pos = line.find(',');
            warmup_nhbrNode_indices.push_back(std::stoll(line.substr(0, pos)));
            int pos2 = line.find(',', pos + 1);
            warmup_nhbrNode_attr_ids.push_back(
                std::stoi(line.substr(pos + 1, pos2 - pos - 1)));
            warmup_nhbrNode_attrs.push_back(line.substr(pos2 + 1));
        }
        while (getline(query_input, line)) {
            int pos = line.find(',');
            nhbrNode_indices.push_back(std::stoll(line.substr(0, pos)));
            int pos2 = line.find(',', pos + 1);
            nhbrNode_attr_ids.push_back(
            std::stoi(line.substr(pos + 1, pos2 - pos - 1)));
            nhbrNode_attrs.push_back(line.substr(pos2 + 1));
        }
    }

    std::vector<std::string> split(const std::string &s, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }
};

#endif
