#include <boost/asio.hpp>
//#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <string>
#include <stdint.h>

using namespace std;
using namespace boost::asio::ip;

class MyRouter
{
public:
    MyRouter(boost::asio::io_service& io_service, uint16_t port)
    : socket_(io_service, udp::endpoint(udp::v4(), port))
    {
        start_receive();
    }
    
    void send(string message, uint16_t port)
    {
        udp::endpoint sendee_endpoint = udp::endpoint(udp::v4(), port);
        socket_.async_send_to(boost::asio::buffer(message), sendee_endpoint,
                              boost::bind(&MyRouter::handle_send, this, message,
                                          boost::asio::placeholders::error,
                                          boost::asio::placeholders::bytes_transferred));
    }
    
private:
    void start_receive()
    {
        socket_.async_receive_from(boost::asio::buffer(recv_buffer_), remote_endpoint_,
                                   boost::bind(&MyRouter::handle_receive, this,
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
    }
    
    void handle_receive(const boost::system::error_code& error, size_t bytes_recvd)
    {
        if (!error || error == boost::asio::error::message_size)
        {
            //            boost::shared_ptr<string> message(new string(make_daytime_string()));
            //            socket_.async_send_to(boost::asio::buffer(*message), remote_endpoint_,
            //                                  boost::bind(&MyRouter::handle_send, this, message));
            
            string recv_str(recv_buffer_.begin(), recv_buffer_.begin() + bytes_recvd);
            cout << "async_receive_from message='" << recv_str << "'" << endl;
            cout << "async_receive_from return " << error << ": " << bytes_recvd << " received" << endl;
            
            string message = "hello";
            socket_.async_send_to(boost::asio::buffer(message), remote_endpoint_,
                                  boost::bind(&MyRouter::handle_send, this, message,
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


