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
#include <iomanip>

#define DV_SEND_SEC 5
#define FAIL_SEC 10

#define INF 100000



using namespace std;
using namespace boost::asio::ip;

boost::asio::io_service io_service;

struct Interface {
    //    Interface() {}
    Interface(uint16_t port, string neighbor_id, int cost)
    : port(port), neighbor_id(neighbor_id), cost(cost),
    fail_timer(io_service) {}
    
    uint16_t port;
    string neighbor_id;
    int cost;
    boost::asio::deadline_timer fail_timer;
};

struct RTEntry {
    RTEntry() {}
    RTEntry(int cost, uint16_t outgoing_port, uint16_t dest_port, string next_hop)
    : cost(cost), outgoing_port(outgoing_port), dest_port(dest_port), next_hop(next_hop) {}
    
    int cost;
    uint16_t outgoing_port;
    uint16_t dest_port;
    string next_hop; // neighbor router id
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
    DVRouter(string id, uint16_t local_port, map<string, shared_ptr<Interface> > neighbors)
    : sock(io_service, udp::endpoint(udp::v4(), local_port)), id(id), local_port(local_port),
    neighbors(neighbors), dv_timer(io_service), stdinput(io_service, STDIN_FILENO)
    {
        mylog.open("log." + id + ".txt", ofstream::out);
        
        // initialize its own distance vector and routing table (only know neighbors' info)
        for (auto& i : neighbors)
        {
            string id = i.first;
            shared_ptr<Interface> interface = i.second;
            dv[id] = interface->cost;
            RouteTable[id] = RTEntry(interface->cost, local_port, interface->port, interface->neighbor_id);
        }
        dv[id] = 0; // dv to itself is zero
        
        // periodically advertise its distance vector to each of its neighbors every DV_SEND_SEC seconds.
        
        // set DV_SEND_SECr
        dv_timer.expires_from_now(boost::posix_time::seconds(DV_SEND_SEC));
        
        // Start an asynchronous wait.
        dv_timer.async_wait(boost::bind(&DVRouter::dv_timeout_handler, this));
        
        // receive from neighbors
        start_receive();
        
        // input from stdin
        start_input();
    }
    
    ~DVRouter()
    {
        mylog.close();
    }
    
    void broadcast_dv()
    {
        for (auto& i : neighbors)
        {
            string neighbor_id = i.first;
            send_dv(neighbor_id);
//            shared_ptr<Interface> interface = i.second;
//            send(message, udp::endpoint(udp::v4(), interface->port));
        }
    }
    
    void send_dv(string neighbor_id)
    {
        DV new_dv(dv);
        
        for (auto& it : RouteTable)
        {
            string dest_id = it.first;
            shared_ptr<Interface> interface = neighbors[neighbor_id];
            if (neighbor_id.compare(it.second.next_hop) == 0)
            {
                new_dv[dest_id] = INF;
            }
            string message = "dv:" + DVMsg(id, new_dv).toString();
            send(message, udp::endpoint(udp::v4(), interface->port));
        }
    }
    
    void change_cost(string neighbor_id, int new_cost, bool reciprocal, bool temp)
    {
        if (neighbors[neighbor_id]->cost != new_cost)
        {
            logtime();
            mylog << "Cost " << id << neighbor_id << " changed from "
            << neighbors[neighbor_id]->cost << " to " << new_cost << endl << endl;
            
            if (!temp) neighbors[neighbor_id]->cost = new_cost;
            RouteTable[neighbor_id].cost = new_cost;
            dv[neighbor_id] = new_cost;
            
//            broadcast(dvmsg());
            broadcast_dv();
            
            if (reciprocal)
            {
                send("cost:" + neighbor_id + ":" + id + ":" + to_string(new_cost), udp::endpoint(udp::v4(), neighbors[neighbor_id]->port));
            }
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
//    string dvmsg()
//    {
//        
//        return "dv:" + DVMsg(id, dv).toString();
//    }
    
    void send(string message, udp::endpoint sendee_endpoint)
    {
        sock.async_send_to(boost::asio::buffer(message), sendee_endpoint,
                           boost::bind(&DVRouter::handle_send, this,
                                       boost::asio::placeholders::error,
                                       boost::asio::placeholders::bytes_transferred));
    }
    
    void dv_timeout_handler()
    {
//        broadcast(dvmsg());
        broadcast_dv();
        dv_timer.expires_from_now(boost::posix_time::seconds(DV_SEND_SEC));
        dv_timer.async_wait(boost::bind(&DVRouter::dv_timeout_handler, this));
    }
    
    void fail_timeout_handler(string src_id, const boost::system::error_code& error)
    {
        if (error == boost::asio::error::operation_aborted) {
            return;
        }
        
        logtime();
        mylog << "Have not received DV from " << src_id << " for " << FAIL_SEC << " seconds. " << flush;
        mylog << "Mark DV to " << src_id << " as Inf." << endl << endl;
        
        mylog << "******************* ";
        logtime();
        mylog << " *******************" << endl;
        
        mylog << "The routing table before change is:" << endl;
        print_routetable();
        mylog << endl;
        
        change_cost(src_id, INF, false, true);
        
        mylog << "The routing table after change is:" << endl;
        print_routetable();
        
        mylog << "*******************------------------------*******************" << endl;
        mylog << endl << endl;
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
                change_cost(dest_id, stoi(message), true, false);
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
            string cost_str = "Inf";
            if (it.second.cost < INF)
                cost_str = to_string(it.second.cost);
            mylog << it.first << "\t\t" << cost_str << "\t\t" << it.second.outgoing_port
            << "(Node "+ id + ")" << "\t\t" << it.second.dest_port << "(Node " + it.second.next_hop + ")" << endl;
        }
    }
    
    void logtime()
    {
        time_t rawtime;
        struct tm * timeinfo;
        time( &rawtime );
        timeinfo = localtime( &rawtime );
        char * time = asctime(timeinfo);
        time[strlen(time)-1] = '\0';
        mylog << " [" << time << "] " << flush;
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
                    << RouteTable[dest_id].dest_port << "(Node " << RouteTable[dest_id].next_hop << "): "
                    << data << endl << endl;
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
                    mylog << id << " received cost change from " << src_id << endl << endl;
                    change_cost(src_id, cost, false, false);
                }
            }
            else if (tag.compare("dv") == 0)  // dv message
            {
                DVMsg dvm = DVMsg::fromString(tokens[1]);
                
                int neighbor_cost = neighbors[dvm.src_id]->cost;
                
                // refresh neighbor's timer
                //                neighbors[dvm.src_id]->fail_timer.cancel();
                neighbors[dvm.src_id]->fail_timer.expires_from_now(boost::posix_time::seconds(FAIL_SEC));
                neighbors[dvm.src_id]->fail_timer.async_wait(boost::bind(&DVRouter::fail_timeout_handler, this, dvm.src_id,
                                                                         boost::asio::placeholders::error));
                
                bool has_change = false;
                
                for (auto& it : dvm.dv)
                {
                    string dest_id = it.first;
                    int distance = it.second;
                    
                    if ((dv.count(dest_id) > 0 && (distance + neighbor_cost < dv[dest_id] ||
                            (distance + neighbor_cost == dv[dest_id] && dvm.src_id.compare(RouteTable[dest_id].next_hop) < 0))) || dv.count(dest_id) == 0)
                    {
                        mylog << "******************* ";
                        logtime();
                        mylog << " *******************" << endl;
                        
                        mylog << "The routing table before change is:" << endl;
                        print_routetable();
                        mylog << endl;
                        
                        mylog << "Change is caused by " << dvm.src_id << "'s DV: ";
                        mylog << "DV{ source id: " << dvm.src_id << ", " << flush;
                        mylog << "(destination, distance) pairs: " << flush;
                        
                        for (auto &it : dvm.dv)
                        {
                            mylog << "(" << it.first << "," << it.second << ")";
                        }
                        
                        mylog << " }." << endl;
                        mylog << "More Specifically, it is due to the distance of " << dvm.src_id << " to "
                        << dest_id << " is " << distance << "." << endl;
                        
                        // update the DV and RouteTable
                        
                        string old_cost_str = "Inf";
                        if (dv.count(dest_id) > 0 && dv[dest_id] < INF)
                            old_cost_str = to_string(dv[dest_id]);
                        
                        dv[dest_id] = min(distance + neighbor_cost, INF);
                        RouteTable[dest_id] = RTEntry(dv[dest_id], local_port, neighbors[dvm.src_id]->port, dvm.src_id);
                        has_change = true;
                        
                        mylog << "Update " << id << " distance to " << dest_id << ": " << neighbor_cost << "(Cost " << id << dvm.src_id << ") + "
                        << distance << "(" << dvm.src_id << " distance to " << dest_id << ") = " << dv[dest_id] << " < " << old_cost_str
                        << "(Old " << id << " distance to " << dest_id + ")" << endl << endl;
                        
                        mylog << "The routing table after change is:" << endl;
                        print_routetable();
                        
                        mylog << "*******************------------------------*******************" << endl;
                        mylog << endl << endl;
                    }
                }
                
                for (auto& it : RouteTable)
                {
                    string dest_id = it.first;
                    int distance = dvm.dv[dest_id];
                    if (it.second.next_hop.compare(dvm.src_id) == 0 && distance + neighbor_cost > dv[dest_id])
                    {
                        mylog << "******************* ";
                        logtime();
                        mylog << " *******************" << endl;
                        
                        mylog << "The routing table before change is:" << endl;
                        print_routetable();
                        mylog << endl;
                        
                        mylog << "Change is caused by " << dvm.src_id << "'s DV: ";
                        mylog << "DV{ source id: " << dvm.src_id << ", " << flush;
                        mylog << "(destination, distance) pairs: " << flush;
                        
                        for (auto &it : dvm.dv)
                        {
                            mylog << "(" << it.first << "," << it.second << ")";
                        }
                        
                        mylog << " }." << endl;
                        mylog << "More Specifically, it is due to the distance of " << dvm.src_id << " to "
                        << dest_id << " is " << distance << "." << endl;
                        
                        // update the DV and RouteTable
                        
                        string old_cost_str = "Inf";
                        if (dv.count(dest_id) > 0 && dv[dest_id] < INF)
                            old_cost_str = to_string(dv[dest_id]);
                        
                        dv[dest_id] = min(distance + neighbor_cost, INF);
                        RouteTable[dest_id] = RTEntry(dv[dest_id], local_port, neighbors[dvm.src_id]->port, dvm.src_id);
                        has_change = true;
                        
                        mylog << "Update " << id << " distance to " << dest_id << ": " << neighbor_cost << "(Cost " << id << dvm.src_id << ") + "
                        << distance << "(" << dvm.src_id << " distance to " << dest_id << ") = " << dv[dest_id] << " < " << old_cost_str
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
//                    broadcast(dvmsg());
                    broadcast_dv();
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
    string id;
    uint16_t local_port;
    map<string, shared_ptr<Interface> > neighbors; // id => Interface
    udp::endpoint remote_endpoint;
    boost::array<char,MAX_LENGTH> recv_buffer;
    map<string, RTEntry> RouteTable; // id => RTEntry
    DV dv; // distance vector
    boost::asio::deadline_timer dv_timer;
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
    map<string, shared_ptr<Interface> > neighbors;
    
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
            shared_ptr<Interface> interface(new Interface(port, dest_router, cost));
            neighbors[dest_router] = interface;
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
        DVRouter rt(id, local_port, neighbors);
        io_service.run();
    }
    catch (exception& e)
    {
        cerr << e.what() << endl;
    }
    
    cout << "\nProgram stoped\n" << endl;
    
    return 0;
}



