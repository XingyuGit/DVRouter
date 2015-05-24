#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>
#include <iostream>
#include <string>
#include <stdint.h>

using namespace std;

class MyRouter
{
public:
    udp_server(boost::asio::io_service& io_service, uint16_t port)
    : socket_(io_service, udp::endpoint(udp::v4(), port))
    {
        start_receive();
    }
    
    void send(string message, uint16_t port)
    {
        send_endpoint = udp::endpoint(udp::v4(), port)
        socket_.async_send_to(boost::asio::buffer(message), send_endpoint,
                              boost::bind(&handle_send, boost::asio::placeholders::error,
                                          boost::asio::placeholders::bytes_transferred));
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
            //            boost::shared_ptr<string> message(new string(make_daytime_string()));
            //            socket_.async_send_to(boost::asio::buffer(*message), remote_endpoint_,
            //                                  boost::bind(&udp_server::handle_send, this, message));
            
            string message = "message";
            socket_.async_send_to(boost::asio::buffer(message), remote_endpoint_,
                                  boost::bind(&udp_server::handle_send, this, message,
                                              boost::asio::placeholders::error,
                                              boost::asio::placeholders::bytes_transferred));
            
            start_receive();
        }
    }
    
    void handle_send(string message, const boost::system::error_code& error,
                     std::size_t bytes_transferred)
    {
        cout << "async_send_to message='" << message << "'" << endl;
        cout << "async_send_to return " << error << ": " << bytes_transferred << " transmitted" << endl;
    }
    
    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    enum { max_length = 1024};
    boost::array<char, max_length> recv_buffer_;
};

int main(void)
{
    boost::asio::io_service io_service;
    int local_port = 10000;
    
    MyRouter rt(io_service, local_port);
    io_service.run();
    
    return 0;
}


