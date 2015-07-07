// One per logical shard (there can be multiple shards per physical node).
service GraphQueryService {

    list<i64> get_neighbors(1: i64 nodeId),

    list<i64> get_neighbors_atype(1: i64 nodeId, 2: i64 atype),

    list<i64> get_neighbors_attr(1: i64 nodeId, 2: i32 attrId, 3: string attrKey),

    list<i64> get_nodes(1: i32 attrId, 2: string attrKey),

    list<i64> get_nodes2(
        1: i32 attrId1,
        2: string attrKey1,
        3: i32 attrId2,
        4: string attrKey2),

}

// One per physical node; handles local aggregation.
service GraphQueryAggregatorService {

    i32 connect_to_local_servers(),

    list<i64> get_neighbors_local(1: i64 nodeId),

    list<i64> get_neighbors_atype_local(1: i64 nodeId, 2: i64 atype),

    list<i64> get_neighbors_attr_local(
        1: i64 nodeId, 2: i32 attrId, 3: string attrKey),

    list<i64> get_nodes_local(1: i32 attrId, 2: string attrKey),

    list<i64> get_nodes2_local(
        1: i32 attrId1,
        2: string attrKey1,
        3: i32 attrId2,
        4: string attrKey2),

}
