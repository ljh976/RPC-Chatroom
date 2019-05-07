#include <chrono>
#include <ctime>
#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/time_util.h>
#include <google/protobuf/timestamp.pb.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "tsd.h"
#include "sns.grpc.pb.h"

using namespace std;
using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Message;
using csce438::ListReply;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;

struct Client
{
    std::string username;
    bool connected = true;
    int following_file_size = 0;
    std::vector<Client*> client_followers;
    std::vector<Client*> client_following;
    ServerReaderWriter<Message, Message>* stream = 0;
    bool operator==(const Client& c1) const
    {
        return (username == c1.username);
    }
};


std::vector<Client> client_db;


int find_user(std::string username)
{
    int index = 0;
    for(Client c : client_db)
    {
        if(c.username == username)
            return index;
        index++;
    }
    return -1;
}

class SNSServiceImpl final : public SNSService::Service
{
    public:

        SNSServiceImpl(ServerInfo server)
        {
            masterServer = MasterServer(server);
        }

        Status List(ServerContext* context, const Request* request, ListReply* list_reply) override
        {
        	loadFollowerData();
            Client user = client_db[find_user(request->username())];
            int index = 0;
            for(Client c : client_db)
            {
                list_reply->add_all_users(c.username);
            }
            std::vector<Client*>::const_iterator it;
            for(it = user.client_followers.begin(); it!=user.client_followers.end(); it++)
            {
                list_reply->add_followers((*it)->username);
            }
            return Status::OK;
        }

        Status Follow(ServerContext* context, const Request* request, Reply* reply) override
        {
        	loadFollowerData();
            std::string username1 = request->username();
            std::string username2 = request->arguments(0);
            int join_index = find_user(username2);
            if(join_index < 0 || username1 == username2)
                reply->set_msg("Join Failed -- Invalid Username");
            else
            {
                Client *user1 = &client_db[find_user(username1)];
                Client *user2 = &client_db[join_index];
                if(std::find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end())
                {
                    reply->set_msg("Join Failed -- Already Following User");
                    return Status::OK;
                }
                user1->client_following.push_back(user2);
                user2->client_followers.push_back(user1);
                reply->set_msg("Join Successful");
            }
            Dump_Follower_data();
            return Status::OK;
        }

        Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override
        {
        	loadFollowerData();
            std::string username1 = request->username();
            std::string username2 = request->arguments(0);
            int leave_index = find_user(username2);
            if(leave_index < 0 || username1 == username2)
                reply->set_msg("Leave Failed -- Invalid Username");
            else
            {
                Client *user1 = &client_db[find_user(username1)];
                Client *user2 = &client_db[leave_index];
                if(std::find(user1->client_following.begin(), user1->client_following.end(), user2) == user1->client_following.end())
                {
                    reply->set_msg("Leave Failed -- Not Following User");
                    return Status::OK;
                }
                user1->client_following.erase(find(user1->client_following.begin(), user1->client_following.end(), user2));
                user2->client_followers.erase(find(user2->client_followers.begin(), user2->client_followers.end(), user1));
                reply->set_msg("Leave Successful");
            }
            Dump_Follower_data();
            return Status::OK;
        }

        Status Login(ServerContext* context, const Request* request, Reply* reply) override
        {
            Client c;
            std::string username = request->username();
            populateUsersFile(username);
            populateClientDB();
            int user_index = find_user(username);
            if(user_index < 0)
            {
                c.username = username;
                client_db.push_back(c);
                reply->set_msg("Login Successful!");
            }
            else
            {
                Client *user = &client_db[user_index];
                if(user->connected)
                    reply->set_msg("Invalid Username");
                else
                {
                    std::string msg = "Welcome Back " + user->username;
                    reply->set_msg(msg);
                    loadFollowerData();
                    user->connected = true;
                }
            }
            return Status::OK;
        }

        Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override
        {
            Message message;
            Client *c;
            loadFollowerData();
            while(stream->Read(&message))
            {
                std::string username = message.username();
                int user_index = find_user(username);
                c = &client_db[user_index];

                
                std::string filename = username+".txt";
                std::ofstream user_file(filename,std::ios::app|std::ios::out|std::ios::in);
                google::protobuf::Timestamp temptime = message.timestamp();
                std::time_t time = temptime.seconds();
                std::string t_str = getTime(time);
                std::string fileinput = t_str+" :: "+message.username()+" >> "+message.msg();
                
                if (message.msg().compare("Reconnecting") == 0)
                {
                	c->connected = true;
                	if(c->stream==0)
                        c->stream = stream;
                    continue;
                }
                else if(message.msg() != "Set Stream")
                    user_file << fileinput;
                else
                {
                    if(c->stream==0)
                        c->stream = stream;
                    std::string line;
                    std::vector<std::string> newest_twenty;
                    std::ifstream in(username+".txt");
                    int count = 0;
                    
                    while(getline(in, line))
                    {
                        if(c->following_file_size > 20)
                        {
                            if(count < c->following_file_size-20)
                            {
                                count++;
                                continue;
                            }
                        }
                        newest_twenty.push_back(line+"\n");
                    }
                    Message new_msg;
                    
                    for(int i = 0; i<newest_twenty.size(); i++)
                    {
                        new_msg.set_msg(newest_twenty[i]);
                        stream->Write(new_msg);
                    }
                    continue;
                }
                
                std::vector<Client*>::const_iterator it;
                for(it = c->client_followers.begin(); it!=c->client_followers.end(); it++)
                {
                    Client *temp_client = *it;
                    if(temp_client->stream==0)
                    {
                    	
                    }
                    if(temp_client->connected)
                    {
                    	
                    }
                    if(temp_client->stream!=0 && temp_client->connected)
                    {
                    	
                        if(!temp_client->stream->Write(message)){
                        	std::cout << " Error in write to " << temp_client->username << std::endl;
                        }
                        else{
                        	
                        }
                    }
                    
                    std::string temp_username = temp_client->username;
                    std::string temp_file = temp_username + "following.txt";
                    std::ofstream following_file(temp_file,std::ios::app|std::ios::out|std::ios::in);
                    following_file << fileinput;
                    temp_client->following_file_size++;
                    std::ofstream user_file(temp_username + ".txt",std::ios::app|std::ios::out|std::ios::in);
                    user_file << fileinput;
                }
            }
            
            c->connected = false;
            return Status::OK;
        }

        int Connect_routing_server(std::string hostname, std::string port_no)
        {
            masterServer.setRoutingAddress(hostname, port_no);
            std::string routingAddress = masterServer.routingAddress();
            routingStub_ = std::unique_ptr<RoutingService::Stub>(RoutingService::NewStub(grpc::CreateChannel(routingAddress, grpc::InsecureChannelCredentials())));

            ClientContext context;
            Reply reply;
            ServerInfo serverInfo;
            serverInfo.set_hostname(ipAddress());
            serverInfo.set_port(masterServer.info.port());

            Status status = routingStub_->AddServer(&context, serverInfo, &reply);

            if(reply.msg() == "success")
            {
                return 1;
            }
            return -1;
        }

        Status Check(ServerContext* context, const Request* request, Reply* reply) override
        {
            std::cout << "Checking . . ." << std::endl;
            reply->set_msg("alive");
            return Status::OK;
        }
		
		std::string getTime(std::time_t& time)
		{
			std::string t_str(std::ctime(&time));
    		t_str[t_str.size()-1] = '\0';
    		return t_str;
		}
		
		void populateUsersFile(std::string username)
		{
            std::ofstream outfile;
            
            outfile.open("users.txt", std::ios_base::app);
            outfile << username << "\n";
            outfile.close();
		}
		
		void populateClientDB(){
            std::ifstream infile("users.txt");
			std::string user;
            while(infile >> user)
            {
            	Client c;
                int user_index = find_user(user);
                if(user_index == -1)
                {
                	c.username = user;
                	client_db.push_back(c);
                }
            }
			
		}
		
        void Dump_Follower_data()
        {
            for (const auto& c : client_db)
            {
                std::ofstream outfile;

                
                outfile.open(c.username+"_following.txt", std::ofstream::trunc);
                outfile.close();

                outfile.open(c.username+"_following.txt", std::ios_base::app);
                for (const auto& following : c.client_following)
                {
                    outfile << following->username << "\n";
                }
            }
        }

        void loadFollowerData()
        {
            for (const auto& client : client_db)
            {
                int index = find_user(client.username);
                Client *c = &client_db[index];
                std::ifstream infile(c->username+"_following.txt");
                std::string follower;
                while(infile >> follower)
                {
                    int user_index = find_user(follower);
                    Client *follower_c = &client_db[user_index];
                    if(std::find(c->client_following.begin(), c->client_following.end(), follower_c) == c->client_following.end())
                    {
                        c->client_following.push_back(follower_c);
                        follower_c->client_followers.push_back(c);
                    }
                }
            }
        }

    private:
        MasterServer masterServer;
        std::unique_ptr<RoutingService::Stub> routingStub_;
};

void RunServer(std::string hostname, std::string port_no)
{
    
    std::string masterPort = "3020"; 
    ServerInfo s;
    s.set_hostname(hostname);
    s.set_port(masterPort);
    SNSServiceImpl service(s);
    
    int status = -1;
    while(status != 1)
    {
        
        std::cout << "Connecting routing server..." << std::endl;
        status = service.Connect_routing_server(hostname, port_no);
        if (status == 1)
        {
            std::cout << "Connected to routing server successfully." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    std::string server_address = ipAddress() + ":" + masterPort;
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server is listening... " << server_address << std::endl;

    server->Wait();
}





void startProcess(pid_t listenerProcess, string hostname, string port);
void monitorProcess(pid_t mainProcess, string hostname, string port);

string errorStr()
{
   char * e = strerror(errno);
   return e ? e : "Error: Unknown";
}

void startProcess(pid_t listenerProcess, string hostname, string port)
{
    std::thread server([hostname, port]()
        {
            RunServer(hostname, port);
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
                        cout << "Error: Listener process is dead or crushed" << endl;
                        slave = pid;
                    }

                    else
                    {
                        cout << "Error: listening process cannot be forked." << endl;
                        return;
                    }
                }

                
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            }
        });

    
    server.join();
    slaveMonitor.join();
}

void monitorProcess(pid_t mainProcess, string hostname, string port)
{
    cout << "Starting listener process" << endl;
    cout << "Main pid: " << mainProcess << endl;
    
    pid_t listenerProcess;
    while(true)
    {
        
        if (0 != kill(mainProcess, 0))
        {
            cout << "Restarting main process" << endl;

            mainProcess = getpid();

            pid_t pid = fork();
            listenerProcess = pid;

            
            if (pid == 0)
            {
                cout << "pid does not exist" << endl;
            }

            else if (pid > 0)
            {
                cout << "Main process has been killed or crashed" << endl;
                cout << "Main pid: " << mainProcess << endl;
                break;
            }

            else
            {
                cout << "Error: listening process cannot be forked." << endl;
                return;
            }
        }

        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    
    startProcess(listenerProcess, hostname, port);
}

int main(int argc, char** argv)
{
    std::string hostname = "0.0.0.0";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:p:")) != -1)
    {
        switch(opt)
        {
            case 'h':
                hostname = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
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
        cout << "Failed to fork listening process" << endl;
        return -1;
    }
    return 0;
}