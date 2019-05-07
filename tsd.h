#ifndef TSD_H
#define TSD_H

#include <stdio.h>
#include <unistd.h>
#include <string.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <memory>
#include <string>
#include <set>
#include "sns.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using csce438::Message;
using csce438::ListReply;
using csce438::Request;
using csce438::Reply;
using csce438::ServerInfo;
using csce438::SNSService;
using csce438::RoutingService;

struct MasterServer
{
    public:
        bool available = false;

        csce438::ServerInfo info;
        std::string routingHostname;
        std::string routingPort;
        std::set<std::string> connectedClients;

        MasterServer() 
        {
            info.set_hostname("0.0.0.0");
            info.set_port("0");
        }

        MasterServer(const ServerInfo &server) 
        {
            info.set_hostname(server.hostname());
            info.set_port(server.port());
        }

        void setRoutingAddress(std::string host, std::string port)
        {
            routingHostname = host;
            routingPort = port;
        }

        std::string routingAddress() const
        {
            return routingHostname + ":" + routingPort;
        }

        std::string address() const
        {
            return info.hostname() + ":" + info.port();
        }

        bool operator==(const MasterServer &comparisonServer) const
        {
            return address() == comparisonServer.address();
        }

        void setAvailable(bool a)
        {
            available = a;
        }
        
        bool isAvailable()
        {
            return available;
        }

};



std::string ipAddress()
{
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);

    
    ifr.ifr_addr.sa_family = AF_INET;

    
    strncpy(ifr.ifr_name, "enp0s3", IFNAMSIZ-1);

    ioctl(fd, SIOCGIFADDR, &ifr);

    close(fd);

    
    return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

#endif