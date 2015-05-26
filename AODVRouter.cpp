#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <stdint.h>

using namespace std;
using namespace boost::asio::ip;

struct Interface {
    Interface() {}
    Interface(uint16_t port, int cost)
    : port(port), cost(cost) {}
    
    uint16_t port;
    int cost;
};

/* ----- TinyAOVDRouter ---------- */

struct RREQ {
    RREQ(src_id, dest_id)
    : src_id(src_id), dest_id(dest_id), hop_count(hop_count) {}
    
    string toString()
    {
        return "rreq:" + src_id + "," + dest_id + "," +
                to_string(hop_count);
    }
    
    static RREQ fromString(string rreq_str)
    {
        size_t found = rreq_str.find(":");
        if (found != std::string::npos &&
            rreq_str.substr(0, found).compare("rreq") == 0)
        {
            size_t next_found = rreq_str.find(",", found + 1);
            string src_id = rreq_str.substr(found + 1, next_found - found);
            
            found = next_found;
            next_found = rreq_str.find(",", found + 1);
            string dest_id = rreq_str.substr(found + 1, next_found - found);
            
            found = next_found;
            next_found = rreq_str.find(",", found + 1);
            int hop_count = stoi(rreq_str.substr(found + 1, next_found - found));
            
            return RREQ(src_id, dest_id, hop_count);
        }
        else
        {
            return emptyRREQ();
        }
    }
    
    RREQ emptyRREQ()
    {
        return RREQ("", "", 0);
    }
    
    bool isEmpty()
    {
        return src_id.length() == 0 &&
                dest_id.length() == 0 &&
                hop_count == 0
    }
    
    string src_id;
    string dest_id;
    int hop_count;
};

struct RREP {
    RREP(src_id, dest_id)
    : src_id(src_id), dest_id(dest_id), hop_count(hop_count) {}
    
    string toString()
    {
        return "rreq:" + src_id + "," + dest_id + "," +
                to_string(hop_count);
    }
    
    static RREP fromString(string rrep_str)
    {
        size_t found = rreq_str.find(":");
        if (found != std::string::npos &&
            rrep_str.substr(0, found).compare("rrep") == 0)
        {
            size_t next_found = rrep_str.find(",", found + 1);
            string src_id = rrep_str.substr(found + 1, next_found - found);
            
            found = next_found;
            next_found = rrep_str.find(",", found + 1);
            string dest_id = rrep_str.substr(found + 1, next_found - found);
            
            found = next_found;
            next_found = rrep_str.find(",", found + 1);
            int hop_count = stoi(rrep_str.substr(found + 1, next_found - found));
            
            return RREP(src_id, dest_id, hop_count);
        }
        else
        {
            return emptyRREP();
        }
    }
    
    RREP emptyRREP()
    {
        return RREP("", "", 0);
    }
    
    bool isEmpty()
    {
        return src_id.length() == 0 &&
        dest_id.length() == 0 &&
        hop_count == 0
    }
    
    string src_id;
    string dest_id;
    int hop_count;
};

struct RERR {
    // TODO: ...
};

// Reverse Route Entry
// src_id => RREntry
struct RREntry {
    uint16_t next_hop; // port
    int hop_count;
};

// Forward Route Entry
// dest_id => FREntry
struct FREntry {
    uint16_t next_hop; // port
    int hop_count;
};

/* --------------------*/

class TinyAOVDRouter
{
    const static int MAX_LENGTH = 1024;
public:
    MyRouter(boost::asio::io_service& io_service, string id,
             uint16_t local_port, map<string, Interface> neighbors)
    : sock(io_service, udp::endpoint(udp::v4(), local_port)), io_service(io_service),
    id(id), local_port(local_port), neighbors(neighbors), timer(io_service)
    {
        // initialize its own distance vector and routing table (only know neighbors' info)
        for (auto& i : neighbors)
        {
            string dest_id = i.first;
            Interface interface = i.second;
            FRTable[dest_id] = FREntry(interface.port, 1);
        }
        
        // receive from neighbors
        start_receive();
    }
    
    // send data message from upper layer
    void send_data(string message, string dest_id)
    {
        if (RTTable.count(dest_id) > 0) // found
        {
            send("data:" + message, udp::endpoint(udp::v4(), RTTable[id].next_hop));
        }
        else // not found -> route discovery
        {
            // store the data in a queue
            data_queue[dest_id].push(message);
            
            // route discovery phase
            route_discovery(dest_id);
        }
    }
    
private:
    
    void route_discovery(string dest_id)
    {
        // construct RREQ
        RREQ rreq(id, dest_id, 0);
        
        // broadcast RREQ
        broadcast(rreq.toString());
    }
    
    void broadcast(string message)
    {
        for (auto& i : neighbors)
        {
            Interface interface = i.second;
            cout << id << " Broadcast to " << i.first << ": " << message << endl;
            udp::endpoint sendee_endpoint(udp::v4(), interface.port);
            send(message, sendee_endpoint);
        }
        cout << endl;
    }
    
    void send(string message, udp::endpoint sendee_endpoint)
    {
        sock.async_send_to(boost::asio::buffer(message), sendee_endpoint,
                           boost::bind(&MyRouter::handle_send, this,
                                       boost::asio::placeholders::error,
                                       boost::asio::placeholders::bytes_transferred));
    }
    
    void start_receive()
    {
        sock.async_receive_from(boost::asio::buffer(recv_buffer), remote_endpoint,
                                boost::bind(&MyRouter::handle_receive, this,
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred));
    }
    
    void handle_receive(const boost::system::error_code& error, size_t bytes_recvd)
    {
        if (!error || error == boost::asio::error::message_size)
        {
            string recv_str(recv_buffer.begin(), recv_buffer.begin() + bytes_recvd);
            
            cout << id << " Received from " << dvm.src_id << ": " << recv_str << endl;
            cout << endl;
            
            RREQ rreq = RREQ::fromString(recv_str);
            RREP rrep = RREP::fromString(recv_str);
//            RERR rerr = RERR::fromString(recv_str); // TODO
            
            if (!rreq.isEmpty()) // is RREQ controll msg
            {
                // increment hop_count
                rreq.hop_count++;
                
                if (id.compare(rreq.dest_id) == 0) // I am the destination
                {
                    // contrust RREP and send (unicast) back to src
                    RREP new_rrep(rreq.src_id, rreq.dest_id, 0);
                    uint16_t port = RRTable[rreq.src_id].next_hop;
                    send(new_rrep.toString(), udp::endpoint(udp::v4(), port));
                }
                else
                {
                    // add / update reverse route table
                    if (RRTable.count(src_id) == 0 ||
                        RRTable[src_id].hop_count > rreq.hop_count)
                    {
                        RRTable[src_id] = RREntry(remote_endpoint.port() /* next_hop */,
                                                  rreq.hop_count);
                    }
                    else // broadcast
                    {
                        broadcast(rreq.toString());
                    }
                }
            }
            else (!rrep.isEmpty()) // is RREP controll msg
            {
                // increment hop_count
                rrep.hop_count++;
                
                if (id.compare(rrep.src_id) == 0) // I am the src
                {
                    // complete. able to send data msgs
                    send_queued_data(rrep.dest_id);
                }
                else
                {
                    // add / update forward route table
                    if (FRTable.count(dest_id) == 0 ||
                        FRTable[dest_id].hop_count > rrep.hop_count)
                    {
                        FRTable[dest_id] = FREntry(remote_endpoint.port() /* next_hop */,
                                                  rreq.hop_count);
                    }
                    else // unicast back using RRTable
                    {
                        uint16_t port = RRTable[rrep.src_id].next_hop;
                        send(rrep.toString(), udp::endpoint(udp::v4(), port));
                    }
                }
            }
//            else (!rerr.isEmpty()) // is RERR controll msg
//            {
//                // TODO
//            }
            else // is data message
            {
                
            }
            
            // continue listening
            start_receive();
        }
    }
    
    void send_queued_data(string dest_id)
    {
        // send all data in data_queue with dest_id
        for (auto& data : data_queue[dest_id])
        {
            send_data(data, dest_id);
        }
    }
    
    void handle_send(const boost::system::error_code& error,
                     std::size_t bytes_transferred)
    {
        
    }
    
    udp::socket sock;
    boost::asio::io_service& io_service;
    string id;
    uint16_t local_port;
    map<string, Interface> neighbors; // id => Interface
    udp::endpoint remote_endpoint;
    boost::array<char,MAX_LENGTH> recv_buffer;
    map<string, RREntry> RRTable; // src_id => RREntry
    map<string, FREntry> FRTable; // dest_id => FREntry
    map<string, queue<string> > data_queue; // dest_id => queue of msgs
};

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        cout << "Wrong arguments. Correct: ./my-router <id>" << endl;
        return 0;
    }
    
    string id = string(argv[1]);
    uint16_t local_port = 0;
    map<string, Interface> neighbors;
    
    ifstream initfile("init.txt");
    string line;
    while (getline(initfile, line))
    {
        vector<string> tokens;
        boost::split(tokens, line, boost::is_any_of(","));
        string src_router = tokens[0];
        string dest_router = tokens[1];
        uint16_t port = stoi(tokens[2]);
        int cost = stoi(tokens[3]);
        
        if (id.compare(src_router) == 0)
        {
            neighbors[dest_router] = Interface(port, cost);
        }
        
        if (local_port == 0 && id.compare(dest_router) == 0)
        {
            local_port = port;
        }
    }
    
    if (local_port == 0) {
        cerr << "No port number for router " << id << endl;
        return 0;
    }
    
    boost::asio::io_service io_service;
    MyRouter rt(io_service, id, local_port, neighbors);
    io_service.run();
    
    return 0;
}



