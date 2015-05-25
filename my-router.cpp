#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <stdint.h>

using namespace std;
using namespace boost::asio::ip;

struct Interface {
    Interface(uint16_t port, int cost)
    : port(port), cost(cost) {}
    
    uint16_t port;
    int cost;
};

struct RTEntry {
    RTEntry(int cost, uint16_t outgoing_port, uint16_t dest_port)
    : cost(cost), outgoing_port(outgoing_port), dest_port(dest_port) {}
    
    int cost;
    uint16_t outgoing_port;
    uint16_t dest_port;
};

typedef map<string,int> DV;

struct DVMsg {
    DVMsg(string src_id, map<string,int>  dv)
    : src_id(src_id), dv(dv) {}
    
    string toString()
    {
        // encode object to string
        string message = "";
        message += src_id;
        message += ":";
        for(auto it = dv.begin(); it != dv.end(); ++it){
            message += it->first;
            message += ",";
            message += to_string(it->second);
            message += ";";
        }
        message += " ";
        return message;
    }

    static DVMsg fromString(string str)
    {
        // decode string to object
        int i = str.find(":");
        string id = str.substr(0,i);
        str = str.substr(i+1);
        map<string, int> m;
        while(str.find(";")!=string::npos){
            i = str.find(",");
            string d_id = str.substr(0,i);
            str = str.substr(i+1);
            i = str.find(";");
            int c = stoi(str.substr(0,i),nullptr,10);
            m[d_id] = c;
            str = str.substr(i+1);
        }
        return DVMsg(id,m);

    }
    
    string src_id;
    DV dv;

};

class MyRouter
{
    const static int MAX_LENGTH = 1024;
public:
    MyRouter(boost::asio::io_service& io_service, string id,
             uint16_t local_port, map<string, Interface> neighbors)
    : sock(io_service, udp::endpoint(udp::v4(), local_port)), io_service(io_service),
    id(id), local_port(local_port), neighbors(neighbors)
    {
        start_receive();
        // TODO: periodically advertise its distance vector to each of its neighbors every 5 seconds.
        for(;;){
            broadcast(dvmsg().toString());
            sleep(5000);    
        }
    }
    
    void broadcast(string message)
    {
        for (auto& i : neighbors)
        {
            Interface interface = i.second;
            udp::endpoint sendee_endpoint(udp::v4(), interface.port);
            cout << "Send to " << sendee_endpoint << endl;
            send(message, sendee_endpoint);
        }
    }
    
    void send(string message, udp::endpoint sendee_endpoint)
    {
        sock.async_send_to(boost::asio::buffer(message), sendee_endpoint,
                           boost::bind(&MyRouter::handle_send, this, message,
                                       boost::asio::placeholders::error,
                                       boost::asio::placeholders::bytes_transferred));
    }
    
private:
    DVMsg dvmsg()
    {
        return DVMsg(id, dv);
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
            cout << "async_receive_from endpoint=" << remote_endpoint << endl;
            cout << "async_receive_from message='" << recv_str << "'" << endl;
            cout << "async_receive_from return " << error << ": " << bytes_recvd << " received" << endl;
            
            // TODO: recaculate routing tables
            bool has_change = false;
            DVMsg dvm = DVMsg::fromString(recv_str);
            int distance = dv[dvm.src_id];
            map<string, int> m = dvm.dv;

            for (auto it=m.begin(); it!=m.end(); ++it)
            {
                string dest_id = it->first;
                int cost = it->second;
                
                if (dv.count(dest_id) > 0)
                {
                    if (cost + distance < dv[dest_id])
                    {
                        dv[dest_id] = cost + distance;
                        RouteTable[dest_id].cost = dv[dest_id];
                        RouteTable[dest_id].dest_port = neighbors[dvm.src_id].port;
                        has_change = true;
                    }
                }
                else
                {
                    dv[dest_id] = cost + distance;
                    RouteTable[dest_id] = RTEntry(cost, local_port, neighbors[dvm.src_id].port);
                    has_change = true;
                }
            }

            // TODO: if any change, broadcast to neighbors (using broadcast())

            if(has_change){
                broadcast(dvmsg().toString());
            }
            
            // below code only for debug (no meaning)
            
            string message = "hello";
            sock.async_send_to(boost::asio::buffer(message), remote_endpoint,
                               boost::bind(&MyRouter::handle_send, this, message,
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred));
            
            // continue listening
            start_receive();
        }
    }
    
    void handle_send(string message, const boost::system::error_code& error,
                     std::size_t bytes_transferred)
    {
        cout << "async_send_to message='" << message << "'" << endl;
        cout << "async_send_to return " << error << ": " << bytes_transferred << " transmitted" << endl;
    }
    
    udp::socket sock;
    boost::asio::io_service& io_service;
    string id;
    uint16_t local_port;
    map<string, Interface> neighbors; // id => Interface
    udp::endpoint remote_endpoint;
    boost::array<char,MAX_LENGTH> recv_buffer;
    map<string, RTEntry> RouteTable; // id => RTEntry
    DV dv;
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



