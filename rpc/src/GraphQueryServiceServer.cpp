#include "GraphQueryService.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

#include "GraphFormatter.hpp"
#include "GraphLogStore.h"
#include "GraphSuffixStore.h"
#include "SuccinctGraph.hpp"
#include "utils.h"
#include "ports.h"

#include <random>
#include <unordered_map>
#include <unordered_set>

#include <boost/functional/hash.hpp>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using boost::shared_ptr;

class GraphQueryServiceHandler : virtual public GraphQueryServiceIf {
private:

    typedef std::unordered_set<
                std::pair<int64_t, int64_t>,
                boost::hash< std::pair<int, int> >> AssocSet;

public:

    GraphQueryServiceHandler(
        const std::string& node_file,
        const std::string& edge_file,
        bool construct,
        int32_t sa_sampling_rate,
        int32_t isa_sampling_rate,
        int32_t npa_sampling_rate,
        int shard_id,
        int total_num_shards,
        const StoreMode store_mode = StoreMode::SuccinctStore)
    : shard_id_(shard_id),
      total_num_shards_(total_num_shards),
      node_file_(node_file),
      edge_file_(edge_file),
      construct_(construct),
      graph_(new SuccinctGraph("")), // just no-op object alloc
      initialized_(false),
      store_mode_(store_mode)
    {
        graph_->set_npa_sampling_rate(npa_sampling_rate);
        graph_->set_sa_sampling_rate(sa_sampling_rate);
        graph_->set_isa_sampling_rate(isa_sampling_rate);

        if (construct) {
            node_table_empty_ = !file_or_dir_exists(node_file);
            edge_table_empty_ = !file_or_dir_exists(edge_file);
        } else {
            node_table_empty_ = !file_or_dir_exists(node_file + ".succinct");
            edge_table_empty_ = !file_or_dir_exists(edge_file + ".succinct");
        }

        LOG_E(
            "shard id %d, total num shards %d; isa %d, sa %d, npa %d"
            " (specified, please check actual)\n",
            shard_id_, total_num_shards_, isa_sampling_rate,
            sa_sampling_rate, npa_sampling_rate);
    }

    void bulk_add_suffix_store(
        GraphSuffixStore& store,
        size_t num_edges_to_add,
        size_t num_nodes,
        size_t num_atypes,
        AssocSet& set,
        const std::string& attr_file,
        int bytes_per_attr,
        int64_t min_time,
        int64_t max_time)
    {
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<int64_t> uni_node(0, num_nodes - 1);
        std::uniform_int_distribution<int> uni_atype(0, num_atypes - 1);
        std::uniform_int_distribution<int64_t> uni_time(min_time, max_time);

        int64_t src, atype;
        SuccinctGraph::Assoc assoc;
        std::ifstream attr_in(attr_file);

        char f[26];
        sprintf(f, "suffix_store_shard%02d.edge", shard_id_);
        std::stringstream ss(f);

        for (size_t i = 0; i < num_edges_to_add; ++i) {
            src = uni_node(rng);
            atype = uni_atype(rng);
            auto pair = std::make_pair(src, atype);

            // NOTE: only add edges to existing assoc lists
            while (set.count(pair) == 0) {
                src = uni_node(rng);
                atype = uni_atype(rng);
                pair = std::make_pair(src, atype);
            }

            GraphFormatter::make_rand_assoc(
                assoc, src, atype,
                attr_file, attr_in, bytes_per_attr,
                uni_time, uni_node, rng);

            ss << assoc.src_id
                << ' ' << assoc.dst_id
                << ' ' << assoc.atype
                << ' ' << assoc.time
                << ' ' << assoc.attr << std::endl;
        }
        ss.flush();

        // Construct store
        store.init("EMPTY_NODE", std::string(f));
    }

    // Loads or constructs graph shards.
    int32_t init() {
        if (initialized_) {
            LOG_E("Already initialized\n");
            return 0;
        }
        LOG_E("In shard %d's init()\n", shard_id_);

        switch (store_mode_) {
        case StoreMode::SuccinctStore:
            if (construct_) {
                LOG_E("Construct is set to true:"
                    " starting to construct & encode\n");
                if (!node_table_empty_ && !edge_table_empty_) {
                    graph_->construct(node_file_, edge_file_); // in parallel
                } else if (!node_table_empty_) {
                    graph_->construct_node_table(node_file_);
                } else if (!edge_table_empty_) {
                    graph_->construct_edge_table(edge_file_);
                } else {
                    assert(false && "Neither node file nor edge file exists!");
                }
            } else {
                LOG_E("Construct is set to false: starting to load\n");
                if (!node_table_empty_ && !edge_table_empty_) {
                    graph_->load(node_file_, edge_file_);
                } else if (!node_table_empty_) {
                    graph_->load_node_table(node_file_);
                } else if (!edge_table_empty_) {
                    graph_->load_edge_table(edge_file_);
                } else {
                    assert(false && "Neither node file nor edge file exists!");
                }
            }
            break;

        case StoreMode::SuffixStore:
            // TODO: load vs. construct
            graph_suffix_store_ = shared_ptr<GraphSuffixStore>(
                new GraphSuffixStore(node_file_, edge_file_));
            graph_suffix_store_->init();
            break;

        case StoreMode::LogStore:
            // TODO: load vs. construct
            graph_log_store_ = shared_ptr<GraphLogStore>(new GraphLogStore(
                node_file_, edge_file_));
            graph_log_store_->init();
            break;

        default:
            break;
        }
        initialized_ = true;
        LOG_E("Initialization at this shard: done\n");
        return 0;
    }

    // In principle, nodeId should be in this shard's edge table.
    void get_neighbors(std::vector<int64_t> & _return, const int64_t nodeId) {
        // Your implementation goes here
        COND_LOG_E("Received: get_neighbors(%lld)\n", nodeId);

        assert(nodeId % total_num_shards_ == shard_id_);
        if (edge_table_empty_) {
            _return.clear();
            return;
        }
#ifdef DEBUG_RPC_NHBR
        auto t1 = get_timestamp();
#endif
        graph_->get_neighbors(_return, nodeId);

#ifdef DEBUG_RPC_NHBR
        auto t2 = get_timestamp();
        if (shard_id_ == 0) {
            LOG_E(",%lld\n", t2 - t1);
        }
#endif
    }

    void get_neighbors_atype(
        std::vector<int64_t> & _return,
        const int64_t nodeId,
        const int64_t atype)
    {
        COND_LOG_E("get_neighbors_atype\n");

        assert(nodeId % total_num_shards_ == shard_id_);
        if (edge_table_empty_) {
            _return.clear();
            return;
        }
#ifdef DEBUG_RPC_NHBR
        auto t1 = get_timestamp();
#endif
        graph_->get_neighbors(_return, nodeId, atype);

#ifdef DEBUG_RPC_NHBR
        auto t2 = get_timestamp();
        if (shard_id_ == 0) {
            LOG_E(",%lld\n", t2 - t1);
        }
#endif
    }

    void get_edge_attrs(
        std::vector<std::string> & _return,
        const int64_t nodeId,
        const int64_t atype)
    {
        COND_LOG_E("get_edge_attrs\n");
        assert(nodeId % total_num_shards_ == shard_id_);
        if (edge_table_empty_) {
            _return.clear();
            return;
        }
        graph_->get_edge_attrs(_return, nodeId, atype);
    }

    void get_nodes(
        std::set<int64_t> & _return,
        const int32_t attrId,
        const std::string& attrKey)
    {
        COND_LOG_E("get_nodes\n");

        _return.clear();
        if (node_table_empty_) {
            return;
        }

        std::set<int64_t> local_keys;
        graph_->get_nodes(local_keys, attrId, attrKey);

        // TODO: this assumes a particular form of hash partitioning
        auto it = _return.begin();
        for (int64_t local_key : local_keys) {
            it = _return.insert(it, local_key * total_num_shards_ + shard_id_);
        }
    }

    void get_nodes2(
        std::set<int64_t> & _return,
        const int32_t attrId1,
        const std::string& attrKey1,
        const int32_t attrId2,
        const std::string& attrKey2)
    {
        COND_LOG_E("get_nodes2\n");

        _return.clear();
        if (node_table_empty_) {
            return;
        }

        std::set<int64_t> local_keys;
        graph_->get_nodes(local_keys, attrId1, attrKey1, attrId2, attrKey2);

        // TODO: this assumes a particular form of hash partitioning
        auto it = _return.begin();
        for (int64_t local_key : local_keys) {
            it = _return.insert(it, local_key * total_num_shards_ + shard_id_);
        }
    }

    void get_attribute_local(
        std::string& _return,
        const int64_t nodeId,
        const int32_t attrId)
    {
        graph_->get_attribute(_return, nodeId, attrId);
    }

    void filter_nodes(
        std::vector<int64_t> & _return,
        const std::vector<int64_t> & nodeIds,
        const int32_t attrId,
        const std::string& attrKey)
    {
        COND_LOG_E("filter_nodes received\n");
        graph_->filter_nodes(_return, nodeIds, attrId, attrKey);
    }

    void assoc_range(
        std::vector<ThriftAssoc>& _return,
        int64_t src,
        int64_t atype,
        int32_t off,
        int32_t len)
    {
        std::vector<SuccinctGraph::Assoc> vec(
            std::move(graph_->assoc_range(src, atype, off, len)));

        // TODO: any better way?
        // NB: the fields are Thrift-generated, so this may not be portable.
        _return.clear();
        _return.resize(vec.size());
        size_t i = 0;
        for (const auto& assoc : vec) {
            _return[i].srcId = assoc.src_id;
            _return[i].dstId = assoc.dst_id;
            _return[i].atype = assoc.atype;
            _return[i].timestamp = assoc.time;
            // should be the same as 'lhs = std::move(rhs)'
            _return[i].attr.assign(std::move(assoc.attr));
            ++i;
        }
    }

    int64_t assoc_count(int64_t src, int64_t atype) {
        return graph_->assoc_count(src, atype);
    }

    void assoc_get(
        std::vector<ThriftAssoc>& _return,
        const int64_t src,
        const int64_t atype,
        const std::set<int64_t>& dstIdSet,
        const int64_t tLow,
        const int64_t tHigh)
    {
        COND_LOG_E("in shard assoc_get, about to call graph\n");
        std::vector<SuccinctGraph::Assoc> vec(
            std::move(graph_->assoc_get(src, atype, dstIdSet, tLow, tHigh)));
        COND_LOG_E("done: in shard assoc_get, about to call graph\n");

        // TODO: any better way?
        // NB: the fields are Thrift-generated, so this may not be portable.
        _return.clear();
        _return.resize(vec.size());
        size_t i = 0;
        for (const auto& assoc : vec) {
            _return[i].srcId = assoc.src_id;
            _return[i].dstId = assoc.dst_id;
            _return[i].atype = assoc.atype;
            _return[i].timestamp = assoc.time;
            // should be the same as 'lhs = std::move(rhs)'
            _return[i].attr.assign(std::move(assoc.attr));
            ++i;
        }
    }

    void obj_get(std::vector<std::string>& _return, const int64_t local_id) {
        graph_->obj_get(_return, local_id);
    }

    void assoc_time_range(
        std::vector<ThriftAssoc>& _return,
        const int64_t src,
        const int64_t atype,
        const int64_t tLow,
        const int64_t tHigh,
        const int32_t limit)
    {
        std::vector<SuccinctGraph::Assoc> vec(std::move(
            graph_->assoc_time_range(src, atype, tLow, tHigh, limit)));

        // TODO: any better way?
        // NB: the fields are Thrift-generated, so this may not be portable.
        _return.clear();
        _return.resize(vec.size());
        size_t i = 0;
        for (const auto& assoc : vec) {
            _return[i].srcId = assoc.src_id;
            _return[i].dstId = assoc.dst_id;
            _return[i].atype = assoc.atype;
            _return[i].timestamp = assoc.time;
            // should be the same as 'lhs = std::move(rhs)'
            _return[i].attr.assign(std::move(assoc.attr));
            ++i;
        }
    }

private:

    // By default, StoreMode::SuccinctStore
    const StoreMode store_mode_;

    const int shard_id_;
    const int total_num_shards_;

    const std::string node_file_;
    const std::string edge_file_;
    const bool construct_;

    const shared_ptr<SuccinctGraph> graph_;
    shared_ptr<GraphLogStore> graph_log_store_ = nullptr;
    shared_ptr<GraphSuffixStore> graph_suffix_store_ = nullptr;

    bool initialized_;

    bool node_table_empty_ = true;
    bool edge_table_empty_ = true;

    // Updates

    // src -> (atype -> [shard id, file offset])
    typedef std::pair<int, int64_t> EdgeUpdatePtr;

    std::unordered_map<int64_t,
        std::unordered_map<int64_t, std::vector<EdgeUpdatePtr>>
    > edge_update_ptrs;

    // src -> (shard id, file offset)
    typedef std::pair<int, int64_t> NodeUpdatePtr;
    std::unordered_map<int64_t, NodeUpdatePtr> node_update_ptrs;
};

int main(int argc, char **argv) {
    if (argc < 2 || argc > 16)
        return -1;
    LOG_E("Command line: ");
    for (int i = 0; i < argc; i++) LOG_E("%s ", argv[i]);
    LOG_E("\n");

    int c;
    int mode = 0, port = QUERY_SERVER_PORT;
    // default Succinct Graph level 0
    int sa_sampling_rate = 32, isa_sampling_rate = 64, npa_sampling_rate = 128;
    int shard_id = 0, total_num_shards = 1;
    int local_host_id = 0, total_num_hosts = 1;
    StoreMode store_mode = StoreMode::SuccinctStore; // by default, Succinct

    while ((c = getopt(argc, argv, "m:p:s:i:n:t:d:h:k:")) != -1) {
        switch (c) {
        case 'm':
            mode = atoi(optarg); // 0 for construct, 1 for load
            break;
        case 'p':
            port = atoi(optarg); // port for this shard server
            break;
        case 's':
            sa_sampling_rate = atoi(optarg);
            break;
        case 'i':
            isa_sampling_rate = atoi(optarg);
            break;
        case 'n':
            npa_sampling_rate = atoi(optarg);
            break;
        case 't':
            total_num_shards = atoi(optarg);
            break;
        case 'd':
            shard_id = atoi(optarg);
            break;
        case 'h':
            local_host_id = atoi(optarg);
            break;
        case 'k':
            total_num_hosts = atoi(optarg);
            break;
        }
    }

    // Hack for multistore setup:
    if (total_num_hosts >= 3) {
        if (local_host_id == total_num_hosts - 1) {
            store_mode = StoreMode::LogStore;
        } else if (local_host_id == total_num_hosts - 2) {
            store_mode = StoreMode::SuffixStore;
        }
    }

    if (optind + 1 >= argc) return -1;
    std::string node_file = std::string(argv[optind]);
    std::string edge_file = std::string(argv[optind + 1]);
    bool construct = (mode == 0) ? true : false;

    try {
        shared_ptr<GraphQueryServiceHandler> handler(
            new GraphQueryServiceHandler(
                node_file,
                edge_file,
                construct,
                sa_sampling_rate,
                isa_sampling_rate,
                npa_sampling_rate,
                shard_id,
                total_num_shards,
                store_mode));

        shared_ptr<TProcessor> processor(
            new GraphQueryServiceProcessor(handler));
        shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
        shared_ptr<TTransportFactory> transportFactory(
            new TBufferedTransportFactory());
        shared_ptr<TProtocolFactory> protocolFactory(
            new TBinaryProtocolFactory());
        // TODO: simple server vs. threaded server?
        TThreadedServer server(
            processor, serverTransport, transportFactory, protocolFactory);

        server.serve();
    } catch (std::exception& e) {
        fprintf(stderr,
                "Exception at GraphQueryServiceServer:main(): %s\n",
                e.what());
    }
    return 0;
}
