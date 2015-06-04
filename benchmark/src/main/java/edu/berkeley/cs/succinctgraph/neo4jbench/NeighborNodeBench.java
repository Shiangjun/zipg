package edu.berkeley.cs.succinctgraph.neo4jbench;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.lang.System;
import java.util.*;
import java.util.concurrent.TimeUnit;

import org.neo4j.graphdb.DynamicLabel;
import org.neo4j.graphdb.GraphDatabaseService;
import org.neo4j.graphdb.Label;
import org.neo4j.graphdb.Node;
import org.neo4j.graphdb.Direction;
import org.neo4j.graphdb.Relationship;
import org.neo4j.graphdb.ResourceIterator;
import org.neo4j.graphdb.Transaction;
import org.neo4j.graphdb.factory.GraphDatabaseFactory;
import org.neo4j.graphdb.schema.IndexDefinition;
import org.neo4j.graphdb.schema.Schema;

import static edu.berkeley.cs.succinctgraph.neo4jbench.BenchUtils.*;

public class NeighborNodeBench {
    private static int WARMUP_N = 20000;
    private static int MEASURE_N = 100000;
    private static int COOLDOWN_N = 500;

    private static Label NODE_LABEL = DynamicLabel.label("Node");

    public static void main(String[] args) throws Exception {
        String type = args[0];
        String db_path = args[1];
        String warmup_file = args[2];
        String query_file = args[3];
        String output_file = args[4];
        WARMUP_N = Integer.parseInt(args[5]);
        MEASURE_N = Integer.parseInt(args[6]);

        PrintWriter out = new PrintWriter(new BufferedWriter(new FileWriter(output_file)));

        List<Integer> warmup_neighbor_indices = new ArrayList<Integer>();
        List<Integer> warmup_node_attributes = new ArrayList<Integer>();
        List<String> warmup_node_queries = new ArrayList<String>();
        getNeighborNodeQueries(warmup_file, warmup_neighbor_indices, warmup_node_attributes, warmup_node_queries);

        List<Integer> neighbor_indices = new ArrayList<Integer>();
        List<Integer> node_attributes = new ArrayList<Integer>();
        List<String> node_queries = new ArrayList<String>();
        getNeighborNodeQueries(query_file, neighbor_indices, node_attributes, node_queries);

        if (type.equals("latency")) {
            neighborNodeLatency(db_path, out, warmup_neighbor_indices, neighbor_indices,
                                       warmup_node_attributes, warmup_node_queries, node_attributes, node_queries, false);
        } else if (type.equals("latency-index")) {

            neighborNodeLatency(db_path, out, warmup_neighbor_indices, neighbor_indices,
                                       warmup_node_attributes, warmup_node_queries, node_attributes, node_queries, true);
        } else {
            System.out.println("No type " + type + " is supported!");
        }
    }

    private static void neighborNodeLatency(String DB_PATH, PrintWriter out,
            List<Integer> warmup_neighbor_indices, List<Integer> neighbor_indices,
            List<Integer> warmup_node_attributes, List<String> warmup_node_queries,
            List<Integer> node_attributes, List<String> node_queries,
            boolean useIndex) {

        System.out.println("Benchmarking getNeighborNode queries");
        // START SNIPPET: startDb
        GraphDatabaseService graphDb = new GraphDatabaseFactory().newEmbeddedDatabase(DB_PATH);
        BenchUtils.registerShutdownHook(graphDb);
        Transaction tx = graphDb.beginTx();
        try {
            BenchUtils.awaitIndexes(graphDb);

            // warmup
            System.out.println("Warming up for " + WARMUP_N + " queries");
            for (int i = 0; i < WARMUP_N; i++) {
                List<Long> result;
                if (!useIndex) {
                    result = getNeighborNode(graphDb, modGet(warmup_neighbor_indices, i),
                                 modGet(warmup_node_attributes, i), modGet(warmup_node_queries, i));
                } else {
                    result = getNeighborNodeUsingIndex(graphDb, modGet(warmup_neighbor_indices, i),
                                 modGet(warmup_node_attributes, i), modGet(warmup_node_queries, i));
                }
                if (result.size() == 0) {
                    System.out.printf("Error: no neighbor nodes for node id: %d, attr %d, search %s\n",
                        modGet(warmup_neighbor_indices, i), modGet(warmup_node_attributes, i), modGet(warmup_node_queries, i));
                    System.exit(0);
                }
            }

            // measure
            System.out.println("Measuring for " + MEASURE_N + " queries");
            double totalSeconds = 0;
            for (int i = 0; i < MEASURE_N; i++) {
                if (i % 10000 == 0) {
                    tx.success();
                    tx.close();
                    tx = graphDb.beginTx();
                }
                int idx = modGet(neighbor_indices, i);
                long queryStart, queryEnd;
                List<Long> result;
                if (!useIndex) {
                    queryStart = System.nanoTime();
                    result = getNeighborNode(graphDb, modGet(warmup_neighbor_indices, i),
                                                    modGet(warmup_node_attributes, i), modGet(warmup_node_queries, i));
                    queryEnd = System.nanoTime();
                } else {
                    queryStart = System.nanoTime();
                    result = getNeighborNodeUsingIndex(graphDb, modGet(warmup_neighbor_indices, i),
                                                              modGet(warmup_node_attributes, i), modGet(warmup_node_queries, i));
                    queryEnd = System.nanoTime();
                }
                if (result.size() == 0) {
                    System.out.printf("Error: no neighbor nodes for node id: %d, attr %d, search %s\n",
                        modGet(neighbor_indices, i), modGet(node_attributes, i), modGet(node_queries, i));
                } else {
                    out.println(result.size() + "," + (queryEnd - queryStart) / 1000);
                }
            }
            out.close();
            tx.success();
        } finally {
            tx.close();
        }
        System.out.println("Shutting down database ...");
        graphDb.shutdown();
    }

    private static List<Long> getNeighborNode(GraphDatabaseService graphDb,
            long node_id, int attr, String search) {
        Node n = graphDb.getNodeById(node_id);
        List<Long> result = new LinkedList<>();
        for (Relationship r : n.getRelationships(Direction.OUTGOING)) {
            Node neighbor = r.getOtherNode(n);
            if (search.equals(neighbor.getProperty("name" + attr))) {
                result.add(neighbor.getId());
            }
        }
        return result;
    }

    private static List<Long> getNeighborNodeUsingIndex(
        GraphDatabaseService graphDb, long node_id, int attr, String search) {

        // populate neighbors
        Set<Node> neighbors = new HashSet<Node>();
        Node n = graphDb.getNodeById(node_id);
        for (Relationship rel : n.getRelationships(Direction.OUTGOING))
            neighbors.add(rel.getOtherNode(n));

        List<Long> result = new LinkedList<>();
        try (ResourceIterator<Node> nodes = graphDb.findNodes(
                 NODE_LABEL, "name" + attr, search)) {
            while (nodes.hasNext()) {
                Node validNode = nodes.next();
                if (neighbors.contains(validNode))
                    result.add(validNode.getId());
            }
        }
        return result;
    }

    private static void getNeighborNodeQueries(String file, List<Integer> indices, List<Integer> attributes, List<String> queries) {
        try {
            BufferedReader br = new BufferedReader(new FileReader(file));
            String line = br.readLine();
            while (line != null) {
                String[] tokens = line.split(",");
                indices.add(Integer.parseInt(tokens[0]));
                attributes.add(Integer.parseInt(tokens[1]));
                queries.add(tokens[2]);
                line = br.readLine();
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}