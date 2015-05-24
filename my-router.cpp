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
    Interface(string id, int port, int link_cost)
    : id(id), port(port), link_cost(link_cost) {}
    string id;
    uint16_t port;
    int link_cost;
};

typedef vector<pair<dest_id, cost> > DV;

struct RTRecord {
    string dest_id;
    int cost;
    uint16_t outgoing_port;
    uint16_t dest_port;
};

class MyRouter
{
    const static int MAX_LENGTH = 1024;
public:
    MyRouter(boost::asio::io_service& io_service, string id,
             uint16_t local_port, vector<Interface> neighbors)
    : sock(io_service, udp::endpoint(udp::v4(), local_port)),
    io_service(io_service), id(id), neighbors(neighbors)
    {
        start_receive();
        // TODO: periodically advertise its distance vector to each of its neighbors every 5 seconds.
    }
    
    void broadcast(string message)
    {
        for (auto& interface : neighbors)
        {
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
            
            // TODO: if any change, broadcast to neighbors (using broadcast())
            
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
    vector<Interface> neighbors;
    udp::endpoint remote_endpoint;
    boost::array<char,MAX_LENGTH> recv_buffer;
    map<string, RTRecord> RoutingTable; // id => RTRecord
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
    vector<Interface> interfaces;
    
    ifstream initfile("init.txt");
    string line;
    while (getline(initfile, line))
    {
        vector<string> tokens;
        boost::split(tokens, line, boost::is_any_of(","));
        string src_router = tokens[0];
        string dest_router = tokens[1];
        uint16_t port = stoi(tokens[2]);
        int link_cost = stoi(tokens[3]);
        
        if (id.compare(src_router) == 0) {
            interfaces.push_back(Interface(dest_router, port, link_cost));
        }
        
        if (local_port == 0 && id.compare(dest_router) == 0) {
            local_port = port;
        }
    }
    
    if (local_port == 0) {
        cerr << "No port number for router " << id << endl;
        return 0;
    }
    
    boost::asio::io_service io_service;
    MyRouter rt(io_service, id, local_port, interfaces);
    io_service.run();
    
    return 0;
}



