#ifndef SUCCINCT_GRAPH_BENCHMARK_H
#define SUCCINCT_GRAPH_BENCHMARK_H

#include <string>
#include <sstream>
#include <vector>
#include "../../external/succinct-cpp/benchmark/include/Benchmark.hpp"
#include "../../include/succinct-graph/SuccinctGraph.hpp"

class GraphBenchmark : public Benchmark {
public:
    GraphBenchmark(SuccinctGraph *graph) : Benchmark() {
        this->graph = graph;
    }

    // BENCHMARKING NEIGHBOR QUERIES
    void benchmark_neighbor_latency(std::string res_path, count_t WARMUP_N, count_t MEASURE_N,
            std::string warmup_query_file, std::string query_file) {
        time_t t0, t1;
        fprintf(stderr, "Benchmarking getNeighbor latency of %s\n", this->graph->succinct_directory().c_str());
        read_neighbor_queries(warmup_query_file, query_file);
        std::ofstream res_stream(res_path);

        // Warmup
        fprintf(stderr, "Warming up for %lu queries...\n", WARMUP_N);
        for(uint64_t i = 0; i < WARMUP_N; i++) {
            std::set<int64_t> result;
            this->graph->get_neighbors(result, warmup_neighbor_indices[i]);
            assert(result.size() != 0 && "No result found in benchmarking neighbor latency");
        }
        fprintf(stderr, "Warmup complete.\n");

        // Measure
        fprintf(stderr, "Measuring for %lu queries...\n", MEASURE_N);
        for(uint64_t i = 0; i < MEASURE_N; i++) {
            std::set<int64_t> result;
            t0 = get_timestamp();
            this->graph->get_neighbors(result, neighbor_indices[i]);
            t1 = get_timestamp();
            assert(result.size() != 0 && "No result found in benchmarking node latency");
            res_stream << result.size() << "," << t1 - t0 << "\n";
        }
        fprintf(stderr, "Measure complete.\n");

        // Cooldown
        fprintf(stderr, "Cooling down for %lu queries...\n", COOLDOWN_N);
        for(uint64_t i = 0; i < COOLDOWN_N; i++) {
            std::set<int64_t> result;
            this->graph->get_neighbors(result, warmup_neighbor_indices[i]);
        }
        fprintf(stderr, "Cooldown complete.\n");

        res_stream.close();
    }

    std::pair<double, double> benchmark_neighbor_throughput(
            std::string warmup_query_file, std::string query_file) {
        double get_neighbor_thput = 0;
        double edges_thput = 0;
        read_neighbor_queries(warmup_query_file, query_file);

        try {
            // Warmup phase
            long i = 0;
            time_t warmup_start = get_timestamp();
            while (get_timestamp() - warmup_start < WARMUP_T) {
                std::set<int64_t> result;
                this->graph->get_neighbors(result, warmup_neighbor_indices[i % warmup_neighbor_indices.size()]);
                i++;
            }

            // Measure phase
            i = 0;
            long edges = 0;
            double totsecs = 0;
            time_t start = get_timestamp();
            while (get_timestamp() - start < MEASURE_T) {
                std::set<int64_t> result;
                time_t query_start = get_timestamp();
                this->graph->get_neighbors(result, neighbor_indices[i % neighbor_indices.size()]);
                time_t query_end = get_timestamp();
                totsecs += (double) (query_end - query_start) / (1E6);
                edges += result.size();
                i++;
            }
            get_neighbor_thput = ((double) i / totsecs);
            edges_thput = ((double) edges / totsecs);

            i = 0;
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_T) {
                std::set<int64_t> result;
                this->graph->get_neighbors(result, warmup_neighbor_indices[i % warmup_neighbor_indices.size()]);
                i++;
            }

        } catch (std::exception &e) {
            fprintf(stderr, "Throughput test ends...\n");
        }

        return std::make_pair(get_neighbor_thput, edges_thput);
    }

    // NODE BENCHMARKING
    void benchmark_node_latency(std::string res_path, count_t WARMUP_N, count_t MEASURE_N,
            std::string warmup_query_file, std::string query_file) {
        time_t t0, t1;
        read_node_queries(warmup_query_file, query_file);
        std::ofstream res_stream(res_path);
        fprintf(stderr, "Benchmarking getNode latency of %s\n", this->graph->succinct_directory().c_str());

        // Warmup
        fprintf(stderr, "Warming up for %lu queries...\n", WARMUP_N);
        for(uint64_t i = 0; i < WARMUP_N; i++) {
            std::set<int64_t> result;
            this->graph->search_nodes(result, warmup_node_attributes[i], warmup_node_queries[i]);
            assert(result.size() != 0 && "No result found in benchmarking node latency");
        }
        fprintf(stderr, "Warmup complete.\n");

        // Measure
        fprintf(stderr, "Measuring for %lu queries...\n", MEASURE_N);
        for(uint64_t i = 0; i < MEASURE_N; i++) {
            std::set<int64_t> result;
            t0 = get_timestamp();
            this->graph->search_nodes(result, node_attributes[i], node_queries[i]);
            t1 = get_timestamp();
            assert(result.size() != 0 && "No result found in benchmarking node latency");
            res_stream << result.size() << "," << t1 - t0 << "\n";
        }
        fprintf(stderr, "Measure complete.\n");

        // Cooldown
        fprintf(stderr, "Cooling down for %lu queries...\n", COOLDOWN_N);
        for(uint64_t i = 0; i < COOLDOWN_N; i++) {
            std::set<int64_t> result;
            this->graph->search_nodes(result, warmup_node_attributes[i], warmup_node_queries[i]);
        }
        fprintf(stderr, "Cooldown complete.\n");

        res_stream.close();
    }

    void benchmark_node_node_latency(std::string res_path, count_t WARMUP_N, count_t MEASURE_N,
            std::string warmup_query_file, std::string query_file) {
        read_node_queries(warmup_query_file, query_file);
        time_t t0, t1;
        std::ofstream res_stream(res_path);
        fprintf(stderr, "Benchmarking getNode with two attributes latency of %s\n", this->graph->succinct_directory().c_str());

        // Warmup
        fprintf(stderr, "Warming up for %lu queries...\n", WARMUP_N);
        for(uint64_t i = 0; i < WARMUP_N; i++) {
            std::set<int64_t> result;
            this->graph->search_nodes(result, warmup_node_attributes[i], warmup_node_queries[i],
                                              warmup_node_attributes2[i], warmup_node_queries2[i]);
            assert(result.size() != 0 && "No result found in benchmarking node two attributes latency");
        }
        fprintf(stderr, "Warmup complete.\n");

        // Measure
        fprintf(stderr, "Measuring for %lu queries...\n", MEASURE_N);
        for(uint64_t i = 0; i < MEASURE_N; i++) {
            std::set<int64_t> result;
            t0 = get_timestamp();
            this->graph->search_nodes(result, node_attributes[i], node_queries[i],
                                              node_attributes2[i], node_queries2[i]);
            t1 = get_timestamp();
            assert(result.size() != 0 && "No result found in benchmarking node two attributes latency");
            res_stream << result.size() << "," << t1 - t0 << "\n";
        }
        fprintf(stderr, "Measure complete.\n");

        // Cooldown
        fprintf(stderr, "Cooling down for %lu queries...\n", COOLDOWN_N);
        for(uint64_t i = 0; i < COOLDOWN_N; i++) {
            std::set<int64_t> result;
            this->graph->search_nodes(result, warmup_node_attributes[i], warmup_node_queries[i],
                                              warmup_node_attributes2[i], warmup_node_queries2[i]);
        }
        fprintf(stderr, "Cooldown complete.\n");

        res_stream.close();
    }

    double benchmark_node_throughput(std::string warmup_query_file, std::string query_file) {
        double thput = 0;
        read_node_queries(warmup_query_file, query_file);

        try {
            // Warmup phase
            long i = 0;
            time_t warmup_start = get_timestamp();
            std::cout << "Warming up" << std::endl;
            int warmup_size = warmup_node_attributes.size();
            while (get_timestamp() - warmup_start < WARMUP_T) {
                std::set<int64_t> result;
                this->graph->search_nodes(result, warmup_node_attributes[i % warmup_size], warmup_node_queries[i % warmup_size]);
                assert(result.size() != 0 && "No result found in benchmarking node throughput");
            }
            fprintf(stderr, "Warmup complete.\n");

            // Measure phase
            i = 0;
            double totsecs = 0;
            time_t start = get_timestamp();
            std::cout << "Measuring throughput" << std::endl;
            int size = node_queries.size();
            while (get_timestamp() - start < MEASURE_T) {
                std::set<int64_t> result;
                time_t query_start = get_timestamp();
                this->graph->search_nodes(result, node_attributes[i % size], node_queries[i % size]);
                time_t query_end = get_timestamp();
                assert(result.size() != 0 && "No result found in benchmarking node throughput");
                totsecs += (double) (query_end - query_start) / (double(1E6));
                i++;
            }
            thput = ((double) i / totsecs);
            std::cout << "Throughput: " << thput << std::endl;

            i = 0;
            time_t cooldown_start = get_timestamp();
            while (get_timestamp() - cooldown_start < COOLDOWN_T) {
                std::set<int64_t> result;
                this->graph->search_nodes(result, warmup_node_attributes[i % warmup_size], warmup_node_queries[i % warmup_size]);
                i++;
            }

        } catch (std::exception &e) {
            fprintf(stderr, "Throughput test ends...\n");
        }

        return thput;
    }

    // BENCHMARKING MIX QUERIES
    void benchmark_mix_latency(std::string res_path, count_t WARMUP_N, count_t MEASURE_N,
            std::string warmup_neighbor_query_file, std::string neighbor_query_file,
            std::string warmup_node_query_file, std::string node_query_file) {
        std::ofstream res_stream(res_path);
        read_neighbor_queries(warmup_neighbor_query_file, neighbor_query_file);
        read_node_queries(warmup_node_query_file, node_query_file);

        fprintf(stderr, "Benchmarking mixQuery latency of %s\n", this->graph->succinct_directory().c_str());
        try {
            // Warmup phase
            fprintf(stderr, "Warming up for %lu queries...\n", WARMUP_N);
            for (int i = 0; i < WARMUP_N; i++) {
                if (i % 2 == 0) {
                    std::set<int64_t> result;
                    this->graph->get_neighbors(result, warmup_neighbor_indices[i/2]);
                    if (result.size() == 0) {
                        fprintf(stderr, "Error getting neighbors for %d.\n", warmup_neighbor_indices[i/2]);
                    }
                } else {
                    std::set<int64_t> result;
                    this->graph->search_nodes(result, warmup_node_attributes[i/2], warmup_node_queries[i/2]);
                    if (result.size() == 0) {
                        fprintf(stderr, "Error searching attr %d for %s\n", warmup_node_attributes[i/2], warmup_node_queries[i/2].c_str());
                    }
                }
            }
            fprintf(stderr, "Warmup complete.\n");

            // Measure phase
            fprintf(stderr, "Measuring for %lu queries...\n", MEASURE_N);
            for (int i = 0; i < MEASURE_N; i++) {
                if (i % 2 == 0) {
                    std::set<int64_t> result;
                    time_t query_start = get_timestamp();
                    this->graph->get_neighbors(result, neighbor_indices[i/2]);
                    time_t query_end = get_timestamp();
                    if (result.size() == 0) {
                        printf("Error getting neighbors for %d\n", neighbor_indices[i/2]);
                    } else {
                        res_stream << result.size() << "," <<  (query_end - query_start) << "\n";
                    }
                } else {
                    std::set<int64_t> result;
                    time_t query_start = get_timestamp();
                    this->graph->search_nodes(result, node_attributes[i/2], node_queries[i/2]);
                    time_t query_end = get_timestamp();
                    if (result.size() == 0) {
                        printf("Error searching for attr %d for %s\n", node_attributes[i/2], node_queries[i/2].c_str());
                    } else {
                        res_stream << result.size() << "," <<  (query_end - query_start) << "\n";
                    }
                }
            }
            fprintf(stderr, "Measure complete.\n");

            // Cooldown phase
            fprintf(stderr, "Cooling down for %lu queries...\n", COOLDOWN_N);
            for (int i = 0; i < COOLDOWN_N; i++) {
                if (i % 2 == 0) {
                    std::set<int64_t> result;
                    this->graph->get_neighbors(result, warmup_neighbor_indices[i/2]);
                } else {
                    std::set<int64_t> result;
                    this->graph->search_nodes(result, warmup_node_attributes[i/2], warmup_node_queries[i/2]);
                }
            }
            fprintf(stderr, "Cooldown complete.\n");

        } catch (std::exception &e) {
            fprintf(stderr, "Throughput test ends...\n");
        }
    }

    void benchmark_neighbor_node_latency(std::string res_path, count_t WARMUP_N, count_t MEASURE_N,
            std::string warmup_query_file, std::string query_file) {
        read_neighbor_node_queries(warmup_query_file, query_file);
        time_t t0, t1;
        std::ofstream res_stream(res_path);
        fprintf(stderr, "Benchmarking getNeighborOfNode latency of %s\n", this->graph->succinct_directory().c_str());

        // Warmup
        fprintf(stderr, "Warming up for %lu queries...\n", WARMUP_N);
        for(uint64_t i = 0; i < WARMUP_N; i++) {
            std::set<int64_t> result;
            this->graph->get_neighbors_of_node(result, warmup_neighbor_indices[i], warmup_node_attributes[i], warmup_node_queries[i]);
            assert(result.size() != 0 && "No result found in benchmarking getNeighborOfNode latency");
        }
        fprintf(stderr, "Warmup complete.\n");

        // Measure
        fprintf(stderr, "Measuring for %lu queries...\n", MEASURE_N);
        for(uint64_t i = 0; i < MEASURE_N; i++) {
            std::set<int64_t> result;
            t0 = get_timestamp();
            this->graph->get_neighbors_of_node(result, neighbor_indices[i], node_attributes[i], node_queries[i]);
            t1 = get_timestamp();
            assert(result.size() != 0 && "No result found in benchmarking getNeighborOfNode latency");
            res_stream << result.size() << "," << t1 - t0 << "\n";
        }
        fprintf(stderr, "Measure complete.\n");

        // Cooldown
        fprintf(stderr, "Cooling down for %lu queries...\n", COOLDOWN_N);
        for(uint64_t i = 0; i < COOLDOWN_N; i++) {
            std::set<int64_t> result;
            this->graph->get_neighbors_of_node(result, warmup_neighbor_indices[i], warmup_node_attributes[i], warmup_node_queries[i]);
        }
        fprintf(stderr, "Cooldown complete.\n");

        res_stream.close();
    }

    double benchmark_mix_throughput(std::string warmup_neighbor_query_file, std::string neighbor_query_file,
            std::string warmup_node_query_file, std::string node_query_file) {
        read_neighbor_queries(warmup_neighbor_query_file, neighbor_query_file);
        read_node_queries(warmup_node_query_file, node_query_file);

        double thput = 0;
        try {
            // Warmup phase
            std::cout << "Warming up" << std::endl;
            int warmup_size = warmup_node_queries.size();
            for (int i = 0; i < WARMUP_N; i++) {
                if (i % 2 == 0) {
                    std::set<int64_t> result;
                    this->graph->get_neighbors(result, warmup_neighbor_indices[i % warmup_size]);
                    if (result.size() == 0) {
                        printf("Error getting neighbors for %d\n", warmup_neighbor_indices[i % warmup_size]);
                        std::exit(1);
                    }
                } else {
                    std::set<int64_t> result;
                    this->graph->search_nodes(result, warmup_node_attributes[i % warmup_size], warmup_node_queries[i % warmup_size]);
                    if (result.size() == 0) {
                        printf("Error searching for attr %d for %s\n", warmup_node_attributes[i % warmup_size], warmup_node_queries[i % warmup_size].c_str());
                        std::exit(1);
                    }
                }
            }

            // Measure phase
            double totsecs = 0;
            std::cout << "Measuring throughput" << std::endl;
            int size = node_attributes.size();
            for (int i = 0; i < MEASURE_N; i++) {
                time_t query_start = get_timestamp();
                if (i % 2 == 0) {
                    std::set<int64_t> result;
                    this->graph->get_neighbors(result, neighbor_indices[i % size]);
                } else {
                    std::set<int64_t> result;
                    this->graph->search_nodes(result, node_attributes[i % size], node_queries[i % size]);
                }
                time_t query_end = get_timestamp();
                totsecs += (double) (query_end - query_start) / (double(1E6));
            }
            thput = ((double) MEASURE_N / totsecs);
            printf("Throughput: %f\n total queries: %lu, total time: %f\n\n", thput, MEASURE_N, totsecs);

            // Cooldown phase
            for (int i = 0; i < COOLDOWN_N; i++) {
                if (i % 2 == 0) {
                    std::set<int64_t> result;
                    this->graph->get_neighbors(result, warmup_neighbor_indices[i % warmup_size]);
                } else {
                    std::set<int64_t> result;
                    this->graph->search_nodes(result, warmup_node_attributes[i % warmup_size], warmup_node_queries[i % warmup_size]);
                }
            }

        } catch (std::exception &e) {
            fprintf(stderr, "Throughput test ends...\n");
        }

        return thput;
    }


protected:
    SuccinctGraph * graph;
    count_t WARMUP_N; count_t MEASURE_N;
    static const count_t COOLDOWN_N = 500;

    std::vector<int> warmup_neighbor_indices;
    std::vector<int> neighbor_indices;

    std::vector<int> warmup_node_attributes;
    std::vector<std::string> warmup_node_queries;
    std::vector<int> node_attributes;
    std::vector<std::string> node_queries;
    std::vector<int> warmup_node_attributes2;
    std::vector<std::string> warmup_node_queries2;
    std::vector<int> node_attributes2;
    std::vector<std::string> node_queries2;

    // READING QUERY FILES
    void read_neighbor_queries(std::string warmup_neighbor_file, std::string query_neighbor_file) {
        std::ifstream warmup_input(warmup_neighbor_file);
        std::ifstream query_input(query_neighbor_file);

        std::string line;
        while (getline(warmup_input, line)) {
            warmup_neighbor_indices.push_back(std::atoi(line.c_str()));
        }

        while (getline(query_input, line)) {
            neighbor_indices.push_back(std::atoi(line.c_str()));
        }
        warmup_input.close();
        query_input.close();
    }

    void read_node_queries(std::string warmup_query_file, std::string query_file) {
        std::ifstream warmup_input(warmup_query_file);
        std::ifstream query_input(query_file);

        std::string line;
        while (getline(warmup_input, line)) {
            std::vector<std::string> toks = split(line, ',');
            warmup_node_attributes.push_back(std::atoi(toks[0].c_str()));
            warmup_node_queries.push_back(toks[1]);
            warmup_node_attributes2.push_back(std::atoi(toks[2].c_str()));
            warmup_node_queries2.push_back(toks[3]);
        }
        while (getline(query_input, line)) {
            std::vector<std::string> toks = split(line, ',');
            node_attributes.push_back(std::atoi(toks[0].c_str()));
            node_queries.push_back(toks[1]);
            node_attributes2.push_back(std::atoi(toks[2].c_str()));
            node_queries2.push_back(toks[3]);
        }
        warmup_input.close();
        query_input.close();
    }

    void read_neighbor_node_queries(std::string warmup_query_file, std::string query_file) {
        std::ifstream warmup_input(warmup_query_file);
        std::ifstream query_input(query_file);

        std::string line;
        while (getline(warmup_input, line)) {
            std::vector<std::string> toks = split(line, ',');
            warmup_neighbor_indices.push_back(std::atoi(toks[0].c_str()));
            warmup_node_attributes.push_back(std::atoi(toks[1].c_str()));
            warmup_node_queries.push_back(toks[2]);
        }
        while (getline(query_input, line)) {
            std::vector<std::string> toks = split(line, ',');
            neighbor_indices.push_back(std::atoi(toks[0].c_str()));
            node_attributes.push_back(std::atoi(toks[1].c_str()));
            node_queries.push_back(toks[2]);
        }
        warmup_input.close();
        query_input.close();
    }

private:
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