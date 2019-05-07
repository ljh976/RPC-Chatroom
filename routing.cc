#include <iostream>
#include <vector>
#include <google/protobuf/util/time_util.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <memory>
#include <chrono>
#include <ctime>
#include <string.h>
#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <grpc++/grpc++.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "tsd.h"
#include "sns.grpc.pb.h"

using namespace std;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Reply;
using csce438::Request;
using csce438::RoutingService;
using csce438::ServerInfo;


void startProcess(pid_t listenerProcess, string hostname, string port);
void monitorProcess(pid_t mainProcess, string hostname, string port);

class RoutingServiceImpl final : public RoutingService::Service
{
    public:
        RoutingServiceImpl(const string hostname, const string port)
        :hostname(hostname), port(port)
        {
            availableServer = MasterServer();
            servers.clear();
        }
        
        void Start()
        {
            
            string server_address = ipAddress() + ":" + port;
            cout << "Activating routing server." <<endl;
			cout << "Address: " << server_address << endl;
            ServerBuilder builder;
            builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
            builder.RegisterService(this);
            unique_ptr<Server> server(builder.BuildAndStart());
            server->Wait();
        }

        
        Status Find_Available_Server(ServerContext *context, const Request *request, ServerInfo *info) override
        {
            cleanServerList();

            if (servers.size() == 0)
            {
                cout << "None of server available" << endl;
                return Status::CANCELLED;
            }
            std::string command = request->arguments(0);
            if (command == "request - server")
            {
                
                if(availableServer.isAvailable())
                {
                    std::cout << "Providing server info to the clinet" << std::endl;
                    std::cout << availableServer.address() << std::endl;
                    info->set_hostname(availableServer.info.hostname());
                    info->set_port(availableServer.info.port());
                }
                else
                {
                    std::cout << "Electing processing..." << std::endl;
                    
                    
                    for(auto& s : servers)
                    {
                        info->set_hostname(s.info.hostname());
                        info->set_port(s.info.port());
                        s.setAvailable(true);
                        availableServer = s;
                        cout << "Got a Server. Address: " << availableServer.address() << endl;
                        break;
                    }
                }
                return Status::OK;
            }
        }

        
        Status AddServer(ServerContext *context, const ServerInfo *info, Reply *reply) override
        {
            cleanServerList();

            MasterServer newServer(*info);
            if (find(servers.begin(), servers.end(), newServer) == servers.end())
            {
                std::cout << "New master server activated. Address: " << newServer.address() << std::endl;
                servers.push_back(std::move(newServer));
                reply->set_msg("success");
            }
            else
            {
                
                std::cout << "Error: Server already activated." << std::endl;
            }

            return Status::OK;
        }

        void cleanServerList()
        {
            int index = 0;
            for (int i = 0; i < servers.size(); i++)
            {
            	auto s = servers.at(i);
                
                
                
                std::string masterServerAddress = s.info.hostname() + ":" + s.info.port();
                cout << "Host info: " << s.info.hostname() << ":" << s.info.port() << endl;
                auto channel = grpc::CreateChannel(masterServerAddress, grpc::InsecureChannelCredentials());
                std::unique_ptr<SNSService::Stub> stub_ = SNSService::NewStub(channel);

                ClientContext context;
                Request request;
                Reply reply;

                Status status = stub_->Check(&context, request, &reply);
                if (!status.ok())
                {    
                    
                    cout << "Terminating: " << s.info.hostname() << ":" << s.info.port() << endl;
                    cout << "Server is available:" << availableServer.isAvailable() << endl;
                    if(s.isAvailable())
                    {
                    	availableServer.info.set_port("0");
                    	availableServer.setAvailable(false);
                    }
                    servers.erase(servers.begin() + i);
                    i--;
                    
                }
                else
                { 
                    
                }
            }
        }

    private:
        string hostname;
        string port;
        MasterServer availableServer;
        vector<MasterServer> servers;
};



string errorStr()
{
   char * e = strerror(errno);
   return e ? e : "Error: unknown.";
}

void startProcess(pid_t listenerProcess, string hostname, string port)
{
    std::thread server([hostname, port]()
        {
            RoutingServiceImpl server(hostname, port);
            server.Start();
        });

    std::thread slaveMonitor([listenerProcess, hostname, port]()
        {
           int status;
            int counter = 0;
            pid_t w, slave;
            pid_t mainProcess = getpid();
            slave = listenerProcess;
            
            while(true)
            {
                
                counter++;
                w = waitpid(slave, &status, WUNTRACED | WNOHANG);
                
                string err = "";
                if(w == -1)
                {
                    err = errorStr();
                }

                if (err == "Error: no child processes")
                {
                    
                    pid_t pid = fork();

                    
                    if (pid == 0)
                    {
                        
                        monitorProcess(getppid(), hostname, port);
                    }

                    
                    else if (pid > 0)
                    {
                        cout << "Error: listener does not exsit or work" << endl;

                        slave = pid;
                    }

                    else
                    {
                        cout << "Error: Forking listening process cannot be proceed" << endl;
                        return;
                    }
                }

                
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        });

    
    server.join();
    slaveMonitor.join();
}

void monitorProcess(pid_t mainProcess, string hostname, string port)
{
    cout << "Listener process is waking up..." << endl;
    cout << "Main pid: " << mainProcess << endl;

    pid_t listenerProcess;
    while(true)
    {
        
        if (0 != kill(mainProcess, 0))
        {
          
            
            mainProcess = getpid();

            
            pid_t pid = fork();
            listenerProcess = pid;

            
            if (pid == 0)
            {
                
            }

            
            else if (pid > 0)
            {
                cout << "Main process has been slained" << endl;
                cout << "current main pid: " << mainProcess << endl;
                break;
            }

            else
            {
                cout << "Error: failed to fork" << endl;
                return;
            }
        }

        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    
    startProcess(listenerProcess, hostname, port);
}

int main(int argc, char **argv)
{
    std::string hostname = "0.0.0.0";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:p:")) != -1){
        switch(opt) {
            case 'h':
                break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command. Check you arguments. \n";
        }
    }

    pid_t mainProcess = getpid();
    pid_t pid = fork();
    if (pid == 0)
    {
        monitorProcess(mainProcess, hostname, port);
    }
    else if (pid > 0)
    {
        startProcess(pid, hostname, port);
    }
    else
    {
        cout << "Error: failed to fork listening process" << endl;
        return -1;
    }
    return 0;
}