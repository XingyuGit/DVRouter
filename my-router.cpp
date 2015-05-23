#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>
#include <iostream>
#include <string>

class udp_server
{
public:
    udp_server(boost::asio::io_service& io_service, short port)
    : socket_(io_service, udp::endpoint(udp::v4(), port))
    {
        start_receive();
    }
    
private:
    void start_receive()
    {
        socket_.async_receive_from(boost::asio::buffer(recv_buffer_), remote_endpoint_,
                                   boost::bind(&udp_server::handle_receive, this,
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
    }
    
    void handle_receive(const boost::system::error_code& error, size_t bytes_recvd)
    {
        if (!error || error == boost::asio::error::message_size)
        {
            boost::shared_ptr<std::string> message(new std::string(make_daytime_string()));
            
            socket_.async_send_to(boost::asio::buffer(*message), remote_endpoint_,
                                  boost::bind(&udp_server::handle_send, this, message));
            start_receive();
        }
    }
    
    void handle_send(boost::shared_ptr<std::string> /*message*/)
    {
    }
    
    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    enum { max_length = 1024};
    boost::array<char, max_length> recv_buffer_;
};

int main(void)
{
    boost::asio::io_service io_service;
    short port = 10000;
    
    udp_server server(io_service, port);
    io_service.run();
    
    return 0;
}


