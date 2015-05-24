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

struct interface {
    interface(string id, int port, int link_cost)
    : id(id), port(port), link_cost(link_cost) {}
    string id;
    uint16_t port;
    int link_cost;
};

class MyRouter
{
public:
    MyRouter(boost::asio::io_service& io_service, string id,
             vector<interface> neighbors)
        : io_service(io_service), id(id), neighbors(neighbors)
    {
        for (auto& i : neighbors)
        {
            udp::socket* sock = new udp::socket(io_service, udp::endpoint(udp::v4(), i.port));
            sockets.push_back(sock);
            start_receive(sock);
        }
    }
    
    void broadcast(string message)
    {
        for (auto& sock : sockets)
        {
            send(message, &sock);
        }
    }
    
    void send(string message, udp::socket* sock)
    {
        sock->async_send_to(boost::asio::buffer(message), sendee_endpoint,
                              boost::bind(&MyRouter::handle_send, this, message,
                                          boost::asio::placeholders::error,
                                          boost::asio::placeholders::bytes_transferred));
    }
    
private:
    void start_receive(udp::socket* sock)
    {
        sock->async_receive_from(boost::asio::buffer(recv_buffer), remote_endpoint,
                                   boost::bind(&MyRouter::handle_receive, this, sock,
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
    }
    
    void handle_receive(udp::socket* sock, const boost::system::error_code& error,
                        size_t bytes_recvd)
    {
        if (!error || error == boost::asio::error::message_size)
        {
            string recv_str(recv_buffer.begin(), recv_buffer.begin() + bytes_recvd);
            cout << "async_receive_from message='" << recv_str << "'" << endl;
            cout << "async_receive_from return " << error << ": " << bytes_recvd << " received" << endl;
            
            string message = "hello";
            sock->async_send_to(boost::asio::buffer(message), sock->remote_endpoint(),
                                  boost::bind(&MyRouter::handle_send, this, message,
                                              boost::asio::placeholders::error,
                                              boost::asio::placeholders::bytes_transferred));
            
            // continue listening
            start_receive(sock);
        }
    }
    
    void handle_send(string message, const boost::system::error_code& error,
                     std::size_t bytes_transferred)
    {
        cout << "async_send_to message='" << message << "'" << endl;
        cout << "async_send_to return " << error << ": " << bytes_transferred << " transmitted" << endl;
    }
    
    boost::asio::io_service& io_service;
    string id;
    vector<interface> neighbors;
    boost::ptr_vector<udp::socket> sockets;
    enum { max_length = 1024};
    boost::array<char,max_length> recv_buffer;
    
    // temp var
    udp::endpoint remote_endpoint;
    udp::endpoint sendee_endpoint;
};

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        cout << "Wrong arguments. Correct: ./my-router <id>" << endl;
    }
    
    string id = string(argv[1]);
    vector<interface> interfaces;
    
    ifstream initfile("init.txt");
    string line;
    while (getline(initfile, line))
    {
        vector<string> tokens;
        boost::split(tokens, line, boost::is_any_of(","));
        string src_router = tokens[0];
        string dest_router = tokens[1];
        string src_port = tokens[2];
        string link_cost = tokens[3];
        
        if (id.compare(src_router) == 0) {
            interfaces.push_back(interface(dest_router, stoi(src_port), stoi(link_cost)));
        }
    }
    
    boost::asio::io_service io_service;
    MyRouter rt(io_service, id, interfaces);
    io_service.run();
    
    return 0;
}



