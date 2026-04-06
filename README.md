My own little hobby project to create a low latency web socket library in C++. WIP.


Plan:

1. Implement http handshake (upgrade to a websocket connection)
2. Implement webframe handling
3. Improve performance / latency (high mutex contention between receiving network packets and processing currently)