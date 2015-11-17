#include "GraphQueryAggregatorService.h"
#include "ports.h"

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/server/TNonblockingServer.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/concurrency/PosixThreadFactory.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

// Dummy program to initialize the Aggregator and die;
// Does not take any command line arguments
int main() {
    boost::shared_ptr<TSocket> socket_(
        new TSocket("localhost", QUERY_HANDLER_PORT));
    boost::shared_ptr<TTransport> transport_(new TBufferedTransport(socket_));
    boost::shared_ptr<TProtocol> protocol_(new TBinaryProtocol(transport_));
    transport_->open();
    GraphQueryAggregatorServiceClient client_(protocol_);
    client_.local_data_init();
    transport_->close();
    return 0;
}
