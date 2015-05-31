#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <stdint.h>
#include <ctime>

using namespace std;
using namespace boost::asio::ip;

struct Interface {
    Interface() {}
    Interface(uint16_t port, int cost)
    : port(port), cost(cost) {}
    
    uint16_t port;
    int cost;
};

struct RTEntry {
    RTEntry() {}
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
        size_t i = str.find(":");
        string id = str.substr(0,i);
        str = str.substr(i+1);
        map<string, int> m;
        while(str.find(";") != string::npos){
            i = str.find(",");
            string d_id = str.substr(0,i);
            str = str.substr(i+1);
            i = str.find(";");
            int c = stoi(str.substr(0,i));
            m[d_id] = c;
            str = str.substr(i+1);
        }
        return DVMsg(id, m);
    }
    
    string src_id;
    DV dv;
    
};

vector<string> my_split(string str, int num_parts, string delimit)
{
    vector<string> res;
    size_t pos_pre = 0;
    while (num_parts > 1)
    {
        size_t pos = str.find_first_of(delimit, pos_pre);
        if (pos == string::npos) break;
        
        string sub = str.substr(pos_pre, pos - pos_pre);
        boost::algorithm::trim(sub);
        pos_pre = pos + 1;
        if (sub.length() == 0) continue;
        
        res.push_back(sub);
        num_parts--;
    }
    
    string sub = str.substr(pos_pre);
    if (sub.length() > 0)
        res.push_back(sub);
    
    return res;
}

class DVRouter
{
    const static int MAX_LENGTH = 8192;
public:
    DVRouter(boost::asio::io_service& io_service, string id,
             uint16_t local_port, map<string, Interface> neighbors)
    : sock(io_service, udp::endpoint(udp::v4(), local_port)), io_service(io_service),
    id(id), local_port(local_port), neighbors(neighbors), timer(io_service),
    stdinput(io_service, STDIN_FILENO)
    {
        mylog.open("log." + id + ".txt", ofstream::out);
        
        // initialize its own distance vector and routing table (only know neighbors' info)
        for (auto& i : neighbors)
        {
            string id = i.first;
            Interface interface = i.second;
            dv[id] = interface.cost;
            RouteTable[id] = RTEntry(interface.cost, local_port, interface.port);
        }
        dv[id] = 0; // dv to itself is zero
        
        // periodically advertise its distance vector to each of its neighbors every 5 seconds.
        
        // set timer
        timer.expires_from_now(boost::posix_time::seconds(5));
        
        // Start an asynchronous wait.
        timer.async_wait(boost::bind(&DVRouter::timeout_handler, this));
        
        // receive from neighbors
        start_receive();
        
        // input from stdin
        start_input();
    }
    
    ~DVRouter()
    {
        mylog.close();
    }
    
    void broadcast(string message)
    {
        for (auto& i : neighbors)
        {
            Interface interface = i.second;
            //mylog << id << " broadcast DV to " << i.first << ": " << message << endl;
            send(message, udp::endpoint(udp::v4(), interface.port));
        }
        //mylog << endl;
    }
    
    void change_cost(string neighbor_id, int new_cost)
    {
        if (neighbors[neighbor_id].cost != new_cost)
        {
            neighbors[neighbor_id].cost = new_cost;
            RouteTable[neighbor_id].cost = new_cost;
            dv[neighbor_id] = new_cost;
            
            send("cost:" + neighbor_id + ":" + id + ":" + to_string(new_cost), udp::endpoint(udp::v4(), neighbors[neighbor_id].port));
            broadcast(dvmsg());
        }
        else
        {
            logtime();
            mylog << "Cost is not changed." << endl << endl;
        }
    }
    
    void send_data(string message, string dest_id, bool is_src)
    {
        if (RouteTable.count(dest_id) > 0 && is_src) // is source
        {
            logtime();
            mylog << id << " send message from " << id << " to " << dest_id << endl << endl;
            send("data:" + dest_id + ":" + id + ":" + message, udp::endpoint(udp::v4(), RouteTable[dest_id].dest_port));
        }
        else if(RouteTable.count(dest_id) > 0 && !is_src)
        {
            send(message, udp::endpoint(udp::v4(), RouteTable[dest_id].dest_port));
        }
    }
    
private:
    string dvmsg()
    {
        return "dv:" + DVMsg(id, dv).toString();
    }
    
    void send(string message, udp::endpoint sendee_endpoint)
    {
        sock.async_send_to(boost::asio::buffer(message), sendee_endpoint,
                           boost::bind(&DVRouter::handle_send, this,
                                       boost::asio::placeholders::error,
                                       boost::asio::placeholders::bytes_transferred));
    }
    
    void timeout_handler()
    {
        broadcast(dvmsg());
        timer.expires_from_now(boost::posix_time::seconds(5));
        timer.async_wait(boost::bind(&DVRouter::timeout_handler, this));
    }
    
    void start_input()
    {
        boost::asio::async_read_until(stdinput, input_buffer, "\n",
                                      boost::bind(&DVRouter::handle_input, this,
                                                  boost::asio::placeholders::error,
                                                  boost::asio::placeholders::bytes_transferred));
    }
    
    void handle_input(const boost::system::error_code& error, std::size_t length)
    {
        if (!error || error == boost::asio::error::message_size)
        {
            boost::asio::streambuf::const_buffers_type bufs = input_buffer.data();
            string str(boost::asio::buffers_begin(bufs),
                       boost::asio::buffers_begin(bufs) + length);
            input_buffer.consume(length);
            
            boost::algorithm::trim_if(str, boost::is_any_of("\r\n "));
            
            logtime();
            mylog << "Your command: " << str << endl << endl;
            
            vector<string> tokens = my_split(str, 3, ":");
            string tag = tokens[0];
            string dest_id = tokens[1];
            string message = tokens[2];
            
            if (tag.compare("cost") == 0) // change neighbor cost, e.g. "cost:B:100"
            {
                change_cost(dest_id, stoi(message));
            }
            else if (tag.compare("data") == 0) // send data, e.g. "data:B:hello"
            {
                send_data(message, dest_id, true);
            }
            else
            {
                logtime();
                mylog << "Invalid command: " << str << endl << endl;
            }
        }
        else if( error == boost::asio::error::not_found)
        {
            cerr << "Did not receive ending character!" << endl;
        }
        
        // Continuing
        start_input();
        
    }
    
    void start_receive()
    {
        sock.async_receive_from(boost::asio::buffer(recv_buffer), remote_endpoint,
                                boost::bind(&DVRouter::handle_receive, this,
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred));
    }
    
    void print_routetable()
    {
        mylog << "Destination\tCost\t\tOutgoing UDP port\tDestination UDP port" << endl;
        for (auto &it : RouteTable)
        {
            mylog << it.first << "\t\t" << it.second.cost << "\t\t" << it.second.outgoing_port
            << "(Node "+ id + ")" << "\t\t" << it.second.dest_port << "(Node " + it.first + ")" << endl;
        }
    }
    
    void logtime()
    {
        time_t t = time(nullptr);
        mylog << "(" << put_time(localtime(&t), "%c %Z") << ")" << flush;
    }
    
    void handle_receive(const boost::system::error_code& error, size_t bytes_recvd)
    {
        if (!error || error == boost::asio::error::message_size)
        {
            string recv_str(recv_buffer.begin(), recv_buffer.begin() + bytes_recvd);
            vector<string> tokens = my_split(recv_str, 2, ":");
            
            string tag = tokens[0];
            
            if (tag.compare("data") == 0) // data message
            {
                tokens = my_split(tokens[1], 3, ":");
                string dest_id = tokens[0];
                string src_id = tokens[1];
                string data = tokens[2];
                
                if (dest_id.compare(id) == 0) // I'm destination
                {
                    logtime();
                    mylog << id << " received data message from " << src_id << ": " << data << endl << endl;
                }
                else
                {
                    logtime();
                    mylog << id << " relay data (src: " << src_id << ", dest: " << dest_id << ") to port "
                    << RouteTable[dest_id].dest_port << ": " << data << endl << endl;
                    send_data(recv_str, dest_id, false);
                }
            }
            else if (tag.compare("cost") == 0)
            {
                tokens = my_split(tokens[1], 3, ":");
                string dest_id = tokens[0];
                string src_id = tokens[1];
                int cost = stoi(tokens[2]);
                
                if (dest_id.compare(id) == 0) // I am the destination
                {
                    logtime();
                    mylog << id << " received cost change from " << src_id << ". Cost " << id << src_id
                    << " from " << neighbors[src_id].cost << " to " << cost << endl << endl;
                    change_cost(src_id, cost);
                }
            }
            else if (tag.compare("dv") == 0)  // dv message
            {
                DVMsg dvm = DVMsg::fromString(tokens[1]);
                
                int neighbor_cost = dv[dvm.src_id];
                map<string, int> remote_dv = dvm.dv;
                bool has_change = false;
                
                for (auto& it : remote_dv)
                {
                    string dest_id = it.first;
                    int cost = it.second;
                    
                    if ((dv.count(dest_id) > 0 && cost + neighbor_cost < dv[dest_id]) || dv.count(dest_id) == 0)
                    {
                        mylog << "******************* ";
                        logtime();
                        mylog << " *******************" << endl;
                        
                        mylog << "The routing table before change is:" << endl;
                        print_routetable();
                        mylog << endl;
                        
                        mylog << "Change is caused by " << dvm.src_id << "'s DV: ";
                        mylog << "(to: " << dest_id << ", cost: " << cost << ")" << endl;
                        
                        // update the DV and RouteTable
                        
                        string old_cost_str = "Inf";
                        if (dv.count(dest_id) > 0)
                            old_cost_str = to_string(dv[dest_id]);
                        
                        dv[dest_id] = cost + neighbor_cost;
                        RouteTable[dest_id] = RTEntry(dv[dest_id], local_port, neighbors[dvm.src_id].port);
                        has_change = true;
                        
                        mylog << "Update " << id << " distance to " << dest_id << ": " << neighbor_cost << "(Cost " << id << dvm.src_id << ") + "
                        << cost << "(" << dvm.src_id << " distance to " << dest_id << ") = " << dv[dest_id] << " < " << old_cost_str
                        << "(Old " << id << " distance to " << dest_id + ")" << endl << endl;
                        
                        mylog << "The routing table after change is:" << endl;
                        print_routetable();
                        
                        mylog << "*******************------------------------*******************" << endl;
                        mylog << endl << endl;
                    }
                }
                
                // if any change, broadcast to neighbors (using broadcast())
                
                if (has_change)
                {
                    broadcast(dvmsg());
                }
            }
        }
        
        // continue listening
        start_receive();
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
    map<string, RTEntry> RouteTable; // id => RTEntry
    DV dv; // distance vector
    boost::asio::deadline_timer timer;
    boost::asio::streambuf input_buffer;
    boost::asio::posix::stream_descriptor stdinput;
    ofstream mylog;
};

int main(int argc, char** argv)
{
//    cout << unitbuf;
//    setvbuf(stdout, NULL, _IONBF, 0);
//    setvbuf(stderr, NULL, _IONBF, 0);
    
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
    
    if (local_port == 0)
    {
        cerr << "No port number for router " << id << endl;
        return 0;
    }
    
    try
    {
        boost::asio::io_service io_service;
        DVRouter rt(io_service, id, local_port, neighbors);
        io_service.run();
    }
    catch (exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    
    cout << "\nProgram stoped\n" << endl;
    
    return 0;
}



