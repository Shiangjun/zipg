#!/usr/bin/env bash

# Stops all servers and the aggregator

sbin=`dirname "$0"`
sbin=`cd "$sbin"; pwd`

# Set process names
export QUERY_SERVER_PROCESS="sserver"
export AGGREGATOR_PROCESS="saggregator"

# Stop servers
echo "Stopping servers..."
"$sbin/stop-servers.sh"

# Stop aggregator
echo "Stopping aggregator..."
"$sbin/stop-aggregator.sh"
