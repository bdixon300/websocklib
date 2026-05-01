My own little hobby project to create a low latency web socket client library in C++. WIP.


Plan:

0. Implement TCP client -- DONE
1. Implement http handshake (upgrade to a websocket connection) -- DONE
2. Implement webframe handling
3. Improve performance / latency (high mutex contention between receiving network packets and processing currently)