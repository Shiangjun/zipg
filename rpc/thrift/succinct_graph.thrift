// An association. The included fields are the same as SuccinctGraph::Assoc.
struct ThriftAssoc {
    1: i64 srcId,
    2: i64 dstId,
    3: i64 atype,
    4: i64 timestamp,
    5: string attr,
}

// One per logical shard (there can be multiple shards per physical node).
service GraphQueryService {

    // Loads or constructs graph shards.
    i32 init(),

    list<i64> get_neighbors(1: i64 nodeId),

    list<i64> get_neighbors_atype(1: i64 nodeId, 2: i64 atype),

    set<i64> get_nodes(1: i32 attrId, 2: string attrKey),

    set<i64> get_nodes2(
        1: i32 attrId1,
        2: string attrKey1,
        3: i32 attrId2,
        4: string attrKey2),

    // The `nodeId` is a local key, and is guaranteed to belong to this
    // current shard.
    string get_attribute_local(1: i64 nodeId, 2: i32 attrId),

    // Filter the nodeIds by checking whether they contain the specified
    // attribute.  Contracts:
    // (1) This shard will only check its own local node table.
    // (2) `nodeIds` contains "local keys".
    // (3) On return, the result contains "local keys" as well.
    list<i64> filter_nodes(
        1: list<i64> nodeIds,
        2: i32 attrId,
        3: string attrKey),

    list<ThriftAssoc> assoc_range(
        1: i64 src, 2: i64 atype, 3: i32 off, 4: i32 len),

    i64 assoc_count(1: i64 src, 2: i64 atype),

    list<ThriftAssoc> assoc_get(
        1: i64 src, 2: i64 atype, 3: set<i64> dstIdSet,
        4: i64 tLow, 5: i64 tHigh),

    list<string> obj_get(1: i64 nodeId),

    list<ThriftAssoc> assoc_time_range(
        1: i64 src, 2: i64 atype,
        3: i64 tLow, 4: i64 tHigh, 5: i32 limit),

}

// One aggregator per machine; handles local aggregation and query routing.
//
// For each user-facing API myAPI(), the myAPI() call will potentially route
// to remote aggregator(s), if appropriate, whereas the myAPI_local() version
// should be used to contact machine-local shards only.
// TODO: the RPC implementation does not handle wildcard queries for now.
service GraphQueryAggregatorService {

    // Entry point to prepare a cluster.  Performs the following in parallel:
    //   (1) Have this aggregator connect to all machine-local shards, and call
    //       init() on them.
    //   (2) Have this aggregator connect both-ways to all other aggregators in
    //       the cluster, so that everyone has a socket to everyone.
    //   (3) Have those connected aggregators connect to their own
    //       local shards as well.
    i32 init(),

    // Have this aggregator connect to machine-local shards (servers),
    // and call init() on them (i.e. loads or constructs).  End users
    // should just call init() once and not call this method.
    i32 init_local_shards(),

    // Have this aggregator connect to all other aggregators.
    i32 connect_to_aggregators(),

    // Have all aggregators shutdown (1) connections to their own shards, (2)
    // connections to other aggregators.
    void shutdown(),
    void disconnect_from_local_shards(),
    void disconnect_from_aggregators(),

    // Primitive queries

    string get_attribute_local(1: i64 nodeId, 2: i32 attrId),

    list<i64> get_neighbors(1: i64 nodeId),

    list<i64> get_neighbors_local(1: i32 shardId, 2: i64 nodeId),

    list<i64> get_neighbors_atype(1: i64 nodeId, 2: i64 atype),

    list<i64> get_neighbors_atype_local(
        1: i32 shardId, 2: i64 nodeId, 3: i64 atype),

    list<i64> get_neighbors_attr(
        1: i64 nodeId, 2: i32 attrId, 3: string attrKey),

    list<i64> get_neighbors_attr_local(
        1: i32 shardId, 2: i64 nodeId, 3: i32 attrId, 4: string attrKey),

    set<i64> get_nodes(1: i32 attrId, 2: string attrKey),

    set<i64> get_nodes_local(1: i32 attrId, 2: string attrKey),

    set<i64> get_nodes2(
        1: i32 attrId1,
        2: string attrKey1,
        3: i32 attrId2,
        4: string attrKey2),

    set<i64> get_nodes2_local(
        1: i32 attrId1,
        2: string attrKey1,
        3: i32 attrId2,
        4: string attrKey2),

    // The passed-in `nodeIds` are global keys that are guaranteed to only
    // belong to shards under this aggregator.  On return, the keys are global.
    list<i64> filter_nodes_local(
        1: list<i64> nodeIds,
        2: i32 attrId,
        3: string attrKey),

    // TAO queries

    list<ThriftAssoc> assoc_range(
        1: i64 src, 2: i64 atype, 3: i32 off, 4: i32 len),

    list<ThriftAssoc> assoc_range_local(
        1: i32 shardId, 2: i64 src, 3: i64 atype, 4: i32 off, 5: i32 len),

    i64 assoc_count(1: i64 src, 2: i64 atype),

    i64 assoc_count_local(1: i32 shardId, 2: i64 src, 3: i64 atype),

    list<ThriftAssoc> assoc_get(
        1: i64 src, 2: i64 atype, 3: set<i64> dstIdSet,
        4: i64 tLow, 5: i64 tHigh),

    list<ThriftAssoc> assoc_get_local(
        1: i32 shardId, 2: i64 src, 3: i64 atype,
        4: set<i64> dstIdSet, 5: i64 tLow, 6: i64 tHigh),

    list<string> obj_get(1: i64 nodeId),

    list<string> obj_get_local(1: i32 shardId, 2: i64 nodeId),

    list<ThriftAssoc> assoc_time_range(
        1: i64 src, 2: i64 atype,
        3: i64 tLow, 4: i64 tHigh, 5: i32 limit),

    list<ThriftAssoc> assoc_time_range_local(
        1: i32 shardId, 2: i64 src, 3: i64 atype,
        4: i64 tLow, 5: i64 tHigh, 6: i32 limit),

}